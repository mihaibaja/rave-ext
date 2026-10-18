/*********************************************************/
// Copyright (C) 2026 Barcelona Supercomputing Center-Centro Nacional de Supercomputación
// SPDX-License-Identifier: BSD-3-Clause
/*********************************************************/
// * Author: Pablo Vizcaino
// * Email:  pablo.vizcaino@bsc.es
/*********************************************************/
#pragma once
#include <stdint.h>

struct scalar_block_data_t{
	int instr;
	uint64_t * PCs;
	uint64_t PC_branch;
	uint64_t PC_loop;
	char has_func_jump;
	int loop_instr;
	int moved_bytes;
	int flops;
	char ** strings;
    // To keep track of dependencies
    int * dst_d;
    int * src1_d;
    int * src2_d;
};
typedef struct scalar_block_data_t scalar_block_data_t;

scalar_block_data_t * alloc_scalar_block(int max_instr);
void free_scalar_block(scalar_block_data_t * scalar_block_data);
