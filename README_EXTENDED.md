# RAVE Profiler - Extended version
## 1. Configuración del Entorno

Todas las variables de entorno(configuración de RAVE) están definidas en el archivo `entorno.sh`. Puedes abrir y editar este archivo libremente para modificar parámetros de la simulación. 

Para aplicar estas variables sobre tu sesión de terminal actual, ejecuta siempre este comando antes de empezar a trabajar:

```bash
source entorno.sh

```

Dentro del script, puedes modificar manualmente la ventana de distancia (en número de instrucciones) que el *profiler* utiliza para detectar y contabilizar los riesgos estructurales y de datos. Por ejemplo:

```bash
export RAVE_RAW_DIST=8
export RAVE_WAR_DIST=20
export RAVE_WAW_DIST=8

```

## 2. Modos de Ejecución y Traza

El nivel de profundidad del análisis del *profiler* se controla mediante la variable de entorno `TRACE_EXTENDED`. Debes configurar esta variable antes de compilar/ejecutar:

### 2.1 Modo Estándar

Realiza un perfilado básico de instrucciones, ancho de banda y métricas generales. Es el por defecto de RAVE.

```bash
export TRACE_EXTENDED=0

```

### 2.2 Modo de Traza Extendida

Activa la categorización avanzada de instrucciones de 16 bits (FMA/Fused, Widening, Narrowing, Moves) y el motor de rastreo de riesgos de datos (RAW, WAW, WAR).

```bash
export TRACE_EXTENDED=1

```
- **¿Qué incluye la traza extendida (por el momento)?**
	- Avg VL, LMUL y ocupación de registros (En % de bits respecto a VLMAX).
	- Número de registros accedidos.
	- Cuantas veces tu/ta afecta (**TamVEctorReal < VLEN**)
	- Cantidad de dependencias vectoriales con distancia máxima configurable para cada tipo de dependencia.
	- Mix de instrucciones redistribuido y ampliado de la siguiente manera:
		- **Arith:** Separado a su vez en FP/INT y con una métrica de cuantas instrucciones escriben en registros escalares y leen de registros escalares. 
			- **Widening:** Reduction, Fused.
			- **Narrowing**
			- **Others:** Computation, reduction, mask (instrucciones que modifican los vectores de mascaras), permutations y moves (separados en si leen de registros escalares, si escriben en registros escalares o si mueve datos unicamente entre registros vectoriales)
		- **Memory:** Se mantiene prácticamente igual pero se añade si son ordered/unordered, para L/S indexed; y si son L/S segmentados para unit, strided e indexed. También se añade la desviación típica del stride para las instrucciones strided.

### 2.3 Modo de Traza Dinámica de Instrucciones (`INST_TRACE_ENABLED`)

Genera una traza dinámica en formato JSON Lines (`.jsonl`) con información a nivel de instrucción individual y registros auxiliares por elemento.

```bash
export RAVE_INST_TRACE_ENABLED=1
export RAVE_INST_TRACE_NAME=rave_inst_trace.jsonl  # opcional, por defecto rave_inst_trace.jsonl
```

#### Independencia de la Arquitectura Física (Lanes y Bancos)
La traza es **completamente independiente** de una configuración concreta de lanes y bancos. Registra el estado arquitectural y espacial (secuencia dinámica, máscaras activas, deltas de direcciones efectivas, índices de permutación/gather, desplazamientos de slide/compress) sin fijar un número de lanes ni partición de memoria. El movimiento físico y los conflictos de bancos se modelan en **postprocesado** para diferentes mappings hardware.

#### Esquema de la Traza

- **Cabecera (`record_type: "header"`):**
  - Versión del esquema (`schema_version`);
  - Versión de RAVE y revisión ISA (`rave_version`, `isa_revision`);
  - VLEN y ELEN (`vlen`, `elen`);
  - Identificación del workload y configuración (`workload`, `config`);
  - Número de harts/threads (`num_harts`);
  - Convención usada para normalizar direcciones (`addr_normalization`).

- **Registro principal por instrucción dinámica (`record_type: "inst"`):**
  - `seqno`, `hart` y `pc`;
  - Instrucción binaria original (`binary`), mnemónico (`op`), cadena ensambladora (`asm`) y operación decodificada (`decoded_op`);
  - Registros o grupos fuente (`srcs`) y destino (`dst`), teniendo en cuenta LMUL y operaciones widening/narrowing;
  - `vl`, `sew`, `lmul`, `vta`, `vma` y `vstart`;
  - Presencia de máscara (`masked`) y número de elementos activos (`active_elements`);
  - Dependencias por operando (`deps`), identificando tipo (`RAW`, `WAR`, `WAW`), número de secuencia del productor (`producer_seqno`) y distancia (`distance`).

- **Registros auxiliares opcionales enlazados mediante `seqno` (`record_type: "aux"`):**
  - `mask_bitmap`: bitmap comprimido en hexadecimal de los bits de máscara y recuento activo;
  - `mem_addrs`: direcciones efectivas relativas (deltas respecto a `base_addr`) de elementos activos (cargas y almacenamientos unit, strided e indexed);
  - `gather_indices`: índices de gather vectoriales (`vrgather`) o resumen estadístico (mínimo, máximo, conteo único);
  - `slide_compress`: desplazamientos de slide (`vslideup`/`vslidedown`), posiciones origen-destino y datos de compactación (`vcompress`);
  - `element_data`: otros datos por elemento demasiado voluminosos para el registro principal.

#### Lector de Referencia y Pruebas
Se proporciona el lector y validador de referencia en `utils/trace_reader.py`:

```bash
# Validar formato y cumplimiento del esquema
python3 utils/trace_reader.py rave_inst_trace.jsonl --validate

# Volcar instrucciones parseadas
python3 utils/trace_reader.py rave_inst_trace.jsonl --dump

# Mostrar resumen
python3 utils/trace_reader.py rave_inst_trace.jsonl --summary
```

Para el explorador interactivo visual:
(Por el momento test nada más)
```bash
python3 utils/trace_viewer.py rave_inst_trace.jsonl
```

Para ejecutar las pruebas de compatibilidad del formato:
```bash
python3 test/trace/test_trace_reader.py
```

## 3. Instrucciones no compatibles con RAVE/QEMU
`vfwredusum.vs`
`vlseg8e32.v`

