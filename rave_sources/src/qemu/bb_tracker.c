#include "bb_tracker.h"
#include "state.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    uint64_t bb_start_pc;
    uint64_t acc_vec_inst;
    uint64_t acc_insnt;
    uint64_t bb_last_pc;
    bool in_bb;
} vcpu_bb_state_t;

static vcpu_bb_state_t *g_vcpu_states = NULL;
static size_t g_max_vcpus = 0;

/** Consideremos los bloques básicos como aquellas secuencias SESE, es decir,
 * single-entry single-exit. Por lo tanto, un bloque básico termina cuando se encuentra
 * una instrucción de salto, una instrucción de llamada, un retorno o una instrucción de fin de programa.
 * Si dos secuencias de instrucciones terminan en la misma instrucción de salto, pero tienen diferentes entradas,
 * se consideran bloques básicos diferentes.
*/

static GHashTable *g_bb_map = NULL;   /* clave: start_pc -> valor: bb_node_t* */                                                                                                        
static GPtrArray  *g_bb_nodes = NULL;
static GMutex      g_bb_lock;

void bb_tracker_init(size_t max_vcpus) {
    g_max_vcpus = (max_vcpus > 0) ? max_vcpus : 64;
    g_vcpu_states = (vcpu_bb_state_t*)calloc(g_max_vcpus, sizeof(vcpu_bb_state_t));
    g_bb_map = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
    g_bb_nodes = g_ptr_array_new();
    g_mutex_init(&g_bb_lock);
}

bb_terminator_t bb_tracker_classify_terminator(uint32_t op) {
    if ((op & 0x3) != 0x3) {
        uint16_t c = (uint16_t)(op & 0xFFFF);
        uint8_t quad   = c & 0x3;
        uint8_t funct3 = (c >> 13) & 0x7;

        if (quad == 0x1) {
            /* c.beqz y c.bnez */
            if (funct3 == 0x6 || funct3 == 0x7) return BB_END;
            /* c.j (en RV64 funct3 == 0x1 es c.addiw, no salto) */
            if (funct3 == 0x5) return BB_END;
        } else if (quad == 0x2) {
            /* c.jr, c.jalr, c.ebreak */
            uint8_t funct4 = (c >> 12) & 0xF;
            uint8_t rs1 = (c >> 7) & 0x1F;
            uint8_t rs2 = (c >> 2) & 0x1F;
            if (funct4 == 0x8 || funct4 == 0x9) {
                if (rs1 != 0 && rs2 == 0) return BB_END;
                if (funct4 == 0x9 && rs1 == 0 && rs2 == 0) return BB_END;
            }
        }
        return BB_TERM_CONTINUE;
    }
    /** Branch */
    if((op & 0b1111111) == 0b1100011) return BB_END;
    /** JAL */
    else if((op & 0b1111111) == 0b1101111) return BB_END;
    /** JALR */
    else if((op & 0b1111111) == 0b1100111) return BB_END;
    else if((op & 0b1111111) == 0b1110011) {  
        uint32_t funct3 = (op >> 12) & 0x7;
        /** TRAP/ECALL */
        if (funct3 == 0) return BB_END;
    } 
    return BB_TERM_CONTINUE;
}

void bb_tracker_on_tb_exec(unsigned int vcpu_index, tb_data_t *tb_data) {
    if (vcpu_index >= g_max_vcpus) return;
    // fprintf(stdout, "VCPU_INDEX: %u\n", vcpu_index);
    vcpu_bb_state_t *st = &g_vcpu_states[vcpu_index];
    if(!st->in_bb) {
        st->acc_insnt = 0;
        st->acc_vec_inst = 0;
        st->in_bb = true;
        st->bb_start_pc = tb_data->pc;
    }

    st->acc_insnt += tb_data->n;
    st->acc_vec_inst += tb_data->vec_inst;
    if(bb_tracker_classify_terminator(tb_data->opcode) == BB_END) {
        st->bb_last_pc = tb_data->end_pc;
        st->in_bb = false;

        gpointer key = GSIZE_TO_POINTER(st->bb_start_pc);
        g_mutex_lock(&g_bb_lock);
        bb_node_t *node = (bb_node_t *)g_hash_table_lookup(g_bb_map, key);
        if(node == NULL){ // Nuevo bloque básico no guardado
            node = (bb_node_t *)malloc(sizeof(bb_node_t));
            node->id = g_bb_nodes->len;
            node->start_pc = st->bb_start_pc;
            node->end_pc = st->bb_last_pc;
            node->num_insns = st->acc_insnt;
            node->num_vec_insns = st->acc_vec_inst;
            node->exec_count = 1;

            g_hash_table_insert(g_bb_map, key, node);
            g_ptr_array_add(g_bb_nodes, node);
        } else {
            node->exec_count++;
        }
        g_mutex_unlock(&g_bb_lock);
    }
}

void bb_tracker_cleanup(void) {
    const char *out_path = (BB_JSON_NAME != NULL) ? BB_JSON_NAME : "rave_bb.json";
    bb_tracker_dump_json(out_path);

    if (g_bb_nodes) {
        for(size_t i = 0; i < g_bb_nodes->len; ++i) {
            bb_node_t *node = (bb_node_t *)g_ptr_array_index(g_bb_nodes, i);
            fprintf(stdout, "BB_ID: %u, START_PC: 0x%lx, END_PC: 0x%lx, NUM_INSNS: %lu, NUM_VEC_INSNS: %lu, EXEC_COUNT: %lu\n",
                    node->id, node->start_pc, node->end_pc, node->num_insns, node->num_vec_insns, node->exec_count);
            free(node);
        }
        g_ptr_array_free(g_bb_nodes, true);
        g_bb_nodes = NULL;
    }
    if (g_bb_map) {
        g_hash_table_destroy(g_bb_map);
        g_bb_map = NULL;
    }
    g_mutex_clear(&g_bb_lock);
    if (g_vcpu_states) {
        free(g_vcpu_states);
        g_vcpu_states = NULL;
    }
}

void bb_tracker_dump_json(const char *filepath) {
    FILE *f = fopen(filepath, "w");
    if (!f) return;

    fprintf(f, "{\n");
    fprintf(f, "  \"generator\": \"RAVE\",\n");
    fprintf(f, "  \"summary\": {\n");
    fprintf(f, "    \"total_unique_bbs\": %u\n", g_bb_nodes ? g_bb_nodes->len : 0);
    fprintf(f, "  },\n");
    fprintf(f, "  \"nodes\": [\n");

    for (guint i = 0; i < g_bb_nodes->len; ++i) {
        bb_node_t *n = (bb_node_t *)g_ptr_array_index(g_bb_nodes, i);
        uint64_t scalar = (n->num_insns >= n->num_vec_insns) ? (n->num_insns - n->num_vec_insns) : 0;
        double v_ratio = (n->num_insns > 0) ? ((double)n->num_vec_insns / n->num_insns) : 0.0;

        fprintf(f, "    {\n");
        fprintf(f, "      \"id\": %u,\n", n->id);
        fprintf(f, "      \"pc\": {\n");
        fprintf(f, "        \"start\": \"0x%lx\",\n", n->start_pc);
        fprintf(f, "        \"end\": \"0x%lx\"\n", n->end_pc);
        fprintf(f, "      },\n");
        fprintf(f, "      \"metrics\": {\n");
        fprintf(f, "        \"instructions\": {\n");
        fprintf(f, "          \"total\": %lu,\n", n->num_insns);
        fprintf(f, "          \"scalar\": %lu,\n", scalar);
        fprintf(f, "          \"vector\": %lu,\n", n->num_vec_insns);
        fprintf(f, "          \"vector_ratio\": %.4f\n", v_ratio);
        fprintf(f, "        },\n");
        fprintf(f, "        \"execution_count\": %lu\n", n->exec_count);
        fprintf(f, "      }\n");
        fprintf(f, "    }%s\n", (i + 1 < g_bb_nodes->len) ? "," : "");
    }

    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    fclose(f);
}