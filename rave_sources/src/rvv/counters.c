/*********************************************************/
// Copyright (C) 2025 Barcelona Supercomputing Center-Centro Nacional de Supercomputación
// SPDX-License-Identifier: BSD-3-Clause
/*********************************************************/
// * Author: Pablo Vizcaino
// * Email:  pablo.vizcaino@bsc.es
/*********************************************************/

#include "counters.h"
#include "formatting.h"
#include "state.h"
#include <math.h>

void print_counters_human(FILE * fd, rave_counters * counters){
	double scalinstr = counters->scalar_instr + counters->vsetvl_instr;
	double vecinstr = 0;
	double unique_vector_registers = 0;
	for(int s=0; s<SEWS; ++s) vecinstr += counters->vector_instr[s];
	for(int reg=0; reg<NUM_VECTOR_REGS; ++reg) {
		if (counters->vector_register_usage[reg] > 0) ++unique_vector_registers;
	}
	double totinstr = scalinstr + vecinstr; 

	//Others...
	double totbytes = counters->moved_bytes_s + counters->moved_bytes_v;
	int level=ic.prev_nest;
	indent(fd,++level,0); fprintf(fd,"Moved bytes: "); P_NUMBER(fd, "%.0f", totbytes); fprintf(fd,"\n");
	indent(fd,++level,0); fprintf(fd,"Scalar: "); P_NUMBER(fd, "%.0f", counters->moved_bytes_s); PERCENTAGE(fd,counters->moved_bytes_s,totbytes,'\n');
	indent(fd,  level,1); fprintf(fd,"Vector: "); P_NUMBER(fd, "%.0f", counters->moved_bytes_v); PERCENTAGE(fd,counters->moved_bytes_v,totbytes,'\n');

	double totflops = counters->scalarflops + counters->vectorflops;
	indent(fd,--level,0); fprintf(fd,"FLOPs: "); P_NUMBER(fd, "%.0f", totflops); fprintf(fd,"\n");
	indent(fd,++level,0); fprintf(fd,"Scalar: "); P_NUMBER(fd, "%.0f", counters->scalarflops); PERCENTAGE(fd,counters->scalarflops,totflops,'\n');
	indent(fd,  level,1); fprintf(fd,"Vector: "); P_NUMBER(fd, "%.0f", counters->vectorflops); PERCENTAGE(fd,counters->vectorflops,totflops,'\n');

    if(TRACE_EXTENDED){
        double vl_accum = 0;
        for(int s = 0; s<SEWS; ++s) vl_accum += counters->velem[s];
        double avg_vl = (vecinstr > 0) ? (vl_accum / vecinstr) : 0.0;
        double avg_vlb = (vecinstr > 0) ? (counters->vl_accumulated_b / vecinstr) : 0.0;
        double avg_lmul = (vecinstr > 0) ? (counters->lmul_accumulated / vecinstr) : 0.0;
        double avg_occ  = (vecinstr > 0) ? (counters->occupancy_accumulated / vecinstr) : 0.0;

        indent(fd,--level,0); fprintf(fd,"Global stats: "); fprintf(fd,"\n");
        indent(fd,++level,0); fprintf(fd,"Average VL: "); P_NUMBER(fd, "%.2f", avg_vl); fprintf(fd, " [avg: "); P_VL(fd, "%.0f", avg_vlb); fprintf(fd, " bits]"); fprintf(fd,"\n");
        indent(fd,  level,0); fprintf(fd,"Average LMUL: "); P_NUMBER(fd, "%.2f", avg_lmul); fprintf(fd,"\n");
        indent(fd,  level,0); fprintf(fd,"Average occupancy: "); P_NUMBER(fd, "%.4f", avg_occ); PERCENTAGE(fd, avg_occ, 1.0, ' '); fprintf(fd, "[VLEN: "); P_VL(fd, "%i", RAVE_VLMAX); fprintf(fd, " bits; avg VLMAX: "); P_VL(fd, "%.0f", counters->VLMAX_accumulated/vecinstr); fprintf(fd, " elems]"); fprintf(fd,"\n");
        indent(fd,  level,0); fprintf(fd,"tu/ta effective vector instructions: "); P_NUMBER(fd, "%.0f", counters->tu_count); fprintf(fd, " / "); P_NUMBER(fd, "%.0f", counters->ta_count); fprintf(fd, "\n");
        indent(fd,  level,1); fprintf(fd,"Unique vector registers: "); P_NUMBER(fd, "%.0f", unique_vector_registers); fprintf(fd,"\n");
        //Print dependences
        if(RAW_DIST || WAR_DIST || WAW_DIST){
            double sdep = counters->RAW_deps+counters->WAR_deps+counters->WAW_deps;
            double vdep = counters->VRAW_deps+counters->VWAR_deps+counters->VWAW_deps;
            indent(fd,--level,0); fprintf(fd,"Dependencies: "); fprintf(fd,"\n");
            ++level;
            if(TRACE_SCALAR || sdep > 0){
                indent(fd,  level,0); fprintf(fd,"Vector With Scalar: "); P_NUMBER(fd, "%.0f", sdep); fprintf(fd,"\n");
                ++level;
                if(RAW_DIST){
                    indent(fd,  level,0); fprintf(fd,"RAW: "); P_NUMBER(fd, "%.0f", counters->RAW_deps);fprintf(fd,"\n");
                }
                if(WAW_DIST){
                    indent(fd,  level,0); fprintf(fd,"WAW: "); P_NUMBER(fd, "%.0f", counters->WAW_deps); fprintf(fd,"\n");
                }
                if(WAR_DIST){
                    indent(fd,  level,1); fprintf(fd,"WAR: "); P_NUMBER(fd, "%.0f", counters->WAR_deps); fprintf(fd,"\n");
                }
                --level;
            }
            indent(fd,  level,1); fprintf(fd,"Vector: "); P_NUMBER(fd, "%.0f", vdep); fprintf(fd,"\n");
            ++level;
            if(RAW_DIST){
                indent(fd,  level,0); fprintf(fd,"RAW: "); P_NUMBER(fd, "%.0f", counters->VRAW_deps); fprintf(fd, " [avg dist.: "); P_NUMBER(fd, "%.2f", counters->VRAW_deps?counters->avg_VRAW_deps/counters->VRAW_deps:0); fprintf(fd, "]"); fprintf(fd,"\n");
            }
            if(WAW_DIST){
                indent(fd,  level,0); fprintf(fd,"WAW: "); P_NUMBER(fd, "%.0f", counters->VWAW_deps); fprintf(fd, " [avg dist.: "); P_NUMBER(fd, "%.2f", counters->VWAW_deps?counters->avg_VWAW_deps/counters->VWAW_deps:0); fprintf(fd, "]"); fprintf(fd,"\n");
            }
            if(WAR_DIST){
                indent(fd,  level,1); fprintf(fd,"WAR: "); P_NUMBER(fd, "%.0f", counters->VWAR_deps); fprintf(fd, " [avg dist.: "); P_NUMBER(fd, "%.2f", counters->VWAR_deps?counters->avg_VWAR_deps/counters->VWAR_deps:0); fprintf(fd, "]"); fprintf(fd,"\n");
            }
            --level;
        }
    }

	//Print general counters
	indent(fd,--level,1); fprintf(fd,"Instructions: "); P_NUMBER(fd, "%.0f", totinstr); fprintf(fd,"\n");
	indent(fd,++level,0); fprintf(fd,"Scalar instr: "); P_NUMBER(fd, "%.0f", counters->scalar_instr); PERCENTAGE(fd,counters->scalar_instr, totinstr,'\n'); 
	indent(fd,  level,0); fprintf(fd,"Vsetvl instr: "); P_NUMBER(fd, "%.0f", counters->vsetvl_instr); PERCENTAGE(fd,counters->vsetvl_instr, totinstr,'\n');
	indent(fd,  level,1); fprintf(fd,"Vector instr: "); P_NUMBER(fd, "%.0f", vecinstr); PERCENTAGE(fd,vecinstr, totinstr,'\n');

#if 1
	++level;
	//Print SEW-specific counters (vec)
	if (!vecinstr) return;
	for(int s=0; s<SEWS; ++s){
		if (COMPRESS_REPORT && counters->vector_instr[s]<=0) continue;
		indent(fd, level, (s==SEWS-1)); 
		fprintf(fd,"SEW %d %s: ", 1<<(s+3), COMPRESS_REPORT?"v.ins":"vector instr" ); P_NUMBER(fd, "%.0f", counters->vector_instr[s]); PERCENTAGE(fd,counters->vector_instr[s], vecinstr, counters->vector_instr[s]>0?' ':'\n');
		if (counters->vector_instr[s]>0){
			fprintf(fd, "[avg VL: "); P_VL(fd, "%.2f",counters->velem[s] / counters->vector_instr[s]); fprintf(fd," elems]\n");

            if(!TRACE_EXTENDED){
                double  totvmem		= counters->vunit_instr[s] + counters->vstride_instr[s] + counters->vidx_instr[s] + counters->vspill_instr[s];
                double  totvarith	= counters->vfp_instr[s] + counters->vint_instr[s];
                double  totvred	= counters->vfp_reductions[s] + counters->vint_reductions[s];
                double  totvother	= counters->vector_instr[s] - totvmem - totvarith - totvred - counters->vmask_instr[s];
                indent(fd,++level,0); fprintf(fd,"Arith: "); P_NUMBER(fd,"%.0f",totvarith); PERCENTAGE(fd,totvarith, counters->vector_instr[s],totvarith>0?' ':'\n');
                if (totvarith>0){
                    fprintf(fd, "[avg VL: "); P_VL(fd, "%.2f", counters->velem_arith[s] / totvarith); fprintf(fd," elems]\n");
                    indent(fd,++level,0); fprintf(fd,"FP: "); P_NUMBER(fd,"%.0f", counters->vfp_instr[s]); PERCENTAGE(fd,counters->vfp_instr[s], totvarith,'\n');
                    indent(fd,  level,1); fprintf(fd,"INT: "); P_NUMBER(fd,"%.0f", counters->vint_instr[s]); PERCENTAGE(fd,counters->vint_instr[s], totvarith,'\n');
                    --level;
                }
                indent(fd,level,0); fprintf(fd,"Reduction: "); P_NUMBER(fd,"%.0f",totvred); PERCENTAGE(fd,totvred, counters->vector_instr[s],totvred>0?' ':'\n');
                if (totvred>0){
                    fprintf(fd, "[avg VL: "); P_VL(fd, "%.2f", counters->velem_reductions[s] / totvred); fprintf(fd," elems]\n");
                    indent(fd,++level,0); fprintf(fd,"FP: "); P_NUMBER(fd,"%.0f", counters->vfp_reductions[s]); PERCENTAGE(fd,counters->vfp_reductions[s], totvred,'\n');
                    indent(fd,  level,1); fprintf(fd,"INT: "); P_NUMBER(fd,"%.0f", counters->vint_reductions[s]); PERCENTAGE(fd,counters->vint_reductions[s], totvred,'\n');
                    --level;
                }

                indent(fd,level,0); fprintf(fd,"Memory: "); P_NUMBER(fd,"%.0f", totvmem); PERCENTAGE(fd,totvmem, counters->vector_instr[s], totvmem>0?' ':'\n');
                if (totvmem>0){
                    fprintf(fd, "[avg VL: "); P_VL(fd,"%.2f",counters->velem_mem[s] / totvmem); fprintf(fd," elems]\n");
                    indent(fd,++level,0); fprintf(fd,"unit: "); P_NUMBER(fd,"%.0f", counters->vunit_instr[s]); PERCENTAGE(fd,counters->vunit_instr[s], totvmem,'\n');
                    indent(fd,  level,0); fprintf(fd,"strided: "); P_NUMBER(fd,"%.0f", counters->vstride_instr[s]); PERCENTAGE(fd,counters->vstride_instr[s], totvmem,'\n');
                    if (counters->vstride_instr[s] > 0){
                        double avg_stride = counters->agg_strides[s] / counters->vstride_instr[s];
                        indent(fd,++level,0); fprintf(fd,"Avg. Stride (B): "); P_NUMBER(fd,"%.2f", avg_stride); fprintf(fd,"\n");
                        --level;
                    }
                    indent(fd,level,0); fprintf(fd,"indexed: "); P_NUMBER(fd,"%.0f", counters->vidx_instr[s]); PERCENTAGE(fd,counters->vidx_instr[s], totvmem,'\n');
                    indent(fd,level,1); fprintf(fd,"whole-register: "); P_NUMBER(fd,"%.0f", counters->vspill_instr[s]); PERCENTAGE(fd,counters->vspill_instr[s], totvmem,'\n');
                    --level;
                }

                indent(fd,level,0);	fprintf(fd,"Mask: "); P_NUMBER(fd,"%.0f", counters->vmask_instr[s]); PERCENTAGE(fd,counters->vmask_instr[s], counters->vector_instr[s], counters->vmask_instr[s]>0?' ':'\n');
                if (counters->vmask_instr[s]>0) {
                    fprintf(fd, "[avg VL: "); P_VL(fd, "%.2f", counters->velem_mask[s] / counters->vmask_instr[s]); fprintf(fd," elems]\n");
                }

                indent(fd,level,1); fprintf(fd,"Other: "); P_NUMBER(fd,"%.0f", totvother); PERCENTAGE(fd,totvother, counters->vector_instr[s], totvother>0?' ':'\n');
                if (totvother>0) {
                    fprintf(fd, "[avg VL: "); P_VL(fd,"%.2f", (counters->velem[s]-counters->velem_arith[s]-counters->velem_reductions[s]-counters->velem_mem[s]-counters->velem_mask[s])/totvother); fprintf(fd," elems]\n");
                }
                --level;
            } else {
                double  totvmem		= counters->vunit_instr[s] + counters->vstride_instr[s] + counters->vidx_instr[s] + counters->vspill_instr[s];
                double  totvarith	= counters->vfp_instr[s] + counters->vint_instr[s];
                double  totvarithnormal = totvarith - counters->vwidening_instr[s] - counters->vnarrowing_instr[s];
                double  totvslides  = counters->vslide1down_instr[s]+counters->vslide1up_instr[s]+counters->vslidedown_instr[s]+counters->vslideup_instr[s];
                double  totvpermutation = counters->vmove_instr[s] + totvslides + counters->vgath_instr[s] + counters->vcmprss_instr[s];
                //double  totvred	= counters->vfp_reductions[s] + counters->vint_reductions[s];
                //double  totvother	= counters->vector_instr[s] - totvmem - totvarith - toWtvred - counters->vmask_instr[s];

                indent(fd,++level,0); fprintf(fd,"Arith: "); P_NUMBER(fd,"%.0f",totvarith); PERCENTAGE(fd,totvarith, counters->vector_instr[s],totvarith>0?' ':'\n');
                if (totvarith>0){
                    fprintf(fd, "[avg VL: "); P_VL(fd, "%.2f", counters->velem_arith[s] / totvarith); fprintf(fd," elems]\n");
                    indent(fd,++level,0); fprintf(fd,"FP: "); P_NUMBER(fd,"%.0f", counters->vfp_instr[s]); PERCENTAGE(fd,counters->vfp_instr[s], totvarith,'\n');
                    indent(fd,  level,0); fprintf(fd,"INT: "); P_NUMBER(fd,"%.0f", counters->vint_instr[s]); PERCENTAGE(fd,counters->vint_instr[s], totvarith,'\n');
                    indent(fd,  level,0); fprintf(fd,"Masked: "); P_NUMBER(fd,"%.0f", counters->vmasked_instr[1][s]); PERCENTAGE(fd,counters->vmasked_instr[0][s], totvarith,'\n');
                    if(counters->vmasked_instr[1][s] > 0){
                        double n = (double)counters->vmasked_instr[1][s];
                        double avg_density = (double)counters->vmasked_disp[1][s] / n;
                        double mean_sq = (double)counters->vmasked_disp_squared[1][s] / n;
                        double variance = mean_sq - (avg_density * avg_density);
                        double std_dev = sqrt(fmax(0.0, variance));
                        
                        indent(fd, ++level, 0); fprintf(fd, "Avg. density: "); P_NUMBER(fd, "%.2f", avg_density); fprintf(fd, "\n");
                        indent(fd,   level, 1); fprintf(fd, "Std. Dev. density: "); P_NUMBER(fd, "%.2f", std_dev); fprintf(fd, "\n");
                        --level;
                    }
                    indent(fd,  level,0); fprintf(fd,"Types:\n");
                    indent(fd,++level,0); fprintf(fd,"Narrowing: "); P_NUMBER(fd,"%.0f", counters->vnarrowing_instr[s]); PERCENTAGE(fd,counters->vnarrowing_instr[s], totvarith,'\n');
                    indent(fd,  level,0); fprintf(fd,"Widening: "); P_NUMBER(fd,"%.0f", counters->vwidening_instr[s]); PERCENTAGE(fd,counters->vwidening_instr[s], totvarith,'\n');
                    if(counters->vwidening_instr[s] > 0) {
                        double totvred = counters->vfp_reductions[s] + counters->vint_reductions[s];
                        indent(fd,++level,0); fprintf(fd,"Reductions: "); P_NUMBER(fd,"%.0f",totvred); PERCENTAGE(fd,totvred, counters->vwidening_instr[s],totvred>0?' ':'\n');
                        if(totvred){
                            indent(fd,++level,0); fprintf(fd,"FP: "); P_NUMBER(fd,"%.0f",counters->vfp_reductions[s]); PERCENTAGE(fd,counters->vfp_reductions[s], totvred, '\n');
                            indent(fd,  level,1); fprintf(fd,"INT: "); P_NUMBER(fd,"%.0f",counters->vint_reductions[s]); PERCENTAGE(fd,counters->vint_reductions[s], totvred, '\n');
                            --level;
                        }
                        indent(fd,level,1); fprintf(fd,"Fused: "); P_NUMBER(fd,"%.0f", counters->vwfused_instr[s]); PERCENTAGE(fd,counters->vwfused_instr[s], counters->vwidening_instr[s],'\n');
                        --level;
                    }
                    indent(fd,  level,1); fprintf(fd,"Normal: "); P_NUMBER(fd,"%.0f", totvarithnormal); PERCENTAGE(fd,totvarithnormal, totvarith,'\n');
                    if(totvarithnormal){
                        indent(fd,++level,0); fprintf(fd,"Permutations: "); P_NUMBER(fd,"%.0f", totvpermutation); PERCENTAGE(fd,totvpermutation, totvarithnormal,'\n');                    
                        if(totvpermutation){
                            indent(fd,  ++level,0); fprintf(fd,"Moves: "); P_NUMBER(fd,"%.0f", counters->vmove_instr[s]); PERCENTAGE(fd,counters->vmove_instr[s], totvarithnormal,'\n');
                            double totvmdepscal = counters->m_inst_s_v[s] + counters->m_inst_v_s[s] - counters->vmove_wh_instr[s];
                            double totvmoves_other = counters->vmove_instr[s] - totvmdepscal;
                            if (counters->vmove_instr[s] > 0) {
                                indent(fd,++level,0); fprintf(fd,"Read from scalar: "); P_NUMBER(fd,"%.0f", counters->m_inst_s_v[s]); PERCENTAGE(fd,counters->m_inst_s_v[s],counters->vmove_instr[s],'\n');
                                indent(fd,  level,0); fprintf(fd,"Write to scalar: "); P_NUMBER(fd,"%.0f", counters->m_inst_v_s[s]); PERCENTAGE(fd,counters->m_inst_v_s[s],counters->vmove_instr[s],'\n');
                                indent(fd,  level,0); fprintf(fd,"Whole register: "); P_NUMBER(fd,"%.0f", counters->vmove_wh_instr[s]); PERCENTAGE(fd,counters->vmove_wh_instr[s],counters->vmove_instr[s],'\n');  
                                indent(fd,  level,1); fprintf(fd,"Others: "); P_NUMBER(fd,"%.0f", totvmoves_other); PERCENTAGE(fd,totvmoves_other,counters->vmove_instr[s],'\n');
                                --level;
                            }
                            indent(fd,  level,0); fprintf(fd,"Slides: "); P_NUMBER(fd,"%.0f", totvslides); PERCENTAGE(fd,totvslides, totvarithnormal,'\n');
                            if(totvslides > 0){
                                indent(fd,++level,0); fprintf(fd,"slideup: "); P_NUMBER(fd,"%.0f", counters->vslideup_instr[s]); PERCENTAGE(fd,counters->vslideup_instr[s], totvslides,'\n');
                                indent(fd,  level,0); fprintf(fd,"slide1up: "); P_NUMBER(fd,"%.0f", counters->vslide1up_instr[s]); PERCENTAGE(fd,counters->vslide1up_instr[s], totvslides,'\n');
                                indent(fd,  level,0); fprintf(fd,"slidedown: "); P_NUMBER(fd,"%.0f", counters->vslidedown_instr[s]); PERCENTAGE(fd,counters->vslidedown_instr[s], totvslides,'\n');
                                indent(fd,  level,1); fprintf(fd,"slide1down: "); P_NUMBER(fd,"%.0f", counters->vslide1down_instr[s]); PERCENTAGE(fd,counters->vslide1down_instr[s], totvslides,'\n');
                                --level;
                            }
                            indent(fd,  level,0); fprintf(fd,"Gather: "); P_NUMBER(fd,"%.0f", counters->vgath_instr[s]); PERCENTAGE(fd,counters->vgath_instr[s], totvarithnormal,'\n');
                            indent(fd,  level,1); fprintf(fd,"Compress: "); P_NUMBER(fd,"%.0f", counters->vcmprss_instr[s]); PERCENTAGE(fd,counters->vcmprss_instr[s], totvarithnormal,'\n');
                            --level;
                        }
                        indent(fd,  level,0); fprintf(fd,"Mask: "); P_NUMBER(fd,"%.0f", counters->vmask_instr[s]); PERCENTAGE(fd,counters->vmask_instr[s], totvarithnormal,'\n');      
                        double totvred = counters->vfp_reductions_n[s] + counters->vint_reductions_n[s];
                        indent(fd,  level,0); fprintf(fd,"Reductions: "); P_NUMBER(fd,"%.0f",totvred); PERCENTAGE(fd,totvred, counters->vwidening_instr[s],'\n');
                        if(totvred){
                            indent(fd,++level,0); fprintf(fd,"FP: "); P_NUMBER(fd,"%.0f",counters->vfp_reductions_n[s]); PERCENTAGE(fd,counters->vfp_reductions_n[s], totvred, '\n');
                            indent(fd,  level,1); fprintf(fd,"INT: "); P_NUMBER(fd,"%.0f",counters->vint_reductions_n[s]); PERCENTAGE(fd,counters->vint_reductions_n[s], totvred, '\n');
                            --level;
                        }              
                        indent(fd,  level,1); fprintf(fd,"Computation: "); P_NUMBER(fd,"%.0f", counters->vcomputation_instr[s]); PERCENTAGE(fd,counters->vcomputation_instr[s], totvarithnormal,'\n');                    
                        if(counters->vcomputation_instr[s] > 0) {
                            indent(fd,  ++level,1); fprintf(fd,"Fused: "); P_NUMBER(fd,"%.0f", counters->vfused_instr[s]); PERCENTAGE(fd,counters->vfused_instr[s], counters->vcomputation_instr[s],'\n');
                            --level;
                        }
                        --level;
                    } --level;
                    double totvdepscal = counters->inst_s_v[s] + counters->inst_v_s[s];
                    indent(fd,  level,1); fprintf(fd,"Scalar deps (excl. moves): "); P_NUMBER(fd,"%.0f", totvdepscal); fprintf(fd, "\n");
                    if (totvdepscal > 0) {
                        indent(fd,++level,0); fprintf(fd,"Read from scalar: "); P_NUMBER(fd,"%.0f", counters->inst_s_v[s]); PERCENTAGE(fd,counters->inst_s_v[s],totvdepscal,'\n');
                        indent(fd,  level,1); fprintf(fd,"Write to scalar: "); P_NUMBER(fd,"%.0f", counters->inst_v_s[s]); PERCENTAGE(fd,counters->inst_v_s[s],totvdepscal,'\n');
                        --level;
                    }
                    --level;
                }

                indent(fd,level,1); fprintf(fd,"Memory: "); P_NUMBER(fd,"%.0f", totvmem); PERCENTAGE(fd,totvmem, counters->vector_instr[s], totvmem>0?' ':'\n');
                if (totvmem>0){
                    fprintf(fd, "[avg VL: "); P_VL(fd,"%.2f",counters->velem_mem[s] / totvmem); fprintf(fd," elems]\n");
                    indent(fd,++level,0); fprintf(fd,"Masked: "); P_NUMBER(fd,"%.0f", counters->vmasked_instr[0][s]); PERCENTAGE(fd,counters->vmasked_instr[0][s], totvarith,'\n');
                    if(counters->vmasked_instr[0][s] > 0){
                        double n = (double)counters->vmasked_instr[0][s];
                        double avg_density = (double)counters->vmasked_disp[0][s] / n;
                        double mean_sq = (double)counters->vmasked_disp_squared[0][s] / n;
                        double variance = mean_sq - (avg_density * avg_density);
                        double std_dev = sqrt(fmax(0.0, variance));
                        
                        indent(fd, ++level, 0); fprintf(fd, "Avg. density: "); P_NUMBER(fd, "%.2f", avg_density); fprintf(fd, "\n");
                        indent(fd,   level, 1); fprintf(fd, "Std. Dev. density: "); P_NUMBER(fd, "%.2f", std_dev); fprintf(fd, "\n");
                        --level;
                    }
                    indent(fd,  level,0); fprintf(fd,"unit: "); P_NUMBER(fd,"%.0f", counters->vunit_instr[s]); PERCENTAGE(fd,counters->vunit_instr[s], totvmem,'\n');
                    if(counters->vseg_instr_unit[s] > 0) {
                        indent(fd,++level,1); fprintf(fd,"Segmented: "); P_NUMBER(fd,"%.0f", counters->vseg_instr_unit[s]); PERCENTAGE(fd,counters->vseg_instr_unit[s], counters->vunit_instr[s],'\n');
                        --level;
                    }
                    indent(fd,  level,0); fprintf(fd,"strided: "); P_NUMBER(fd,"%.0f", counters->vstride_instr[s]); PERCENTAGE(fd,counters->vstride_instr[s], totvmem,'\n');
                    if (counters->vstride_instr[s] > 0){
                        double avg_stride = counters->agg_strides[s] / counters->vstride_instr[s];
                        double stride_variance = counters->agg_strides_squared[s] / counters->vstride_instr[s] - avg_stride * avg_stride;
                        indent(fd,++level,0); fprintf(fd,"Segmented: "); P_NUMBER(fd,"%.0f", counters->vseg_instr_stride[s]); PERCENTAGE(fd,counters->vseg_instr_stride[s], counters->vstride_instr[s],'\n');
                        indent(fd,level,0); fprintf(fd,"Avg. Stride (B): "); P_NUMBER(fd,"%.2f", avg_stride); fprintf(fd,"\n");
                        indent(fd,level,1); fprintf(fd,"Std. Dev. Stride (B): "); P_NUMBER(fd,"%.2f", sqrt(fmax(0.0, stride_variance))); fprintf(fd,"\n");
                        --level;
                    }
                    indent(fd,level,0); fprintf(fd,"indexed: "); P_NUMBER(fd,"%.0f", counters->vidx_instr[s]); PERCENTAGE(fd,counters->vidx_instr[s], totvmem,'\n');
                    if(counters->vidx_instr[s] > 0) {
                        indent(fd,++level,0); fprintf(fd,"Segmented: "); P_NUMBER(fd,"%.0f", counters->vseg_instr_idx[s]); PERCENTAGE(fd,counters->vseg_instr_idx[s], counters->vidx_instr[s],'\n');
                        indent(fd,  level,0); fprintf(fd,"Ordered: "); P_NUMBER(fd,"%.0f", counters->vidx_instr_ordered[s]); PERCENTAGE(fd,counters->vidx_instr_ordered[s], counters->vidx_instr[s],'\n');
                        indent(fd,  level,1); fprintf(fd,"Unordered: "); P_NUMBER(fd,"%.0f", counters->vidx_instr_unordered[s]); PERCENTAGE(fd,counters->vidx_instr_unordered[s], counters->vidx_instr[s],'\n');
                        --level;
                    }
                    indent(fd,level,1); fprintf(fd,"whole-register: "); P_NUMBER(fd,"%.0f", counters->vspill_instr[s]); PERCENTAGE(fd,counters->vspill_instr[s], totvmem,'\n');
                    --level;
                }
                --level;
            }
		}
	}
#endif
}

void print_csv_header(FILE * fd){
		fprintf(fd,"moved_bytes_s,moved_bytes_v");
		fprintf(fd,",flops_s,flops_v");
		fprintf(fd,",tot_instr,scalar_instr,vsetvl_instr,vec_instr");
		for(int s=0; s<SEWS; ++s){
		 	int sew=1<<(s+3);	
			fprintf(fd,	",v_e%d_instr,v_e%d_elems", sew,sew);
			fprintf(fd, ",v_e%d_arith,v_e%d_arith_elems,v_e%d_fp,v_e%d_int",sew,sew,sew,sew);
			fprintf(fd, ",v_e%d_reduction,v_e%d_reduction_elems,v_e%d_reduction_fp,v_e%d_reduction_int",sew,sew,sew,sew);
			fprintf(fd, ",v_e%d_mem,v_e%d_mem_elems,v_e%d_memunit,v_e%d_memidx,v_e%d_memstrided,v_e%d_avg_stride,v_e%d_memspill",sew,sew,sew,sew,sew,sew,sew);
			fprintf(fd, ",v_e%d_mask,v_e%d_mask_elems",sew,sew);
			fprintf(fd, ",v_e%d_other,v_e%d_other_elems",sew,sew);
		}
		//fprintf(fd,"\n");
}

void print_counters_csv(FILE * fd, rave_counters * counters){
	double totinstr = counters->scalar_instr + counters->vsetvl_instr;
	double totvec = 0;
	for(int s=0; s<SEWS; ++s) totvec += counters->vector_instr[s];
	totinstr += totvec;

	//Print general counters
	fprintf(fd,",%.0f,%.0f", counters->moved_bytes_s, counters->moved_bytes_v);
	fprintf(fd,",%.0f,%.0f", counters->scalarflops, counters->vectorflops); 
	fprintf(fd,",%.0f,%.0f,%.0f,%.0f", totinstr, counters->scalar_instr, counters->vsetvl_instr, totvec);

	//Print SEW-specific counters (vec)
	for(int s=0; s<SEWS; ++s){
		double  totvmem		= counters->vunit_instr[s] + counters->vstride_instr[s] + counters->vidx_instr[s] + counters->vspill_instr[s];
		double  totvarith	= counters->vfp_instr[s] + counters->vint_instr[s];
		double  totvred	= counters->vfp_reductions[s] + counters->vint_reductions[s];
		double  totvother	= counters->vector_instr[s] - totvmem - totvarith - totvred - counters->vmask_instr[s];
		double strides = (counters->vstride_instr[s] > 0)? counters->agg_strides[s] / counters->vstride_instr[s] : 0;
		double velem_other = counters->velem[s] - counters->velem_arith[s] - counters->velem_reductions[s] - counters->velem_mem[s] - counters->velem_mask[s];

		fprintf(fd,",%.0f,%.0f", counters->vector_instr[s], counters->velem[s]);
		fprintf(fd,",%.0f,%.0f,%.0f,%.0f", totvarith, counters->velem_arith[s], counters->vfp_instr[s], counters->vint_instr[s]);
		fprintf(fd,",%.0f,%.0f,%.0f,%.0f", totvred, counters->velem_reductions[s], counters->vfp_reductions[s], counters->vint_reductions[s]);
		fprintf(fd,",%.0f,%.0f,%.0f,%.0f,%.0f,%.2f,%.0f", totvmem, counters->velem_mem[s], counters->vunit_instr[s], counters->vidx_instr[s], counters->vstride_instr[s], strides, counters->vspill_instr[s]);
		fprintf(fd,",%.0f,%.0f", counters->vmask_instr[s], counters->velem_mask[s]); 
		fprintf(fd,",%.0f,%.0f", totvother, velem_other);
	}
	//fprintf(fd,"\n");
}

/* -------------------------------------------------------------------------- */
/* JSON report                                                                 */
/* -------------------------------------------------------------------------- */

static double safe_div(double num, double den){
	return den > 0 ? num/den : 0.0;
}

static void print_sew_json(FILE * fd, rave_counters * c, int s){
	/* El detalle de QUE instrucciones se han ejecutado sale del arbol de
	 * categorias (nivel de region). Aqui quedan las estadisticas, incluidas las
	 * dos que NO se pueden deducir de el:
	 *   - los elementos procesados por tipo de operacion (el arbol cuenta
	 *     instrucciones, no elementos), que ademas se solapan: una mascara suma
	 *     en `arith` y en `mask`, asi que su suma puede pasar de `elems`.
	 *   - el stride medio (y su desviacion) de los accesos strided, que es una
	 *     media interna, no un recuento.
	 */
	double avg_stride = safe_div(c->agg_strides[s], c->vstride_instr[s]);
	double stride_var = c->vstride_instr[s] > 0
		? (c->agg_strides_squared[s]/c->vstride_instr[s] - avg_stride*avg_stride)
		: 0.0;
	int f = 1;

	if (stride_var < 0) stride_var = 0;

	fputc('{', fd);
	json_push();
	json_int(fd, &f, "sew", 1 << (s+3));
	json_int(fd, &f, "instr", c->vector_instr[s]);
	json_int(fd, &f, "elems", c->velem[s]);
	json_num(fd, &f, "avg_vl", safe_div(c->velem[s], c->vector_instr[s]));
	json_obj_open(fd, &f, "elems_by_kind");
		{ int h = 1;
		  json_int(fd, &h, "arith", c->velem_arith[s]);
		  json_int(fd, &h, "memory", c->velem_mem[s]);
		  json_int(fd, &h, "reduction", c->velem_reductions[s]);
		  json_int(fd, &h, "mask", c->velem_mask[s]); }
	json_obj_close(fd);
	json_num(fd, &f, "avg_stride_b", avg_stride);
	json_num(fd, &f, "stddev_stride_b", sqrt(stride_var));
	json_pop();
	json_indent(fd);
	fputc('}', fd);
}

void print_counters_json(FILE * fd, rave_counters * c){
	double vecinstr = 0, unique_regs = 0, vl_accum = 0, totinstr, totbytes, totflops;
	int f = 1, g;

	for(int s=0; s<SEWS; ++s){
		vecinstr += c->vector_instr[s];
		vl_accum += c->velem[s];
	}
	for(int r=0; r<NUM_VECTOR_REGS; ++r) if (c->vector_register_usage[r] > 0) ++unique_regs;
	totinstr = c->scalar_instr + c->vsetvl_instr + vecinstr;
	totbytes = c->moved_bytes_s + c->moved_bytes_v;
	totflops = c->scalarflops + c->vectorflops;

	fputc('{', fd);
	json_push();

	json_obj_open(fd, &f, "moved_bytes");
		{ int h = 1; json_int(fd,&h,"total",totbytes); json_int(fd,&h,"scalar",c->moved_bytes_s); json_int(fd,&h,"vector",c->moved_bytes_v); }
	json_obj_close(fd);

	json_obj_open(fd, &f, "flops");
		{ int h = 1; json_int(fd,&h,"total",totflops); json_int(fd,&h,"scalar",c->scalarflops); json_int(fd,&h,"vector",c->vectorflops); }
	json_obj_close(fd);

	json_obj_open(fd, &f, "global_stats");
	g = 1;
	json_num(fd, &g, "avg_vl_elems", safe_div(vl_accum, vecinstr));
	json_num(fd, &g, "avg_vl_bits", safe_div(c->vl_accumulated_b, vecinstr));
	json_num(fd, &g, "avg_lmul", safe_div(c->lmul_accumulated, vecinstr));
	json_num(fd, &g, "avg_occupancy", safe_div(c->occupancy_accumulated, vecinstr));
	json_int(fd, &g, "vlen_bits", RAVE_VLMAX);
	json_num(fd, &g, "avg_vlmax_elems", safe_div(c->VLMAX_accumulated, vecinstr));
	json_int(fd, &g, "tu_effective", c->tu_count);
	json_int(fd, &g, "ta_effective", c->ta_count);
	json_int(fd, &g, "unique_vector_registers", unique_regs);
	json_obj_close(fd);

	json_obj_open(fd, &f, "dependencies");
	g = 1;
	json_obj_open(fd, &g, "scalar");
		{ int h = 1;
		  json_int(fd,&h,"total",c->RAW_deps + c->WAR_deps + c->WAW_deps);
		  json_int(fd,&h,"raw",c->RAW_deps);
		  json_int(fd,&h,"waw",c->WAW_deps);
		  json_int(fd,&h,"war",c->WAR_deps); }
	json_obj_close(fd);
	json_obj_open(fd, &g, "vector");
		{ int h = 1;
		  json_int(fd,&h,"total",c->VRAW_deps + c->VWAR_deps + c->VWAW_deps);
		  json_obj_open(fd,&h,"raw");
			{ int k = 1; json_int(fd,&k,"count",c->VRAW_deps); json_num(fd,&k,"avg_dist",safe_div(c->avg_VRAW_deps, c->VRAW_deps)); }
		  json_obj_close(fd);
		  json_obj_open(fd,&h,"waw");
			{ int k = 1; json_int(fd,&k,"count",c->VWAW_deps); json_num(fd,&k,"avg_dist",safe_div(c->avg_VWAW_deps, c->VWAW_deps)); }
		  json_obj_close(fd);
		  json_obj_open(fd,&h,"war");
			{ int k = 1; json_int(fd,&k,"count",c->VWAR_deps); json_num(fd,&k,"avg_dist",safe_div(c->avg_VWAR_deps, c->VWAR_deps)); }
		  json_obj_close(fd); }
	json_obj_close(fd);
	json_obj_close(fd);

	/* Distancias de las indexadas: para cada instruccion, media del rango
	 * (max-min) de direcciones; en el JSON, la media de esas medias dando el
	 * mismo peso a cada instruccion (no al numero de ejecuciones). */
	json_obj_open(fd, &f, "memory_addresses");
	{
		const char * grupos[2] = { "load", "store" };
		int h0 = 1;      /* primer miembro de memory_addresses */
		for (int g = 0; g < 2; ++g){
			double n_instr = 0, suma_medias = 0, ejecuciones = 0;
			int h = 1;
			for (int i = 0; i < RAVE_VINSTR_COUNT; ++i){
				const char * mn;
				if (rave_vinstr_category_of(i) != RAVE_VINSTR_CAT_INDEXED) continue;
				if (c->addr_range_n[i] <= 0) continue;
				mn = rave_vinstr_mnemonic(i);
				if (mn == NULL) continue;
				if (g == 0 && !(mn[0] == 'v' && mn[1] == 'l')) continue;
				if (g == 1 && !(mn[0] == 'v' && mn[1] == 's')) continue;
				suma_medias += c->addr_range_sum[i] / c->addr_range_n[i];
				ejecuciones += c->addr_range_n[i];
				n_instr += 1;
			}
			if (n_instr <= 0) continue;
			json_obj_open(fd, &h0, grupos[g]);
				json_int(fd, &h, "instr", n_instr);
				json_int(fd, &h, "executions", ejecuciones);
				json_num(fd, &h, "avg_range_b", suma_medias / n_instr);
			json_obj_close(fd);
		}
	}
	json_obj_close(fd);

	json_obj_open(fd, &f, "instructions_previous_version");
	g = 1;
	json_int(fd, &g, "total", totinstr);
	json_int(fd, &g, "scalar", c->scalar_instr);
	json_int(fd, &g, "vsetvl", c->vsetvl_instr);
	json_int(fd, &g, "vector", vecinstr);
	json_arr_open(fd, &g, "sew");
	{
		int ef = 1;
		for(int s=0; s<SEWS; ++s){
			json_elem(fd, &ef);
			print_sew_json(fd, c, s);
		}
	}
	json_arr_close(fd);
	json_obj_close(fd);     /* cierra "instructions_previous_version" */

	json_pop();             /* cierra "counters" */
	json_indent(fd);
	fputc('}', fd);
}

/* -------------------------------------------------------------------------- */
/* Arbol de contadores por instruccion (mismos niveles que docs/rvv instr.md)  */
/* -------------------------------------------------------------------------- */

/* Suma cada contador sobre los cuatro SEW y acumula por categoria (una
 * instruccion pertenece a su categoria hoja y a toda su cadena de padres). */
static void vinstr_totals(rave_counters * c, double * instr_tot, double * cat_tot){
	int i, cat;
	for (i = 0; i < RAVE_VINSTR_COUNT; ++i){
		double total = 0;
		for (int s = 0; s < SEWS; ++s) total += c->vinstr[i][s];
		instr_tot[i] = total;
	}
	for (i = 0; i < RAVE_VINSTR_NCAT; ++i) cat_tot[i] = 0;
	for (i = 0; i < RAVE_VINSTR_COUNT; ++i)
		for (cat = rave_vinstr_category_of(i); cat >= 0; cat = rave_vinstr_categories[cat].parent)
			cat_tot[cat] += instr_tot[i];
}

/* Un objeto por categoria con las instrucciones que ha ejecutado la region (y
 * las subcategorias con cuenta). Las categorias e instrucciones a cero no se
 * emiten, para que el JSON no lleve 630 claves vacias. */
static void print_instr_category_json(FILE * fd, int * first, int cat,
                                     const double * instr_tot, const double * cat_tot){
	int inner = 1;
	json_obj_open(fd, first, rave_vinstr_categories[cat].name);
	for (int c = 0; c < RAVE_VINSTR_NCAT; ++c)
		if (rave_vinstr_categories[c].parent == cat && cat_tot[c] > 0)
			print_instr_category_json(fd, &inner, c, instr_tot, cat_tot);
	/* Las indexadas no se emiten una a una: se suman por prefijo del
	 * mnemonico en dos claves, `load` (vl*) y `store` (vs*). */
	if (cat == RAVE_VINSTR_CAT_INDEXED){
		double load = 0, store = 0;
		for (int i = 0; i < RAVE_VINSTR_COUNT; ++i){
			const char * mn;
			if (rave_vinstr_category_of(i) != cat || instr_tot[i] <= 0) continue;
			mn = rave_vinstr_mnemonic(i);
			if (mn == NULL) continue;
			if (mn[0] == 'v' && mn[1] == 'l') load += instr_tot[i];
			else if (mn[0] == 'v' && mn[1] == 's') store += instr_tot[i];
		}
		if (load > 0) json_int(fd, &inner, "load", load);
		if (store > 0) json_int(fd, &inner, "store", store);
		json_obj_close(fd);
		return;
	}
	for (int i = 0; i < RAVE_VINSTR_COUNT; ++i)
		if (rave_vinstr_category_of(i) == cat && instr_tot[i] > 0)
			json_int(fd, &inner, rave_vinstr_name(i), instr_tot[i]);
	json_obj_close(fd);
}

void print_instr_tree_json(FILE * fd, int * first, rave_counters * counters){
	double instr_tot[RAVE_VINSTR_COUNT];
	double cat_tot[RAVE_VINSTR_NCAT];
	int inner = 1;
	vinstr_totals(counters, instr_tot, cat_tot);
	/* Las estadisticas de siempre viven en counters.instructions_previous_version;
	 * el recuento por instruccion del listado va aqui, en "instructions". */
	json_obj_open(fd, first, "instructions");
	for (int cat = 0; cat < RAVE_VINSTR_NCAT; ++cat)
		if (rave_vinstr_categories[cat].parent == -1 && cat_tot[cat] > 0)
			print_instr_category_json(fd, &inner, cat, instr_tot, cat_tot);
	json_obj_close(fd);
}
