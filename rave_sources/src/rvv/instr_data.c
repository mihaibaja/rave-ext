/*********************************************************/
// Copyright (C) 2026 Barcelona Supercomputing Center-Centro Nacional de Supercomputación
// SPDX-License-Identifier: BSD-3-Clause
/*********************************************************/
// * Author: Pablo Vizcaino
// * Email:  pablo.vizcaino@bsc.es
/*********************************************************/

#include "instr_data.h"
#include "utils.h" //For contains_string(char*)
#include "rave2prv.h"
#include "instr2prv.h"
#include "state.h"

int64_t get_loop_offset(uint32_t insn_opcode){
	int64_t offset;
	if ((insn_opcode&0x3)!=0x3){ //Compressed
		offset = (((insn_opcode>>3)&0x3)<<1) + (((insn_opcode>>10)&0x3)<<3) + (((insn_opcode>>2)&0x1)<<5) + (((insn_opcode>>5)&0x3)<<6) + (((insn_opcode>>12)&0x1)<<8);
		//Sign extend the 9 bit number
		offset <<= (64-9);
		offset >>= (64-9);
	}else{
		offset = (((insn_opcode>>31)&0x1)<<12) + (((insn_opcode>>7)&0x1)<<11) + (((insn_opcode>>25)&0x3F)<<5) + (((insn_opcode>>8)&0xF)<<1);
		//Sign extend the 13 bit number
		offset <<= (64-13);
		offset >>= (64-13);
	}
	return offset;
}

int16_t instr_set_vector_type(uint32_t insn_opcode){
				int16_t type = T_VECTOR; 
				int16_t subtype = T_OTHER;
				int16_t subsubtype = T_NOTYPE;
				int16_t subsubsubtype = T_NOTYPE;


				int opcode = get_bit_field(insn_opcode,6,0); 
				int funct6,funct3,mop,vs1;
				switch (opcode){
								case MAJOR_LOAD:
												subtype = T_LOAD;
#ifdef RVV_07
												mop = get_bit_field(insn_opcode,28,26);
												if (mop == 0 || mop == 4) subsubtype = T_UNIT;
												else if (mop == 2 || mop == 6) subsubtype = T_STRIDE;
												else if (mop == 3 || mop == 7) subsubtype = T_INDEX; 
#else
												mop = get_bit_field(insn_opcode,27,26);
												if (mop == 0){
													unsigned int rs2 = get_bit_field(insn_opcode,24,20);
													if (rs2 == 8) subsubtype = T_SPILL;
													else subsubtype = T_UNIT;
												}
												else if (mop == 2) subsubtype = T_STRIDE;
												else if (mop == 1 || mop == 3) subsubtype = T_INDEX; 
#endif
												break;
								case MAJOR_STORE:
												subtype = T_STORE;
#ifdef RVV_07
												mop = get_bit_field(insn_opcode,28,26);
												if (mop == 0) subsubtype = T_UNIT;
												else if (mop == 2) subsubtype = T_STRIDE;
												else if (mop == 3 || mop == 7) subsubtype = T_INDEX; 
#else
												mop = get_bit_field(insn_opcode,27,26);
												if (mop == 0){ 
													unsigned int rs2 = get_bit_field(insn_opcode,24,20);
													if (rs2 == 8) subsubtype = T_SPILL;
													else subsubtype = T_UNIT;
												}else if (mop == 2) subsubtype = T_STRIDE;
												else if (mop == 1 || mop == 3) subsubtype = T_INDEX; 
#endif
												break;
								case MAJOR_ARITH:
												funct3 = get_bit_field(insn_opcode,14,12);
												funct6 = get_bit_field(insn_opcode,31,26);
												if (funct3 == 1 || funct3 == 5){ // OPFVV, OPFVF
																if (funct6 >= 0x18 && funct6 <= 0x1F){ //mask gen
																				subtype = T_MASK;
#ifdef RVV_07
																}else if (funct6 != 0x0C && //vfmv.f.s
																					funct6 != 0x0D && //vfmv.s.f
																					funct6 != 0x17 && //vfmerge, vfmv
																					funct6 != 0x22 && //fconverts
																					(funct6 != 0x23 || get_bit_field(insn_opcode,19,15)!=0x10)){ //fclass
#else
																}else if (funct6 != 0x0E && //vfslide1up
																					funct6 != 0x0F && //vfslide1down
																					funct6 != 0x10 && //vfmv.f.s or vfmv.s.f
																					funct6 != 0x12 && //converts
																					funct6 != 0x17 && //vfmerge, vfmv
																					(funct6 != 0x13 || get_bit_field(insn_opcode,19,15)!=0x10)){ //fclass
#endif
																				if (funct6 == 1 || funct6 ==3 || funct6 == 5 || funct6 == 7 || funct6 == 49 || funct6 == 51){
																					subtype = T_REDUCTION;
																				}else{
																					subtype = T_ARITH;
																					subsubsubtype = (funct6 >= 40 && funct6 <= 48) ? T_FUSED : T_SINGLE;
																				}

																				subsubtype = T_FP;
																}
												}else if (funct3 == 0 || funct3 == 3 || funct3 == 4){ //OPIVV, OPIVI, OPIVX
																if (funct6 >= 0x18 && funct6 <= 0x1F){ //mask gen
																				subtype = T_MASK;
																}else if (funct6 != 0x0C && //vrgather
																					funct6 != 0x0E && //slideup, gatherei16 in 1.0
																					funct6 != 0x0F && //slidedown
																					funct6 != 0x2E && //vnclipu
																					funct6 != 0x2F && //vnclip
																					funct6 != 0x17 //vmerge/vmv
#ifndef RVV_07																																
																					&& (funct6 != 0x27 || funct3 != 3) //vmv1r
#endif																																							
																					){ 
																		if (funct6 == 48 || funct6 == 49){
																				subtype = T_REDUCTION;
																		}else{
																				subtype = T_ARITH;
																				subsubsubtype = T_SINGLE;
																		}
																				subsubtype = T_INT;
																}
												}else if (funct3 == 2 || funct3 == 6){ //OPMVV, OPMVX,
#ifdef RVV_07
																if (funct6 == 0x16){ //VMUNARY0
																	vs1 = get_bit_field(insn_opcode,19,15);
																	if (vs1 >= 0x01 && vs1 <= 0x03){
																				subtype = T_MASK;
																	}
#else
																if (funct6 == 0x14){ //VMUNARY0
																	vs1 = get_bit_field(insn_opcode,19,15);
																	if (vs1 >= 0x01 && vs1 <= 0x03){
																				subtype = T_MASK;
																	}
#endif
																}else if (funct6 >= 0x18 && funct6 <= 0x1F){ //mask arith
																				subtype = T_MASK;
#ifdef RVV_07
																}else if (funct6 != 0x0D && //vmv.s.x
																					funct6 != 0x0E && //slide1up
																					funct6 != 0x0F && //slide1down
																					funct6 != 0x14 && //popc
																					funct6 != 0x15 && //vmfirst
																					funct6 != 0x0C && //vext
																					funct6 != 0x17){ //vmcompress
#else
																}else if (funct6 != 0x10 && //vmv.s.x, vmv.x.s, vpopc, vfirst
																					funct6 != 0x0E && //slide1up
																					funct6 != 0x0F && //slide1down
																					funct6 != 0x12 && //vzext
																					funct6 != 0x17){ //vmcompress
#endif
																				if ((funct6 >= 0 && funct6 <= 7) /*|| (funct6 == 48 || funct6 == 49)*/){
																					subtype = T_REDUCTION;
																				}else{
																					subtype = T_ARITH;
																					subsubsubtype = (funct6 == 41 || funct6 == 43 || funct6 == 45 || funct6 == 47) ? T_FUSED : T_SINGLE;
																				}
																				subsubtype = T_INT;
																}
												}
												break;
								default:
												break;
				}
				return (type | subtype | subsubtype | subsubsubtype);
}

/** Important aclarations:
 * - The fields are combined using bitwise OR operations.
 * - This only takes into account RVV 1.0 especification, thus, RVV 0.7 results may be wrong.
 * - To distinguish between some instructions, e.g. narrowing and widening we need to bring the asm string, i.e.,
 *   we don't allways use the opcode to determine the type_ext.
 * - We use cur_type to extend memory decoding without decoding everything that has just been decoded.
 */
uint16_t instr_set_vector_type_ext(uint32_t insn_opcode, char* instr_asm, uint16_t cur_type){
    int opcode = get_bit_field(insn_opcode,6,0); 
    int funct6,funct3, vm, vs1, mop;
    unsigned int nf;
    uint16_t type_ext = 0x0000;

    switch(opcode){
        case MAJOR_ARITH:
            type_ext |= TYPE_EXT_ARITH;  
            funct3 = get_bit_field(insn_opcode,14,12);
            funct6 = get_bit_field(insn_opcode,31,26);
            vs1 = get_bit_field(insn_opcode, 19, 15);
            vm  = get_bit_field(insn_opcode, 25, 25);

            /** NARROWING & WIDENING INSTRUCTIONS */
            if(instr_asm[0] == 'v' && (instr_asm[1] == 'n' || (instr_asm[1] == 'f' && instr_asm[2] == 'n'))){       // Narrowing

                type_ext |= TYPE_EXT_ARITH_NARROWING;

            }else if(instr_asm[0] == 'v' && (instr_asm[1] == 'w' || (instr_asm[1] == 'f' && instr_asm[2] == 'w'))){ // Widening

                type_ext |= TYPE_EXT_ARITH_WIDENING;

            }else{

                type_ext |= TYPE_EXT_ARITH_NORMAL;

            }

            /** FUSED INSTRUCTIONS */
            if (strstr(instr_asm, "macc") || strstr(instr_asm, "madd") || 
                strstr(instr_asm, "msac") || strstr(instr_asm, "msub")) {
                
                type_ext |= TYPE_EXT_ARITH_COMP_FUSED;
                if (instr_asm[0] == 'v' && instr_asm[1] == 'f') {
                    type_ext |= TYPE_EXT_FP;
                }
                
            } else {
                // By default we set the instruction as Computation, e.g. add, sub...
                type_ext |= TYPE_EXT_ARITH_COMPUTATION;
                if (instr_asm[0] == 'v' && instr_asm[1] == 'f') {
                    type_ext |= TYPE_EXT_FP;
                }
            }

            // To check whether the instruction reads from a scalar reg       
            if (funct3 > 3 && funct3 < 7) {
                type_ext |= TYPE_EXT_READ_SCALAR;
            }

            /** MOVE INSTRUCTIONS */
            if(funct6 == 0x10){
                if ((funct3 == 2 || funct3 == 1) && vs1 == 0) {                         // vmv.x.s, vfmv.f.s
                    type_ext     &= ~MASK_EXT_SUBSUBTYPE;
                    type_ext |= TYPE_EXT_ARITH_MOVE; 
                    type_ext |= TYPE_EXT_WRITE_SCALAR;
                    type_ext |= (funct3==1)?TYPE_EXT_FP:TYPE_EXT_INT;

                } else if ((funct3 == 2 || funct3 == 1) && (vs1 == 16 || vs1 == 17)) {  // vcpop.m y vfirst.m
                    type_ext     &= ~MASK_EXT_SUBSUBTYPE;   
                    type_ext |= TYPE_EXT_ARITH_MASK;
                    type_ext |= TYPE_EXT_WRITE_SCALAR;
                    type_ext |= TYPE_EXT_INT;

                }
                else if(funct3 == 6 || funct3 == 5){                                    // vmv.s.x, vfmv.s.f
                    type_ext     &= ~MASK_EXT_SUBSUBTYPE;
                    type_ext |= TYPE_EXT_ARITH_MOVE; 
                    type_ext |= (funct3==5)?TYPE_EXT_FP:TYPE_EXT_INT;
                }

            } else if(funct6 == 0x27 && funct3 == 3){                 // vmv1r.v, vmv2r.v... Whole register...
                type_ext     &= ~MASK_EXT_SUBSUBTYPE;
                type_ext     |= TYPE_EXT_ARITH_MOVE;
                type_ext     |= TYPE_EXT_WHOLE_REGISTER;
                type_ext     |= TYPE_EXT_INT;
            } else if(funct6 == 0x17 && vm == 1) {                    // vmv.v.v, vmv.v.x, vfmv.v.f
                type_ext     &= ~MASK_EXT_SUBSUBTYPE;
                type_ext     |= TYPE_EXT_ARITH_MOVE;
                if (funct3 == 1 || funct3 == 5) {
                    type_ext |= TYPE_EXT_FP;                          // vfmv.v.f
                } else {
                    type_ext |= TYPE_EXT_INT;                         // vmv.v.v, vmv.v.x, vmv.v.i
                }
            }

            /** REDUCTION INSTRUCTIONS */
            if (funct3 == 2 && ((funct6 >= 0 && funct6 <= 7) || funct6 == 0x30 || funct6 == 0x31)) {   
                /**OPMVV: 010 
                 * f6 == 0x30 y 0x31: vwredsumu y vwredsum
                 * others: vredsum, vredand, vredor, vredxor, vredminu, vredmin, vredmaxu, vredmax
                */
                type_ext &= ~MASK_EXT_SUBSUBTYPE;
                type_ext |= TYPE_EXT_ARITH_REDUCTION;
                type_ext |= TYPE_EXT_INT;     
            } else if (funct3 == 1 &&
                ((funct6%2 == 1 && funct6 > 0 && funct6 < 8) || funct6 == 0x31 || funct6 == 0x33)) {   
                /**OPFVV: 001
                 * f6 == 0x31 y 0x33: vfwredsumu y vfwredsum
                 * others: vfredosum, vfredusum, vfredmin, vfredmax
                */
                type_ext &= ~MASK_EXT_SUBSUBTYPE;
                type_ext |= TYPE_EXT_ARITH_REDUCTION;
                type_ext |= TYPE_EXT_FP;     
            } 

            /** SLIDE INSTRUCTIONS */
            else if (funct6 == 14 || funct6 == 15) {   
                type_ext |= (funct6 == 14)?TYPE_EXT_SLIDE_UP:TYPE_EXT_SLIDE_DOWN; // funct6 == 14: slideup; else slidedown

                if(funct3 == 0b100 || funct3 == 0b011){ // vslideup, vslidedown
                    type_ext &= ~MASK_EXT_SUBSUBTYPE;
                    type_ext |= TYPE_EXT_ARITH_SLIDE;
                    type_ext |= TYPE_EXT_INT;
                    type_ext |= TYPE_EXT_SLIDE_N1;
                }else if(funct3 == 0b110){              // vslide1up, vslide1down
                    type_ext &= ~MASK_EXT_SUBSUBTYPE;
                    type_ext |= TYPE_EXT_ARITH_SLIDE;
                    type_ext |= TYPE_EXT_INT;
                    type_ext |= TYPE_EXT_SLIDE_1;
                }else if(funct3 == 0b101){              // vfslide1up, vfslide1down
                    type_ext &= ~MASK_EXT_SUBSUBTYPE;
                    type_ext |= TYPE_EXT_ARITH_SLIDE;
                    type_ext |= TYPE_EXT_FP;
                    type_ext |= TYPE_EXT_SLIDE_1;
                }
            }

            /** COMPRESS INSTRUCTIONS */
            if (funct6 == 0b010111 && funct3 == 0b010) {
                type_ext &= ~MASK_EXT_SUBSUBTYPE;
                type_ext |= TYPE_EXT_ARITH_COMPRESS;
                type_ext |= TYPE_EXT_INT;
            }

            /** GATHER INSTRUCTIONS */
            else if(funct6 == 0b001100 && (funct3 == 0b000 || funct3 == 0b011 || funct3 == 0b100)){  // vrgather
                type_ext &= ~MASK_EXT_SUBSUBTYPE;
                type_ext |= TYPE_EXT_ARITH_GATHER;
                type_ext |= TYPE_EXT_INT;
            } else if(funct6 == 0b001110 && funct3 == 0b000){                                    // vrgatherei16
                type_ext &= ~MASK_EXT_SUBSUBTYPE;
                type_ext |= TYPE_EXT_ARITH_GATHER;
                type_ext |= TYPE_EXT_INT;
            }
            break;

        case MAJOR_LOAD:
            type_ext |= TYPE_EXT_LOAD;
			nf = (insn_opcode >> 29) & 0x7;
            if(nf > 0) {
                type_ext |= TYPE_EXT_MEMORY_SEGMENTED;
            }
            if(is_subsubtype(cur_type, T_INDEX)){
                mop = get_bit_field(insn_opcode,28,26);
                if(mop == 3) type_ext |= TYPE_EXT_MEMORY_ORDERED;
                else       type_ext |= TYPE_EXT_MEMORY_UNORDERED; // I know that as its 000 we dont need to do this but to be more explicit I prefer to.
            }
 
            break;

        case MAJOR_STORE:
            type_ext |= TYPE_EXT_STORE;	
			nf = (insn_opcode >> 29) & 0x7;
            if(nf > 0) {
                type_ext |= TYPE_EXT_MEMORY_SEGMENTED;
            }	
            if(is_subsubtype(cur_type, T_INDEX)){
                mop = get_bit_field(insn_opcode,28,26);
                if(mop == 3) type_ext |= TYPE_EXT_MEMORY_ORDERED;
                else       type_ext |= TYPE_EXT_MEMORY_UNORDERED; // I know that as its 000 we dont need to do this but to be more explicit I prefer to.
                
            }
 
            break;
    }
    return type_ext;
}

uint16_t instr_set_scalar_type(uint32_t insn_opcode){

	uint16_t type = T_SCALAR;
	int f7 = (insn_opcode & 0x7F);
	int quadrant = (insn_opcode&0x3);
	if (quadrant!=0x3){ //Compressed
		int f3 = (insn_opcode>>13)&0x7;

		if  (quadrant==1){
		 	if (f3 >= 6){
				type |= T_BRANCH;
			}else if (f3 == 5){
				type |= T_JUMP;
			}
		}
		else if (quadrant==0 && f3!=0){
			type |= T_MEMORY;
		}
		else if (quadrant==2){
			int rs1 = (insn_opcode >> 7)&0x1F;
			int rs2 = (insn_opcode >> 2)&0x1F;
		  if ((f3>=1 && f3 <=3) || (f3>=5)){
				type |= T_MEMORY;
			}else if (f3==4 && rs1!=0 && rs2==0){
				type |= T_JUMP;
			}
		}
	}else{ //Not Compressed
		if (f7 == 0x063){
		 	type |= T_BRANCH;
		}
		else if (f7 == 0x03 || f7 == 0x23 || f7 == 0x07 || f7 == 0x27){ 
		 	type |= T_MEMORY;
		}
		else if (f7 == 0x53) type |= T_SINGLE;
		else if (f7 == 0x43) type |= T_FUSED;
		else if (f7 == 0x6F || f7 == 0x67) type |= T_JUMP; 
	}

	return type;
}

instr_data * fill_instr_struct(uint64_t pc, char * instr, uint32_t insn_opcode, int prv_print){

	char * instr_fields[8]; //8 is more than enough

	//Look for fields in char* instr 
	int field_start=0;
	#ifdef RVV_07
	int field_idx=-1; //Skip first
	#else
	int field_idx=0; 
	#endif
	int reading_field=0;
	for(int i=0;; ++i){
		char c = instr[i];
		if (c==',' || c==' ' || c=='"' || c=='(' || c==')' || c=='\0'){
			if (reading_field){
				reading_field=0;
				if (field_idx >= 0){
					int field_length = (i-field_start+1);
					instr_fields[field_idx]	= malloc(sizeof(char)*field_length);
					for(int j=0; j<field_length-1; ++j) instr_fields[field_idx][j] = instr[field_start+j];
					instr_fields[field_idx][field_length-1]='\0';
	
				}
				field_idx++;
	
				if (field_idx > 7){
				 	printf("Too many fields\n");
					break;
				}
			}
		}else{
			if (reading_field==0) field_start=i;
			reading_field=1;
		}
		if (c=='\0') break;
	}


	//Fill fields in struct
	instr_data * data = (instr_data*)malloc(sizeof(instr_data));

	my_strcpy(data->asm_string,instr);
	data -> instr32 = insn_opcode;
	
	data -> PC = pc;
	data -> dst =  (field_idx > 1) ? reg2prv(instr_fields[1]) : 0;
	data -> src1 = (field_idx > 2) ? reg2prv(instr_fields[2]) : 0; 
	data -> src2 = (field_idx > 3) ? reg2prv(instr_fields[3]) : 0;
	data -> src3 = -1;

	data -> dst_d  =  (field_idx > 1) ? reg2id(instr_fields[1]) : 0;
	data -> src1_d = (field_idx > 2) ? reg2id(instr_fields[2]) : 0; 
	data -> src2_d = (field_idx > 3) ? reg2id(instr_fields[3]) : 0;
	data -> src3_d = -1;

	if (contains_string(instr_fields[0], "vset")){
		data -> type = T_VSETVL;
		if (prv_print) data -> paraver_code = instr2prv(insn_opcode);
	}else if (instr_fields[0][0]=='v'){
		data -> type = instr_set_vector_type(insn_opcode);
        data -> type_ext = instr_set_vector_type_ext(insn_opcode, instr_fields[0], data -> type);

		if (is_subtype(data->type, T_STORE)){
			//change it back to "memory" (general)
			data -> type &= ~T_STORE; 
			data -> type |= T_MEMORY;
			data -> src3 = data -> dst;
			data -> src3_d = data -> dst_d;
            data -> dst_d = -1;
			//data -> dst = 0;
		}
		if (is_subtype(data->type, T_ARITH)){
			if (is_subsubsubtype(data->type, T_FUSED)){
				data -> src3 = data -> dst;
                data -> src3_d = data -> dst_d;
			}
		}
		if (prv_print) data -> paraver_code = instr2prv(insn_opcode);
	}else{
		data -> type = instr_set_scalar_type(insn_opcode); 
		if (prv_print){
			int opcode = get_bit_field(insn_opcode,6,0);
			int funct3 = get_bit_field(insn_opcode,14,12);
			int	funct6 = get_bit_field(insn_opcode,31,26);
		 	data -> paraver_code = 1010 + (opcode | (funct3<<7) | (funct6<<10));
		}
	}
	for(int i=0; i<field_idx; ++i){
		//printf("%s\n",instr_fields[i]);
		free(instr_fields[i]);
	}
	return data;
}

enum RAVE_API_t decode_rave_api(uint32_t insn_opcode){
	unsigned int dst = (insn_opcode>>7)&0x1F;
	if (dst!=0) return NO_API;
	unsigned int major = (insn_opcode)&0x7F;
	unsigned int funct3=(insn_opcode>>12)&0x7;
	unsigned int funct6=(insn_opcode>>26)&0x3F;
	int32_t imm = ((int32_t)insn_opcode>>20); 

	if (major == 0x13 && funct3 == 0){
		if (imm==-2) return RESTART_TRACE; //li x0, -2 (restart trace)
		else if (imm==-3) return ENABLE_TRACE; //li x0, -3 (enable trace)
		else if (imm==-4) return DISABLE_TRACE; //li x0, -4 (disable trace)		
		else if (imm==-7) return ENABLE_REGIONS; //li x0, -5 (enable regions)
		else if (imm==-8) return DISABLE_REGIONS; //li x0, -6 (disable regions)		
	}else if (major==0x33){
		if (funct3 == 0x6) return EVENT_AND_VALUE; // or x0, ..., ... (rave_event_and_value)		
		else if (funct3 == 0x7) return NAME_EVENT_VALUE; //and x0, ..., ... (name event value)		
		else if (funct3 == 1 && funct6==0) return EVENT_STRING; // sll x0, ..., ... (event string)
		else if (funct3 == 5 && funct6==0) return VALUE_STRING;// srl x0, ..., ... (action: value string)
		else if (funct3 == 0 && funct6==0) return BEGIN_REGION; // add x0, ..., ... (action: begin region string)
		else if (funct3 == 0 && funct6==0x10) return END_REGION; // sub x0, ..., ... (action: end region string)
	//----------------------------------------
	// OMP control
	//----------------------------------------
	}else if (major == 0x13 && funct3 == 0 &&  imm==-5) return PARALLEL_BARRIER; //li x0, -5 (parallel_barrier)		
	else if (major == 0x33 && funct3 == 4 && funct6 == 0) return PARALLEL_BEGIN; //xor x0, ..., ... (parallel begin)		
	else if (major == 0x13 && funct3 == 0 &&  imm==-6) return PARALLEL_END; //li x0, -6 (parallel_end)		
	return NO_API;
}
