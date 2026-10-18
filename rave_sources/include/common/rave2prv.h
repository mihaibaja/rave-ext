/*********************************************************/
// Copyright (C) 2025 Barcelona Supercomputing Center-Centro Nacional de Supercomputación
// SPDX-License-Identifier: BSD-3-Clause
/*********************************************************/
// * Author: Pablo Vizcaino
// * Email:  pablo.vizcaino@bsc.es
/*********************************************************/
#pragma once

#include <stdint.h>
#include <stdio.h>

#define event_pc 47000001
#define event_scalb 47000003
#define event_addr 47000005
#define event_dst 47000006
#define event_src1 47000007
#define event_src2 47000008
#define event_src3 47000009
#define event_instruction 47000015
#define event_class 47000016
#define event_vl 47000019
#define event_rvl 47000020
#define event_VLEN 47000029
#define event_ELEN 47000030
#define event_sew 47000031
#define event_lmul 47000032
#define event_indexes 48000000
#define event_stride 50000000

#define clean_event(PRV) fprintf(PRV, ":%d:0:%d:0:%d:0:%d:0:%d:0:%d:0:%d:0:%d:0:%d:0:%d:0:%d:0:%d:0:%d:0:%d:0:%d:0", event_pc, event_scalb, event_addr, event_dst, event_src1, event_src2, event_src3, event_instruction, event_class, event_vl, event_rvl, event_sew, event_lmul, event_indexes, event_stride)

#define clean_event_vector(PRV) fprintf(PRV, ":%d:0:%d:0:%d:0:%d:0:%d:0:%d:0:%d:0:%d:0:%d:0:%d:0:%d:0", event_scalb, event_addr, event_dst, event_src1, event_src2, event_src3, event_vl, event_sew, event_lmul, event_indexes, event_stride)
#define clean_event_scalar(PRV) fprintf(PRV, ":%d:0", event_rvl)

#define PRV_SCALAR 1000
#define SCALAR_ROW 0
#define VECTOR_ROW 1
void set_pipelines(int N);
void trace_row(FILE * fd, int process, int cpu, int pipeline, uint64_t tstamp);
void trace_event_value(FILE * fd, int event, uint64_t value);
int reg2prv(char * r);
int reg2id(const char * reg_name);
static inline int valid_reg_id(int reg_id){
	return reg_id >= 0 && reg_id < 96;
}
void open_file(FILE **fd, char * name);
void write_prv(FILE * fd, int procs, int * OMPthreads, int pipelines);
void write_row(FILE * fd, int procs, int * OMPthreads, int pipelines);
void events_and_values_to_pcf(FILE * fd, int event, int max_nested);
