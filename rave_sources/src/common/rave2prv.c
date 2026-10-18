/*********************************************************/
// Copyright (C) 2025 Barcelona Supercomputing Center-Centro Nacional de Supercomputación
// SPDX-License-Identifier: BSD-3-Clause
/*********************************************************/
// * Author: Pablo Vizcaino
// * Email:  pablo.vizcaino@bsc.es
/*********************************************************/

#include "rave2prv.h"
#include "regions.h"
#include "events.h"
#include <stdlib.h>
#include <string.h>

#include "state.h" //For N_PIPELINES

void trace_row(FILE * fd, int process, int cpu, int pipeline, uint64_t tstamp){
	pipeline=pipeline % N_PIPELINES; //For MUSA: pipeline will always be 0
	fprintf(fd, "\n2:1:%d:%d:%d:%lu", process+1, cpu+1, pipeline+1, tstamp);
}
void trace_event_value(FILE * fd, int event, uint64_t value){
	fprintf(fd, ":%d:%lu", event,value);
}
int reg2prv(char * r){
	if (strcmp(r, "zero")==0) return 102; 
	if (strcmp(r, "ra")==0)   return 103; 
	if (strcmp(r, "sp")==0)   return 104; 
	if (strcmp(r, "gp")==0)   return 105; 
	if (strcmp(r, "tp")==0)   return 106; 
	if (strcmp(r, "t1")==0)   return 97; 
	if (strcmp(r, "t2")==0)   return 98; 
	if (strcmp(r, "fp")==0)   return 8; 
	if (strcmp(r, "s1")==0)   return 9; 
	if (strcmp(r, "a0")==0)   return 10; 
	if (strcmp(r, "a1")==0)   return 11; 
	if (strcmp(r, "a2")==0)   return 12; 
	if (strcmp(r, "a3")==0)   return 13; 
	if (strcmp(r, "a4")==0)   return 14; 
	if (strcmp(r, "a5")==0)   return 15; 
	if (strcmp(r, "a6")==0)   return 16; 
	if (strcmp(r, "a7")==0)   return 17; 
	if (strcmp(r, "s2")==0)   return 18; 
	if (strcmp(r, "s3")==0)   return 19; 
	if (strcmp(r, "s4")==0)   return 20; 
	if (strcmp(r, "s5")==0)   return 21; 
	if (strcmp(r, "s6")==0)   return 22; 
	if (strcmp(r, "s7")==0)   return 23; 
	if (strcmp(r, "s8")==0)   return 24; 
	if (strcmp(r, "s9")==0)   return 25; 
	if (strcmp(r, "s10")==0)  return 26; 
	if (strcmp(r, "s11")==0)  return 27; 
	if (strcmp(r, "t3")==0)   return 28; 
	if (strcmp(r, "t4")==0)   return 29; 
	if (strcmp(r, "t5")==0)   return 30; 
	if (strcmp(r, "t6")==0)   return 31; 
	if (strcmp(r, "ft0")==0)  return 32; 
	if (strcmp(r, "ft1")==0)  return 33; 
	if (strcmp(r, "ft2")==0)  return 34; 
	if (strcmp(r, "ft3")==0)  return 35; 
	if (strcmp(r, "ft4")==0)  return 36; 
	if (strcmp(r, "ft5")==0)  return 37; 
	if (strcmp(r, "ft6")==0)  return 38; 
	if (strcmp(r, "ft7")==0)  return 39; 
	if (strcmp(r, "fs0")==0)  return 100; 
	if (strcmp(r, "fs1")==0)  return 101; 
	if (strcmp(r, "fa0")==0)  return 42; 
	if (strcmp(r, "fa1")==0)  return 43; 
	if (strcmp(r, "fa2")==0)  return 44; 
	if (strcmp(r, "fa3")==0)  return 45; 
	if (strcmp(r, "fa4")==0)  return 46; 
	if (strcmp(r, "fa5")==0)  return 47; 
	if (strcmp(r, "fa6")==0)  return 48; 
	if (strcmp(r, "fa7")==0)  return 49; 
	if (strcmp(r, "fs2")==0)  return 50; 
	if (strcmp(r, "fs3")==0)  return 51; 
	if (strcmp(r, "fs4")==0)  return 52; 
	if (strcmp(r, "fs5")==0)  return 53; 
	if (strcmp(r, "fs6")==0)  return 54; 
	if (strcmp(r, "fs7")==0)  return 55; 
	if (strcmp(r, "fs8")==0)  return 56; 
	if (strcmp(r, "fs9")==0)  return 57; 
	if (strcmp(r, "fs10")==0) return 58; 
	if (strcmp(r, "fs11")==0) return 59; 
	if (strcmp(r, "ft8")==0)  return 60; 
	if (strcmp(r, "ft9")==0)  return 61; 
	if (strcmp(r, "ft10")==0) return 62; 
	if (strcmp(r, "ft11")==0) return 63; 
	if (strcmp(r, "v0")==0)   return 64; 
	if (strcmp(r, "v1")==0)   return 65; 
	if (strcmp(r, "v2")==0)   return 66; 
	if (strcmp(r, "v3")==0)   return 67; 
	if (strcmp(r, "v4")==0)   return 68; 
	if (strcmp(r, "v5")==0)   return 69; 
	if (strcmp(r, "v6")==0)   return 70; 
	if (strcmp(r, "v7")==0)   return 71; 
	if (strcmp(r, "v8")==0)   return 72; 
	if (strcmp(r, "v9")==0)   return 73; 
	if (strcmp(r, "v10")==0)  return 74; 
	if (strcmp(r, "v11")==0)  return 75; 
	if (strcmp(r, "v12")==0)  return 76; 
	if (strcmp(r, "v13")==0)  return 77; 
	if (strcmp(r, "v14")==0)  return 78; 
	if (strcmp(r, "v15")==0)  return 79; 
	if (strcmp(r, "v16")==0)  return 80; 
	if (strcmp(r, "v17")==0)  return 81; 
	if (strcmp(r, "v18")==0)  return 82; 
	if (strcmp(r, "v19")==0)  return 83; 
	if (strcmp(r, "v20")==0)  return 84; 
	if (strcmp(r, "v21")==0)  return 85; 
	if (strcmp(r, "v22")==0)  return 86; 
	if (strcmp(r, "v23")==0)  return 87; 
	if (strcmp(r, "v24")==0)  return 88; 
	if (strcmp(r, "v25")==0)  return 89; 
	if (strcmp(r, "v26")==0)  return 90; 
	if (strcmp(r, "v27")==0)  return 91; 
	if (strcmp(r, "v28")==0)  return 92; 
	if (strcmp(r, "v29")==0)  return 93; 
	if (strcmp(r, "v30")==0)  return 94; 
	if (strcmp(r, "v31")==0)  return 95; 
	if (strcmp(r, "t0")==0)   return 96; 
	if (strcmp(r, "s0")==0)   return 99; 
	return 0;
}

int reg2id(const char * r){
	static const char * scalar_names[32] = {
		"zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
		"s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
		"a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
		"s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
	};
	static const char * float_names[32] = {
		"ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7",
		"fs0", "fs1", "fa0", "fa1", "fa2", "fa3", "fa4", "fa5",
		"fa6", "fa7", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7",
		"fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"
	};
	char * end;
	long index;

	if (r == NULL) return -1;
	if (r[0] == 'v') {
		index = strtol(r + 1, &end, 10);
		return (*end == '\0' && index >= 0 && index < 32) ? RAVE_INI_REG_V + index : -1;
	}
	if (r[0] == 'x') {
		index = strtol(r + 1, &end, 10);
		return (*end == '\0' && index >= 0 && index < 32) ? RAVE_INI_REG_S + index : -1;
	}
	if (r[0] == 'f') {
		index = strtol(r + 1, &end, 10);
		return (*end == '\0' && index >= 0 && index < 32) ? RAVE_INI_REG_F + index : -1;
	}
	for (index = 0; index < 32; ++index) {
		if (strcmp(r, scalar_names[index]) == 0) return RAVE_INI_REG_S + index;
		if (strcmp(r, float_names[index]) == 0) return RAVE_INI_REG_F + index;
	}
	if (strcmp(r, "fp") == 0) return RAVE_INI_REG_S + 8;
	return -1;
}

void open_file(FILE **fd, char * name){
	*fd = fopen(name, "w+");
	if (*fd == NULL){
		printf("cannot open: %s\n", name);
		exit(-1);
	}
}

void write_prv(FILE * fd, int procs, int * OMPthreads, int pipelines){
	fprintf(fd, "#Paraver (00/00/0000 at 00:00):1_ns:1(1):%d",procs);
	for(int p=0; p<procs; ++p){
		fprintf(fd, ":%d(", OMPthreads[p]);
		for(int t=0; t<OMPthreads[p]-1; ++t){
			fprintf(fd, "%d:1,", pipelines);
		}
		fprintf(fd, "%d:1)", pipelines);
	}
}

void write_row(FILE * fd, int procs, int * OMPthreads, int pipelines){
	fprintf(fd, "LEVEL WORKLOAD SIZE 1\n"
					"Full System\n"
					"LEVEL APPL SIZE %d\n",procs);
	for(int p=0; p<procs; ++p) fprintf(fd, "Proc%d\n",p);

	int totalOMPthreads = 0;
	for(int p=0; p<procs; ++p) totalOMPthreads += OMPthreads[p];
	fprintf(fd, "LEVEL TASK SIZE %d\n",totalOMPthreads);
	for(int p=0; p<procs; ++p){
		for(int t=0; t<OMPthreads[p]; ++t){
			fprintf(fd, "p%d.t%d\n",p,t);
		}
	}
	int totalpipes=totalOMPthreads*pipelines;
	fprintf(fd, "LEVEL THREAD SIZE %d\n",totalpipes);
	for(int p=0; p<procs; ++p){
		for(int t=0; t<OMPthreads[p]; ++t){
			fprintf(fd, "p%d.t%d.scalar\n",p,t);
			fprintf(fd, "p%d.t%d.vector\n",p,t);
		}
	}
}

void events_and_values_to_pcf(FILE * fd, int event, int max_nested){
	for(int i=1; i<=max_nested; ++i){
		fprintf(fd,"EVENT_TYPE\n9\t%d\tRegions_nest_%d\n",event+i-1, i);
		region_unique_list_t * curr = first_unique_region;
		if (curr!=NULL) fprintf(fd,"VALUES\n");
		while (curr != NULL){
			/*if (curr->region->nesting==i)*/ fprintf(fd,"%d\t%s\n",curr->region_id, curr->region->name);
			curr = curr->next;
		}
	}
	event_info * curr = first_event_info;
	while(curr != NULL){
		fprintf(fd,"EVENT_TYPE\n9\t%ld\t%s\n",curr->ID, curr->name);
		value_info * values = curr->values;
		if (values!=NULL){
			fprintf(fd,"VALUES\n");
			while(values!=NULL){
				fprintf(fd,"%ld\t%s\n",values->ID, values->name);
				values = values->next;
			}
		}
		curr = curr->next;
	}
}
