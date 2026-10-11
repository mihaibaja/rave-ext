/*********************************************************/
// Copyright (C) 2025 Barcelona Supercomputing Center-Centro Nacional de Supercomputación
// SPDX-License-Identifier: BSD-3-Clause
/*********************************************************/
// * Author: Pablo Vizcaino
// * Email:  pablo.vizcaino@bsc.es
/*********************************************************/

#include "init_exit.h"
#include "threading.h"
#include "state.h"
//#include "tb_hook.h"
#include "counters_generic.h"
#include "rave2prv.h"
#include "regions.h"
#include "formatting.h"
#include "profiling.h"
#include "write_pcf.h"
#include "utils.h"
#include "inst_trace.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <unistd.h>

void rave_exit()
{
	rave_counters global_counters;
	reset_counters(&global_counters);
	double * global_counters_ptr = (double *)&global_counters;
	for(int i=0; i<N_THREADS; ++i){
		double * thread_counters_ptr = (double *)&cpus_state[i].accum_counters; 
		for(size_t c=0; c<sizeof(rave_counters)/sizeof(double); ++c){
			global_counters_ptr[c] += thread_counters_ptr[c]; 
		}
	}

	internal_end_region(-1, "GLOBAL_REGION", &global_counters, ACCUM_REGIONS, NULL);


	//print_samples();
	//rave_eventandcounters(-1, 0, -1, &global_counters); //End Global event
	if(PRINT_REPORT && !STREAM_REPORT){
		//Warning:
		if (track_regions.total_regions<=1){
			P_WARNING(FD_REPORT, "%s","----------------- WARNING! ---------------"); fprintf(FD_REPORT,"\n");
			fprintf(FD_REPORT, "You did not define any code regions. Remember that code regions are defined with \"rave_begin/end_region\" now (or trace_\"begin/end\"_region if you are using sdv_trace\n");
			P_WARNING(FD_REPORT, "%s","--------------------------------------------"); fprintf(FD_REPORT,"\n");
		}
		print_region_report(FD_REPORT, ACCUM_REGIONS);
		//print_events_report(FD_REPORT);
	}
	if (PRINT_CSV){
		print_region_csv(FD_CSV, ACCUM_REGIONS);
		//print_events_csv(FD_CSV);
	}

	if (PRINT_PROFILE){
		init_dwfl(BINARY_NAME);
		for(int i=0; i<N_THREADS; ++i){
			fprintf(FD_PROFILE,"-------------------" " PROFILED LOOPS (thread %d) " "--------------------" "\n", i);
			print_loop_profile(FD_PROFILE, &cpus_state[i].loop_profile);
			fprintf(FD_PROFILE, "--------------------------------------------------------------------------\n");
		}
		//print_region_profile(FD_PROFILE, ACCUM_REGIONS);
	}

	if (PRINT_CALLTRACE){
#if 1
		//init_dwfl(BINARY_NAME);
		for(int i=0; i<N_THREADS; ++i){
			fprintf(FD_CALLTRACE,"-------------------" " CALL TRACE (thread %d) " "--------------------" "\n", i);
			print_call_trace(FD_CALLTRACE, &cpus_state[i].call_trace);
			fprintf(FD_CALLTRACE, "--------------------------------------------------------------------------\n");
		}
#endif

	}

	if (FD_CSV!=NULL) fclose(FD_CSV);
	if (FD_REPORT!=NULL) fclose(FD_REPORT);
	if (FD_PROFILE!=NULL) fclose(FD_PROFILE);
	if (FD_CALLTRACE!=NULL) fclose (FD_CALLTRACE);

#ifdef TIMEDEBUG
	printf("Cycles in Translation: %.4f %d times, %lu (%.2f)\n", (double)time_trans/num_trans, num_trans, time_trans, (double)time_trans/(time_trans+time_vcpu_exe+time_vcpu_control));
	printf("Cycles in VCPU_exe: %.4f %d times, %lu (%.2f)\n", (double)time_vcpu_exe/num_vcpu_exe, num_vcpu_exe, time_vcpu_exe, (double)time_vcpu_exe/(time_trans+time_vcpu_exe+time_vcpu_control));
	printf("Cycles in VCPU_event: %.4f %d times, %lu (%.2f)\n", (double)time_vcpu_control/num_vcpu_control, num_vcpu_control, time_vcpu_control, (double)time_vcpu_control/(time_trans+time_vcpu_exe+time_vcpu_control));
#endif

	if (disabled_once && PRINT_PRV && (PRINT_CSV || PRINT_REPORT)){
		P_WARNING(stdout, "%s", "WARNING: Possible mismatch between Report/CSV and Paraver trace, as you used the rave_stop_trace/trace_disable directive"); fprintf(stdout,"\n");
	}

	if (PRINT_PRV){
		//Align end of trace
		if (TRACE_ENABLED){
			for(int i=0; i<N_THREADS; ++i){
				if (cpus_state[i].timestamp > 0){
					trace_row(FD_PRV,mpi_rank, i, cpus_state[i].last_row, cpus_state[i].timestamp+1);
					clean_event(FD_PRV); 
				}
			}
		}

		int fd = fileno(FD_PRV);
		fclose(FD_PRV);
		file_lock(fd, LOCK_UN); //Unlock PRV

		if (mpi_rank > 0){
			//Write NTHREADS to file
			fprintf(FD_COMM,"%d\n",N_THREADS);
			fd=fileno(FD_COMM);
			fclose(FD_COMM);
			file_lock(fd, LOCK_UN); //Unlock COMM
		}else if (mpi_rank == 0){
			//Read COMM from others
			int * N_THREADS_all = (int *)malloc(sizeof(int)*mpi_size);
			N_THREADS_all[0] = N_THREADS;
			for(int i=1; i<mpi_size; ++i){
				char * rank_comm_file = malloc(snprintf(NULL, 0, "%s-%d.com", filename,i));
				sprintf(rank_comm_file, "%s-%d.com", filename,i);

				FD_COMM = fopen(rank_comm_file, "r");
				file_lock(fileno(FD_COMM), LOCK_EX); //Wait for lock on COM
																						 //Read N_THREADS
				int n_threads_rank;
				int ret = fscanf(FD_COMM, "%d\n", &n_threads_rank);
				if (!ret) n_threads_rank=0;
				N_THREADS_all[i] = n_threads_rank;

				file_lock(fileno(FD_COMM), LOCK_UN); //Unlock COMM
				fclose(FD_COMM);
				remove(rank_comm_file);
				free(rank_comm_file);
			}

			//Write ROW
			int len = strlen(filename)+1+4;
			char * ext_filename = malloc(len);
			sprintf(ext_filename, "%s.row", filename);
			open_file(&FD_ROW, ext_filename);
			if (MUSA){
				fprintf(FD_ROW, "LEVEL CPU SIZE 1\n");
				fprintf(FD_ROW, "scalar+vec\n");
				fprintf(FD_ROW, "LEVEL TASK SIZE 1\n");
				fprintf(FD_ROW, "scalar+vec\n");
				fprintf(FD_ROW, "LEVEL NODE SIZE 1\n");
				fprintf(FD_ROW, "scalar+vec\n");
				fprintf(FD_ROW, "LEVEL THREAD SIZE 1\n");
				fprintf(FD_ROW, "scalar+vec\n");
			}else{
				write_row(FD_ROW, mpi_size, N_THREADS_all, N_PIPELINES);
			}

			fclose(FD_ROW);	

			//Write PCF
			sprintf(ext_filename, "%s.pcf", filename);
			open_file(&FD_PCF, ext_filename);
			events_and_values_to_pcf(FD_PCF, REGION_EVENT, track_regions.max_nested );
			write_pcf(FD_PCF);
			fclose(FD_PCF);

			free(ext_filename);

			//Rewrite header when more than 1 cpu was used (either MPI, OMP, or both)
			if (mpi_size > 1 || N_THREADS > expected_threads){ 
				FILE * FD_NEWPRV;
				char namebuff[32];
				sprintf(namebuff, "tmpfile-%d", getpid());
				open_file(&FD_NEWPRV, namebuff);
				write_prv(FD_NEWPRV, mpi_size, N_THREADS_all, N_PIPELINES); // write header
				trace_row(FD_PRV,0, 0, SCALAR_ROW, 0);
				trace_event_value(FD_PRV,event_VLEN,RAVE_VLMAX);
				trace_event_value(FD_PRV,event_ELEN,RAVE_ELEN);

#define PRV_BUFFSIZE 2048
				char buff[PRV_BUFFSIZE];

				//Merging
				for(int i=0; i<mpi_size; ++i){
					fprintf(FD_NEWPRV, "\n");

					char * rank_file;
					if (mpi_size==1){
						rank_file = malloc(snprintf(NULL, 0, "%s.prv", filename));
						sprintf(rank_file, "%s.prv", filename);
					}else{
						rank_file = malloc(snprintf(NULL, 0, "%s-%d.prv", filename, i));
						sprintf(rank_file, "%s-%d.prv", filename, i);
					}

					FD_PRV = fopen(rank_file, "r");
					file_lock(fileno(FD_PRV), LOCK_EX); //Wait for lock on PRV

					int found_newline=0;
					int r;
					//Skip header
					while (!found_newline && (r=fread(buff, 1, PRV_BUFFSIZE, FD_PRV))){
						for(int j=0; j<r; ++j){
							if (buff[j]=='\n'){
								found_newline=1;
								fwrite(&buff[j+1], 1, r-j-1, FD_NEWPRV);
								break;
							}
						}
					}
					//Copy PRV
					while ((r=fread(buff, 1, PRV_BUFFSIZE, FD_PRV)))	fwrite(buff, 1, r, FD_NEWPRV);

					fclose(FD_PRV);
					file_lock(fileno(FD_PRV), LOCK_UN); //Unlock PRV
					remove(rank_file);
					free(rank_file);
				}
				fclose(FD_NEWPRV);

				char * main_file = malloc(snprintf(NULL, 0, "%s.prv", filename));
				sprintf(main_file, "%s.prv", filename);
				rename(namebuff, main_file);
			}
		}
		free(filename);
	}
	free_regions();
	//free_event_regions(); //Legacy
	if (INST_TRACE_ENABLED) {
		inst_trace_exit();
	}
	fflush(stdout);
}


void rave_init(int argc, char **argv){

	//Initialize:
	mpi_rank = 0;
	mpi_size = 1;
	N_THREADS = 0; 
	expected_threads = 0;
	alloc_threads = 0;


	N_PIPELINES = 2;
	MUSA = 0;

	PLAIN_TEXT=0;
	COMPRESS_REPORT=0;

	timestamp = 0;
	base = -1;


	char * RAVE_VLEN = getenv("RAVE_VLEN");
#if defined(RVV_07) || defined(RVV_10)
	RAVE_VLMAX = RAVE_VLEN==NULL? 16384 : atoi(RAVE_VLEN);
#elif defined(X86_64)
	RAVE_VLMAX = RAVE_VLEN==NULL? 512 : atoi(RAVE_VLEN);
#endif

	parallel_region.master_thread = -1;
	//long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
	//printf("nprocs: %d\n", nprocs);
	char * OMP_NUM_THREADS = getenv("OMP_NUM_THREADS");
	int omp_threads = OMP_NUM_THREADS==NULL? 1 : atoi(OMP_NUM_THREADS);
	char * RAVE_MAX_THREADS = getenv("RAVE_MAX_THREADS");
	int rave_threads = RAVE_MAX_THREADS==NULL ? 1 : atoi(RAVE_MAX_THREADS);
	alloc_threads = omp_threads > rave_threads ? omp_threads : rave_threads;

	char * world_rank = getenv("OMPI_COMM_WORLD_SIZE");
	if ( world_rank!=NULL/* && alloc_threads < 4*/ ) alloc_threads += 3; //Mpi process adds two/three threads
	mpi_size = world_rank==NULL? 1 : atoi(world_rank);

	char * RAVE_INST_TRACE = getenv("RAVE_INST_TRACE_ENABLED");
	if (RAVE_INST_TRACE != NULL && atoi(RAVE_INST_TRACE) == 1) INST_TRACE_ENABLED = 1;
	char * RAVE_INST_NAME = getenv("RAVE_INST_TRACE_NAME");
	if (RAVE_INST_NAME != NULL) {
		INST_TRACE_NAME = strdup(RAVE_INST_NAME);
	}

	expected_threads = alloc_threads;
	cpus_state = (thread_state_t*)malloc(sizeof(thread_state_t)*alloc_threads);

	for(int i=0; i<argc; ++i){
		if (contains_string(argv[i], "TRACE_SCALAR")) TRACE_SCALAR = 1;
		else if (contains_string(argv[i], "TRACE_ADDR")) TRACE_ADDR = 1;
		else if (contains_string(argv[i], "TRACE_INDEXES")) TRACE_INDEXES = 1;
		else if (contains_string(argv[i], "TRACE_EXTENDED")) TRACE_EXTENDED = 1;
		else if (contains_string(argv[i], "INST_TRACE_ENABLED")) INST_TRACE_ENABLED = 1;
		else if (contains_string(argv[i], "INST_TRACE_NAME")){
			size_t j; for(j=0; j<strlen(argv[i]); ++j) if (argv[i][j] == '=') break;
			if (INST_TRACE_NAME) free(INST_TRACE_NAME);
			int len = strlen(&argv[i][j+1]);
			INST_TRACE_NAME = malloc(len+1);
			strcpy(INST_TRACE_NAME, &argv[i][j+1]);
		}
		else if (contains_string(argv[i], "DEBUG_INFO")) DEBUG_INFO = 1;
		else if (contains_string(argv[i], "PRINT_PRV")) PRINT_PRV = 1;
		else if (contains_string(argv[i], "PRINT_LOGFILE")) PRINT_LOGFILE = 1;
		else if (contains_string(argv[i], "PRINT_REPORT")) PRINT_REPORT = 1;
		else if (contains_string(argv[i], "STREAM_REPORT")) STREAM_REPORT = 1;
		else if (contains_string(argv[i], "PRINT_CSV")) PRINT_CSV = 1;
		else if (contains_string(argv[i], "PRINT_PROFILE")) PRINT_PROFILE = 1;
		else if (contains_string(argv[i], "PRINT_CALLTRACE")) PRINT_CALLTRACE = 1;
		else if (contains_string(argv[i], "ACCUM_REGIONS")) ACCUM_REGIONS = 1;
		else if (contains_string(argv[i], "PLAIN_TEXT")) PLAIN_TEXT = 1;
		else if (contains_string(argv[i], "COMPRESS_REPORT")) COMPRESS_REPORT = 1;
		else if (contains_string(argv[i], "OTHER_CHILDS")) OTHER_CHILDS = 1;
		else if (contains_string(argv[i], "MUSA")) {
			MUSA = 1;
			N_PIPELINES = 1;
		}
		else if (contains_string(argv[i], "PRV_NAME")){
#if 1
			size_t j; for(j=0; j<strlen(argv[i]); ++j)	if (argv[i][j] == '=') break;

			int l_filename = strlen(&argv[i][j+1]);
			filename = malloc(l_filename+1);
			strcpy(filename, &argv[i][j+1]);

			int l_ext = 4; //.prv, .pcf, .row

			//Check for MPI
			char * prv_filename;
			if (mpi_size > 1){
				//if (world_rank != NULL && strcmp(world_rank,"1")){
				char * rank = getenv("OMPI_COMM_WORLD_RANK");
				mpi_rank=atoi(rank);
				int l_rank = strlen(rank);
				prv_filename = malloc(l_filename+l_rank+l_ext+1);
				strcpy(prv_filename, &argv[i][j+1]);
				sprintf(&prv_filename[l_filename],"-%s.prv",rank);
				open_file(&FD_PRV, prv_filename);
				file_lock(fileno(FD_PRV), LOCK_EX); //Lock PRV for this process

				//Communications file
				if (mpi_rank > 0){
					sprintf(&prv_filename[l_filename],"-%s.com",rank);
					open_file(&FD_COMM,prv_filename);
					file_lock(fileno(FD_COMM), LOCK_EX); //Lock COM for this process
				}
			}else{
				prv_filename = malloc(l_filename+l_ext+1);
				strcpy(prv_filename, &argv[i][j+1]);
				strcpy(&prv_filename[l_filename],".prv");
				open_file(&FD_PRV, prv_filename);
				file_lock(fileno(FD_PRV), LOCK_EX); //Lock PRV for this process
			}
			free(prv_filename);
#endif
			}
			else if (contains_string(argv[i], "CSV_NAME")){
				//++i;
				size_t j; for(j=0; j<strlen(argv[i]); ++j) if (argv[i][j] == '=') break;
				mpi_size = world_rank==NULL? 1 : atoi(world_rank);
				if (mpi_size > 1){
					char * rank = getenv("OMPI_COMM_WORLD_RANK");
					mpi_rank=atoi(rank);
					int l_rank = strlen(rank);
					int l_filename = strlen(&argv[i][j+1]);
					char * csv_filename = malloc(l_filename+l_rank+1);
					strcpy(csv_filename, &argv[i][j+1]);
					sprintf(&csv_filename[l_filename],"-%s",rank);
					FD_CSV = fopen(csv_filename, "w+");
				}else{
					FD_CSV = fopen(&argv[i][j+1], "w+");
				}
			}
			else if (contains_string(argv[i], "REPORT_NAME")){
				size_t j; for(j=0; j<strlen(argv[i]); ++j) if (argv[i][j] == '=') break;
				FD_REPORT = fopen(&argv[i][j+1], "w");
			}
			else if (contains_string(argv[i], "PROFILE_NAME")){
				size_t j; for(j=0; j<strlen(argv[i]); ++j) if (argv[i][j] == '=') break;
				FD_PROFILE = fopen(&argv[i][j+1], "w");
			}
			else if (contains_string(argv[i], "CALLTRACE_NAME")){
				size_t j; for(j=0; j<strlen(argv[i]); ++j) if (argv[i][j] == '=') break;
				FD_CALLTRACE = fopen(&argv[i][j+1], "w");
			}
			else if (contains_string(argv[i], "REGION_EVENT")){
				size_t j; for(j=0; j<strlen(argv[i]); ++j) if (argv[i][j] == '=') break;
				int event = atoi(&argv[i][j+1]);
				REGION_EVENT = event>0 ? event : REGION_EVENT;
			}
			else if (contains_string(argv[i], "RAW_DIST")){
				size_t j; for(j=0; j<strlen(argv[i]); ++j) if (argv[i][j] == '=') break;
				RAW_DIST = atoi(&argv[i][j+1]);
			}
			else if (contains_string(argv[i], "WAR_DIST")){
				size_t j; for(j=0; j<strlen(argv[i]); ++j) if (argv[i][j] == '=') break;
				WAR_DIST = atoi(&argv[i][j+1]);
			}
			else if (contains_string(argv[i], "WAW_DIST")){
				size_t j; for(j=0; j<strlen(argv[i]); ++j) if (argv[i][j] == '=') break;
				WAW_DIST = atoi(&argv[i][j+1]);
			}
			else if (contains_string(argv[i], "BINARY_NAME")){
				int len = strlen(argv[i]);
				int j; for(j=0; j<len; ++j) if (argv[i][j] == '=') break;
				BINARY_NAME = malloc(len-j+1);
				strcpy(BINARY_NAME,&argv[i][j+1]);
			}
			else if (contains_string(argv[i], "PROFILE_WEIGHT")){
				size_t j; for(j=0; j<strlen(argv[i]); ++j) if (argv[i][j] == '=') break;
				if (contains_string(&argv[i][j+1], "ELEM")){
					PROFILE_WEIGHT = w_ELEMS;
				}else if (contains_string(&argv[i][j+1], "INSTR")){
					PROFILE_WEIGHT = w_INSTR;
				}else{
					PROFILE_WEIGHT = w_ELEMS;
					printf("Unknown PROFILE_WEIGHT value (not ELEM or INSTR), defaulting to ELEM\n");
				}
			}
		}
		if (PRINT_REPORT && FD_REPORT==NULL) FD_REPORT = stdout; 
		if (PRINT_PROFILE && FD_PROFILE==NULL) FD_PROFILE = stdout; 
		if (PRINT_CALLTRACE && FD_CALLTRACE==NULL) FD_CALLTRACE = stdout; 


		if (PRINT_PRV){
			write_prv(FD_PRV, 1, &expected_threads, N_PIPELINES);
			trace_row(FD_PRV, 0, 0, SCALAR_ROW, 0);
			trace_event_value(FD_PRV,event_VLEN,RAVE_VLMAX);
			trace_event_value(FD_PRV,event_ELEN,RAVE_ELEN);
		}

		if (INST_TRACE_ENABLED){
			inst_trace_init();
		}

		rave_ini_regions();


		rave_counters global_counters;
		reset_counters(&global_counters);

		//rave_eventandcounters(-1, 1, -1, &global_counters); //Start global event
		internal_begin_region(0, "GLOBAL_REGION", &global_counters, ACCUM_REGIONS);


		/* Register translation block and exit callbacks */
	}
