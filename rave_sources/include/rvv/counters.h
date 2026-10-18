/*********************************************************/
// Copyright (C) 2025 Barcelona Supercomputing Center-Centro Nacional de Supercomputación
// SPDX-License-Identifier: BSD-3-Clause
/*********************************************************/
// * Author: Pablo Vizcaino
// * Email:  pablo.vizcaino@bsc.es
/*********************************************************/
#pragma once

#include <stdio.h>

#include "instr_counters.h"

#define NUM_VECTOR_REGS 32

#define PERCENTAGE(fd,x,y,fin)\
	if (x>0) {\
		fprintf(fd, " "); P_PERCENTAGE(fd, "(%.2f%%)", ((y)==0?0:(100.0*(x))/(y)));\
	}\
	fprintf(fd,"%c",fin);

#define SEWS 4
struct rave_counters{
				double scalar_instr;
				double vsetvl_instr;
				double vector_instr[SEWS];
				double velem[SEWS];

				/* Contador propio de cada instruccion vectorial: una entrada
				 * por forma de encoding, indexada por rave_vinstr_id. */
				double vinstr[RAVE_VINSTR_COUNT][SEWS];

				//Memory
				double velem_mem[SEWS];
				double vunit_instr[SEWS];
				double vstride_instr[SEWS];
				double agg_strides[SEWS];
				double agg_strides_squared[SEWS];
				double vidx_instr[SEWS];
                double vidx_instr_ordered[SEWS];
                double vidx_instr_unordered[SEWS]; // We could remove `vidx_instr`. We'll keep it for though.
				double vspill_instr[SEWS];
                double vseg_instr_unit[SEWS];
                double vseg_instr_stride[SEWS];
                double vseg_instr_idx[SEWS];

				double vector_register_usage[NUM_VECTOR_REGS];

				//Arith
				double velem_arith[SEWS];
				double vfp_instr[SEWS];
				double vint_instr[SEWS];
                double vnarrowing_instr[SEWS];
                double vwidening_instr[SEWS];
                double vmove_instr[SEWS];
                double vmove_wh_instr[SEWS];
                /**Slides */
                double vslideup_instr[SEWS];
                double vslide1up_instr[SEWS];
                double vslidedown_instr[SEWS];
                double vslide1down_instr[SEWS];
                /**Others */
                double vgath_instr[SEWS];
                double vcmprss_instr[SEWS];
                double vcomputation_instr[SEWS];
                double vwfused_instr[SEWS];
                double vfused_instr[SEWS];
                /**Masked instructions */
                double vmasked_instr[2][SEWS];         // Divided into 2 cat: L/S & arith.
                double vmasked_disp[2][SEWS];          // To track accumulates & thus be able after to tell the average.
                double vmasked_disp_squared[2][SEWS];  // To be able to tell the variance of the average.

				//Reductions
				double velem_reductions[SEWS];
				double vfp_reductions[SEWS];
				double vint_reductions[SEWS];
				double vfp_reductions_n[SEWS];
				double vint_reductions_n[SEWS];

				//Masks
				double velem_mask[SEWS];
				double vmask_instr[SEWS];

				//General
				double moved_bytes_s;
				double moved_bytes_v;
				double scalarflops;
				double vectorflops;

                // To check scalar-vector instructions and distinguish common from moves.
                double inst_s_v[SEWS];
                double inst_v_s[SEWS];
                double m_inst_s_v[SEWS];
                double m_inst_v_s[SEWS];

                // To track accumulates and thus be able after to tell the average.
                double vl_accumulated_b;
                double lmul_accumulated;
                double occupancy_accumulated;
                double VLMAX_accumulated;

                // To keep track of the number of dependencies(scalar and vector)
                double VRAW_deps;
                double VWAR_deps;
                double VWAW_deps;
                double RAW_deps;
                double WAR_deps;
                double WAW_deps;
                double avg_VRAW_deps;
                double avg_VWAR_deps;
                double avg_VWAW_deps;

                // To track the number of (effective) times we've had ta vs tu
                double ta_count;
                double tu_count;
};
typedef struct rave_counters rave_counters;


void print_counters_human(FILE * fd, rave_counters * counters);
void print_csv_header(FILE * fd);
void print_counters_csv(FILE * fd, rave_counters * counters);
void print_counters_json(FILE * fd, rave_counters * counters);

/* Contadores por instruccion agrupados como en "docs/rvv instr.md": cuelga sus
 * categorias directamente de la region (una clave por nivel del listado). */
void print_instr_tree_json(FILE * fd, int * first, rave_counters * counters);
