export RAVE_TRACE_SCALAR=1
export RAVE_PRINT_REPORT=1
export RAVE_VLEN=16384

export RAVE_TRACE_EXTENDED=1
export RAVE_RAW_DIST=10
export RAVE_WAR_DIST=10
export RAVE_WAW_DIST=10
export RAVE_DEBUG_INFO=0

# Trace de instrucciones (opcional).
export RAVE_INST_TRACE_ENABLED=0
export RAVE_INST_TRACE_NAME=rave_inst_trace.jsonl
export 

# Trace de bloques básicos (opcional).
export RAVE_TRACE_BB=0
export RAVE_BB_JSON_NAME=rave_bb_analysis.json

# Salida JSON del reporte (opcional).
#   RAVE_PRINT_JSON=1  activa el volcado JSON.
#   RAVE_JSON_NAME     fichero de salida (si esta vacio, se usa <binario>_rave.json).
export RAVE_PRINT_JSON=1
export RAVE_JSON_NAME=rave_json.json