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
                double  totvpermutation = counters->vmove_instr[s] + counters->vslide_instr[s] + counters->vgath_instr[s] + counters->vcmprss_instr[s];
                //double  totvred	= counters->vfp_reductions[s] + counters->vint_reductions[s];
                //double  totvother	= counters->vector_instr[s] - totvmem - totvarith - toWtvred - counters->vmask_instr[s];

                indent(fd,++level,0); fprintf(fd,"Arith: "); P_NUMBER(fd,"%.0f",totvarith); PERCENTAGE(fd,totvarith, counters->vector_instr[s],totvarith>0?' ':'\n');
                if (totvarith>0){
                    fprintf(fd, "[avg VL: "); P_VL(fd, "%.2f", counters->velem_arith[s] / totvarith); fprintf(fd," elems]\n");
                    indent(fd,++level,0); fprintf(fd,"FP: "); P_NUMBER(fd,"%.0f", counters->vfp_instr[s]); PERCENTAGE(fd,counters->vfp_instr[s], totvarith,'\n');
                    indent(fd,  level,0); fprintf(fd,"INT: "); P_NUMBER(fd,"%.0f", counters->vint_instr[s]); PERCENTAGE(fd,counters->vint_instr[s], totvarith,'\n');
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
                            indent(fd,  level,0); fprintf(fd,"Slides: "); P_NUMBER(fd,"%.0f", counters->vslide_instr[s]); PERCENTAGE(fd,counters->vslide_instr[s], totvarithnormal,'\n');
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
                    indent(fd,++level,0); fprintf(fd,"unit: "); P_NUMBER(fd,"%.0f", counters->vunit_instr[s]); PERCENTAGE(fd,counters->vunit_instr[s], totvmem,'\n');
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
	double instr       = c->vector_instr[s];
	double elems       = c->velem[s];
	double arith       = c->vfp_instr[s] + c->vint_instr[s];
	double mem         = c->vunit_instr[s] + c->vstride_instr[s] + c->vidx_instr[s] + c->vspill_instr[s];
	double red         = c->vfp_reductions[s] + c->vint_reductions[s];
	double arith_norm  = arith - c->vwidening_instr[s] - c->vnarrowing_instr[s];
	double perm        = c->vmove_instr[s] + c->vslide_instr[s] + c->vgath_instr[s] + c->vcmprss_instr[s];
	double red_n       = c->vfp_reductions_n[s] + c->vint_reductions_n[s];
	double scal_deps   = c->inst_s_v[s] + c->inst_v_s[s];
	double moves_other = c->vmove_instr[s] - (c->m_inst_s_v[s] + c->m_inst_v_s[s] - c->vmove_wh_instr[s]);
	double other       = instr - mem - arith - red - c->vmask_instr[s];
	double elem_other  = elems - c->velem_arith[s] - c->velem_reductions[s] - c->velem_mem[s] - c->velem_mask[s];
	double avg_stride  = safe_div(c->agg_strides[s], c->vstride_instr[s]);
	double stride_var  = c->vstride_instr[s] > 0 ? (c->agg_strides_squared[s]/c->vstride_instr[s] - avg_stride*avg_stride) : 0.0;
	int f = 1, g;

	if (stride_var < 0) stride_var = 0;

	fputc('{', fd);
	json_push();
	json_int(fd, &f, "sew", 1 << (s+3));
	json_int(fd, &f, "instr", instr);
	json_int(fd, &f, "elems", elems);
	json_num(fd, &f, "avg_vl", safe_div(elems, instr));

	json_obj_open(fd, &f, "arith");
	g = 1;
	json_int(fd, &g, "instr", arith);
	json_int(fd, &g, "elems", c->velem_arith[s]);
	json_num(fd, &g, "avg_vl", safe_div(c->velem_arith[s], arith));
	json_int(fd, &g, "fp", c->vfp_instr[s]);
	json_int(fd, &g, "int", c->vint_instr[s]);
	json_int(fd, &g, "narrowing", c->vnarrowing_instr[s]);
	json_int(fd, &g, "widening", c->vwidening_instr[s]);
	json_obj_open(fd, &g, "widening_reductions");
		{ int h = 1; json_int(fd,&h,"total",red); json_int(fd,&h,"fp",c->vfp_reductions[s]); json_int(fd,&h,"int",c->vint_reductions[s]); }
	json_obj_close(fd);
	json_int(fd, &g, "widening_fused", c->vwfused_instr[s]);
	json_int(fd, &g, "normal", arith_norm);
	json_obj_open(fd, &g, "permutations");
		{
			int h = 1;
			json_int(fd, &h, "total", perm);
			json_obj_open(fd, &h, "moves");
				{ int k = 1;
				  json_int(fd,&k,"total",c->vmove_instr[s]);
				  json_int(fd,&k,"read_from_scalar",c->m_inst_s_v[s]);
				  json_int(fd,&k,"write_to_scalar",c->m_inst_v_s[s]);
				  json_int(fd,&k,"whole_register",c->vmove_wh_instr[s]);
				  json_int(fd,&k,"others",moves_other);
				}
			json_obj_close(fd);
			json_int(fd, &h, "slides", c->vslide_instr[s]);
			json_int(fd, &h, "gather", c->vgath_instr[s]);
			json_int(fd, &h, "compress", c->vcmprss_instr[s]);
		}
	json_obj_close(fd);
	json_int(fd, &g, "mask", c->vmask_instr[s]);
	json_obj_open(fd, &g, "reductions");
		{ int h = 1; json_int(fd,&h,"total",red_n); json_int(fd,&h,"fp",c->vfp_reductions_n[s]); json_int(fd,&h,"int",c->vint_reductions_n[s]); }
	json_obj_close(fd);
	json_obj_open(fd, &g, "computation");
		{ int h = 1; json_int(fd,&h,"total",c->vcomputation_instr[s]); json_int(fd,&h,"fused",c->vfused_instr[s]); }
	json_obj_close(fd);
	json_obj_open(fd, &g, "scalar_deps");
		{ int h = 1; json_int(fd,&h,"total",scal_deps); json_int(fd,&h,"read_from_scalar",c->inst_s_v[s]); json_int(fd,&h,"write_to_scalar",c->inst_v_s[s]); }
	json_obj_close(fd);
	json_obj_close(fd); /* arith */

	json_obj_open(fd, &f, "memory");
	g = 1;
	json_int(fd, &g, "instr", mem);
	json_int(fd, &g, "elems", c->velem_mem[s]);
	json_num(fd, &g, "avg_vl", safe_div(c->velem_mem[s], mem));
	json_int(fd, &g, "unit", c->vunit_instr[s]);
	json_int(fd, &g, "segmented_unit", c->vseg_instr_unit[s]);
	json_int(fd, &g, "strided", c->vstride_instr[s]);
	json_num(fd, &g, "avg_stride_b", avg_stride);
	json_num(fd, &g, "stddev_stride_b", sqrt(stride_var));
	json_int(fd, &g, "segmented_strided", c->vseg_instr_stride[s]);
	json_int(fd, &g, "indexed", c->vidx_instr[s]);
	json_int(fd, &g, "indexed_ordered", c->vidx_instr_ordered[s]);
	json_int(fd, &g, "indexed_unordered", c->vidx_instr_unordered[s]);
	json_int(fd, &g, "segmented_indexed", c->vseg_instr_idx[s]);
	json_int(fd, &g, "whole_register", c->vspill_instr[s]);
	json_obj_close(fd);

	json_obj_open(fd, &f, "reduction");
		{ int h = 1; json_int(fd,&h,"instr",red); json_int(fd,&h,"elems",c->velem_reductions[s]); json_int(fd,&h,"fp",c->vfp_reductions[s]); json_int(fd,&h,"int",c->vint_reductions[s]); }
	json_obj_close(fd);
	json_obj_open(fd, &f, "mask");
		{ int h = 1; json_int(fd,&h,"instr",c->vmask_instr[s]); json_int(fd,&h,"elems",c->velem_mask[s]); json_num(fd,&h,"avg_vl",safe_div(c->velem_mask[s], c->vmask_instr[s])); }
	json_obj_close(fd);
	json_obj_open(fd, &f, "other");
		{ int h = 1; json_int(fd,&h,"instr",other); json_int(fd,&h,"elems",elem_other); json_num(fd,&h,"avg_vl",safe_div(elem_other, other)); }
	json_obj_close(fd);

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

	json_obj_open(fd, &f, "instructions");
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
	json_obj_close(fd);

	json_pop();
	json_indent(fd);
	fputc('}', fd);
}
