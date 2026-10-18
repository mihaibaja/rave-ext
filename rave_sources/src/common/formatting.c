/*********************************************************/
// Copyright (C) 2026 Barcelona Supercomputing Center-Centro Nacional de Supercomputación
// SPDX-License-Identifier: BSD-3-Clause
/*********************************************************/
// * Author: Pablo Vizcaino
// * Email:  pablo.vizcaino@bsc.es
/*********************************************************/

#include "formatting.h"

indent_control_t ic = {0};

const char sym_pipe[]={"│"};
const char sym_cross[]={"├"};
const char sym_line[]={"─"};
const char sym_ele[]={"└"};

void reset_indent(void){
	ic.buffer[0]='\0';
	ic.offset=0;
	ic.nesting=0;
	ic.clear_previous=0;
	ic.prev_nest=0;
	ic.spaces=0;
}

char * indent_add(int is_last){
	//If previous was last, we need to change last segment to spaces:
	if (ic.nesting>0){
		if (ic.clear_previous){
			//I need to put a space and shift 2 positions, because the special characters are 3 chars wide.
			ic.buffer[ic.offsets[ic.nesting-1]] = ' ';
			char * rewrite = &ic.buffer[ic.offsets[ic.nesting-1]+1];
			while (*rewrite != '\0'){
				*(rewrite) = *(rewrite+2);
				++rewrite;
			}
			ic.offset -= 2;
		}else{ 
			//If not, change last indent character to a pipe
			strcpy(&ic.buffer[ic.offsets[ic.nesting-1]], sym_pipe); 
		}
	}
	ic.clear_previous=0;
	//Increase nesting
	++ic.nesting;

	//Append some spaces
	for(int i=0; i<ic.spaces; ++i) ic.buffer[ic.offset++] = ' ';

	ic.offsets[ic.nesting-1] = ic.offset;

	//Append the special character
	if (is_last) strcpy(&ic.buffer[ic.offset], sym_ele);
	else strcpy(&ic.buffer[ic.offset], sym_cross);
	ic.offset+=3;
	ic.buffer[ic.offset] = '\0';

	if (is_last) ic.clear_previous=1;
	

	return ic.buffer;
}

char * indent_sub(int howmany, int is_last){
	//I don't need to clear previous, as I'm already going back.	
	ic.clear_previous=0;
	//Remove nesting:
	ic.nesting -= howmany;
	if(ic.nesting<0) ic.nesting=0;

	//Append the special character
	if (ic.nesting>0){
		ic.offset = ic.offsets[ic.nesting-1]; 
		if (is_last){
		 	strcpy(&ic.buffer[ic.offset], sym_ele);
			ic.clear_previous=1;
		}else strcpy(&ic.buffer[ic.offset], sym_cross);
		ic.offset+=3;
		ic.buffer[ic.offset] = '\0';
	}
	return ic.buffer;
}

char * indent_none(int is_last){
	//I don't need to clear previous, because previous will never be last (if i'm not indenting...)
	ic.clear_previous=0;
	//If is last, change last indent character to an L
	if (is_last){
		strcpy(&ic.buffer[ic.offsets[ic.nesting-1]], sym_ele);
		ic.clear_previous=1;
	}
	return ic.buffer;
}

void indent(FILE * fd, int level, int is_last){
	int inc = level-(ic.prev_nest);
	ic.prev_nest=level;
	if (inc==0) indent_none(is_last);
	else if (inc>0) indent_add(is_last);
	else indent_sub(-inc,is_last);
	fprintf(fd,"%s─ ",ic.buffer);
}

/* -------------------------------------------------------------------------- */
/* Minimal JSON writer (pretty printed, one member per line, 2-space nesting)  */
/* -------------------------------------------------------------------------- */

static int json_level = 0;

void json_reset(void){
	json_level = 0;
}

void json_push(void){
	++json_level;
}

void json_pop(void){
	if (json_level > 0) --json_level;
}

void json_indent(FILE * fd){
	fputc('\n', fd);
	for(int i=0; i<json_level; ++i) fputs("  ", fd);
}

static void json_key(FILE * fd, int * first, const char * key){
	if (!*first) fputc(',', fd);
	*first = 0;
	json_indent(fd);
	fputc('"', fd);
	json_escape(fd, key);
	fputs("\": ", fd);
}

void json_sep(FILE * fd, int * first){
	if (!*first) fputc(',', fd);
	*first = 0;
}

void json_elem(FILE * fd, int * first){
	if (!*first) fputc(',', fd);
	*first = 0;
	json_indent(fd);
}

void json_obj_open(FILE * fd, int * first, const char * key){
	json_key(fd, first, key);
	fputc('{', fd);
	json_push();
}

void json_obj_close(FILE * fd){
	json_pop();
	json_indent(fd);
	fputc('}', fd);
}

void json_arr_open(FILE * fd, int * first, const char * key){
	json_key(fd, first, key);
	fputc('[', fd);
	json_push();
}

void json_arr_close(FILE * fd){
	json_pop();
	json_indent(fd);
	fputc(']', fd);
}

void json_num(FILE * fd, int * first, const char * key, double value){
	json_key(fd, first, key);
	if (value == (double)(long long)value) fprintf(fd, "%.0f", value);
	else fprintf(fd, "%.6f", value);
}

void json_int(FILE * fd, int * first, const char * key, double value){
	json_key(fd, first, key);
	fprintf(fd, "%.0f", value);
}

void json_bool(FILE * fd, int * first, const char * key, int value){
	json_key(fd, first, key);
	fputs(value ? "true" : "false", fd);
}

void json_key_only(FILE * fd, int * first, const char * key){
	json_key(fd, first, key);
}

void json_str(FILE * fd, int * first, const char * key, const char * value){
	json_key(fd, first, key);
	if (value == NULL){
		fputs("null", fd);
	}else{
		fputc('"', fd);
		json_escape(fd, value);
		fputc('"', fd);
	}
}

void json_escape(FILE * fd, const char * s){
	if (s == NULL) return;
	for(; *s; ++s){
		unsigned char c = (unsigned char)*s;
		switch(c){
			case '"':  fputs("\\\"", fd); break;
			case '\\': fputs("\\\\", fd); break;
			case '\n': fputs("\\n", fd); break;
			case '\t': fputs("\\t", fd); break;
			case '\r': fputs("\\r", fd); break;
			case '\b': fputs("\\b", fd); break;
			case '\f': fputs("\\f", fd); break;
			default:
				if (c < 0x20) fprintf(fd, "\\u%04x", c);
				else fputc(c, fd);
		}
	}
}
