/*********************************************************/
// Copyright (C) 2025 Barcelona Supercomputing Center-Centro Nacional de Supercomputación
// SPDX-License-Identifier: BSD-3-Clause
/*********************************************************/
// * Author: Pablo Vizcaino
// * Email:  pablo.vizcaino@bsc.es
/*********************************************************/

#include "regions.h"
#include "formatting.h"
#include "utils.h"
#include "counters_generic.h"
#include "rave2prv.h"

#include <string.h>
#include <stdlib.h>

track_regions_t track_regions;
region_unique_list_t *first_unique_region;
region_unique_list_t *last_unique_region; 

#include "threading.h" //for mpi_rank

int name_to_id(const char * name){
	region_unique_list_t * curr = first_unique_region;
	while (curr != NULL){
		if (curr->region != NULL && strcmp(curr->region->name,name)==0){
			return curr->region_id;
		}
		curr = curr->next;
	}
	//Did not find it:
	return -1;
}

void rave_ini_regions(void){
	track_regions.first_region = NULL;
	track_regions.nesting = -1;
	track_regions.total_regions = 0;

	first_unique_region = NULL;
	last_unique_region = NULL;

	track_regions.stack_top = NULL;
}

//TODO: This is slow. Add an associative cache!
region_node_t* dfs_find_recursive(region_node_t* curr, const char * name){
	if (curr==NULL) return NULL;
	if (strcmp(curr -> region.name,name)==0) return curr;
//	printf(" ...no..\n");
	region_node_t* child = curr->first_child;
	while(child!=NULL){
	//	printf("   search its childs...\n");
		region_node_t* found = dfs_find_recursive(child,name);
		if (found!=NULL) return found;
		//printf("   It was not any of its childs\n");
		child = child->next_sibling; 
	}
	return NULL;
}

void internal_end_region(int cpu_index, const char * name, rave_counters * current_counters, int accumulate, FILE * fd){


	//Close region at the top of the stack
	//We can assert that given a name, only up to one region can be open with that name
	//We can also assert that if accumulate is set to one, only one region with that name will exist
	
	if (track_regions.stack_top != NULL && 
			track_regions.stack_top->region_node != NULL /*&& 
			(name[0]=='\0' || strcmp(track_regions.stack_top -> region_node->region.name,name)==0)*/){

		update_counters(&track_regions.stack_top->region_node->region.delta_counters, current_counters);
		track_regions.stack_top->region_node->region.closed = 1;
		//track_regions.stack_top->region_node->region.enabled |= enabled;
		track_regions.stack_top->region_node->region.executions++; 
		--track_regions.nesting;
		++track_regions.total_regions;
		if (accumulate){
			//avg_counters(&curr->region.acc_counters, &curr->region.delta_counters, curr->region.executions);
			add_counters(&(track_regions.stack_top->region_node->region.acc_counters), &(track_regions.stack_top->region_node->region.delta_counters));
		}

		if (fd!=NULL){ //Streaming mode
			print_region_human(fd, track_regions.total_regions-1, &track_regions.stack_top->region_node->region, 0, 0, 1, 1);
		}

		region_stack_t * prev = track_regions.stack_top->prev;
		free(track_regions.stack_top);
		track_regions.stack_top = prev;
	}else{
		printf("Trying to close region %s, but last region is %s\n",name, track_regions.stack_top != NULL? track_regions.stack_top -> region_node -> region.name:"NULL");
		exit(-1);
	}
}


void internal_begin_region(int cpu_index, const char * name, rave_counters * current_counters, int accumulate/*, int enabled*/){

	++track_regions.nesting;
	if (track_regions.max_nested < track_regions.nesting) track_regions.max_nested = track_regions.nesting;

	region_t * parent = NULL;
	if (track_regions.stack_top != NULL)
		parent = &track_regions.stack_top->region_node->region;

	region_stack_t * new_top = (region_stack_t *)malloc(sizeof(region_stack_t));
	new_top -> prev = track_regions.stack_top;
	track_regions.stack_top = new_top;

#if 1
	//If I accumulate, check if the region already exists:
	if (accumulate){
		region_node_t * curr = dfs_find_recursive(track_regions.first_region, name);
		if (curr != NULL){
				update_counters(&curr->region.delta_counters, current_counters);
				curr -> region.delta_counters = *current_counters;
				curr -> region.closed = 0;
				//curr -> region.enabled |= enabled;
				track_regions.stack_top -> region_node = curr; 
				track_regions.stack_top = new_top;
				if (curr->region.parent != parent) curr->region.parent=NULL;
				if (curr->region.nesting != track_regions.nesting) curr->region.nesting=-1;
				return;
			}
	}
#endif

	//New region:
	region_node_t * new_node = (region_node_t*) malloc(sizeof(region_node_t));
	new_top -> region_node = new_node;


	//Become the last sibling of the parent's last child:
	if (new_top -> prev!= NULL){
		if (new_top -> prev -> region_node -> last_child != NULL){
			new_top -> prev -> region_node -> last_child -> next_sibling = new_node;
		}else{
			new_top -> prev -> region_node -> first_child = new_node;
		}
		new_top -> prev -> region_node -> last_child  = new_node;
	}else{
		track_regions.first_region = new_node;
	}


	if(track_regions.first_region==NULL){
		track_regions.first_region = new_node;
	}


	//New region family:
	new_node -> first_child = NULL;
	new_node -> last_child = NULL;
	new_node -> next_sibling = NULL;


	//Initialize region:
	//new_node -> region.enabled |= enabled;
	new_node -> region.parent = parent; 
	new_node -> region.closed = 0;
	new_node -> region.executions = 0;
	new_node -> region.nesting = track_regions.nesting;
	my_strcpy(new_node -> region.name, name);
	int len = strlen(name);
	if (len > track_regions.max_name) track_regions.max_name = len;

	reset_counters(&new_node->region.acc_counters);
	new_node -> region.delta_counters = *current_counters;
	//printf("INI: %.2f\n", current_counters->scalar_instr);
	new_node -> region.opened_by = cpu_index;

	//Add it to unique list of names:
	if (name_to_id(name)==-1){
		region_unique_list_t * new_unique = malloc(sizeof(region_unique_list_t));
		new_unique -> region = &(new_node -> region);
		new_unique -> next = NULL;
		if (last_unique_region != NULL){
			new_unique -> region_id = last_unique_region -> region_id + 1; 
			last_unique_region -> next = new_unique;
		}else{
			new_unique -> region_id = 0;
			first_unique_region = new_unique;
		}
		last_unique_region = new_unique;
	}

}

void dfs_free_recursive(region_node_t* curr){
	if (curr==NULL) return;
	region_node_t* child = curr->first_child;
	while(child!=NULL){
		dfs_free_recursive(child);
		region_node_t* next = child->next_sibling;
		free(child);
		child = next; 
	}
}


void free_regions(void){
	//Traverse tree
	dfs_free_recursive(track_regions.first_region);

	region_unique_list_t * uniq = first_unique_region;
	while (uniq != NULL){
		region_unique_list_t * tmp = uniq->next;
		free(uniq);
		uniq = tmp;
	}
}

void ini_other_child(region_t *other_child, region_node_t *curr, int accumulate){
	other_child->closed = 1;
	other_child->nesting = curr->region.nesting + 1;
	char * suffix = "_OTHER";
	int prefix_size = strlen(curr->region.name);
	int suffix_size = strlen(suffix);
	other_child->name = malloc(prefix_size + suffix_size + 1);
	memcpy(other_child->name, curr->region.name, prefix_size);
	memcpy(&other_child->name[prefix_size], suffix, suffix_size);
	other_child->name[prefix_size + suffix_size] = '\0';
	other_child->executions = 1;
	other_child->opened_by = -1;
	copy_counters(&other_child->delta_counters, &curr->region.delta_counters);
	copy_counters(&other_child->acc_counters, &curr->region.acc_counters);
	region_node_t* child = curr->first_child;
	while(child!=NULL){
		//Accumulate children's counters
		if (accumulate) sub_counters(&other_child->acc_counters, &child->region.acc_counters);
		else sub_counters(&other_child->delta_counters, &child->region.delta_counters);
		child = child->next_sibling;
	}
}


void print_region_human(FILE * fd, int n, region_t * region, int accumulate, int indent_region, int last_child, int no_childs){
			//Control nesting of output:
			ic.spaces = COMPRESS_REPORT ? 2 : 4;

			//If we accumulate, we don't indent
			if (indent_region){
				indent(fd,region->nesting, last_child);
			}

			//Print header
			if (!PLAIN_TEXT) fprintf(fd, BOLD_WHITE);	

			fprintf(fd,"Region #%d: ", n);
			P_NAME(fd,"%s",region->name);
			if (!accumulate) fprintf(fd," [Nesting: %d]",region->nesting);
			fprintf(fd," (Rank: %d, Thread: %d)", mpi_rank,region->opened_by);
			if (accumulate){
			 	fprintf(fd,". Executed %d times%s", region->executions,region->executions>1?" (counters are averaged)":"");
				mul_counters(&region->delta_counters, &region->acc_counters, 1.0/region->executions);
			}
			fprintf(fd,"\n");

			if (!PLAIN_TEXT) fprintf(fd, CLEAR_FORMAT);	

			//Print counters:
			indent(fd, accumulate ? 1 : region->nesting+1, accumulate || no_childs); P_COUNTERS(fd, "%s", "Counters:");	fprintf(fd,"\n");
			ic.spaces = COMPRESS_REPORT ? 2 : 4;

			print_counters_human(fd, &region->delta_counters);
}

void dfs_report_recursive(region_node_t* curr, FILE * fd, int accumulate, int * nregion){
	if (curr==NULL) return;

	if (curr->region.closed/* && curr->region.enabled*/){
		//last = 1 if it has no more siblings
		//If we accumulate, we don't indent, so all childs are single-childs
		//			int last = (accumulate || curr->next_sibling == NULL || curr->first_child == NULL);
		int last_child = curr->next_sibling == NULL;
		int no_childs = curr->first_child == NULL;
		print_region_human(fd, *nregion, &curr->region, accumulate, !accumulate, last_child, no_childs); 
		*nregion = *nregion+1;
	}

	region_node_t* child = curr->first_child;
	if (child==NULL) return;
	while(child!=NULL){
		//Accumulate children's counters
		//Report childs recursively
		dfs_report_recursive(child, fd, accumulate, nregion);
		child = child->next_sibling;
	}

	if (OTHER_CHILDS){
		region_t other_child;
		ini_other_child(&other_child, curr, accumulate);
		print_region_human(fd, *nregion, &other_child, accumulate, !accumulate, 1, 1); 
		*nregion = *nregion+1;
		free(other_child.name);
	}
}


void print_region_report(FILE * fd, int accumulate){
	fprintf(fd,"-------------------" " REPORT " "-------------------" "\n"); 
	int nregion=0;
	reset_indent();
	dfs_report_recursive(track_regions.first_region, fd, accumulate, &nregion);
	fprintf(fd,"------------------------------------------------\n");
	fflush(fd);
}

void dfs_csv_recursive(region_node_t* curr, FILE * fd, int accumulate, int * nregion){
	if (curr==NULL) return;
		if (curr->region.closed/* && curr->region.enabled*/){
			fprintf(fd,"%d,%d,%d,%s",mpi_rank, curr->region.opened_by, *nregion, curr->region.name);
			fprintf(fd,",%d(%s)", curr->region.nesting, *nregion==0? "N/A" : curr->region.parent==NULL?"Multiple":curr->region.parent->name);
			fprintf(fd,",%d", curr->region.executions);
			print_counters_csv(fd, accumulate ? &curr->region.acc_counters : &curr->region.delta_counters);
			*nregion = *nregion+1;
			//fprintf(fd, ",%s\n", *nregion==1? "N/A" : curr->region.parent==NULL?"Multiple":curr->region.parent->name);
			fprintf(fd, "\n");
		}

	region_node_t* child = curr->first_child;
	if (child==NULL) return;
	while(child!=NULL){
		dfs_csv_recursive(child, fd, accumulate, nregion);
		child = child->next_sibling;
	}
	if (OTHER_CHILDS){
		region_t other_child;
		ini_other_child(&other_child, curr, accumulate);
		print_region_human(fd, *nregion, &other_child, accumulate, !accumulate, 1, 1); 
		fprintf(fd,"%d,%d,%d,%s,%d(%s),%d",mpi_rank, -1, *nregion, other_child.name,curr->region.nesting+1,curr->region.name, 1);
		print_counters_csv(fd, accumulate ? &other_child.acc_counters : &other_child.delta_counters);
		fprintf(fd, "\n");
		*nregion = *nregion+1;
		free(other_child.name);
	}
}
void print_region_csv(FILE * fd, int accumulate){
	fprintf(fd,"process_id,thread_id,region,name,nesting(parent),executions,");
	print_csv_header(fd);
	fprintf(fd,"\n");
	int nregion=0;
	dfs_csv_recursive(track_regions.first_region, fd, accumulate, &nregion);
	fflush(fd);
}


void region_trace(unsigned int cpu_index, int event, int value){
	set_lock(write_lock);
	if (PRINT_PRV){
		uint64_t thread_timestamp = cpus_state[cpu_index].timestamp;
		trace_row(FD_PRV,mpi_rank, cpu_index, SCALAR_ROW, thread_timestamp);
		trace_event_value(FD_PRV,event,value);
		if (!TRACE_SCALAR) trace_event_value(FD_PRV,event_instruction, PRV_SCALAR*!MUSA);
		trace_row(FD_PRV,mpi_rank, cpu_index, VECTOR_ROW, thread_timestamp);
		trace_event_value(FD_PRV,event, value);
	}
	release_lock(write_lock);
}

void rave_begin_region(char * str_ptr, thread_state_t * state){
	if (!REGIONS_ENABLED) return;
	internal_begin_region(state->cpu_index, str_ptr, &state->accum_counters, ACCUM_REGIONS);
	region_trace(state->cpu_index, REGION_EVENT+track_regions.nesting-1, name_to_id(str_ptr));
	if (PRINT_LOGFILE){
		set_lock(write_lock);
		plugin_outs("Begin region ");
		plugin_outs(str_ptr);
		plugin_outs("\n");
		release_lock(write_lock);
	}
}
void rave_end_region(char * str_ptr, thread_state_t * state){
	if (!REGIONS_ENABLED) return;
	internal_end_region(state->cpu_index, str_ptr, &state->accum_counters, ACCUM_REGIONS, STREAM_REPORT?FD_REPORT:NULL);
	region_trace(state->cpu_index, REGION_EVENT+track_regions.nesting, 0);
	if (PRINT_LOGFILE){
		set_lock(write_lock);
		plugin_outs("End region ");
		plugin_outs(str_ptr);
		plugin_outs("\n");
		release_lock(write_lock);
	}
}

/* -------------------------------------------------------------------------- */
/* JSON report                                                                 */
/* -------------------------------------------------------------------------- */

static void json_region(FILE * fd, int id, region_t * region, rave_counters * counters){
	int f = 1;
	fputc('{', fd);
	json_push();
	json_int(fd, &f, "id", id);
	json_str(fd, &f, "name", region->name);
	json_int(fd, &f, "nesting", region->nesting);
	json_int(fd, &f, "rank", mpi_rank);
	json_int(fd, &f, "thread", region->opened_by);
	json_int(fd, &f, "executions", region->executions);
	json_str(fd, &f, "parent", region->parent != NULL ? region->parent->name : NULL);
	json_key_only(fd, &f, "counters");
	print_counters_json(fd, counters);
	json_pop();
	json_indent(fd);
	fputc('}', fd);
}

static void json_emit_region(FILE * fd, int * nregion, int * first, region_t * region, rave_counters * delta, rave_counters * acc, int executions, int accumulate){
	rave_counters tmp;
	if (accumulate){
		double exec = executions > 0 ? (double)executions : 1.0;
		mul_counters(&tmp, acc, 1.0/exec);
	}else{
		copy_counters(&tmp, delta);
	}
	json_elem(fd, first);
	json_region(fd, *nregion, region, &tmp);
	*nregion = *nregion + 1;
}

void dfs_json_recursive(region_node_t* curr, FILE * fd, int accumulate, int * nregion, int * first){
	if (curr==NULL) return;

	if (curr->region.closed){
		json_emit_region(fd, nregion, first, &curr->region, &curr->region.delta_counters, &curr->region.acc_counters, curr->region.executions, accumulate);
	}

	region_node_t* child = curr->first_child;
	while(child!=NULL){
		dfs_json_recursive(child, fd, accumulate, nregion, first);
		child = child->next_sibling;
	}

	if (OTHER_CHILDS){
		region_t other_child;
		memset(&other_child, 0, sizeof(region_t));
		ini_other_child(&other_child, curr, accumulate);
		json_emit_region(fd, nregion, first, &other_child, &other_child.delta_counters, &other_child.acc_counters, other_child.executions, accumulate);
		free(other_child.name);
	}
}

void print_region_json(FILE * fd, int accumulate){
	int nregion = 0;
	int first = 1;

	json_reset();
	fputc('{', fd);
	json_push();
	json_str(fd, &first, "generator", "RAVE");
	json_str(fd, &first, "binary", BINARY_NAME);
	json_int(fd, &first, "vlen_bits", RAVE_VLMAX);
	json_bool(fd, &first, "trace_extended", TRACE_EXTENDED);
	json_bool(fd, &first, "accumulate_regions", accumulate);
	json_int(fd, &first, "region_count", track_regions.total_regions);
	json_arr_open(fd, &first, "regions");
	{
		int ef = 1;
		dfs_json_recursive(track_regions.first_region, fd, accumulate, &nregion, &ef);
	}
	json_arr_close(fd);
	json_pop();
	json_indent(fd);
	fputs("}\n", fd);
	fflush(fd);
}
