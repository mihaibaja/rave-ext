/*********************************************************/
// Copyright (C) 2026 Barcelona Supercomputing Center-Centro Nacional de Supercomputación
// SPDX-License-Identifier: BSD-3-Clause
/*********************************************************/
// * Author: Pablo Vizcaino
// * Email:  pablo.vizcaino@bsc.es
/*********************************************************/
#pragma once

#include <stdio.h>
#include <string.h>

#define CLEAR_FORMAT "\033[0m"
#if 0
	#define BOLD_RED "\033[1;37;41m"
	#define BOLD_BLUE "\033[1;37;44m"
	#define BOLD_GREEN "\033[1;30;42m"
	#define BOLD_PINK "\033[1;37;45m"
	#define BOLD_CYAN "\033[1;30;46m"
	#define BOLD_WHITE "\033[1m"
	#define BOLD_YELLOW "\033[1;30;43m"
#else
	#define BOLD_RED "\033[1;31m"
	#define BOLD_BLUE "\033[1;34m"
	#define BOLD_GREEN "\033[1;32m"
	#define BOLD_PINK "\033[1;35m"
	#define BOLD_CYAN "\033[1;36m"
	#define BOLD_BLACK_ON_CYAN "\033[1;30;46m"
	#define BOLD_WHITE "\033[1m"
	#define BOLD_YELLOW "\033[1;33m"
	#define BOLD_BLACK_ON_YELLOW "\033[1;30;43m"
#endif

//From state.h
extern int PLAIN_TEXT;
extern int COMPRESS_REPORT;
#define P_GENERIC(fd,format,x,color)\
	if(!PLAIN_TEXT) fprintf(fd,color); \
	fprintf(fd,format,x);\
	if(!PLAIN_TEXT) fprintf(fd,CLEAR_FORMAT);


#define P_WARNING(fd,format,name) P_GENERIC(fd,format,name,BOLD_RED)
#define P_COUNTERS(fd,format,name) P_GENERIC(fd,format,name,BOLD_BLACK_ON_YELLOW)
//#define P_COUNTERS(fd,format,name) P_GENERIC(fd,format,name,BOLD_YELLOW)
#define P_NAME(fd,format,name) P_GENERIC(fd,format,name,BOLD_BLACK_ON_CYAN)
//#define P_NAME(fd,format,name) P_GENERIC(fd,format,name,BOLD_CYAN)
#define P_NUMBER(fd,format,x) P_GENERIC(fd,format,x,BOLD_BLUE)
#define P_VL(fd,format,x) P_GENERIC(fd,format,x,BOLD_GREEN)
#define P_PERCENTAGE(fd,format,x) P_GENERIC(fd,format,x,BOLD_PINK)

#define P_NUMBER2(fd, format, x) P_GENERIC(fd,format,x,BOLD_GREEN) 
#define P_NUMBER3(fd, format, x) P_GENERIC(fd,format,x,BOLD_PINK)
#define P_NUMBER4(fd, format, x) P_GENERIC(fd,format,x,BOLD_YELLOW)
#define P_NUMBER5(fd, format, x) P_GENERIC(fd,format,x,BOLD_RED)

struct indent_control_t{
	char buffer[1024];
	int offset;
	short offsets[128]; //offsets point to where each nest symbol is
	int nesting;
	int clear_previous;
	int prev_nest;
	int spaces;
};
typedef struct indent_control_t indent_control_t;

//Exported variables
extern indent_control_t ic;

void reset_indent(void);
char * indent_add(int is_last);
char * indent_sub(int howmany, int is_last);
char * indent_none(int is_last);
void indent(FILE * fd, int level, int is_last);

/* -------------------------------------------------------------------------- */
/* Minimal JSON writer helpers (used by the `rave --json <file>` output).      */
/* Pretty printed: one member per line with 2-space nesting.                   */
/* Every member helper takes a `first` flag so the caller does not need to     */
/* know whether it is emitting the first member of an object/array:            */
/*   int first = 1;                                                            */
/*   json_obj_open(fd, &first, "counters");  ...  json_obj_close(fd);          */
/* Nesting is tracked internally: use json_push()/json_pop() when a brace is   */
/* written by hand (e.g. around a nested print_*_json() call) and json_elem()  */
/* to separate the elements of an array.                                       */
/* -------------------------------------------------------------------------- */
void json_reset(void);
void json_push(void);
void json_pop(void);
void json_indent(FILE * fd);
void json_elem(FILE * fd, int * first);
void json_sep(FILE * fd, int * first);
void json_obj_open(FILE * fd, int * first, const char * key);
void json_obj_close(FILE * fd);
void json_arr_open(FILE * fd, int * first, const char * key);
void json_arr_close(FILE * fd);
void json_num(FILE * fd, int * first, const char * key, double value);
void json_int(FILE * fd, int * first, const char * key, double value);
void json_bool(FILE * fd, int * first, const char * key, int value);
void json_key_only(FILE * fd, int * first, const char * key);
void json_str(FILE * fd, int * first, const char * key, const char * value);
void json_escape(FILE * fd, const char * s);
