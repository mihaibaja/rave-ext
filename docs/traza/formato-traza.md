# Formato de Trazas de Ejecución (RAVE / RISC-V)

Este documento describe la estructura y los campos del archivo de trazas generado durante el *profiling* de aplicaciones RISC-V con la extensión vectorial (RVV) utilizando RAVE. 

El archivo está estructurado en formato **JSONL** (JSON Lines), lo que significa que cada línea de texto es un objeto JSON independiente.

El archivo contiene tres tipos principales de registros, definidos por el campo `"record_type"`: `header`, `inst` y `aux`.

---

## 1. Registro de Cabecera (`"record_type": "header"`)
Suele aparecer únicamente en la primera línea del archivo. Define la configuración global de la simulación y las características de la arquitectura de hardware simulada.

* **`schema_version` / `rave_version`**: Versiones del esquema de datos y de la herramienta de *profiling*.
* **`isa_revision`**: Versión de la arquitectura RISC-V simulada (ej. `RVV-1.0`).
* **`vlen`**: *Vector Length* máximo. Longitud máxima del registro vectorial en bits (ej. 16384). *Parámetro físico clave del hardware.*
* **`elen`**: *Element Length* máximo. Tamaño máximo de un elemento en bits (ej. 64).
* **`workload`**: Ruta o nombre del ejecutable/carga de trabajo que se ha simulado.
* **`config`**: Parámetros de la simulación de *profiling* (como las distancias para el análisis de dependencias de datos).
* **`num_harts`**: Número de *Hardware Threads* (núcleos lógicos) simulados.

---

## 2. Registro de Instrucción (`"record_type": "inst"`)
Representa el flujo principal de ejecución. Cada línea describe una instrucción ejecutada y el contexto exacto del *datapath* en ese momento. Es la base para cualquier script de análisis (como `opcode_classifier.py`).

### Campos Base
* **`seqno`**: Número de secuencia único de la instrucción en la traza cronológica.
* **`hart`**: ID del *hardware thread* que ejecuta la instrucción.
* **`pc`**: *Program Counter*. La dirección de memoria de la instrucción en hexadecimal.
* **`binary`**: Código máquina de la instrucción (los 32 bits codificados en hexadecimal).
* **`op`**: Nemónico principal o código de operación base (ej. `vle32.v`, `addi`, `vsetvl`).
* **`asm`**: Ensamblador completo decodificado, incluyendo registros y operandos (ej. `vle32.v v1,(a1)`).
* **`decoded_op`**: Clasificación arquitectónica de la operación (ej. `SCALAR`, `VECTOR_LOAD_UNIT`, `VECTOR_ARITH_REDUCTION`).
* **`dst` / `srcs`**: Listas con los registros destino y fuente implicados. Pueden ser escalares (`x0` a `x31`), flotantes (`f0` a `f31`) o vectoriales (`v0` a `v31`).

### Contexto Vectorial (Estado del VTYPE)
* **`vl`**: *Vector Length* activo. El número real de elementos que procesa la instrucción en ese ciclo.
* **`sew`**: *Standard Element Width*. Ancho del elemento actual en bits (ej. 8, 16, 32).
* **`lmul`**: *Length Multiplier*. Indica la agrupación de registros vectoriales (ej. `m1`, `m2`, `m4`).
* **`vta` / `vma`**: Políticas para los elementos restantes y enmascarados: *Tail Agnostic* y *Mask Agnostic* (1 = *agnostic*, 0 = *undisturbed*).
* **`masked`**: Booleano (`true`/`false`) que indica si la instrucción utiliza un registro máscara.

### Dependencias y Hazards (`deps`)
Lista de objetos que detallan los riesgos de datos (*data hazards*) detectados en tiempo de simulación:
* `operand`: El registro específico que causa la dependencia.
* `dep_type`: El tipo de riesgo estructural (`RAW` - Read After Write, `WAW`, `WAR`).
* `producer_seqno`: El número de secuencia (`seqno`) de la instrucción previa que generó el dato que estamos esperando.
* `distance`: Distancia temporal (en número de instrucciones) hasta el productor. *Dato vital para justificar la necesidad de forwardings u optimizar el scheduler.*

---

## 3. Registro Auxiliar (`"record_type": "aux"`)
Proporciona información extra para operaciones complejas. Se vincula a la instrucción que lo generó usando el mismo `"seqno"`. Suelen acompañar a las instrucciones de carga/almacenamiento en memoria (Load/Store Unit).

* **`aux_type`**: Define el tipo de dato auxiliar. Generalmente `"mem_addrs"` (direcciones de memoria).
* **`mem_type`**: Patrón de acceso a memoria detectado:
  * `unit`: Elementos contiguos en memoria (Unit-Stride).
  * `strided`: Elementos separados por una distancia constante.
  * `indexed`: Accesos Scatter/Gather donde las posiciones son aleatorias/basadas en un vector de índices.
* **`base_addr`**: Dirección base desde la que empieza el acceso a memoria.
* **`stride_bytes`**: La distancia en bytes entre elementos (para accesos *strided*).
* **`deltas`**: Un array con los desplazamientos (*offsets*) exactos de cada elemento respecto a la dirección base. *Crucial para medir la congestión del bus de memoria y el rendimiento de la caché L1.*

# Aclaraciones importantes:
* **Hay instrucciones como vlseg o vsseg que no se genera correctamente su asm, por mucho que si se identifiquen luego bien (nf>0)**
* **Si TRACE_SCALAR==1 la traza contendrá también instrucciones escalares.**