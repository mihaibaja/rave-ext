/*********************************************************/
// Copyright (C) 2026 Barcelona Supercomputing Center-Centro Nacional de Supercomputación
// SPDX-License-Identifier: BSD-3-Clause
/*********************************************************/
// * Author: Pablo Vizcaino
// * Email:  pablo.vizcaino@bsc.es
/*********************************************************/
#pragma once

#include "profiling.h"
#include "counters.h"
#include <stdint.h>

//Exported Variables
extern uint64_t timestamp;
extern char TRACE_ENABLED;
extern char ACCUM_REGIONS;
extern FILE * FD_PRV;
extern FILE * FD_REPORT;
extern int N_PIPELINES;
extern char PRINT_LOGFILE;
extern char PRINT_PROFILE;
extern char PRINT_CALLTRACE;
extern char PRINT_PRV;
extern int MUSA;
extern int RAVE_ELEN;
extern int RAVE_VLMAX;
extern char TRACE_INDEXES;
extern char OTHER_CHILDS;
extern char REGIONS_ENABLED;
extern int REGION_EVENT;
extern char STREAM_REPORT;
extern char TRACE_ADDR;
extern char TRACE_SCALAR;
extern char TRACE_EXTENDED;
extern char INST_TRACE_ENABLED;
extern char * INST_TRACE_NAME;
extern FILE * FD_INST_TRACE;
extern char DEBUG_INFO;
extern int RAW_DIST;
extern int WAR_DIST;
extern int WAW_DIST;
extern uint64_t base;
extern int disabled_once;
extern uint64_t timestamp;
extern int PLAIN_TEXT;
extern int COMPRESS_REPORT;
enum weight_t {w_ELEMS, w_INSTR};
extern int PROFILE_WEIGHT;
extern char PRINT_REPORT;
extern char PRINT_CSV;
extern char PRINT_JSON;
extern char * filename;
extern char * BINARY_NAME;
extern FILE * FD_PCF;
extern FILE * FD_ROW;
extern FILE * FD_CSV;
extern FILE * FD_JSON;
extern FILE * FD_COMM;
extern FILE * FD_PROFILE;
extern FILE * FD_CALLTRACE;

// Registers inicialization index.
#define RAVE_INI_REG_S 0
#define RAVE_INI_REG_F 32
#define RAVE_INI_REG_V 64
	
typedef struct inst_compressed_t inst_compressed_t;
struct inst_compressed_t {
    char * asm_string;
    uint64_t pos;
    uint16_t type;
};

//Per-thread info
struct thread_state_t{
	int cpu_index;
	int last_row;
#if defined(RVV_07) || defined(RVV_10)
	int reset_stride;
	int last_was_vsetvl;
#endif
	int scalar_instr_since_vector;
	int print_first_scalar;
	char need_align;
	uint64_t timestamp;
	rave_counters accum_counters;

	//For loop detection:
	profile_t loop_profile;

	//For callstack:
	calltrace_t  call_trace;

	//For events
	int rave_event_number;
	int rave_value_number;

	//For MUSA:
	int prev_dst;

    // To check dependencies
    struct inst_compressed_t write_reg_deps[96];
    struct inst_compressed_t read_reg_deps[96];

    uint64_t inst_trace_last_write[96];
    uint64_t inst_trace_last_read[96];

	void * regs;
};
typedef struct thread_state_t thread_state_t;

extern thread_state_t * cpus_state;

void restart_trace();
void enable_regions();
void disable_regions();
void enable_trace();
void disable_trace(int cpu_index);

void reset_thread(int cpu_index);
