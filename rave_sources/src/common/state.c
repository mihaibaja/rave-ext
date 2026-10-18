/*********************************************************/
// Copyright (C) 2026 Barcelona Supercomputing Center-Centro Nacional de Supercomputación
// SPDX-License-Identifier: BSD-3-Clause
/*********************************************************/
// * Author: Pablo Vizcaino
// * Email:  pablo.vizcaino@bsc.es
/*********************************************************/

#include "state.h"
#include "threading.h"
#include "rave2prv.h"
#include <stdio.h>
#include "counters_generic.h"
#include "utils.h"
#include <string.h>

uint64_t timestamp;
//QEMU_PLUGIN_EXPORT int qemu_plugin_version;

char TRACE_ENABLED = 1; //Enabled by default 
char ACCUM_REGIONS = 0;
FILE * FD_PRV;
FILE * FD_REPORT;
int N_PIPELINES;
char PRINT_LOGFILE = 0;
char PRINT_PROFILE = 0;
char PRINT_CALLTRACE = 0;
char PRINT_PRV = 0;
int MUSA;
int RAVE_ELEN = 64;
int RAVE_VLMAX = 0;
char TRACE_INDEXES = 0;
char OTHER_CHILDS = 0;

char REGIONS_ENABLED = 1; //Enabled by default 
int REGION_EVENT = 1000;
char STREAM_REPORT = 0;
char TRACE_ADDR = 0;
char TRACE_SCALAR = 0;
char TRACE_EXTENDED = 0;
char INST_TRACE_ENABLED = 0;
char * INST_TRACE_NAME = NULL;
FILE * FD_INST_TRACE = NULL;
char DEBUG_INFO = 0;
int RAW_DIST = 8;
int WAR_DIST = 20;
int WAW_DIST = 8;
uint64_t base;
int disabled_once = 0;
uint64_t timestamp;
int PLAIN_TEXT=0;
int COMPRESS_REPORT=0;
int PROFILE_WEIGHT=w_ELEMS;
char PRINT_REPORT = 0;
char PRINT_CSV = 0;
char PRINT_JSON = 0;
char * filename = NULL; 
char * BINARY_NAME = NULL;
FILE * FD_PCF;
FILE * FD_ROW;
FILE * FD_CSV;
FILE * FD_JSON;
FILE * FD_COMM;
FILE * FD_PROFILE;
FILE * FD_CALLTRACE;

thread_state_t * cpus_state;

void restart_trace(){
	//restart prv
	if (PRINT_PRV){
		FD_PRV = freopen(NULL, "w+", FD_PRV);
		if (N_THREADS > expected_threads) expected_threads = N_THREADS;
		write_prv(FD_PRV, 1, &expected_threads, N_PIPELINES); 
		trace_row(FD_PRV,0, 0, SCALAR_ROW, 0);
		trace_event_value(FD_PRV,event_VLEN,RAVE_VLMAX);
		trace_event_value(FD_PRV,event_ELEN,RAVE_ELEN);
	}
	//restart global region
	//global_region -> closed = 0;
	//reset_counters(&global_region->counters);
	timestamp=0;
	for(int i=0; i<N_THREADS; ++i){
		reset_thread(i);
	}
	TRACE_ENABLED=1;
}

void enable_regions(){
	REGIONS_ENABLED=1;
}
void disable_regions(){
	REGIONS_ENABLED=0;
}

void enable_trace(){
	for(int i=0; i<N_THREADS; ++i){
		cpus_state[i].print_first_scalar = 1;
	}
	TRACE_ENABLED=1;
}

void disable_trace(int cpu_index){
	TRACE_ENABLED=0;
	disabled_once=1;
	if (PRINT_PRV){
		trace_row(FD_PRV,mpi_rank, cpu_index, SCALAR_ROW, timestamp);
		clean_event(FD_PRV); 
#if defined(RVV_07) || defined(RVV_10)
		if (!MUSA){
#endif
			trace_row(FD_PRV,mpi_rank, cpu_index, VECTOR_ROW, timestamp);
			clean_event(FD_PRV); 
#if defined(RVV_07) || defined(RVV_10)
		}
#endif
	}
}

void reset_thread(int cpu_index){
	thread_state_t * state = &cpus_state[cpu_index];
	state -> cpu_index = cpu_index;
	state -> last_row = 0;
#if defined(RVV_07) || defined(RVV_10)
	state -> reset_stride = 0;
	state -> last_was_vsetvl = 0;
#endif
	state -> scalar_instr_since_vector = 0;
	state -> print_first_scalar = 1;
	state -> need_align = 1;
	state -> timestamp = 0;

	//Loop control:
	reset_profile(&state->loop_profile);

	//Call trace:
	reset_calltrace(&state->call_trace);

	state -> rave_event_number=-1;
	state -> rave_value_number=-1;
	reset_counters(&(state->accum_counters));
	memset(state->write_reg_deps, 0, sizeof(state->write_reg_deps));
	memset(state->read_reg_deps, 0, sizeof(state->read_reg_deps));
	memset(state->inst_trace_last_write, 0, sizeof(state->inst_trace_last_write));
	memset(state->inst_trace_last_read, 0, sizeof(state->inst_trace_last_read));
	
	//Musa:
	state -> prev_dst = 0;
	
	setup_regs(cpu_index);

}
