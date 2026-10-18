/*********************************************************/
// Copyright (C) 2026 Barcelona Supercomputing Center-Centro Nacional de Supercomputación
// SPDX-License-Identifier: BSD-3-Clause
/*********************************************************/
#define _GNU_SOURCE
#include "inst_trace.h"
#include "threading.h"
#include "utils.h"
#include "state.h"
#include "rave2prv.h"
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>

#define MAX_TRACE_DEPS 16
#define MAX_REGS_IN_GROUP 8
#define MAX_HEX_STR_LEN 4096

static uint64_t inst_trace_seqno = 0;

typedef struct {
    char operand[16];
    char dep_type[8]; // "RAW", "WAR", "WAW"
    uint64_t producer_seqno;
    uint64_t distance;
} inst_trace_dep_t;

static inline bool valid_reg(int id) {
    return (id >= 0 && id < 96);
}

static void reg_id_to_name(int id, char *buf, size_t maxlen) {
    if (id < 0) {
        buf[0] = '\0';
        return;
    }
    if (id < RAVE_INI_REG_F) {
        snprintf(buf, maxlen, "x%d", id - RAVE_INI_REG_S);
    } else if (id < RAVE_INI_REG_V) {
        snprintf(buf, maxlen, "f%d", id - RAVE_INI_REG_F);
    } else if (id < 96) {
        snprintf(buf, maxlen, "v%d", id - RAVE_INI_REG_V);
    } else {
        snprintf(buf, maxlen, "r%d", id);
    }
}

static void write_json_escaped_string(FILE * f, const char * str) {
    if (!str) {
        fputs("\"\"", f);
        return;
    }
    fputc('"', f);
    for (const char * p = str; *p; p++) {
        switch (*p) {
            case '"':  fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\b': fputs("\\b", f); break;
            case '\f': fputs("\\f", f); break;
            case '\n': fputs("\\n", f); break;
            case '\r': fputs("\\r", f); break;
            case '\t': fputs("\\t", f); break;
            default:
                if ((unsigned char)*p < 0x20) {
                    fprintf(f, "\\u%04x", (unsigned char)*p);
                } else {
                    fputc(*p, f);
                }
                break;
        }
    }
    fputc('"', f);
}

static const char * get_lmul_string(uint64_t lmul) {
#ifdef RVV_07
    switch (lmul & 0x3) {
        case 0: return "m1";
        case 1: return "m2";
        case 2: return "m4";
        case 3: return "m8";
        default: return "m1";
    }
#else
    switch (lmul & 0x7) {
        case 0: return "m1";
        case 1: return "m2";
        case 2: return "m4";
        case 3: return "m8";
        case 5: return "mf8";
        case 6: return "mf4";
        case 7: return "mf2";
        default: return "reserved";
    }
#endif
}

static int get_lmul_factor(uint64_t lmul) {
#ifdef RVV_07
    return 1 << (lmul & 0x3);
#else
    if ((lmul & 0x7) < 4) {
        return 1 << (lmul & 0x7);
    }
    return 1; // fractional LMUL uses single register
#endif
}

static const char * decode_operation_name(instr_data * instr) {
    if (is_type(instr->type, T_VSETVL)) {
        return "VSETVL";
    }
    if (is_type(instr->type, T_VECTOR)) {
        if (is_subtype(instr->type, T_LOAD)) {
            if (is_subsubtype(instr->type, T_UNIT)) return "VECTOR_LOAD_UNIT";
            if (is_subsubtype(instr->type, T_STRIDE)) return "VECTOR_LOAD_STRIDED";
            if (is_subsubtype(instr->type, T_INDEX)) return "VECTOR_LOAD_INDEXED";
            return "VECTOR_LOAD";
        }
        if (is_subtype(instr->type, T_STORE)) {
            if (is_subsubtype(instr->type, T_UNIT)) return "VECTOR_STORE_UNIT";
            if (is_subsubtype(instr->type, T_STRIDE)) return "VECTOR_STORE_STRIDED";
            if (is_subsubtype(instr->type, T_INDEX)) return "VECTOR_STORE_INDEXED";
            return "VECTOR_STORE";
        }
        if (is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_GATHER)) {
            return "VECTOR_GATHER";
        }
        if (is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_SLIDE)) {
            return "VECTOR_SLIDE";
        }
        if (is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_COMPRESS)) {
            return "VECTOR_COMPRESS";
        }
        if (is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_MOVE)) {
            return "VECTOR_MOVE";
        }
        if (is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_MASK)) {
            return "VECTOR_MASK_OP";
        }
        if (is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_REDUCTION)) {
            return "VECTOR_ARITH_REDUCTION";
        }
        if (is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_COMP_FUSED)) {
            return "VECTOR_ARITH_FUSED";
        }
        if (is_type_ext(instr->type_ext, MASK_EXT_SUBTYPE, TYPE_EXT_ARITH_WIDENING)) {
            return "VECTOR_ARITH_WIDENING";
        }
        if (is_type_ext(instr->type_ext, MASK_EXT_SUBTYPE, TYPE_EXT_ARITH_NARROWING)) {
            return "VECTOR_ARITH_NARROWING";
        }
        if (is_subtype(instr->type, T_ARITH)) {
            if (is_subsubtype(instr->type, T_FP)) return "VECTOR_ARITH_FP";
            return "VECTOR_ARITH_INT";
        }
        return "VECTOR_OTHER";
    }
    return "SCALAR";
}

void inst_trace_init(void) {
    if (!INST_TRACE_ENABLED) return;

    if (INST_TRACE_NAME == NULL) {
        char * env_name = getenv("RAVE_INST_TRACE_NAME");
        if (env_name != NULL && strlen(env_name) > 0) {
            INST_TRACE_NAME = strdup(env_name);
        } else {
            INST_TRACE_NAME = strdup(INST_TRACE_DEFAULT_NAME);
        }
    }

    FD_INST_TRACE = fopen(INST_TRACE_NAME, "w");
    if (FD_INST_TRACE == NULL) {
        fprintf(stderr, "RAVE Warning: Could not open instruction trace file '%s'. Falling back to stderr.\n", INST_TRACE_NAME);
        FD_INST_TRACE = stderr;
    }

    inst_trace_seqno = 0;

    // Header record
    set_lock(write_lock);
    fprintf(FD_INST_TRACE, "{\"record_type\":\"header\","
                           "\"schema_version\":\"%s\","
                           "\"rave_version\":\"%s\","
#ifdef RVV_07
                           "\"isa_revision\":\"RVV-0.7.1\","
#else
                           "\"isa_revision\":\"RVV-1.0\","
#endif
                           "\"vlen\":%d,"
                           "\"elen\":%d,"
                           "\"workload\":",
            INST_TRACE_SCHEMA_VERSION,
            INST_TRACE_RAVE_VERSION,
            RAVE_VLMAX,
            RAVE_ELEN);

    write_json_escaped_string(FD_INST_TRACE, BINARY_NAME ? BINARY_NAME : "unknown");

    fprintf(FD_INST_TRACE, ",\"config\":{\"raw_dist\":%d,\"war_dist\":%d,\"waw_dist\":%d,\"trace_scalar\":%d},"
                           "\"num_harts\":%d,"
                           "\"addr_normalization\":\"base_relative\"}\n",
            RAW_DIST, WAR_DIST, WAW_DIST, TRACE_SCALAR,
            expected_threads > 0 ? expected_threads : 1);
    fflush(FD_INST_TRACE);
    release_lock(write_lock);
}

void inst_trace_exit(void) {
    if (FD_INST_TRACE != NULL) {
        set_lock(write_lock);
        fflush(FD_INST_TRACE);
        if (FD_INST_TRACE != stdout && FD_INST_TRACE != stderr) {
            fclose(FD_INST_TRACE);
        }
        FD_INST_TRACE = NULL;
        release_lock(write_lock);
    }
}

static int collect_reg_group(int base_reg, int count, int out_regs[MAX_REGS_IN_GROUP]) {
    int n = 0;
    if (!valid_reg(base_reg)) return 0;

    if (base_reg >= RAVE_INI_REG_V) {
        int v_idx = base_reg - RAVE_INI_REG_V;
        for (int i = 0; i < count && (v_idx + i) < 32 && n < MAX_REGS_IN_GROUP; ++i) {
            out_regs[n++] = RAVE_INI_REG_V + v_idx + i;
        }
    } else {
        out_regs[n++] = base_reg;
    }
    return n;
}

void inst_trace_emit_vector(thread_state_t * state, instr_data * instr,
                            uint64_t vl, uint64_t sew, uint64_t lmul,
                            uint64_t vtype, uint64_t addr, int stride,
                            void * indexes, int is_indexed) {
    if (!INST_TRACE_ENABLED || FD_INST_TRACE == NULL) return;

    uint32_t insn_opcode = instr->instr32;
    int vm = (insn_opcode >> 25) & 1;
    bool is_masked = (vm == 0);

    bool vta = (vtype >> 6) & 1;
    bool vma = (vtype >> 7) & 1;

    int lmul_factor = get_lmul_factor(lmul);

    bool is_widening = is_type_ext(instr->type_ext, MASK_EXT_SUBTYPE, TYPE_EXT_ARITH_WIDENING);
    bool is_narrowing = is_type_ext(instr->type_ext, MASK_EXT_SUBTYPE, TYPE_EXT_ARITH_NARROWING);

    int dst_count = is_widening ? (lmul_factor * 2) : lmul_factor;
    if (dst_count > 8) dst_count = 8;
    if (dst_count < 1) dst_count = 1;

    int src2_count = is_narrowing ? (lmul_factor * 2) : lmul_factor;
    if (src2_count > 8) src2_count = 8;
    if (src2_count < 1) src2_count = 1;

    int dst_regs[MAX_REGS_IN_GROUP];
    int n_dst = 0;
    if (valid_reg(instr->dst_d)) {
        n_dst = collect_reg_group(instr->dst_d, dst_count, dst_regs);
    }

    int src_regs[32];
    int n_src = 0;

    int src1_group[MAX_REGS_IN_GROUP];
    int n_src1 = collect_reg_group(instr->src1_d, lmul_factor, src1_group);
    for (int i = 0; i < n_src1 && n_src < 32; ++i) src_regs[n_src++] = src1_group[i];

    int src2_group[MAX_REGS_IN_GROUP];
    int n_src2 = collect_reg_group(instr->src2_d, src2_count, src2_group);
    for (int i = 0; i < n_src2 && n_src < 32; ++i) src_regs[n_src++] = src2_group[i];

    int src3_group[MAX_REGS_IN_GROUP];
    int n_src3 = collect_reg_group(instr->src3_d, lmul_factor, src3_group);
    for (int i = 0; i < n_src3 && n_src < 32; ++i) src_regs[n_src++] = src3_group[i];

    // If masked, register v0 is implicitly an input source
    if (is_masked) {
        int v0_reg = RAVE_INI_REG_V + 0;
        bool already_in = false;
        for (int i = 0; i < n_src; ++i) {
            if (src_regs[i] == v0_reg) { already_in = true; break; }
        }
        if (!already_in && n_src < 32) {
            src_regs[n_src++] = v0_reg;
        }
    }

    set_lock(write_lock);
    uint64_t seqno = ++inst_trace_seqno;

    // Track dependencies per operand
    inst_trace_dep_t deps[MAX_TRACE_DEPS];
    int n_deps = 0;

    // RAW dependencies on source registers
    for (int i = 0; i < n_src && n_deps < MAX_TRACE_DEPS; ++i) {
        int r = src_regs[i];
        if (valid_reg(r) && state->inst_trace_last_write[r] != 0) {
            reg_id_to_name(r, deps[n_deps].operand, sizeof(deps[n_deps].operand));
            strcpy(deps[n_deps].dep_type, "RAW");
            deps[n_deps].producer_seqno = state->inst_trace_last_write[r];
            deps[n_deps].distance = (seqno >= deps[n_deps].producer_seqno) ? (seqno - deps[n_deps].producer_seqno) : 0;
            n_deps++;
        }
    }

    // WAW and WAR dependencies on destination registers
    for (int i = 0; i < n_dst && n_deps < MAX_TRACE_DEPS; ++i) {
        int r = dst_regs[i];
        if (valid_reg(r)) {
            if (state->inst_trace_last_write[r] != 0 && n_deps < MAX_TRACE_DEPS) {
                reg_id_to_name(r, deps[n_deps].operand, sizeof(deps[n_deps].operand));
                strcpy(deps[n_deps].dep_type, "WAW");
                deps[n_deps].producer_seqno = state->inst_trace_last_write[r];
                deps[n_deps].distance = (seqno >= deps[n_deps].producer_seqno) ? (seqno - deps[n_deps].producer_seqno) : 0;
                n_deps++;
            }
            if (state->inst_trace_last_read[r] != 0 && n_deps < MAX_TRACE_DEPS) {
                reg_id_to_name(r, deps[n_deps].operand, sizeof(deps[n_deps].operand));
                strcpy(deps[n_deps].dep_type, "WAR");
                deps[n_deps].producer_seqno = state->inst_trace_last_read[r];
                deps[n_deps].distance = (seqno >= deps[n_deps].producer_seqno) ? (seqno - deps[n_deps].producer_seqno) : 0;
                n_deps++;
            }
        }
    }

    // Update state register dependency tracking
    for (int i = 0; i < n_src; ++i) {
        int r = src_regs[i];
        if (valid_reg(r)) state->inst_trace_last_read[r] = seqno;
    }
    for (int i = 0; i < n_dst; ++i) {
        int r = dst_regs[i];
        if (valid_reg(r)) state->inst_trace_last_write[r] = seqno;
    }

    // 1. Write Main Instruction Record
    fprintf(FD_INST_TRACE, "{\"record_type\":\"inst\","
                           "\"seqno\":%" PRIu64 ","
                           "\"hart\":%d,"
                           "\"pc\":\"0x%" PRIx64 "\","
                           "\"binary\":\"0x%08" PRIx32 "\","
                           "\"op\":",
            seqno, state->cpu_index, instr->PC, insn_opcode);

    // Extract opcode mnemonic
    char op_name[64];
    size_t asm_len = strlen(instr->asm_string);
    size_t k = 0;
    while (k < asm_len && instr->asm_string[k] != ' ' && instr->asm_string[k] != '\t' && k < 63) {
        op_name[k] = instr->asm_string[k];
        k++;
    }
    op_name[k] = '\0';
    write_json_escaped_string(FD_INST_TRACE, op_name);

    fprintf(FD_INST_TRACE, ",\"asm\":");
    write_json_escaped_string(FD_INST_TRACE, instr->asm_string);

    fprintf(FD_INST_TRACE, ",\"decoded_op\":\"%s\",", decode_operation_name(instr));

    // Destination register group
    fprintf(FD_INST_TRACE, "\"dst\":[");
    for (int i = 0; i < n_dst; ++i) {
        char rname[16];
        reg_id_to_name(dst_regs[i], rname, sizeof(rname));
        fprintf(FD_INST_TRACE, "%s\"%s\"", (i > 0 ? "," : ""), rname);
    }
    fprintf(FD_INST_TRACE, "],\"srcs\":[");
    for (int i = 0; i < n_src; ++i) {
        char rname[16];
        reg_id_to_name(src_regs[i], rname, sizeof(rname));
        fprintf(FD_INST_TRACE, "%s\"%s\"", (i > 0 ? "," : ""), rname);
    }
    fprintf(FD_INST_TRACE, "],");

    // Vector state fields
    int sew_bits = 8 << sew;
    const char * lmul_str = get_lmul_string(lmul);
    fprintf(FD_INST_TRACE, "\"vl\":%" PRIu64 ","
                           "\"sew\":%d,"
                           "\"lmul\":\"%s\","
                           "\"vta\":%d,"
                           "\"vma\":%d,"
                           "\"masked\":%s,"
                           "\"deps\":[",
            vl, sew_bits, lmul_str, vta ? 1 : 0, vma ? 1 : 0,
            is_masked ? "true" : "false");

    for (int i = 0; i < n_deps; ++i) {
        fprintf(FD_INST_TRACE, "%s{\"operand\":\"%s\",\"dep_type\":\"%s\",\"producer_seqno\":%" PRIu64 ",\"distance\":%" PRIu64 "}",
                (i > 0 ? "," : ""), deps[i].operand, deps[i].dep_type, deps[i].producer_seqno, deps[i].distance);
    }
    fprintf(FD_INST_TRACE, "]}\n");

    // 2. Auxiliary Records linked by seqno

    // Aux: mask bitmap
    size_t mask_bytes_len = (vl > 0) ? ((vl + 7) / 8) : 0;
    char * mask_buf = (is_masked && mask_bytes_len > 0) ? get_vreg(state, 0, mask_bytes_len) : NULL;

    if (is_masked && mask_buf != NULL && mask_bytes_len > 0) {
        char hex_str[MAX_HEX_STR_LEN];
        size_t print_bytes = mask_bytes_len > 256 ? 256 : mask_bytes_len;
        size_t off = snprintf(hex_str, sizeof(hex_str), "0x");
        for (int i = (int)print_bytes - 1; i >= 0 && off < sizeof(hex_str) - 3; --i) {
            off += snprintf(hex_str + off, sizeof(hex_str) - off, "%02x", (uint8_t)mask_buf[i]);
        }
        fprintf(FD_INST_TRACE, "{\"record_type\":\"aux\","
                               "\"seqno\":%" PRIu64 ","
                               "\"aux_type\":\"mask_bitmap\","
                               "\"mask_bitmap\":\"%s\","
                               "\"total_elements\":%" PRIu64 "}\n",
                seqno, hex_str, vl);
    }

    // Aux: memory effective address relative deltas
    if (is_subtype(instr->type, T_MEMORY)) {
        const char * mtype = "unit";
        int elem_bytes = 1 << sew;
        if (is_subsubtype(instr->type, T_STRIDE)) mtype = "strided";
        else if (is_subsubtype(instr->type, T_INDEX)) mtype = "indexed";

        fprintf(FD_INST_TRACE, "{\"record_type\":\"aux\","
                               "\"seqno\":%" PRIu64 ","
                               "\"aux_type\":\"mem_addrs\","
                               "\"mem_type\":\"%s\","
                               "\"base_addr\":\"0x%" PRIx64 "\","
                               "\"stride_bytes\":%d,"
                               "\"deltas\":[",
                seqno, mtype, addr, is_subsubtype(instr->type, T_STRIDE) ? stride : elem_bytes);

        uint64_t emitted_deltas = 0;
        uint64_t max_deltas = (vl > 256) ? 256 : vl;

        for (uint64_t i = 0; i < max_deltas; ++i) {
            if (is_masked && mask_buf != NULL) {
                if (!(((uint8_t)mask_buf[i / 8] >> (i % 8)) & 1)) continue;
            }
            int64_t delta = 0;
            if (is_subsubtype(instr->type, T_UNIT)) {
                delta = (int64_t)(i * elem_bytes);
            } else if (is_subsubtype(instr->type, T_STRIDE)) {
                delta = (int64_t)(i * stride);
            } else if (is_subsubtype(instr->type, T_INDEX) && indexes != NULL) {
                if (sew == 0) delta = ((int8_t*)indexes)[i];
                else if (sew == 1) delta = ((int16_t*)indexes)[i];
                else if (sew == 2) delta = ((int32_t*)indexes)[i];
                else delta = ((int64_t*)indexes)[i];
            }
            fprintf(FD_INST_TRACE, "%s%" PRId64, (emitted_deltas > 0 ? "," : ""), delta);
            emitted_deltas++;
        }
        fprintf(FD_INST_TRACE, "]}\n");
    }

    // Aux: gather indices
    if (is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_GATHER)) {
        int vs1 = (insn_opcode >> 15) & 0x1F;
        size_t g_bytes = vl * (1 << sew);
        char * g_buf = get_vreg(state, vs1, g_bytes);
        if (g_buf != NULL) {
            int64_t min_idx = INT64_MAX, max_idx = INT64_MIN;
            uint64_t sample_n = (vl > 128) ? 128 : vl;
            fprintf(FD_INST_TRACE, "{\"record_type\":\"aux\","
                                   "\"seqno\":%" PRIu64 ","
                                   "\"aux_type\":\"gather_indices\","
                                   "\"indices\":[", seqno);
            for (uint64_t i = 0; i < sample_n; ++i) {
                int64_t val = 0;
                if (sew == 0) val = ((uint8_t*)g_buf)[i];
                else if (sew == 1) val = ((uint16_t*)g_buf)[i];
                else if (sew == 2) val = ((uint32_t*)g_buf)[i];
                else val = ((uint64_t*)g_buf)[i];

                if (val < min_idx) min_idx = val;
                if (val > max_idx) max_idx = val;
                fprintf(FD_INST_TRACE, "%s%" PRId64, (i > 0 ? "," : ""), val);
            }
            if (min_idx == INT64_MAX) { min_idx = 0; max_idx = 0; }
            fprintf(FD_INST_TRACE, "],\"min_index\":%" PRId64 ",\"max_index\":%" PRId64 ",\"sample_count\":%" PRIu64 "}\n",
                    min_idx, max_idx, sample_n);
            free(g_buf);
        }
    }

    // Aux: slide or compress
    if (is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_SLIDE)) {
        bool is_slideup = (strstr(instr->asm_string, "slideup") != NULL || strstr(instr->asm_string, "slide1up") != NULL);
        int64_t offset = 1;
        int funct3 = (insn_opcode >> 12) & 0x7;
        if (funct3 == 3 || funct3 == 4) { // .vi or .vx
            if (funct3 == 3) offset = (insn_opcode >> 15) & 0x1F; // 5-bit imm
            else {
                int rs1 = (insn_opcode >> 15) & 0x1F;
                offset = get_xreg(state, rs1);
            }
        }
        fprintf(FD_INST_TRACE, "{\"record_type\":\"aux\","
                               "\"seqno\":%" PRIu64 ","
                               "\"aux_type\":\"slide_compress\","
                               "\"sub_type\":\"%s\","
                               "\"offset\":%" PRId64 ","
                               "\"positions\":{\"src_start\":0,\"dst_start\":%" PRId64 ",\"count\":%" PRIu64 "}}\n",
                seqno, is_slideup ? "slideup" : "slidedown", offset,
                is_slideup ? offset : 0,
                (offset < (int64_t)vl) ? (vl - (uint64_t)offset) : 0);
    } else if (is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_COMPRESS)) {
        fprintf(FD_INST_TRACE, "{\"record_type\":\"aux\","
                               "\"seqno\":%" PRIu64 ","
                               "\"aux_type\":\"slide_compress\","
                               "\"sub_type\":\"compress\","
                               "\"total_elements\":%" PRIu64 "}\n",
                seqno, vl);
    }

    if (mask_buf != NULL) {
        free(mask_buf);
    }

    release_lock(write_lock);
}

void inst_trace_emit_vsetvl(thread_state_t * state, instr_data * instr,
                            uint64_t rvl, uint64_t vl, uint64_t vtype) {
    if (!INST_TRACE_ENABLED || FD_INST_TRACE == NULL) return;

    set_lock(write_lock);
    uint64_t seqno = ++inst_trace_seqno;

    int dst_reg = instr->dst_d;
    int src1_reg = instr->src1_d;

    inst_trace_dep_t deps[MAX_TRACE_DEPS];
    int n_deps = 0;

    if (valid_reg(src1_reg) && state->inst_trace_last_write[src1_reg] != 0) {
        reg_id_to_name(src1_reg, deps[n_deps].operand, sizeof(deps[n_deps].operand));
        strcpy(deps[n_deps].dep_type, "RAW");
        deps[n_deps].producer_seqno = state->inst_trace_last_write[src1_reg];
        deps[n_deps].distance = (seqno >= deps[n_deps].producer_seqno) ? (seqno - deps[n_deps].producer_seqno) : 0;
        n_deps++;
    }

    if (valid_reg(dst_reg)) {
        if (state->inst_trace_last_write[dst_reg] != 0) {
            reg_id_to_name(dst_reg, deps[n_deps].operand, sizeof(deps[n_deps].operand));
            strcpy(deps[n_deps].dep_type, "WAW");
            deps[n_deps].producer_seqno = state->inst_trace_last_write[dst_reg];
            deps[n_deps].distance = (seqno >= deps[n_deps].producer_seqno) ? (seqno - deps[n_deps].producer_seqno) : 0;
            n_deps++;
        }
        if (state->inst_trace_last_read[dst_reg] != 0) {
            reg_id_to_name(dst_reg, deps[n_deps].operand, sizeof(deps[n_deps].operand));
            strcpy(deps[n_deps].dep_type, "WAR");
            deps[n_deps].producer_seqno = state->inst_trace_last_read[dst_reg];
            deps[n_deps].distance = (seqno >= deps[n_deps].producer_seqno) ? (seqno - deps[n_deps].producer_seqno) : 0;
            n_deps++;
        }
        state->inst_trace_last_write[dst_reg] = seqno;
    }
    if (valid_reg(src1_reg)) {
        state->inst_trace_last_read[src1_reg] = seqno;
    }

    int sew_bits = 8 << ((vtype >> 3) & 0x7);
    uint64_t lmul_code = vtype & 0x7;
    bool vta = (vtype >> 6) & 1;
    bool vma = (vtype >> 7) & 1;

    char dst_name[16];
    char src_name[16];
    reg_id_to_name(dst_reg, dst_name, sizeof(dst_name));
    reg_id_to_name(src1_reg, src_name, sizeof(src_name));

    fprintf(FD_INST_TRACE, "{\"record_type\":\"inst\","
                           "\"seqno\":%" PRIu64 ","
                           "\"hart\":%d,"
                           "\"pc\":\"0x%" PRIx64 "\","
                           "\"binary\":\"0x%08" PRIx32 "\","
                           "\"op\":\"vsetvl\","
                           "\"asm\":",
            seqno, state->cpu_index, instr->PC, instr->instr32);
    write_json_escaped_string(FD_INST_TRACE, instr->asm_string);
    fprintf(FD_INST_TRACE, ",\"decoded_op\":\"VSETVL\","
                           "\"dst\":[\"%s\"],"
                           "\"srcs\":[\"%s\"],"
                           "\"vl\":%" PRIu64 ","
                           "\"sew\":%d,"
                           "\"lmul\":\"%s\","
                           "\"vta\":%d,"
                           "\"vma\":%d,"
                           "\"masked\":false,"
                           "\"deps\":[",
            dst_name, src_name, vl, sew_bits, get_lmul_string(lmul_code),
            vta ? 1 : 0, vma ? 1 : 0);

    for (int i = 0; i < n_deps; ++i) {
        fprintf(FD_INST_TRACE, "%s{\"operand\":\"%s\",\"dep_type\":\"%s\",\"producer_seqno\":%" PRIu64 ",\"distance\":%" PRIu64 "}",
                (i > 0 ? "," : ""), deps[i].operand, deps[i].dep_type, deps[i].producer_seqno, deps[i].distance);
    }
    fprintf(FD_INST_TRACE, "]}\n");

    release_lock(write_lock);
}

void inst_trace_emit_scalar(thread_state_t * state, uint64_t pc,
                            const char * asm_str, uint32_t insn_opcode,
                            int dst, int src1, int src2) {
    if (!INST_TRACE_ENABLED || !TRACE_SCALAR || FD_INST_TRACE == NULL) return;

    set_lock(write_lock);
    uint64_t seqno = ++inst_trace_seqno;

    inst_trace_dep_t deps[MAX_TRACE_DEPS];
    int n_deps = 0;

    if (valid_reg(src1) && state->inst_trace_last_write[src1] != 0) {
        reg_id_to_name(src1, deps[n_deps].operand, sizeof(deps[n_deps].operand));
        strcpy(deps[n_deps].dep_type, "RAW");
        deps[n_deps].producer_seqno = state->inst_trace_last_write[src1];
        deps[n_deps].distance = (seqno >= deps[n_deps].producer_seqno) ? (seqno - deps[n_deps].producer_seqno) : 0;
        n_deps++;
    }
    if (valid_reg(src2) && state->inst_trace_last_write[src2] != 0) {
        reg_id_to_name(src2, deps[n_deps].operand, sizeof(deps[n_deps].operand));
        strcpy(deps[n_deps].dep_type, "RAW");
        deps[n_deps].producer_seqno = state->inst_trace_last_write[src2];
        deps[n_deps].distance = (seqno >= deps[n_deps].producer_seqno) ? (seqno - deps[n_deps].producer_seqno) : 0;
        n_deps++;
    }

    if (valid_reg(dst)) {
        if (state->inst_trace_last_write[dst] != 0) {
            reg_id_to_name(dst, deps[n_deps].operand, sizeof(deps[n_deps].operand));
            strcpy(deps[n_deps].dep_type, "WAW");
            deps[n_deps].producer_seqno = state->inst_trace_last_write[dst];
            deps[n_deps].distance = (seqno >= deps[n_deps].producer_seqno) ? (seqno - deps[n_deps].producer_seqno) : 0;
            n_deps++;
        }
        if (state->inst_trace_last_read[dst] != 0) {
            reg_id_to_name(dst, deps[n_deps].operand, sizeof(deps[n_deps].operand));
            strcpy(deps[n_deps].dep_type, "WAR");
            deps[n_deps].producer_seqno = state->inst_trace_last_read[dst];
            deps[n_deps].distance = (seqno >= deps[n_deps].producer_seqno) ? (seqno - deps[n_deps].producer_seqno) : 0;
            n_deps++;
        }
        state->inst_trace_last_write[dst] = seqno;
    }
    if (valid_reg(src1)) state->inst_trace_last_read[src1] = seqno;
    if (valid_reg(src2)) state->inst_trace_last_read[src2] = seqno;

    char dst_name[16];
    char src1_name[16];
    char src2_name[16];
    reg_id_to_name(dst, dst_name, sizeof(dst_name));
    reg_id_to_name(src1, src1_name, sizeof(src1_name));
    reg_id_to_name(src2, src2_name, sizeof(src2_name));

    fprintf(FD_INST_TRACE, "{\"record_type\":\"inst\","
                           "\"seqno\":%" PRIu64 ","
                           "\"hart\":%d,"
                           "\"pc\":\"0x%" PRIx64 "\","
                           "\"binary\":\"0x%08" PRIx32 "\","
                           "\"op\":",
            seqno, state->cpu_index, pc, insn_opcode);

    char op_name[64];
    size_t asm_len = asm_str ? strlen(asm_str) : 0;
    size_t k = 0;
    while (k < asm_len && asm_str[k] != ' ' && asm_str[k] != '\t' && k < 63) {
        op_name[k] = asm_str[k];
        k++;
    }
    op_name[k] = '\0';
    write_json_escaped_string(FD_INST_TRACE, op_name);

    fprintf(FD_INST_TRACE, ",\"asm\":");
    write_json_escaped_string(FD_INST_TRACE, asm_str ? asm_str : "");

    fprintf(FD_INST_TRACE, ",\"decoded_op\":\"SCALAR\","
                           "\"dst\":[%s%s%s],"
                           "\"srcs\":[",
            valid_reg(dst) ? "\"" : "", valid_reg(dst) ? dst_name : "", valid_reg(dst) ? "\"" : "");

    int n_s = 0;
    if (valid_reg(src1)) {
        fprintf(FD_INST_TRACE, "\"%s\"", src1_name);
        n_s++;
    }
    if (valid_reg(src2)) {
        fprintf(FD_INST_TRACE, "%s\"%s\"", n_s > 0 ? "," : "", src2_name);
    }

    fprintf(FD_INST_TRACE, "],\"vl\":0,\"sew\":0,\"lmul\":\"m1\",\"vta\":0,\"vma\":0,"
                           "\"masked\":false,\"deps\":[");

    for (int i = 0; i < n_deps; ++i) {
        fprintf(FD_INST_TRACE, "%s{\"operand\":\"%s\",\"dep_type\":\"%s\",\"producer_seqno\":%" PRIu64 ",\"distance\":%" PRIu64 "}",
                (i > 0 ? "," : ""), deps[i].operand, deps[i].dep_type, deps[i].producer_seqno, deps[i].distance);
    }
    fprintf(FD_INST_TRACE, "]}\n");

    release_lock(write_lock);
}
