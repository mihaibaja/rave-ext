/*********************************************************/
// Copyright (C) 2026 Barcelona Supercomputing Center-Centro Nacional de Supercomputación
// SPDX-License-Identifier: BSD-3-Clause
/*********************************************************/
// * Author: Pablo Vizcaino
// * Email:  pablo.vizcaino@bsc.es
/*********************************************************/

#include "utils.h"
#include "qemu-plugin.h"
#include <stdlib.h>

#ifdef RVV_07
//include/hw/core/cpu.h (0.7 :307 (def)) //Util for knowing OFFSET REGS
//qemu_get_cpu returns an ArchCPU, which has a CPURISCVState (typdef of CPUArchState), 
//ArchCPU is defined in target/riscv/cpu.h (l:277)
//CPUArchState is defined in target/riscv/cpu.h (l:114)
//In accel/tcg/plugin-gen.c (l:175) is a good place to put : printf("Offset is %ld\n",offsetof(ArchCPU, env));
#define OFFSET_CPUState (33552) //For 0.7
#define RV_VLEN_MAX (256*64)
#define OFFSET_XREGS (sizeof(uint64_t)*32 + sizeof(uint64_t)*32 + sizeof(uint64_t)*(32*RV_VLEN_MAX/64))
#define OFFSET_VREGS (sizeof(uint64_t)*32 + sizeof(uint64_t)*32)
void setup_regs(unsigned int cpu_index){
	uint8_t *cpu = qemu_get_cpu(cpu_index);
	cpus_state[cpu_index].regs = (void*)(cpu + OFFSET_CPUState); 
}
int64_t get_vl(thread_state_t * state){
	uint8_t * regs = (uint8_t*)state->regs;
	return *(uint64_t*)(regs + OFFSET_XREGS + sizeof(uint64_t)*2);
}
int64_t get_vtype(thread_state_t * state){
	uint8_t * regs = (uint8_t*)state->regs;
	return *(uint64_t*)(regs + OFFSET_XREGS + sizeof(uint64_t)*4);
}
int64_t get_xreg(thread_state_t * state, int reg){
	uint8_t * regs = (uint8_t*)state->regs;
	return *(uint64_t*)(regs + sizeof(uint64_t)*reg); 
}
char * get_vreg(thread_state_t * state, int reg, int vlB){
	uint8_t * regs = (uint8_t*)state->regs;
	char * data = (char*)regs + OFFSET_VREGS + sizeof(uint64_t)*reg*RV_VLEN_MAX/64;
	char * values = aligned_alloc(8, vlB);
	for(int i=0; i<vlB; ++i){
		values[i] = data[i]; 
	}
	return values; 
}
#else
int idx_xregs;
int idx_vregs;
int idx_vl;
int idx_vtype;
void setup_regs(unsigned int cpu_index){
	//Registers:
	idx_xregs=idx_vl=idx_vtype=idx_vregs=-1;

	GArray * regs = qemu_plugin_get_registers();
	for (unsigned int i = 0; i < regs->len; i++) {
		qemu_plugin_reg_descriptor *desc = &g_array_index(regs, qemu_plugin_reg_descriptor, i);
		if (idx_xregs < 0 && (g_strcmp0(desc->name, "zero")==0)) idx_xregs=i;
		if (idx_vregs < 0 && (g_strcmp0(desc->name, "v0")==0)) idx_vregs=i;
		if (idx_vtype < 0 && (g_strcmp0(desc->name, "vtype")==0)) idx_vtype=i;
		if (idx_vl < 0 && (g_strcmp0(desc->name, "vl")==0)) idx_vl=i;
	}
	cpus_state[cpu_index].regs = (void*)regs;
}

int64_t get_vl(thread_state_t * state){
  qemu_plugin_reg_descriptor *desc = &g_array_index((GArray*)(state->regs), qemu_plugin_reg_descriptor, idx_vl);
	GByteArray *buf = g_byte_array_new();
	qemu_plugin_read_register(desc->handle, buf);
	uint64_t reg_val = *((uint64_t*)buf->data);
  g_byte_array_unref(buf);
	return (int64_t)reg_val; 
}
int64_t get_vtype(thread_state_t * state){
  qemu_plugin_reg_descriptor *desc = &g_array_index((GArray*)(state->regs), qemu_plugin_reg_descriptor, idx_vtype);
	GByteArray *buf = g_byte_array_new();
	qemu_plugin_read_register(desc->handle, buf);
	uint64_t reg_val = *((uint64_t*)buf->data);
  g_byte_array_unref(buf);
	return (int64_t)reg_val; 
}
int64_t get_xreg(thread_state_t * state, int reg){
  qemu_plugin_reg_descriptor *desc = &g_array_index((GArray*)(state->regs), qemu_plugin_reg_descriptor, idx_xregs+reg);
	GByteArray *buf = g_byte_array_new();
	qemu_plugin_read_register(desc->handle, buf);
	uint64_t reg_val = *((uint64_t*)buf->data);
  g_byte_array_unref(buf);
	return (int64_t)reg_val; 
}
char * get_vreg(thread_state_t * state, int reg, int vlB){
	if (vlB <= 0) return NULL;
	int nregs = (vlB*8 + RAVE_VLMAX - 1) / RAVE_VLMAX; //Ceiling division
	size_t alloc_sz = ((vlB + 7) / 8) * 8;
	char * values = aligned_alloc(8, alloc_sz);
	int idx = 0;

	for (int nr = 0; nr<nregs; ++nr){
	  qemu_plugin_reg_descriptor *desc = &g_array_index((GArray*)(state->regs), qemu_plugin_reg_descriptor, idx_vregs+reg);
		GByteArray *buf = g_byte_array_new();
		qemu_plugin_read_register(desc->handle, buf);
		//Read until enough bytes read (idx<vlB) or overflowed register (i<vlmax)
		for(int i=0; idx<vlB && i<(RAVE_VLMAX/8); ++i) values[idx++] = buf->data[i];
	  g_byte_array_unref(buf);
	}

	return values; 
}
#endif

void plugin_outs(char * str){
	qemu_plugin_outs(str);
}

char contains_string(char * str, const char * find){
	int slen = strlen(str);
	int flen = strlen(find);
	int progress = 0;
	for(int i=0; i<slen; ++i){
		if (str[i] == find[progress]) ++progress;
		else if (str[i] == find[0]) progress=1;
		else progress = 0;
		if (progress == flen) return 1;
	}
	return 0;
}

void rave_read_string(unsigned int cpu_index, uint64_t string_addr, uint64_t len, char * string, uint64_t maxlen){
	//Read string from guest memory
	#ifndef RVV_07
	GByteArray *mem_buf = g_byte_array_new();
	#endif
	uint64_t i;
	for(i=0; i<maxlen && i<len; ++i){
		#ifdef RVV_07
		cpu_memory_rw_debug(qemu_get_cpu(cpu_index), string_addr + i, (uint8_t*)&string[i], 1, 0);
		#else
    qemu_plugin_read_memory_vaddr(string_addr + i, mem_buf, 1); 
		string[i] = (uint8_t)mem_buf->data[0];
		#endif
		if (string[i] == '\n') string[i] = '\0';
		if (string[i] == '\0') break;
	}
	string[i] = '\0';
	#ifndef RVV_07
	g_byte_array_unref(mem_buf);
	#endif
}

