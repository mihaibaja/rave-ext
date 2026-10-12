/*********************************************************/
// Copyright (C) 2025 Barcelona Supercomputing Center-Centro Nacional de Supercomputación
// SPDX-License-Identifier: BSD-3-Clause
/*********************************************************/
// * Author: Pablo Vizcaino
// * Email:  pablo.vizcaino@bsc.es
/*********************************************************/
#pragma once

#include "counters.h"
#include "stdint.h"
#include "state.h"

struct region_t{
	char closed;
	int nesting;
	char * name;
	int executions;
	rave_counters delta_counters;
	rave_counters acc_counters;
	int opened_by;
	struct region_t * parent;
	//int enabled;
};
typedef struct region_t region_t;


struct region_node_t{
	region_t region;
	struct region_node_t * first_child;
	struct region_node_t * last_child;
	struct region_node_t * next_sibling;
};
typedef struct region_node_t region_node_t;

struct region_stack_t{
	region_node_t * region_node;
	struct region_stack_t * prev;
};
typedef struct region_stack_t region_stack_t;

struct track_regions_t{
	region_node_t * first_region;
	int nesting; //TODO: This should be THREAD-independent?
	int total_regions;
	int max_nested;
	int max_name;
	region_stack_t * stack_top;
};
typedef struct track_regions_t track_regions_t;

struct region_unique_list_t{
	int region_id;
	region_t * region;
	struct region_unique_list_t * next;
};
typedef struct region_unique_list_t region_unique_list_t;

//Exported Variables
extern track_regions_t track_regions;
extern region_unique_list_t *first_unique_region;
extern region_unique_list_t *last_unique_region; 

int name_to_id(const char * name);

region_node_t* dfs_find_recursive(region_node_t* curr, const char * name);
void dfs_report_recursive(region_node_t* curr, FILE * fd, int accumulate, int * nregion);
void dfs_csv_recursive(region_node_t* curr, FILE * fd, int accumulate, int * nregion);
void dfs_free_recursive(region_node_t* curr);

void free_regions(void);

void rave_ini_regions(void);
void internal_end_region(int cpu_index, const char * name, rave_counters * current_counters, int accumulate, FILE * fd);
void internal_begin_region(int cpu_index, const char * name, rave_counters * current_counters, int accumulate/*, int enabled*/);

void print_region_human(FILE * fd, int n, region_t * region, int accumulate, int indent_region, int last_child, int no_childs);
void print_region_report(FILE * fd, int accumulate);
void print_region_csv(FILE * fd, int accumulate);
void print_region_json(FILE * fd, int accumulate);
void dfs_json_recursive(region_node_t* curr, FILE * fd, int accumulate, int * nregion, int * first);

void rave_begin_region(char * str_ptr, thread_state_t * state);
void rave_end_region(char * str_ptr, thread_state_t * state);
