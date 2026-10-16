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

## 3. Salida en formato JSON

Además del reporte en texto, RAVE puede volcar los contadores en **JSON**. Hay dos formas equivalentes.

Desde el wrapper, con la opción `--json` (se consume antes de lanzar el binario, por lo que **no interfiere con los flags del propio benchmark**):

```bash
rave --json salida.json ./mi_benchmark [args...]
rave --json=salida.json ./mi_benchmark [args...]
rave --rave-json salida.json ./mi_benchmark [args...]
```

O mediante variables de entorno, al estilo de `RAVE_REPORT_NAME`:

```bash
export RAVE_PRINT_JSON=1
export RAVE_JSON_NAME=salida.json   # si se omite, se usa <binario>_rave.json
```

### 3.1 Cuándo se escribe y qué lo afecta

El JSON se escribe **al terminar el programa** (en `rave_exit`), con los contadores de
todas las regiones. Es un único documento JSON, con un objeto por región.

| Variable | Efecto sobre el contenido |
|---|---|
| `RAVE_TRACE_EXTENDED=1` | activa las métricas extendidas (ocupación, LMUL, dependencias, subcategorías). **Sin ella, esos campos salen a 0.** |
| `RAVE_VLEN` | VLEN en bits (por defecto `16384`) → cambia `vlen_bits`, `avg_vlmax_elems`, `avg_occupancy`… |
| `RAVE_ACCUM_REGIONS` | si está activo, cada región lleva la **media** de sus ejecuciones en vez del **delta** de la última |

El **esquema del documento** (campo a campo, con las reglas de parseo y los solapes
entre categorías) está en **`docs/PROFILER_JSON_API.md`**, que es el contrato de
entrada del visor (`../profiler/`, un proyecto aparte que no está en este
repositorio). Hay además un **esquema legible por máquina**
(`schema/profiler_json_schema_v1.json`) y un validador de referencia que comprueba
la forma y las invariantes del contrato (descomposiciones de contadores, orden de
los bloques SEW, campos extendidos a cero sin traza extendida):

```bash
# Forma (esquema) + invariantes
python3 utils/profiler_json_reader.py salida.json --validate

# Resumen por región y SEW
python3 utils/profiler_json_reader.py salida.json --summary

# Pruebas del esquema y del validador
python3 test/profiler_json/test_profiler_json_reader.py
```

El JSON incluye, **por región**: bytes movidos, FLOPs, *global stats* (Average VL, LMUL, ocupación, `tu`/`ta` efectivos, registros vectoriales únicos), dependencias (RAW/WAW/WAR escalares y vectoriales con distancia media) y el mix de instrucciones por SEW (arith FP/INT, narrowing/widening con sus reducciones y fused, permutaciones/moves, máscaras, reducciones, cómputo/fused y el desglose de memoria unit/strided/indexed —con `indexed_ordered`/`indexed_unordered`—/segmentado).

## 4. Instrucciones no compatibles con RAVE/QEMU
`vfwredusum.vs`
`vlseg8e32.v`

## 5. Determinismo y variabilidad del JSON

### 5.1 Entre ejecuciones idénticas (no determinismo)

Medido con 5 ejecuciones de `axpy_vector.exe 512` (267 valores hoja):
**solo 4 campos varían**, y muy poco (~10^2 sobre ~10^7, ≈0,001 %):

| Campo | Comportamiento |
|---|---|
| `counters.instructions.scalar` | varía (±~100) |
| `counters.instructions.total` | varía (±~100); arrastra el anterior |
| `counters.moved_bytes.scalar` | varía (±~100) |
| `counters.moved_bytes.total` | varía (±~100); arrastra el anterior |

Causa: esas magnitudes cuentan el **arranque escalar del programa** (cargador
dinámico y procesado del entorno). Verificado: alargando `PATH` 400 caracteres,
`instructions.scalar` cambia en ~19 y `moved_bytes.scalar` en ~20, mientras que
**todos los contadores vectoriales permanecen idénticos**.

**Todos los campos vectoriales son estables** (`instructions.vector`, `sew[*].*`,
`global_stats.*`, `dependencies.vector.*`, `moved_bytes.vector`, `flops.vector`).
Si necesitas determinismo estricto, usa esos; para los escalares, compara con
tolerancia relativa (~1e-5) o ignóralos.

### 5.2 Según la configuración

| Si cambia… | Cambian estos campos |
|---|---|
| `RAVE_TRACE_EXTENDED` (1→0) | `trace_extended`; pasan a 0: `global_stats.avg_vl_bits`, `avg_lmul`, `avg_occupancy`, `avg_vlmax_elems`, **todo** `dependencies.*`, `arith.computation.*`, `arith.scalar_deps.*` |
| `RAVE_VLEN` | `vlen_bits` (raíz y `global_stats`), `avg_vlmax_elems`, `avg_occupancy`, `avg_vl_elems`, `avg_vl_bits` y `elems`/`avg_vl` por SEW |
| `RAVE_ACCUM_REGIONS` | `accumulate_regions` y **el valor** de todos los contadores de región: media por ejecución en vez de delta |
| `--json` vs `RAVE_JSON_NAME` | solo la ruta del fichero |

### 5.3 Según el binario y sus argumentos

* `binary`: es la ruta **tal como se pasó** en la línea de órdenes (relativa o
  absoluta) → puede diferir entre invocaciones sin que cambie nada más.
* `region_count` y el tamaño de `regions[]`: dependen del programa
  (0 sin regiones… N; RiVEC siempre 1 con `GLOBAL_REGION`).
* `id`, `nesting`, `rank`, `thread`, `executions`, `parent`: dependen del programa.
* Todos los contadores dependen obviamente del binario, tamaño de problema y
  argumentos (p. ej. `axpy` con `256` da `vec_instr=4096` y con `512` da `8192`).

## 6. Avisos al interpretar los contadores

* **`ta`/`ma` están forzados.** El wrapper arranca QEMU con
  `rvv_ta_all_1s=true,rvv_ma_all_1s=true`, así que la semántica de cola/máscara es
  *agnostic*: espera `tu_effective = 0` y `ta_effective` ≈ instrucciones vectoriales.
  Tenlo en cuenta al interpretar `ta_effective`/`tu_effective`.
* **`tu`/`ta` solo cuentan instrucciones infrautilizadas**: se incrementan únicamente
  si `VL efectiva < VLEN·LMUL` (línea 567 de `src/rvv/callbacks.c`).
* **`other.instr` puede ser negativo** y hay **valores duplicados** (`mask` y
  `reduction` se cuentan dentro de `arith` y vuelven a aparecer al mismo nivel). No
  agregues el eje "función" junto con el eje "forma". Detalle en
  `docs/PROFILER_JSON_API.md` §1.5.
* `avg_vl_elems` es la media de VL en **elementos** sobre todas las instrucciones
  vectoriales (mezcla SEW y LMUL); `avg_vl_bits` es la media en **bits**, así que
  pondera distinto cuando hay varios SEW. Para "cuánto se llena el datapath" usa
  `avg_occupancy`.
* **El CSV NO es equivalente.** El CSV tiene 98 columnas planas (una fila por región)
  y es un **subconjunto**: no incluye ocupación, LMUL, dependencias, ni el desglose
  extendido (narrowing/widening/permutations/fused/scalar_deps), y agrupa esas
  categorías dentro de `other`. Para las métricas de C-T2 usa el JSON.
* Como referencia, un JSON de un benchmark pequeño ocupa ~10 KB (pretty-printed,
  ~400 líneas).

## 7. Recetas: resumir muchos JSON en una tabla

### 7.1 Sin dependencias

```python
import json, pathlib

print("\t".join(["fichero","region","moved_bytes","vec_instr","avg_vl","ocupacion","vRAW"]))
for f in sorted(pathlib.Path("resultados").rglob("*.json")):
    d = json.load(open(f))
    for r in d["regions"]:
        c, g = r["counters"], r["counters"]["global_stats"]
        print("\t".join(map(str, [
            f.name, r["name"], c["moved_bytes"]["total"],
            c["instructions"]["vector"], round(g["avg_vl_elems"], 2),
            round(g["avg_occupancy"], 4), c["dependencies"]["vector"]["raw"]["count"]])))
```

### 7.2 Con pandas

Requiere `pandas` (no viene instalado por defecto: `pip install pandas`).

```python
import json, pathlib, pandas as pd

rows = []
for f in sorted(pathlib.Path("resultados").rglob("*.json")):
    d = json.load(open(f))
    for r in d["regions"]:
        c, g = r["counters"], r["counters"]["global_stats"]
        rows.append({
            "fichero": f.name, "region": r["name"],
            "moved_bytes": c["moved_bytes"]["total"],
            "vec_instr": c["instructions"]["vector"],
            "avg_vl": g["avg_vl_elems"], "ocupacion": g["avg_occupancy"],
            "vRAW": c["dependencies"]["vector"]["raw"]["count"],
        })
df = pd.DataFrame(rows)
print(df.to_string(index=False))
```

### 7.3 Visor gráfico

El visor `../profiler/` (proyecto aparte, fuera de este repositorio) abre estos JSON
desde dentro del programa (pestañas por región, leyenda en texto y diagramas de
tarta). Los ejemplos se ejecutan desde el directorio que contiene a `rave-ext/`:

```bash
cd profiler && ./build/profiler            # abre a pantalla completa y eliges el .json con O
./build/profiler --open ../resultados/small/somier.json
./build/profiler --open ../resultados/large/lavaMD.json --region 5   # entra en el bucle vectorial
```

La opción `--region N` abre directamente la región N (1..N), útil cuando el fichero
tiene varias. Dentro del programa se cambia con `Tab`, `N`/`P` o pulsando la pestaña.

## 8. Instrumentar regiones: ejemplo de `lavaMD`

`RiVEC/_lavaMD/kernel/kernel_vector.c` está instrumentado con la API de regiones
(§«Instrumentación» del `README.md`): `rave_begin_region("nombre")` /
`rave_end_region("nombre")` alrededor de los bucles del kernel, formando un árbol
de cuatro niveles dentro de `GLOBAL_REGION`:

| Nivel | Región | Qué abarca | Ejecuciones en `small` |
|---|---|---|---|
| 1 | `lavaMD_interactions` | toda la fase de interacciones | 64 |
| 2 | `lavaMD_box_pair` | caja actual + una vecina | 1.000 |
| 3 | `lavaMD_home_particle` | una partícula de la caja actual | 96.000 |
| 4 | `lavaMD_vector_loop` | bucle vectorial RVV (donde están las RVV) | 96.000 |

Detalles de la implementación:

* Las llamadas van protegidas por `#ifdef RAVE_REGIONS` y el `makefile` de lavaMD
  aporta `-DRAVE_REGIONS -I$(RAVE_INCLUDE)` (con
  `RAVE_INCLUDE ?= ../../rave-ext/build/interfaces`). Sin ese define el fichero
  compila igual en cualquier toolchain y el binario no cambia.
* Estas regiones se ejecutan **cientos de miles de veces**. Hay que ejecutar con
  **`RAVE_ACCUM_REGIONS=1`**, que agrega las repeticiones en un nodo por nombre
  (los contadores pasan a ser la media por ejecución). `run_rivec.sh` ya lo hace
  así solo para `lavaMD`. Medido en `-boxes1d 4`: sin acumular el JSON tendría
  193.065 entradas (~2 GB) y con acumulación tiene 5 entradas y 53 KB.
* `profiler` muestra cada región como una **pestaña**, así que se puede comparar
  el mix de instrucciones de la fase completa (nivel 1) con el del bucle vectorial
  (nivel 4): `lavaMD_box_pair` = 10.678 instrucciones (7.200 vectoriales) y
  `lavaMD_vector_loop` = 69 instrucciones (59 vectoriales) por iteración.
  Estos valores **por ejecución no cambian con el tamaño** del problema: lo que
  cambia es el número de ejecuciones (`executions`), que es lo que escala.

### 8.1 Coste de la instrumentación (medido)

Misma entrada (`lavaMD -boxes1d 4`), comparando el binario **sin** regiones con el
instrumentado:

| Contador | Sin regiones | Con regiones | Diferencia |
|---|---|---|---|
| `instructions.total` | 87.608.090 | 88.150.597 | +542.507 (**+0,62 %**) |
| `instructions.scalar` | 80.024.088 | 80.470.595 | +446.507 (+0,56 %) |
| `instructions.vector` | 7.200.001 | 7.200.001 | **0 (idéntico)** |

Los marcadores son instrucciones escalares (`add x0`/`sub x0`), así que **el mix
vectorial no se altera**: son ~1,4 instrucciones por marcador y el coste es
despreciable frente al total. Aun así, conviene tenerlo presente al comparar los
totales de `lavaMD` con los de los otros benchmarks (que no están instrumentados).

### 8.2 Ejecuciones por tamaño

| Tamaño | `boxes1d` | interacciones | caja+vecina | partícula | bucle vectorial | `region_count` |
|---|---|---|---|---|---|---|
| tiny | 1 | 1 | 1 | 96 | 96 | 195 |
| small | 4 | 64 | 1.000 | 96.000 | 96.000 | 193.065 |
| large | 10 | 1.000 | 21.952 | 2.107.392 | 2.107.392 | 4.237.737 |

Ninguno de esos casos se puede ejecutar sin `RAVE_ACCUM_REGIONS=1`: el de `large`
tendría 4,2 millones de entradas en `regions[]` (del orden de decenas de GB).

Efecto lateral a tener en cuenta: al reejecutar `run_rivec.sh`, el
`lavaMD.json` pasa a tener 5 regiones en vez de 1, y `accumulate_regions` pasa a
`true` (sus contadores son medias por ejecución, no deltas de la última).
