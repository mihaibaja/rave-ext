/*********************************************************/
// Copyright (C) 2026 Barcelona Supercomputing Center-Centro Nacional de Supercomputación
// SPDX-License-Identifier: BSD-3-Clause
/*********************************************************/
#pragma once

#include "state.h"
#include "instr_data.h"
#include <stdint.h>
#include <stdio.h>

#define INST_TRACE_SCHEMA_VERSION "1.0.0"               // Para compatibilidad con versiones anteriores o modificaciones, no cambiar este valor
#define INST_TRACE_RAVE_VERSION "2.0-extended"          // Para asegurar reproducibilidad de trazas
#define INST_TRACE_DEFAULT_NAME "rave_inst_trace.jsonl" // Nombre por defecto del archivo de trazas

void inst_trace_init(void);
void inst_trace_exit(void);

void inst_trace_emit_vector(thread_state_t * state, instr_data * instr,
                            uint64_t vl, uint64_t sew, uint64_t lmul,
                            uint64_t vtype, uint64_t addr, int stride,
                            void * indexes, int is_indexed);

void inst_trace_emit_vsetvl(thread_state_t * state, instr_data * instr,
                            uint64_t rvl, uint64_t vl, uint64_t vtype);

void inst_trace_emit_scalar(thread_state_t * state, uint64_t pc,
                            const char * asm_str, uint32_t insn_opcode,
                            int dst, int src1, int src2);
