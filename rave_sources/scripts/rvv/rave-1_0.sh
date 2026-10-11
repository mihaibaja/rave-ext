#!/bin/bash

get_bin_name(){
	i=1
	while [[ ! -f "${!i}" || ! -x "${!i}" ]] || ! file -b --mime-type "${!i}" 2>/dev/null | grep -q -v '^text/'; do
		if [ $i -gt $# ]; then break; fi
		((i++))
	done
	echo ${!i}
}

if [ $# -lt 1 ] || [[ "$1" == "--help" ]]; then
	echo "Usage: $0 path/to/your/binary [arguments]"
	echo "Additionally, these environment variables control the tracing plugin:"
echo "
 - **RAVE_TRACE_SCALAR**: If set to \"1\", adds tracing information for each scalar instruction (trace gets a lot bigger). Otherwise, scalar instructions are treated as bursts. (default: 0)
 - **RAVE_TRACE_ADDR**: If set to \"1\", adds tracing information of the base address of vector loads/stores. (default: 0)
 - **RAVE_TRACE_INDEXES**: If set to \"1\", adds tracing information of the offsets in vector indexed loads/stores. (default: 0)
 - **RAVE_TRACE_EXTENDED**: If set to \"1\", enables extended tracing information. (default: 0)
 - **RAVE_INST_TRACE_ENABLED**: If set to \"1\", enables dynamic instruction-level tracing. (default: 0)
 - **RAVE_INST_TRACE_NAME**: Sets the filename for the instruction trace (default: rave_inst_trace.jsonl).
 - **RAVE_DEBUG_INFO**: If set to \"1\", enables debug information. (default: 0)
 - **RAVE_RAW_DIST**: Sets the RAW dependency distance threshold.
 - **RAVE_WAR_DIST**: Sets the WAR dependency distance threshold.
 - **RAVE_WAW_DIST**: Sets the WAW dependency distance threshold.

Control RAVE internals:
 - RAVE_VLEN: Sets the maximum available vector-length in bits (default: 16384).
 - RAVE_SYSROOT: Sets the path to a user-specified RISC-V sysroot.
 - RAVE_CUSTOM_EXTENSIONS: Appends RISC-V extensions to the QEMU cpu (e.g. \"zicbom=true,zicboz=true,zicbop=true,zicond=true\" to emulate the bananapif3 boards). 

Control logfile generation:
 - RAVE_PRINT_LOGFILE: If set to \"1\", a logfile is generated with all the executed instructions. (default: 0).
 - RAVE_LOGFILE_NAME: Sets the name of the generated logfile (default: qemulog.log). Additionally, automatically sets RAVE_LOGFILE to 1.

Control PRV generation:
 - RAVE_PRINT_PRV: If set to \"1\", a paraver trace is generated with all the executed instructions. (default: 0).
 - RAVE_PRV_NAME: Sets the name of the generated paraver trace (default: qemutrace). Additionally, automatically sets RAVE_PRV to 1.
 - RAVE_REGION_EVENT: Set paraver event where the first nesting level of regions will be mapped. (default: 1000) 

Control report / profile / csv generation:
 - RAVE_PLAIN_TEXT: Print the report/profile without colours or highlighted text. (default: 0).
 - RAVE_ACCUM_REGIONS: If set to "1", a region that appears twice will be aggregated/accumulated. (default: 0).

 - RAVE_PRINT_REPORT: If set to "1", the tracer will print to stdout a vector counter summary for each executed code region. (default: 0).
 - RAVE_REPORT_NAME: Redirects the report to the provided file name. Additionally, automatically sets RAVE_REPORT to 1. 
 - RAVE_STREAM_REPORT: If set to "1", the report of each region is printed immediately when that region is closed (default: 0). automatically sets RAVE_PRINT_REPORT to 1.


 - RAVE_PRINT_PROFILE: If set to "1", the tracer will print to stdout a profiling of the executed loops. (default: 0).
 - RAVE_PROFILE_NAME: Redirects the profile to the provided file name. Additionally, automatically sets RAVE_PROFILE to 1. 

 - RAVE_PRINT_CSV: If set to "1", the tracer will print a CSV with the hardware counter summary for each executed code region. (default: 0).
 - RAVE_CSV_NAME: Sets the name of the generated csv trace (default: rave_summary.csv). Additionally, automatically sets RAVE_CSV to 1.
"
	exit -1
fi

#Legacy support... To be drop at some point.
if [ "$RAVE_PRINT_SCALAR" == "1" ]; then
	export RAVE_TRACE_SCALAR=1
	echo RAVE WARNING! RAVE_PRINT_SCALAR is deprecated, changed it to RAVE_TRACE_SCALAR
fi
if [ "$RAVE_PRINT_ADDR" == "1" ]; then
	export RAVE_TRACE_ADDR=1
	echo RAVE WARNING! RAVE_PRINT_ADDR is deprecated, changed it to RAVE_TRACE_ADDR
fi

args=;
if [ "$RAVE_TRACE_SCALAR" == "1" ]; then #OPT-in
	args=$args",TRACE_SCALAR=on"
fi
if [ "$RAVE_TRACE_ADDR" == "1" ]; then
	args=$args",TRACE_ADDR=on"
fi
if [ "$RAVE_TRACE_INDEXES" == "1" ]; then
	args=$args",TRACE_INDEXES=on"
fi
if [ "$RAVE_TRACE_EXTENDED" == "1" ]; then
	args=$args",TRACE_EXTENDED=on"
fi
if [ "$RAVE_INST_TRACE_ENABLED" == "1" ]; then
	args=$args",INST_TRACE_ENABLED=on"
fi
if [ "$RAVE_INST_TRACE_NAME" != "" ]; then
	args=$args",INST_TRACE_NAME=$RAVE_INST_TRACE_NAME"
fi
if [ "$RAVE_DEBUG_INFO" == "1" ]; then
	args=$args",DEBUG_INFO=on"
fi
if [ "$RAVE_RAW_DIST" != "" ]; then
	args=$args",RAW_DIST=$RAVE_RAW_DIST"
fi
if [ "$RAVE_WAR_DIST" != "" ]; then
	args=$args",WAR_DIST=$RAVE_WAR_DIST"
fi
if [ "$RAVE_WAW_DIST" != "" ]; then
	args=$args",WAW_DIST=$RAVE_WAW_DIST"
fi
if [ "$RAVE_PRINT_LOGFILE" == "1" ] || [ "$RAVE_LOGFILE_NAME" != "" ]; then
	args=$args",PRINT_LOGFILE=on"
	if [ "$RAVE_LOGFILE_NAME" == "" ];then
		RAVE_LOGFILE_NAME=qemulog.log
	fi
	QEMU_OPTION=-D 
fi

if [ "$RAVE_VLEN" == "" ];then
	RAVE_VLEN=16384
fi

if [ "$RAVE_PRINT_PRV" == "1" ] || [ "$RAVE_PRV_NAME" != "" ]; then #OPT-in
	if [ "$RAVE_PRV_NAME" == "" ]; then
		RAVE_PRV_NAME=qemutrace
	fi
	args=$args",PRINT_PRV=on,PRV_NAME=$RAVE_PRV_NAME"
fi

if [ "$RAVE_PRINT_REPORT" == "1" ]; then #OPT-in
	args=$args",PRINT_REPORT=on"
fi
if [ "$RAVE_STREAM_REPORT" == "1" ]; then #OPT-in
	args=$args",STREAM_REPORT=on"
fi

if [ "$RAVE_ACCUM_REGIONS" == "1" ]; then #OPT-in
	args=$args",ACCUM_REGIONS=on"
fi
if [ "$RAVE_REGION_EVENT" != "" ]; then #OPT-in
	args=$args",REGION_EVENT=$RAVE_REGION_EVENT"
fi
if [ "$RAVE_REPORT_NAME" != "" ]; then #OPT-in
	args=$args",PRINT_REPORT=on,REPORT_NAME=$RAVE_REPORT_NAME"
fi
BIN_NAME=$(get_bin_name $@)
args=$args",BINARY_NAME=$BIN_NAME"
if [ "$RAVE_PRINT_PROFILE" == "1" ]; then #OPT-in
	args=$args",PRINT_PROFILE=on"
fi
if [ "$RAVE_PROFILE_NAME" != "" ]; then #OPT-in
	args=$args",PRINT_PROFILE=on,PROFILE_NAME=$RAVE_PROFILE_NAME"
fi
if [ "$RAVE_PRINT_CALLTRACE" == "1" ]; then #OPT-in
	args=$args",PRINT_CALLTRACE=on"
fi
if [ "$RAVE_CALLTRACE_NAME" != "" ]; then #OPT-in
	args=$args",PRINT_CALLTRACE=on,CALLTRACE_NAME=$RAVE_CALLTRACE_NAME"
fi
if [ "$RAVE_PRINT_CSV" == "1" ] || [ "$RAVE_CSV_NAME" != "" ]; then #OPT-in
	if [ "$RAVE_CSV_NAME" == "" ]; then
		if [ "$BIN_NAME" != "" ]; then
			RAVE_CSV_NAME=`basename ${BIN_NAME}`_rave.csv
		else
			RAVE_CSV_NAME=rave_summary.csv
		fi
	fi
	args=$args",PRINT_CSV=on,CSV_NAME=$RAVE_CSV_NAME"
fi
if [ "$RAVE_COMPRESS_REPORT" == "1" ]; then
	args=$args",COMPRESS_REPORT=on"
fi
if [ "$RAVE_OTHER_CHILDS" == "1" ]; then
	args=$args",OTHER_CHILDS=on"
fi
if [ "$RAVE_PROFILE_WEIGHT" != "" ]; then
	args=$args",PROFILE_WEIGHT=$RAVE_PROFILE_WEIGHT"
fi
if [ "$RAVE_PLAIN_TEXT" == "1" ]; then
	args=$args",PLAIN_TEXT=on"
fi
if [ "$RAVE_MUSA" == "1" ]; then
	args=$args",MUSA=on"
fi

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
#SYSROOT:
if [ "$RAVE_SYSROOT" == "" ]; then
	RAVE_SYSROOT=${SCRIPT_DIR}/../../../sysroot 
fi

RAVE_PLUGIN=${SCRIPT_DIR}/../lib 
QEMU_PATH=${SCRIPT_DIR}/../qemu/bin
QEMU_CPU=rv64,v=true,vext_spec=v1.0,vlen=$RAVE_VLEN,rvv_ta_all_1s=true,rvv_ma_all_1s=true
if [ "$RAVE_CUSTOM_EXTENSIONS" != "" ]; then 
	QEMU_CPU=${QEMU_CPU},${RAVE_CUSTOM_EXTENSIONS}
fi

ENV_VARS="-E LD_LIBRARY_PATH=${QEMU_SYSROOT}/lib:${QEMU_SYSROOT}/lib/riscv64-linux-gnu:${SCRIPT_DIR}/../../../parallel:$LD_LIBRARY_PATH -E PATH=$PATH -E LD_RUN_PATH=$LD_RUN_PATH"
OMPT_LIB=${SCRIPT_DIR}/../../../parallel/ompt.so
if [ -f $OMPT_LIB ]; then
	ENV_VARS="${ENV_VARS} -E LD_PRELOAD=${OMPT_LIB}:$LD_PRELOAD"
fi

export LD_LIBRARY_PATH=${SCRIPT_DIR}/../../../elfutils/lib:$LD_LIBRARY_PATH
${QEMU_PATH}/qemu-riscv64 $QEMU_OPTION $RAVE_LOGFILE_NAME -d plugin -plugin ${RAVE_PLUGIN}/librave.so$args -L ${RAVE_SYSROOT} ${ENV_VARS} -cpu $QEMU_CPU $@
