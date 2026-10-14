/*********************************************************/
// Copyright (C) 2026 Barcelona Supercomputing Center-Centro Nacional de Supercomputación
// SPDX-License-Identifier: BSD-3-Clause
/*********************************************************/
// * Author: Pablo Vizcaino
// * Email:  pablo.vizcaino@bsc.es
/*********************************************************/
#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* [xxxx][tttt][MMMM][mmmm] */
#define T_NOTYPE 0x0000

#define T_SCALAR 0x1000
		#define T_BRANCH 0x0100
		#define T_JUMP 0x0600
#define T_VECTOR 0x2000
#define T_VSETVL 0x3000

#define T_OTHER  0x0000
#define T_ARITH  0x0100
#define T_REDUCTION 0x0500
	#define T_FP     0x0010
	#define T_INT    0x0020
		#define T_SINGLE 0x0001
		#define T_FUSED	 0x0002

#define T_LOAD   0x0200
#define T_STORE  0x0300
#define T_MASK   0x0400
#define T_MEMORY T_LOAD //Until we report ld/st separately
	#define T_UNIT   0x0010
	#define T_STRIDE 0x0020
	#define T_INDEX  0x0030
	#define T_SPILL  0x0040


#define is_type(x,y) (((x^y)&0xF000)==0)
#define is_subtype(x,y) (((x^y)&0x0F00)==0)
#define is_subsubtype(x,y) (((x^y)&0x00F0)==0)
#define is_subsubsubtype(x,y) (((x^y)&0x000F)==0)

/*
 * Extended instruction type:
 *
 * bits 0..1   vector class
 * bits 2..3   vector arithmetic/memory subtype -> As memory subtype is correctly parsed throughout type we are not going to parse it here.
 * bits 4..6   vector arithmetic operation
 * bits 7..8   vector register transfer
 * bit 9       masked (1) / unmasked (0)
 * bit 10      floating point (1) / integer (0)
 * bit 11-12   specific diferences: e.g. slideup vs slidedown
 */
#define TYPE_EXT_ARITH               0x0000     // 00
#define TYPE_EXT_LOAD                0x0001     // 01
#define TYPE_EXT_STORE               0x0002     // 10

// Arith/Memory Subtype (bits 3:2)
#define TYPE_EXT_ARITH_NORMAL        0x0000     // 00
#define TYPE_EXT_ARITH_WIDENING      0x0004     // 01 (0x0004)
#define TYPE_EXT_ARITH_NARROWING     0x0008     // 10 (0x0008)
#define TYPE_EXT_MEMORY_NORMAL       0x0004     // 00
#define TYPE_EXT_MEMORY_SEGMENTED    0x0004     // 01

// Arith Operation (bits 6:4) && Ordered/Unordered for memory ops( -TODO podemos ponerlo como un único bit) 
#define TYPE_EXT_ARITH_COMPUTATION   0x0000     // 000 (Default ALU)
    #define TYPE_EXT_ARITH_COMP_FUSED  0x0050     // 101 (0x0050)
#define TYPE_EXT_ARITH_REDUCTION     0x0020     // 010 (0x0020)
#define TYPE_EXT_ARITH_MASK          0x0040     // 100 (0x0040)
// Permutation insts
#define TYPE_EXT_ARITH_MOVE          0x0010     // 001 (0x0010)
#define TYPE_EXT_ARITH_SLIDE         0x0030     // 011 (0x0030)
#define TYPE_EXT_ARITH_COMPRESS      0x0060     // 110 (0x0060)
#define TYPE_EXT_ARITH_GATHER        0x0070     // 111 (0x0070)

#define TYPE_EXT_MEMORY_UNORDERED    0x0000     // 000
#define TYPE_EXT_MEMORY_ORDERED      0x0010     // 001

// Register Transfer (bits 8:7) 
#define TYPE_EXT_VECTOR_VECTOR       0x0000     // 00
#define TYPE_EXT_WRITE_SCALAR        0x0080     // 01 (0x0080)
#define TYPE_EXT_READ_SCALAR         0x0100     // 10 (0x0100)
#define TYPE_EXT_WHOLE_REGISTER      0x0180     // 11 (0x0180)

// Flags independientes (bits 9 y 10)
#define TYPE_EXT_MASKED              0x0200     // bit 9:  1 (0x0200)
#define TYPE_EXT_FP                  0x0400     // bit 10: 1 (0x0400)
#define TYPE_EXT_INT                 0x0000     // bit 10: 0

// Specific diferences (bits 11 y 12)
/** SLIDE */
#define TYPE_EXT_SLIDE_UP            0x0800     // bit 11: 1
#define TYPE_EXT_SLIDE_DOWN          0x0000     // bit 11: 0    
#define TYPE_EXT_SLIDE_1             0x1000     // bit 12: 1   
#define TYPE_EXT_SLIDE_N1            0x0000     // bit 12: 0   



/** Para el is_type_ext los que tengan 1 único bit de marcado o no, se usará como mascara directamente el TYPE_EXT_XXXX 
 * e.g. is_type_ext(type_ext, TYPE_EXT_MASKED, TYPE_EXT_MASKED) para saber si está marcado el bit 9. Para los que tengan varios
 * bits, se usará la mascara de bits correspondiente, e.g. is_type_ext(type_ext, MASK_EXT_BASE, TYPE_EXT_LOAD) 
 * para saber si es un tipo de memoria (load o store) */

#define MASK_EXT_BASE        0x0003     // bits 1:0 (0000 0000 0000 0011)
#define MASK_EXT_SUBTYPE     0x000C     // bits 3:2 (0000 0000 0000 1100)
#define MASK_EXT_SUBSUBTYPE  0x0070     // bits 6:4 (0000 0000 0111 0000) -> Ampliada a 3 bits
#define MASK_EXT_OPERAND     0x0180     // bits 8:7 (0000 0001 1000 0000)
#define MASK_EXT_SPECIFIC    0x1800     // bits 11:12 (0001 1000 1000 0000)

#define is_type_ext(x, mask, y) (((x) & (mask)) == (y))

struct instr_data{
	//enum instr_type type;
	uint16_t type;
    uint16_t type_ext;
	uint32_t instr32;  // Only for strided...and mem eew.. and scalar mem?
  uint64_t PC;

	uint32_t paraver_code;
	char * asm_string;
	short src1;
	short src2;
	short src3;
	short dst;

    // For dependencies analysis.
	short src1_d;
	short src2_d;
	short src3_d;
	short dst_d;
//	enum v_major_type v_majortype;
//	enum v_minor_type v_minortype;
};
typedef struct instr_data instr_data;

struct qemu_event{
	int event;
	int value;
};
typedef struct qemu_event qemu_event;

//instr_data * scalar_empty_struct;

#define MAJOR_LOAD 0x7 //0b0000111
#define MAJOR_STORE 0x27 //0b0100111
#define MAJOR_ARITH 0x57 //0b1010111
#define get_bit_field(insn_opcode, high, low) ((insn_opcode >> low) & ((1<<(high-low+1))-1))
instr_data * fill_instr_struct(uint64_t pc, char * instr, uint32_t insn_opcode, int PRINT_PRV);
uint16_t instr_set_scalar_type(uint32_t insn_opcode);
int64_t get_loop_offset(uint32_t insn_opcode);


enum RAVE_API_t { NO_API, RESTART_TRACE, ENABLE_TRACE, DISABLE_TRACE, ENABLE_REGIONS, DISABLE_REGIONS, EVENT_AND_VALUE, NAME_EVENT_VALUE,
									EVENT_STRING, VALUE_STRING, BEGIN_REGION, END_REGION, PARALLEL_BARRIER, PARALLEL_BEGIN, PARALLEL_END};
enum RAVE_API_t decode_rave_api(uint32_t insn_opcode);


