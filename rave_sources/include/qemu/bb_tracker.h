#ifndef RAVE_QEMU_BB_TRACKER_H
#define RAVE_QEMU_BB_TRACKER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <qemu-plugin.h>

typedef enum {
    BB_TERM_CONTINUE = 0, /* Corte artificial de TCG (vsetvli, store, cruce de página)... */
    BB_END
} bb_terminator_t;

typedef struct  {
    uint64_t pc;
    uint64_t end_pc;
    uint32_t opcode;
    uint32_t vec_inst;
    uint32_t n;
} tb_data_t;    

typedef struct {                                                                                                                                                                        
    uint32_t id;              /* Identificador único del nodo: 0, 1, 2... */
    uint64_t start_pc;        /* Dirección de la primera instrucción */
    uint64_t end_pc;          /* Dirección de la instrucción terminadora */
    uint64_t num_insns;       /* Total de instrucciones del bloque */
    uint64_t num_vec_insns;   /* Total de instrucciones vectoriales */
    uint64_t exec_count;      /* Número de veces que se ha ejecutado */
} bb_node_t;

/* Inicialización del tracker y reserva de estructuras */
void bb_tracker_init(size_t max_vcpus);

/* Clasificador estático de fin de bloque (RVC 16-bit y RISC-V 32-bit) */
bb_terminator_t bb_tracker_classify_terminator(uint32_t op);

/* Callback de ejecución registrado por cada TB en QEMU */
void bb_tracker_on_tb_exec(unsigned int vcpu_index, tb_data_t *userdata);

/* Limpieza de memoria al salir del plugin */
void bb_tracker_cleanup(void);

/* Volcado de los datos del tracker en formato JSON */
void bb_tracker_dump_json(const char *filepath);

#endif