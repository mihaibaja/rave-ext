/*********************************************************/
// Copyright (C) 2025 Barcelona Supercomputing Center-Centro Nacional de Supercomputación
// SPDX-License-Identifier: BSD-3-Clause
/*********************************************************/
// * Author: Pablo Vizcaino
// * Email:  pablo.vizcaino@bsc.es
/*********************************************************/
#define _GNU_SOURCE
#include "threading.h"
#include "instr_data.h"
#include "rave2prv.h"
#include "state.h"
#include "utils.h"
#include "profiling.h"
#include "scalar_blocks.h"
#include "inst_trace.h"
#include <sched.h>
#include <string.h>

static void record_vector_register(double * usage, short reg){
	if (reg >= RAVE_INI_REG_V && reg < RAVE_INI_REG_V + NUM_VECTOR_REGS) {
		usage[reg - RAVE_INI_REG_V] = 1;
	}
}

static inline uint16_t get_distance_bin(long dist) {
    if (dist <= 1) return 0;
    if (dist == 2) return 1;
    if (dist == 3) return 2;
    if (dist == 4) return 3;
    if (dist <= 8) return 4;
    if (dist <= 16) return 5;
    if (dist <= 32) return 6;
    if (dist <= 64) return 7;
    return 8;
}

#define INVALID_RAW_IDX 15
/**
 * 0: Load
 * 1: Integer arithmetic
 * 2: Floating-point arithmetic
 * 3: Fused operations
 * 4: Permutations
 * 5: Reductions
 * 6: Mask operations
 * 7: Scalar to Vector
 */
static inline uint16_t get_prod_idx(uint16_t type) {
    // 0. Load
    if (is_type_ext(type, MASK_EXT_BASE, TYPE_EXT_LOAD)) return 0;

    // 7. Scalar to Vector (la instrucción lee un escalar y produce/escribe en vector)
    if (is_type_ext(type, MASK_EXT_OPERAND, TYPE_EXT_READ_SCALAR) 
        && is_type_ext(type, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_MOVE)) return 7;

    // Si es instrucción aritmética
    if (is_type_ext(type, MASK_EXT_BASE, TYPE_EXT_ARITH)) {
        uint16_t op = type & MASK_EXT_SUBSUBTYPE;

        // 4. Permutaciones (move, slide, compress, gather)
        if (op == TYPE_EXT_ARITH_MOVE   || op == TYPE_EXT_ARITH_SLIDE ||
            op == TYPE_EXT_ARITH_COMPRESS || op == TYPE_EXT_ARITH_GATHER) return 4;
        // 5. Reducciones
        if (op == TYPE_EXT_ARITH_REDUCTION) return 5;
        // 6. Operaciones de Máscara
        if (op == TYPE_EXT_ARITH_MASK) return 6;
        // 3. Fused (FMA / widening fused, etc.)
        if (op == TYPE_EXT_ARITH_COMP_FUSED) return 3;

        // 1 y 2. Aritmética estándar
        if (op == TYPE_EXT_ARITH_COMPUTATION) {
            if (is_type_ext(type, TYPE_EXT_FP, TYPE_EXT_FP)) return 2; // fp arith
            return 1; // int arith
        }
    }

    return INVALID_RAW_IDX;
}
/**
 * 0: Store
 * 1: Integer arithmetic
 * 2: Floating-point arithmetic
 * 3: Fused operations
 * 4: Permutations
 * 5: Reductions
 * 6: Mask operations
 * 7: Vector to scalar
 */
static inline uint16_t get_cons_idx(uint16_t type) {
    // 0. Store
    if (is_type_ext(type, MASK_EXT_BASE, TYPE_EXT_STORE)) return 0;

    // 7. Vector to Scalar (la instrucción vectorial extrae a escalar)
    if (is_type_ext(type, MASK_EXT_OPERAND, TYPE_EXT_WRITE_SCALAR)
        && is_type_ext(type, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_MOVE)) return 7;

    // Si es instrucción aritmética consumidora
    if (is_type_ext(type, MASK_EXT_BASE, TYPE_EXT_ARITH)) {
        uint16_t op = type & MASK_EXT_SUBSUBTYPE;

        // 4. Permutaciones
        if (op == TYPE_EXT_ARITH_MOVE   || op == TYPE_EXT_ARITH_SLIDE ||
            op == TYPE_EXT_ARITH_COMPRESS || op == TYPE_EXT_ARITH_GATHER) return 4;

        // 5. Reducciones
        if (op == TYPE_EXT_ARITH_REDUCTION) return 5;

        // 6. Operaciones de Máscara
        if (op == TYPE_EXT_ARITH_MASK) return 6;

        // 3. Fused
        if (op == TYPE_EXT_ARITH_COMP_FUSED) return 3;

        // 1 y 2. Aritmética estándar
        if (op == TYPE_EXT_ARITH_COMPUTATION) {
            if (is_type_ext(type, TYPE_EXT_FP, TYPE_EXT_FP)) return 2; // fp arith
            return 1; // int arith
        }
    }

    return INVALID_RAW_IDX;
}

static int rvv_mis_search(uint16_t mask, const uint16_t *conflict) {
    if (mask == 0) return 0;
    int v = __builtin_ctz(mask);
    int without_v = rvv_mis_search(mask & ~(1U << v), conflict);
    int with_v = 1 + rvv_mis_search(mask & ~conflict[v] & ~(1U << v), conflict);
    return (with_v > without_v) ? with_v : without_v;
}

static void track_vector_parallelism_available(thread_state_t *state, short dst_d, short src1_d, short src2_d, short src3_d) {
    int cur = state->rvv_raw_win_idx;
    state->rvv_raw_win[cur].dst_d  = dst_d;
    state->rvv_raw_win[cur].src1_d = src1_d;
    state->rvv_raw_win[cur].src2_d = src2_d;
    state->rvv_raw_win[cur].src3_d = src3_d;
    state->rvv_raw_win_idx = (cur + 1) % 16;
    if (state->rvv_raw_win_len < 16) {
        state->rvv_raw_win_len++;
    }

    static const int win_sizes[4] = {2, 4, 8, 16};
    for (int w_idx = 0; w_idx < 4; w_idx++) {
        int W = win_sizes[w_idx];
        if (state->rvv_raw_win_len < W) continue;

        uint16_t dep[16] = {0};
        for (int i = 0; i < W; i++) {
            int slot_i = (cur - (W - 1 - i) + 32) % 16;
            short d = state->rvv_raw_win[slot_i].dst_d;
            if (valid_reg_id(d) && d >= RAVE_INI_REG_V) {
                for (int j = i + 1; j < W; j++) {
                    int slot_j = (cur - (W - 1 - j) + 32) % 16;
                    short s1 = state->rvv_raw_win[slot_j].src1_d;
                    short s2 = state->rvv_raw_win[slot_j].src2_d;
                    short s3 = state->rvv_raw_win[slot_j].src3_d;
                    if (s1 == d || s2 == d || s3 == d) {
                        dep[i] |= (1U << j);
                    }
                }
            }
        }

        // Transitive closure (Warshall)
        for (int k = 0; k < W; k++) {
            for (int i = 0; i < W; i++) {
                if (dep[i] & (1U << k)) {
                    dep[i] |= dep[k];
                }
            }
        }

        // Undirected conflict mask
        uint16_t conflict[16] = {0};
        for (int i = 0; i < W; i++) {
            conflict[i] = dep[i];
            for (int j = 0; j < W; j++) {
                if (dep[j] & (1U << i)) {
                    conflict[i] |= (1U << j);
                }
            }
        }

        uint16_t all_mask = (W == 16) ? 0xFFFF : ((1U << W) - 1);
        int mis = rvv_mis_search(all_mask, conflict);

        state->accum_counters.par_avail_sum[w_idx] += (double)mis;
        state->accum_counters.par_avail_count[w_idx] += 1.0;
    }
}

static int is_vector_reg(int reg){
	return reg >= RAVE_INI_REG_V && reg < RAVE_INI_REG_V + NUM_VECTOR_REGS;
}

static int movement_category_for(const instr_data * instr){
    if (is_type_ext(instr->type_ext, MASK_EXT_SUBTYPE, TYPE_EXT_WHOLE_REGISTER)){
        return RAVE_MOVEMENT_WHOLE_REGISTER;
    }
	if (is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_SLIDE)){
		return RAVE_MOVEMENT_SLIDES;
	}
	if (is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_GATHER)){
		return RAVE_MOVEMENT_GATHERS;
	}
	if (is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_COMPRESS)){
		return RAVE_MOVEMENT_COMPRESS;
	}
	if (is_subtype(instr->type, T_REDUCTION) ||
	    is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_REDUCTION)){
		return RAVE_MOVEMENT_REDUCTIONS;
	}
	if (is_type_ext(instr->type_ext, MASK_EXT_OPERAND, TYPE_EXT_WHOLE_REGISTER)){
		return RAVE_MOVEMENT_WHOLE_REGISTER;
	}
	if (is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_MASK)){
		return RAVE_MOVEMENT_MASK_OPS_SCANS;
	}
	if (is_type_ext(instr->type_ext, MASK_EXT_SUBTYPE, TYPE_EXT_ARITH_WIDENING) ||
	    is_type_ext(instr->type_ext, MASK_EXT_SUBTYPE, TYPE_EXT_ARITH_NARROWING)){
		return RAVE_MOVEMENT_LAYOUT_CONVERSIONS;
    }
	return -1;
}

static void count_vector_operands(const instr_data * instr, int * reads, int * writes){
	*reads = 0;
	*writes = 0;
	if (is_vector_reg(instr->src1)) (*reads)++;
	if (is_vector_reg(instr->src2)) (*reads)++;
	if (is_vector_reg(instr->src3)) (*reads)++;
	if (is_vector_reg(instr->dst))  (*writes)++;
}

inline uint64_t synch_threads(thread_state_t * state){
	int cpu_index = state->cpu_index;
	if (cpu_index >= N_THREADS){ //Wait for the thread to be properly initialized
		sched_yield();
		return -1;
	}
	//Core info
	uint64_t thread_timestamp = state->timestamp;

	if (N_THREADS>1){	
		if (state->need_align){
			//Target is global when align=1, is parallle barrier when align=2
			uint64_t target_time = state->need_align==1 ? thread_timestamp : parallel_region.barrier_time;
			if (thread_timestamp < target_time){ //Jump only forward
				if (TRACE_ENABLED && PRINT_PRV && !MUSA){
					set_lock(write_lock);
					trace_row(FD_PRV,mpi_rank, cpu_index, state->last_row, thread_timestamp);
					clean_event(FD_PRV); 
					release_lock(write_lock);
				}
				thread_timestamp=target_time; 
				state->print_first_scalar = 1;
			}
			state->need_align = 0;
		}
	}

	return thread_timestamp;
}

inline void detect_loop_start(profile_t * loop_profile, uint64_t PC){
	if (loop_profile->jump_PC!=(uint64_t)-1){
		if (PC != loop_profile->jump_PC){ //Loop not taken
			update_PC(loop_profile);
			//reset it so next one-it loop is not that far off...
			loop_profile -> loop_weight = 0;
			for(int i=0; i<NUM_VREGS; ++i) loop_profile -> used_vreg[i] = 0;
		}else{ //Loop taken
			if (loop_profile->loop_its == 1){ //If we took the first branch, reset counters and do actual measures
				loop_profile->loop_weight = 0;
				loop_profile->loop_instr = 0;
				loop_profile->loop_vinstr = 0;
				for(int i=0; i<NUM_VREGS; ++i) loop_profile -> used_vreg[i] = 0;
			}else if (loop_profile->loop_its == 2){ //If we took the second iteration, assume iteration 0 was equal to iteration 1.
				loop_profile->loop_weight *= 2;
				loop_profile->loop_instr *= 2;
				loop_profile->loop_vinstr *= 2;
			}
			loop_profile->loop_its++;
		}
		loop_profile->jump_PC=-1;
	}
}

inline void detect_func_start(calltrace_t * call_trace, uint64_t PC){
	if (call_trace->next_is_func == 1){
		call_trace->next_is_func = 0;
		add_to_calltrace(call_trace, PC);
	}
}

void scalar_block_exec(thread_state_t * state, scalar_block_data_t * data){

	uint64_t thread_timestamp = synch_threads(state);
	if (thread_timestamp==(uint64_t)-1) return;
	profile_t * loop_profile = &state->loop_profile;
	int n_instr = data->instr;

	if (TRACE_ENABLED){
        if (INST_TRACE_ENABLED && TRACE_SCALAR){
            for(int i=0; i<n_instr; ++i){
                inst_trace_emit_scalar(state,
                                       data->PCs ? data->PCs[i] : 0,
                                       (data->strings ? data->strings[i] : NULL),
                                       0,
                                       (data->dst_d ? data->dst_d[i] : -1),
                                       (data->src1_d ? data->src1_d[i] : -1),
                                       (data->src2_d ? data->src2_d[i] : -1));
            }
        }
        if(TRACE_EXTENDED){
            for(int i=0; i<n_instr; ++i){
                inst_compressed_t instr_c;

                instr_c.pos = 0;
                instr_c.g_pos = ++state->accum_counters.g_pos;
                instr_c.type = 0x0000; //Scalar instruction
                
                int dst  = data->dst_d  ? data->dst_d[i]  : -1;
                int src1 = data->src1_d ? data->src1_d[i] : -1;
                int src2 = data->src2_d ? data->src2_d[i] : -1;

                if (valid_reg_id(dst))  state->write_reg_deps[dst]  = instr_c;
                if (valid_reg_id(src1)) state->read_reg_deps[src1] = instr_c;
                if (valid_reg_id(src2)) state->read_reg_deps[src2] = instr_c;
            }
        }
		//LOOP PROFILER CONTROL
		if (PRINT_PROFILE){
			for(int i=0; i<n_instr; ++i){
				//Detect loop beginning: 
				detect_loop_start(loop_profile, data->PCs[i]);

				//Detect loop end
				if (data->PCs[i] == data->PC_branch){
					if (data->PCs[i] != loop_profile->curr_loop_PC){ //New loop
						loop_profile->loop_its = 1;
						loop_profile->loop_instr = (data->PC_branch-data->PC_loop)/4; //Approximation
						loop_profile->loop_vinstr = 0; //Approximation
					}
					loop_profile->curr_loop_PC = data->PC_branch; 
					loop_profile->jump_PC = data->PC_loop; 
				}
				loop_profile->loop_instr++;
				loop_profile->loop_weight++;
			}
		}

		//CALL TRACE CONTROL
		if (PRINT_CALLTRACE){
#if 1
			//If we are on a new function, add it to the call trace
			detect_func_start(&state->call_trace, data->PCs[0]);
			//If block ends with a jump, next block will be a new function
			if (data->has_func_jump) state->call_trace.next_is_func = 1; //Next block will be on a new funcion!
#endif
		}

		//LOGFILE
		if (PRINT_LOGFILE && TRACE_SCALAR){
			set_lock(write_lock);
			for(int i=0; i<n_instr; ++i){
				plugin_outs(data->strings[i]);
				plugin_outs("\n");
			}
			release_lock(write_lock);
		}

		//PRV GENERATION
		if (PRINT_PRV){
			int cpu_index = state->cpu_index;
			char row = SCALAR_ROW;
			char row_change = (row != state->last_row) ?1:0;
			if (row_change && !MUSA){ 
				set_lock(write_lock);
				trace_row(FD_PRV,mpi_rank, cpu_index, state->last_row, thread_timestamp);
				clean_event(FD_PRV);
				release_lock(write_lock);
			}
			//Scalar instructions should always be printed when: row changed(1), type changed (2), is first scalar in the trace (3)
			if (row_change || state->last_was_vsetvl || state->print_first_scalar){	
				set_lock(write_lock);
				trace_row(FD_PRV,mpi_rank, cpu_index, row, thread_timestamp);
				trace_event_value(FD_PRV,event_instruction,PRV_SCALAR*!MUSA);
				trace_event_value(FD_PRV,event_class,T_SCALAR);
				trace_event_value(FD_PRV,event_pc, data->PCs[0]);
				if (MUSA){ //for MUSA
					int prev_dst = state->prev_dst;
					state->prev_dst = 0;
					trace_event_value(FD_PRV,event_scalb, 0);
					if (TRACE_ADDR) trace_event_value(FD_PRV,event_addr, 0);
					trace_event_value(FD_PRV,event_dst, prev_dst);
					trace_event_value(FD_PRV,event_src1, 0);
					trace_event_value(FD_PRV,event_src2, 0);
					trace_event_value(FD_PRV,event_src3, 0);
					trace_event_value(FD_PRV,event_vl, 0);
					trace_event_value(FD_PRV,event_sew, 0);
					trace_event_value(FD_PRV,event_lmul, 0);
				}
				release_lock(write_lock);
			}
		}
	}
	
	//Counters
	state->accum_counters.scalar_instr += n_instr;
	state->accum_counters.scalarflops += data->flops;
	state->accum_counters.moved_bytes_s += data->moved_bytes;

	//Update state
	thread_timestamp += n_instr;
	if (thread_timestamp > timestamp) timestamp = thread_timestamp;
	state->scalar_instr_since_vector += n_instr;
	state->timestamp = thread_timestamp;
	state->last_row = SCALAR_ROW;
	state->print_first_scalar = 0;
	state->last_was_vsetvl = 0; 
}

void rolling_scalar_block(uint32_t opcode, uint64_t PC, scalar_block_data_t * data){
		int16_t type = instr_set_scalar_type(opcode);

		if (TRACE_ENABLED && PRINT_PROFILE){
			if (is_subtype(type, T_BRANCH)){
				int64_t offset = get_loop_offset(opcode);
				if (offset <= 0){
					data->PC_branch = PC;
					data->PC_loop = PC + offset;
				}
			}
		}
		if (PRINT_CALLTRACE){
#if 1
			if (data->has_func_jump){
				fprintf(stderr, "JUMP not on last instruction in scalar block, aborting\n");
				exit(-1);
			}
			if (is_subtype(type, T_JUMP)){
				//printf("  Block contains jump\n");
				data->has_func_jump = 1;	
			}
#endif
		}
		if (is_subsubsubtype(type, T_FUSED)) data->flops += 2;
		else if (is_subsubsubtype(type, T_SINGLE)) data->flops += 1;
		else if (is_subtype(type, T_MEMORY)){
			int quadrant = (opcode &0x3);
			if (quadrant == 3){
				int width = (opcode >> 12)&0x3; //3 instead of 7 to %4
				data->moved_bytes += (1<<(width));
			}else if ((quadrant == 0) || (quadrant == 2)){
				int width = (opcode >> 13)&0x3; //3 instead of 7 to %4
				data->moved_bytes += (width==1 || width==3) ? 8 : 4; 
				//Double: 001, 011, 101, 111
				//Single: 010, 110
			}
		}
		++data->instr;
}

inline int count_1s(char * reg, int vlB){
    int i = 0, contador = 0;
    bool terminado = false;
    while(i*8 < vlB && !terminado){
        for(int j = 0; j < 8 && !terminado; j++){
            contador += ((reg[i]>>j)&0x1)?1:0;
            if(i*8+j+1 >= vlB) terminado = true;
        }
        i++;
    }
    return contador;
}

//Vector and vsetvli instructions
void insn_exec(thread_state_t * state, instr_data * instr){

    if(DEBUG_INFO) fprintf(stdout, "Executing instruction: %s\n", instr->asm_string);
	uint64_t thread_timestamp = synch_threads(state);
	if (thread_timestamp==(uint64_t)-1) return;

	profile_t * loop_profile = &state->loop_profile;
	if (TRACE_ENABLED){
		if (PRINT_PROFILE){
			detect_loop_start(loop_profile, instr->PC);
		}
		if (PRINT_CALLTRACE){
#if 1
			detect_func_start(&state->call_trace, instr->PC); 
#endif
		}
	}

	int row = SCALAR_ROW;
	uint64_t vl=0, vtype = 0, sew=3, lmul=1, rvl=0;
	//double lmul_value;
	uint64_t addr = 0;
	int stride = 0;

	int8_t * indexes_8 = NULL;
	int16_t * indexes_16 = NULL;
	int32_t * indexes_32 = NULL;
	int64_t * indexes_64 = NULL;

    // To keep track of dependencies
    if(TRACE_EXTENDED && !is_type(instr->type, T_VSETVL)){
        inst_compressed_t instr_c;
        instr_c.pos   = ++state->accum_counters.v_pos;
        instr_c.g_pos = ++state->accum_counters.g_pos;
        instr_c.type = instr->type_ext;
        if (valid_reg_id(instr->dst_d)) {
            // CASO A: Destino Vectorial (Vector -> Vector)
            if (instr->dst_d >= RAVE_INI_REG_V) {
                // VWAW
                if (state->write_reg_deps[instr->dst_d].pos != 0) {
                    long dist_waw = instr_c.pos - state->write_reg_deps[instr->dst_d].pos;
                    state->accum_counters.VWAW_deps[get_distance_bin(dist_waw)]++;
                    state->accum_counters.total_VWAW_deps++;
                    state->accum_counters.avg_VWAW_deps += dist_waw;

                    if (DEBUG_INFO) {
                        fprintf(stdout, "VWAW dependency on vreg %d: pos %li, prev write at pos %li\n", 
                            instr->dst_d, instr_c.pos, state->write_reg_deps[instr->dst_d].pos);
                    }
                }
                // VWAR
                if (state->read_reg_deps[instr->dst_d].pos != 0) {
                    long dist_war = instr_c.pos - state->read_reg_deps[instr->dst_d].pos;
                    state->accum_counters.VWAR_deps[get_distance_bin(dist_war)]++;
                    state->accum_counters.total_VWAR_deps++;
                    state->accum_counters.avg_VWAR_deps += dist_war;

                    if (DEBUG_INFO) {
                        fprintf(stdout, "VWAR dependency on vreg %d: pos %li, prev read at pos %li\n", 
                            instr->dst_d, instr_c.pos, state->read_reg_deps[instr->dst_d].pos);
                    }
                }
            } 
            // CASO B: Destino Escalar (Vector escribe en registro escalar x1..x31)
            else if (instr->dst_d > 0) {
                // WAW Escalar: Escalar previo escribió en dst_d y ahora el vector lo sobrescribe
                if (state->write_reg_deps[instr->dst_d].g_pos != 0) {
                    long dist_waw_s = instr_c.g_pos - state->write_reg_deps[instr->dst_d].g_pos;
                    if (dist_waw_s <= WAW_DIST) {
                        state->accum_counters.WAW_deps++;
                    }
                    if (DEBUG_INFO) {
                        fprintf(stdout, "Scalar WAW on xreg %d: g_pos %li, prev write at g_pos %li (dist %li)\n",
                            instr->dst_d, instr_c.g_pos, state->write_reg_deps[instr->dst_d].g_pos, dist_waw_s);
                    }
                }

                // WAR Escalar: Escalar previo leyó dst_d y ahora el vector lo sobrescribe
                if (state->read_reg_deps[instr->dst_d].g_pos != 0) {
                    long dist_war_s = instr_c.g_pos - state->read_reg_deps[instr->dst_d].g_pos;
                    if (dist_war_s <= WAR_DIST) {
                        state->accum_counters.WAR_deps++;
                    }
                    if (DEBUG_INFO) {
                        fprintf(stdout, "Scalar WAR on xreg %d: g_pos %li, prev read at g_pos %li (dist %li)\n",
                            instr->dst_d, instr_c.g_pos, state->read_reg_deps[instr->dst_d].g_pos, dist_war_s);
                    }
                }
            }
        }
        #define CHECK_V_TO_V_RAW(src_reg) \
            if(valid_reg_id(src_reg)) { \
                if (src_reg >= RAVE_INI_REG_V && state->write_reg_deps[src_reg].pos != 0 ) { \
                    long dist_chaining = instr_c.pos-state->write_reg_deps[src_reg].pos; \
                    state->accum_counters.VRAW_deps[get_distance_bin(dist_chaining)]++; \
                    state->accum_counters.avg_VRAW_deps += dist_chaining;  \
                    state->accum_counters.total_VRAW_deps++; \
                    if (dist_chaining <= RAW_DIST_CHAINING) { \
                        uint8_t prod = get_prod_idx(state->write_reg_deps[src_reg].type); \
                        uint8_t cons = get_cons_idx(instr->type_ext);  \
                        if (prod != INVALID_RAW_IDX && cons != INVALID_RAW_IDX) { \
                            state->accum_counters.RAW_MATRIX_PC[prod][cons]++; \
                        } \
                    } \
                    if(DEBUG_INFO) { \
                        fprintf(stdout, "VRAW dependency detected on register %d: read at pos %li, instruction: %s, previous write at pos %li\n", \
                            (src_reg), instr_c.pos, instr->asm_string, \
                            state->write_reg_deps[src_reg].pos); \
                    } \
                } else if(src_reg > 0) {  \
                    if(state->write_reg_deps[src_reg].g_pos != 0 && instr_c.g_pos - state->write_reg_deps[src_reg].g_pos <= RAW_DIST) {  \
                        state->accum_counters.RAW_deps++; \
                    } \
                } \
            }

        CHECK_V_TO_V_RAW(instr->src1_d);
        CHECK_V_TO_V_RAW(instr->src2_d);
        CHECK_V_TO_V_RAW(instr->src3_d);
        #undef CHECK_V_TO_V_RAW

        if(valid_reg_id(instr->dst_d)) state->write_reg_deps[instr->dst_d] = instr_c;
        if(valid_reg_id(instr->src1_d)) state->read_reg_deps[instr->src1_d] = instr_c;
        if(valid_reg_id(instr->src2_d)) state->read_reg_deps[instr->src2_d] = instr_c;
        if(valid_reg_id(instr->src3_d)) state->read_reg_deps[instr->src3_d] = instr_c;

        track_vector_parallelism_available(state, instr->dst_d, instr->src1_d, instr->src2_d, instr->src3_d);
    }

	if (is_type(instr->type, T_VECTOR)){ //VECTOR
		record_vector_register(state->accum_counters.vector_register_usage, instr->dst);
		record_vector_register(state->accum_counters.vector_register_usage, instr->src1);
		record_vector_register(state->accum_counters.vector_register_usage, instr->src2);
		record_vector_register(state->accum_counters.vector_register_usage, instr->src3);
		loop_profile -> used_vreg[((instr->instr32)>>7)&0x1F] = 1;
		row = VECTOR_ROW;
		vl = get_vl(state); 
		vtype = get_vtype(state);
#ifdef RVV_07
		sew = (vtype >> 2)&0x7;
		lmul = vtype&0x3;
#else
		sew = (vtype >> 3)&0x7;
		lmul = vtype&0x7;

#endif
		if (is_subtype(instr->type, T_MEMORY)){
			/* la base rs1 hace falta para TRACE_ADDR, para la traza y para
			 * medir el rango de direcciones de las indexadas */
			int is_indexed = is_subsubtype(instr->type, T_INDEX);
			if (TRACE_ADDR || INST_TRACE_ENABLED || is_indexed){
				int src1 = (instr->instr32>>15)&0x1F;
				addr = get_xreg(state,src1);
			}

			if (is_subsubtype(instr->type, T_STRIDE)){
				int src2 = (instr->instr32>>20)&0x1F;
				stride = get_xreg(state,src2);
			}
			int data_sew = sew;      /* SEW del dato (escala del offset) */
#ifndef RVV_07
			int width = (instr->instr32 >> 12)&0x3; 
			sew = width;             /* SEW del indice (como leer el vector) */
#endif

			if((TRACE_INDEXES || INST_TRACE_ENABLED || is_indexed) && is_indexed){
				int src2 = (instr->instr32>>20)&0x1F;
				char * contents = get_vreg(state,src2,vl*(2<<(sew)));
				if (sew==0) indexes_8 = (int8_t*)contents;
				else if (sew==1) indexes_16 = (int16_t*)contents;
				else if (sew==2) indexes_32 = (int32_t*)contents;
				else if (sew==3) indexes_64 = (int64_t*)contents;
			}

			/* Rango de direcciones de esta ejecucion: base + indice*EEW.
			 * Se acumula (max-min) y el numero de ejecuciones por
			 * instruccion; el informe publica la media del rango. */
			if (is_indexed && instr->vinstr_idx >= 0 && vl > 0){
				int64_t emin = 0, emax = -1, step = (int64_t)1 << data_sew;
				for (uint64_t i = 0; i < vl; ++i){
					int64_t idx = indexes_8 ? (int64_t)indexes_8[i] :
					             indexes_16 ? (int64_t)indexes_16[i] :
					             indexes_32 ? (int64_t)indexes_32[i] :
					             indexes_64 ? (int64_t)indexes_64[i] : 0;
					int64_t a = (int64_t)addr + idx * step;
					if (emax < 0){ emin = emax = a; }
					if (a < emin) emin = a;
					if (a > emax) emax = a;
				}
				if (emax >= emin){
					state->accum_counters.addr_range_sum[instr->vinstr_idx] +=
						(double)(emax - emin);
					state->accum_counters.addr_range_n[instr->vinstr_idx] += 1;
				}
			}

			/* si nadie mas los va a usar, liberar los indices ya */
			if (!TRACE_INDEXES && !INST_TRACE_ENABLED){
				free(indexes_8); free(indexes_16); free(indexes_32); free(indexes_64);
				indexes_8 = NULL; indexes_16 = NULL;
				indexes_32 = NULL; indexes_64 = NULL;
			}

#ifndef RVV_07
			if ((((instr->instr32>>20)&0xFF) == 0x28)){
				sew = 0; //sew: 1 byte (8 bits)
				vl = RAVE_VLMAX / 8; //vl
			}
		}else if ( ((instr->instr32&0x7F)==0x57) && (((instr->instr32>>26)&0x3F)==0x27) && (((instr->instr32>>12)&0x07)==0x03)) {
			//Whole register move
			int NFIELDS = (instr->instr32>>15)&0x1F;
			lmul = NFIELDS==7?3 : NFIELDS==3?2 : NFIELDS==1?1 : 0;
			sew = 0; //sew: 1 byte (8 bits)
			vl = RAVE_VLMAX / 8; //vl
#endif
		}
	}else if (is_type(instr->type, T_VSETVL)){ 
		int src1 = (instr->instr32>>15)&0x1F;
		rvl = get_xreg(state,src1);
	}

	//  Logfile  //
	if (TRACE_ENABLED){
		if (PRINT_LOGFILE){
			if (!TRACE_SCALAR && state->scalar_instr_since_vector>0){
				char string[128];
				if (-1 == sprintf(string,"%d", state->scalar_instr_since_vector)){
					printf("Error sprintf\n"); 
					exit(-1);
				}
				set_lock(write_lock);
				plugin_outs(string);
				plugin_outs(" scalar instructions\n");
				release_lock(write_lock);
			}
			set_lock(write_lock);
			plugin_outs(instr->asm_string);
			plugin_outs("\n");
			release_lock(write_lock);
		}

		//  Instruction Trace  //
		if (INST_TRACE_ENABLED){
			if (is_type(instr->type, T_VSETVL)){
				inst_trace_emit_vsetvl(state, instr, rvl, vl, vtype);
			}else{
				void * idx_ptr = indexes_8 ? (void*)indexes_8 :
				                 indexes_16 ? (void*)indexes_16 :
				                 indexes_32 ? (void*)indexes_32 : (void*)indexes_64;
				inst_trace_emit_vector(state, instr, vl, sew, lmul, vtype, addr, stride,
				                       idx_ptr, is_subsubtype(instr->type, T_INDEX));
			}
		}

		//  PRV  //
		if (PRINT_PRV){
			int cpu_index = state->cpu_index;
			char row_change = (row != state->last_row) ?1:0;
			if (row_change && !MUSA){ 
				set_lock(write_lock);
				trace_row(FD_PRV,mpi_rank, cpu_index, state->last_row, thread_timestamp);
				clean_event(FD_PRV);
				release_lock(write_lock);
			}
			//Scalar instructions should always be printed when: row changed(1), type changed (2), is first scalar in the trace (3)
			if (is_type(instr->type, T_VSETVL)){
				set_lock(write_lock);
				trace_row(FD_PRV,mpi_rank, cpu_index, row, thread_timestamp);
				trace_event_value(FD_PRV,event_class,instr->type);
				trace_event_value(FD_PRV,event_pc, instr->PC);
				trace_event_value(FD_PRV,event_instruction, instr->paraver_code);
				if (MUSA){
					int prev_dst = state->prev_dst;
					state->prev_dst = instr->dst;
					trace_event_value(FD_PRV,event_dst, prev_dst);
				}else{
					trace_event_value(FD_PRV,event_dst, instr->dst);
				}
				trace_event_value(FD_PRV,event_src1, instr->src1);
				trace_event_value(FD_PRV,event_rvl, rvl); 
				release_lock(write_lock);
			}else{ //VECTOR 
				set_lock(write_lock);
				trace_row(FD_PRV,mpi_rank, cpu_index, row, thread_timestamp);
				trace_event_value(FD_PRV,event_class,instr->type);
				trace_event_value(FD_PRV,event_pc, instr->PC);
				trace_event_value(FD_PRV,event_scalb, state->scalar_instr_since_vector);
				if (TRACE_ADDR) trace_event_value(FD_PRV,event_addr, addr);
				if (TRACE_INDEXES){
					if (indexes_8 != NULL){
						trace_event_value(FD_PRV, event_indexes, 1);
						for(uint64_t i=0; i<vl; ++i) trace_event_value(FD_PRV, event_indexes+1+i, (int)indexes_8[i]);
						free(indexes_8);
						indexes_8 = NULL;
					}else if (indexes_16 != NULL){
						trace_event_value(FD_PRV, event_indexes, 1);
						for(uint64_t i=0; i<vl; ++i)  trace_event_value(FD_PRV, event_indexes+1+i, (int)indexes_16[i]);
						free(indexes_16);
						indexes_16 = NULL;
					}else if (indexes_32 != NULL){
						trace_event_value(FD_PRV, event_indexes, 1);
						for(uint64_t i=0; i<vl; ++i)  trace_event_value(FD_PRV, event_indexes+1+i, (int)indexes_32[i]);
						free(indexes_32);
						indexes_32 = NULL;
					}else if (indexes_64 != NULL){
						trace_event_value(FD_PRV, event_indexes, 1);
						for(uint64_t i=0; i<vl; ++i)  trace_event_value(FD_PRV, event_indexes+1+i, indexes_64[i]);
						free(indexes_64);
						indexes_64 = NULL;
					}
				}
				if (MUSA){
					int prev_dst = state->prev_dst;
					state->prev_dst = instr->dst;
					trace_event_value(FD_PRV,event_dst, prev_dst);
				}else{
					trace_event_value(FD_PRV,event_dst, instr->dst);
				}
				trace_event_value(FD_PRV,event_src1, instr->src1);
				trace_event_value(FD_PRV,event_src2, instr->src2);
				if (instr->src3>=0) trace_event_value(FD_PRV,event_src3, instr->src3);
				trace_event_value(FD_PRV,event_instruction, instr->paraver_code);
				trace_event_value(FD_PRV,event_vl, vl);
				if (MUSA){
					trace_event_value(FD_PRV,event_sew, 1<<(3+sew));
					int musa_lmul = lmul<4 ? 1<<lmul : lmul==5?18 : lmul==6?14 : lmul==7?12 : 0;
					trace_event_value(FD_PRV,event_lmul, musa_lmul);
				}else{
					trace_event_value(FD_PRV,event_sew, sew);
					trace_event_value(FD_PRV,event_lmul, lmul);
				}
				if (is_type(instr->type, T_VECTOR) && is_subtype(instr->type, T_MEMORY) && is_subsubtype(instr->type, T_STRIDE)){
					trace_event_value(FD_PRV,event_stride, stride);
					state->reset_stride = 1;
				}else if (state->reset_stride){
					trace_event_value(FD_PRV,event_stride, 0);
					state->reset_stride = 0;
				}	
				release_lock(write_lock);
			}
		}
	}

	if (indexes_8) { free(indexes_8); indexes_8 = NULL; }
	if (indexes_16) { free(indexes_16); indexes_16 = NULL; }
	if (indexes_32) { free(indexes_32); indexes_32 = NULL; }
	if (indexes_64) { free(indexes_64); indexes_64 = NULL; }

	// Counters //
	loop_profile->loop_instr++; 
	if (is_type(instr->type, T_VECTOR)) {
		if (PROFILE_WEIGHT == w_ELEMS)
			loop_profile->loop_weight += vl;
		else if (PROFILE_WEIGHT == w_INSTR)
			loop_profile->loop_weight ++;
		loop_profile->loop_vinstr++;

		state->scalar_instr_since_vector=0;
		++state->accum_counters.vector_instr[sew];
		state->accum_counters.velem[sew] += vl;
		// Contador propio de la instruccion (independiente de TRACE_EXTENDED).
		if (instr->vinstr_idx >= 0)
			++state->accum_counters.vinstr[instr->vinstr_idx][sew];

        if(TRACE_EXTENDED){
            // To track accumulated vl, lmul, occupancy...
            vtype = get_vtype(state);
            double real_lmul = 0;
            double real_sew_bits = 8 << sew; // 0,1,2,3 -> SEW 8,16,32,64
            bool ta = (vtype >> 6) & 0x1; // tail agnostic
            bool ma = (vtype >> 7) & 0x1; // mask agnostic
            int masked = is_type_ext(instr->type_ext, TYPE_EXT_MASKED, TYPE_EXT_MASKED);
            int masked_num_1s = -1;
            int has_vector_dest = is_vector_reg(instr->dst);
            if (lmul < 4) {
                real_lmul = (double)(1 << lmul); // 0,1,2,3 -> 1,2,4,8
            } 
            #ifndef RVV_07
            else {
                // Soporte para fractional LMUL en RVV 1.0 (Códigos 5,6,7 -> 1/8, 1/4, 1/2)
                real_lmul = 1.0 / (double)(1 << (8 - lmul)); 
            }
            #endif

            double effective_vl = (vl * real_sew_bits)>(RAVE_VLMAX * real_lmul) ? RAVE_VLMAX * real_lmul : (vl * real_sew_bits);
            state->accum_counters.vl_accumulated_b += effective_vl; // Acumulación de VL en bits
            state->accum_counters.VLMAX_accumulated += RAVE_VLMAX*real_lmul/real_sew_bits; // Acumulación de VLMAX para avg
            state->accum_counters.lmul_accumulated += real_lmul;

            int mcat = movement_category_for(instr);
            if (mcat >= 0) {
                int nreads = 0, nwrites = 0;
                // Calcular el número de bytes que se leen y escriben en la memoria de registros vectoriales
                double bytes = (double)(RAVE_VLMAX * real_lmul) / 8.0;
                count_vector_operands(instr, &nreads, &nwrites);
                state->accum_counters.movement_instr[mcat][sew] += 1.0;
                state->accum_counters.movement_vrf_read[mcat][sew] += bytes * nreads;
                state->accum_counters.movement_vrf_write[mcat][sew] += bytes * nwrites;
            }

            if(effective_vl < real_lmul*RAVE_VLMAX) {
                if(ta)
                    state->accum_counters.ta_count++;
                else
                    state->accum_counters.tu_count++;
            }
            double occupancy = (RAVE_VLMAX * real_lmul > 0) ? effective_vl / (RAVE_VLMAX * real_lmul) : 0;
            state->accum_counters.occupancy_accumulated += occupancy;

            double vlmax_elems = RAVE_VLMAX * real_lmul / real_sew_bits;
            if (has_vector_dest) {
                if (ta) ++state->accum_counters.policy_ta_instr[sew];
                else    ++state->accum_counters.policy_tu_instr[sew];
                state->accum_counters.policy_total_dest_elems[sew] += vlmax_elems;
                if (!ta && (double)vl < vlmax_elems)
                    state->accum_counters.policy_tail_preserved_elems[sew] +=
                        vlmax_elems - (double)vl;
            }

            // Extended type counters.
            if(is_type_ext(instr->type_ext, MASK_EXT_BASE, TYPE_EXT_ARITH)){
                if(is_type_ext(instr->type_ext, TYPE_EXT_MASKED, TYPE_EXT_MASKED)){
                    state->accum_counters.vmasked_instr[1][sew]++;
                    char * mask = get_vreg(state,0,effective_vl);
                    int elems = (int)(effective_vl / real_sew_bits);
                    if (elems > 0) {
                        int num_1s = count_1s(mask, elems);
                        double density = (double)num_1s / (double)elems;
                        state->accum_counters.vmasked_disp[1][sew] += density;
                        state->accum_counters.vmasked_disp_squared[1][sew] += density * density;
                        state->accum_counters.vmasked_active_elems[1][sew] += num_1s;
                        state->accum_counters.vmasked_lanes[1][sew] += elems;
                        masked_num_1s = num_1s;
                    } else {
                        masked_num_1s = 0;
                    }
                    free(mask);
                }
                state->accum_counters.velem_arith[sew] += vl; 
                if(is_type_ext(instr->type_ext, TYPE_EXT_FP, TYPE_EXT_FP)){
                    ++state->accum_counters.vfp_instr[sew];
                    if (is_subsubsubtype(instr->type, T_FUSED)) state->accum_counters.vectorflops += 2*vl; // e.g. FMA. We use current decoding as it's exactly as we needed in this aspect.
                    else state->accum_counters.vectorflops += vl;
                }else{
                    ++state->accum_counters.vint_instr[sew];
                }

                if(is_type_ext(instr->type_ext, MASK_EXT_SUBTYPE, TYPE_EXT_ARITH_WIDENING)){
                    ++state->accum_counters.vwidening_instr[sew];
                    fprintf(stdout, "Widening instruction detected: %s\n", instr->asm_string);
                    if(is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_COMP_FUSED)){
                        ++state->accum_counters.vwfused_instr[sew];
                    } else if(is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_REDUCTION)){
                        if(is_type_ext(instr->type_ext, TYPE_EXT_FP, TYPE_EXT_INT)){
                            ++state->accum_counters.vint_reductions[sew];
                        } else {
                            ++state->accum_counters.vfp_reductions[sew];
                        }
                    }
                } else if(is_type_ext(instr->type_ext, MASK_EXT_SUBTYPE, TYPE_EXT_ARITH_NARROWING)) {
                    ++state->accum_counters.vnarrowing_instr[sew];
                } else {
                    if(is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_MOVE)){
                        ++state->accum_counters.vmove_instr[sew];
                        if(is_type_ext(instr->type_ext, MASK_EXT_OPERAND, TYPE_EXT_WRITE_SCALAR)){
                            ++state->accum_counters.m_inst_v_s[sew];
                            ++state->accum_counters.sv_vector_to_scalar_instr[sew];
                        } else if(is_type_ext(instr->type_ext, MASK_EXT_OPERAND, TYPE_EXT_READ_SCALAR)){
                            ++state->accum_counters.m_inst_s_v[sew];
                            ++state->accum_counters.sv_scalar_to_vector_instr[sew];
                        } else if(is_type_ext(instr->type_ext, MASK_EXT_OPERAND, TYPE_EXT_WHOLE_REGISTER)){
                            ++state->accum_counters.vmove_wh_instr[sew];
                        }
                    } else {
                        if(is_type_ext(instr->type_ext, MASK_EXT_OPERAND, TYPE_EXT_WRITE_SCALAR)){
                            ++state->accum_counters.inst_v_s[sew];
                            ++state->accum_counters.sv_scalar_result_instr[sew];
                        } else if(is_type_ext(instr->type_ext, MASK_EXT_OPERAND, TYPE_EXT_READ_SCALAR)){
                            ++state->accum_counters.inst_s_v[sew];
                            if (is_type_ext(instr->type_ext, TYPE_EXT_FP, TYPE_EXT_FP)) {
                                ++state->accum_counters.sv_broadcast_vf_instr[sew];
                                state->accum_counters.sv_broadcast_vf_elems[sew] += vl;
                            } else {
                                ++state->accum_counters.sv_broadcast_vx_instr[sew];
                                state->accum_counters.sv_broadcast_vx_elems[sew] += vl;
                            }
                        }
                        if(is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_MASK)){
                            ++state->accum_counters.vmask_instr[sew];
                        } else if(is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_COMPRESS)){
                            ++state->accum_counters.vcmprss_instr[sew];
                        } else if(is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_GATHER)){
                            ++state->accum_counters.vgath_instr[sew];
                        } else if(is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_SLIDE)){
                            if(is_type_ext(instr->type_ext, TYPE_EXT_SLIDE_UP, TYPE_EXT_SLIDE_UP)){
                                if(is_type_ext(instr->type_ext, TYPE_EXT_SLIDE_1, TYPE_EXT_SLIDE_1)){
                                    ++state->accum_counters.vslide1up_instr[sew];
                                }else{
                                    ++state->accum_counters.vslideup_instr[sew];
                                }
                            }else{
                                if(is_type_ext(instr->type_ext, TYPE_EXT_SLIDE_1, TYPE_EXT_SLIDE_1)){
                                    ++state->accum_counters.vslide1down_instr[sew];
                                }else{
                                    ++state->accum_counters.vslidedown_instr[sew];
                                }
                            }
                        } else if(is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_COMPUTATION) || 
                                  is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_COMP_FUSED)){
                            ++state->accum_counters.vcomputation_instr[sew];
                            if(is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_COMP_FUSED)){
                                ++state->accum_counters.vfused_instr[sew];
                            }
                        } else if(is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_REDUCTION)){
                            if(is_type_ext(instr->type_ext, TYPE_EXT_FP, TYPE_EXT_INT))
                                ++state->accum_counters.vint_reductions_n[sew];
                            else
                                ++state->accum_counters.vfp_reductions_n[sew];
                        }
                    }
                }

            }else if (is_subtype(instr->type, T_MEMORY)){ // We'll be using type for memory counters.
                if(is_type_ext(instr->type_ext, TYPE_EXT_MASKED, TYPE_EXT_MASKED)){
                    state->accum_counters.vmasked_instr[0][sew]++;
                    char * mask = get_vreg(state,0,effective_vl);
                    int lanes = (int)(effective_vl / real_sew_bits);
                    if (lanes > 0) {
                        int num_1s = count_1s(mask, lanes);
                        double density = (double)num_1s / (double)lanes;
                        state->accum_counters.vmasked_disp[0][sew] += density;
                        state->accum_counters.vmasked_disp_squared[0][sew] += density * density;
                        state->accum_counters.vmasked_active_elems[0][sew] += num_1s;
                        state->accum_counters.vmasked_lanes[0][sew] += lanes;
                        masked_num_1s = num_1s;
                    } else {
                        masked_num_1s = 0;
                    }
                    free(mask);
                }
                state->accum_counters.moved_bytes_v += vl*(1<<(sew));
                state->accum_counters.velem_mem[sew] += vl;
                if (is_subsubtype(instr->type, T_UNIT)) {
                    ++state->accum_counters.vunit_instr[sew];
                    if(is_type_ext(instr->type_ext, MASK_EXT_SUBTYPE, TYPE_EXT_MEMORY_SEGMENTED)){
                        ++state->accum_counters.vseg_instr_unit[sew];
                    }
                } else if (is_subsubtype(instr->type, T_STRIDE)){
                    ++state->accum_counters.vstride_instr[sew];
                    state->accum_counters.agg_strides[sew] += stride;
                    state->accum_counters.agg_strides_squared[sew] += (double)stride * stride;
                    if(is_type_ext(instr->type_ext, MASK_EXT_SUBTYPE, TYPE_EXT_MEMORY_SEGMENTED)){
                        ++state->accum_counters.vseg_instr_stride[sew];
                    }
                }
                else if (is_subsubtype(instr->type, T_INDEX)){
                    ++state->accum_counters.vidx_instr[sew];
                    if(is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_MEMORY_ORDERED)){
                        ++state->accum_counters.vidx_instr_ordered[sew];
                    }else{
                        ++state->accum_counters.vidx_instr_unordered[sew];
                    }
                    if(is_type_ext(instr->type_ext, MASK_EXT_SUBTYPE, TYPE_EXT_MEMORY_SEGMENTED)){
                        ++state->accum_counters.vseg_instr_idx[sew];
                    }
                } 
                else if (is_subsubtype(instr->type, T_SPILL)) ++state->accum_counters.vspill_instr[sew];
            }
            if (masked) {
                if (ma) ++state->accum_counters.policy_ma_instr[sew];
                else    ++state->accum_counters.policy_mu_instr[sew];
            }
            if (has_vector_dest) {
                double active = (masked && masked_num_1s >= 0)
                    ? (double)masked_num_1s : (double)vl;
                state->accum_counters.policy_active_written_elems[sew] += active;
                if (masked && !ma && masked_num_1s >= 0)
                    state->accum_counters.policy_mask_preserved_elems[sew] +=
                        (double)vl - (double)masked_num_1s;
            }

            // End of extended type counters.
        }else {
            if (is_subtype(instr->type, T_ARITH)){
                if (is_subsubtype(instr->type, T_FP)){
                    ++state->accum_counters.vfp_instr[sew];
                    state->accum_counters.velem_arith[sew] += vl; 
                    if (is_subsubsubtype(instr->type, T_FUSED)) state->accum_counters.vectorflops += 2*vl; 
                    else state->accum_counters.vectorflops += vl;
                }else if (is_subsubtype(instr->type, T_INT)){
                    ++state->accum_counters.vint_instr[sew];
                    state->accum_counters.velem_arith[sew] += vl; 
                }
            }else if (is_subtype(instr->type, T_REDUCTION)){
                state->accum_counters.velem_reductions[sew] += vl;
                if (is_subsubtype(instr->type, T_FP)){
                    ++state->accum_counters.vfp_reductions[sew];
                }else if (is_subsubtype(instr->type, T_INT)){
                    ++state->accum_counters.vint_reductions[sew];
                }
            }else if (is_subtype(instr->type, T_MASK)){
                ++state->accum_counters.vmask_instr[sew];
                state->accum_counters.velem_mask[sew] += vl; 
            }else if (is_subtype(instr->type, T_MEMORY)){
                state->accum_counters.moved_bytes_v += vl*(1<<(sew));
                state->accum_counters.velem_mem[sew] += vl; 
                if (is_subsubtype(instr->type, T_UNIT)) ++state->accum_counters.vunit_instr[sew];
                else if (is_subsubtype(instr->type, T_STRIDE)){
                    ++state->accum_counters.vstride_instr[sew];
                    state->accum_counters.agg_strides[sew] += stride;
                    state->accum_counters.agg_strides_squared[sew] += (double)stride * stride;
                }
                else if (is_subsubtype(instr->type, T_INDEX)) ++state->accum_counters.vidx_instr[sew];
                else if (is_subsubtype(instr->type, T_SPILL)) ++state->accum_counters.vspill_instr[sew];
            }
        }
        if (is_subtype(instr->type, T_MEMORY)) {
            if (is_subsubtype(instr->type, T_UNIT) || is_subsubtype(instr->type, T_SPILL))
                state->accum_counters.velem_mem_unit[sew] += vl;
            else if (is_subsubtype(instr->type, T_STRIDE))
                state->accum_counters.velem_mem_stride[sew] += vl;
            else if (is_subsubtype(instr->type, T_INDEX))
                state->accum_counters.velem_mem_indexed[sew] += vl;
        }
	}else if (is_type(instr->type, T_VSETVL)){ 
		loop_profile->loop_weight += 1;
		++state->accum_counters.vsetvl_instr;
		++state->accum_counters.sv_vsetvl_instr[sew];
		if (instr->vinstr_idx >= 0)
			++state->accum_counters.vinstr[instr->vinstr_idx][sew];
	}

	thread_timestamp++;
	//TODO: Use an atomic here?
	if (thread_timestamp > timestamp) timestamp = thread_timestamp;

	//Update state
	state->timestamp = thread_timestamp;
	state->last_row = row;
	state->print_first_scalar = 0;
	state->last_was_vsetvl = is_type(instr->type, T_VSETVL);

}
