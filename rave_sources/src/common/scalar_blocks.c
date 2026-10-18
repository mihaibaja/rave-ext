/*********************************************************/
// Copyright (C) 2026 Barcelona Supercomputing Center-Centro Nacional de Supercomputación
// SPDX-License-Identifier: BSD-3-Clause
/*********************************************************/
// * Author: Pablo Vizcaino
// * Email:  pablo.vizcaino@bsc.es
/*********************************************************/
#include "scalar_blocks.h"
#include <stdlib.h>


#include <stdio.h>
scalar_block_data_t * alloc_scalar_block(int max_instr){
	scalar_block_data_t * scalar_block_data = (scalar_block_data_t *)malloc(sizeof(scalar_block_data_t));
	scalar_block_data->instr=0;
	scalar_block_data->PCs = (uint64_t*)malloc(sizeof(uint64_t)*max_instr);
	scalar_block_data->PC_branch=-1;
	scalar_block_data->PC_loop=-1;
	scalar_block_data->has_func_jump=0;
	scalar_block_data->moved_bytes=0;
	scalar_block_data->flops=0;
	scalar_block_data->loop_instr=0;
	scalar_block_data->strings = NULL;
    scalar_block_data->dst_d = NULL;
    scalar_block_data->src1_d = NULL;
    scalar_block_data->src2_d = NULL;
	return scalar_block_data;
}
void free_scalar_block(scalar_block_data_t * scalar_block_data){
    if (scalar_block_data->strings != NULL) {
        for (int i = 0; i < scalar_block_data->instr; ++i) {
            free(scalar_block_data->strings[i]);
        }
        free(scalar_block_data->strings);
    }

    free(scalar_block_data->dst_d);
    free(scalar_block_data->src1_d);
    free(scalar_block_data->src2_d);
    free(scalar_block_data->PCs);
    free(scalar_block_data);
}
