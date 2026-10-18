# profiler — formato de entrada JSON

Este documento describe el JSON que **consume el visor** (`../profiler/`, que no
está en este repositorio): un único documento con un objeto por **región** y, dentro
de cada región, sus contadores. **Lo genera RAVE**, así que este contrato vive en el
repositorio junto al generador y hay que mantenerlo sincronizado con él (ver el
historial, §5); no describe cómo se genera el fichero (eso está en
`README_EXTENDED.md`, §3, y en `schema/profiler_json_schema_v1.json`).

Un JSON de un benchmark pequeño ocupa ~10 KB (pretty-printed, ~400 líneas).

---

## 1. Estructura

```
{
  "generator": "RAVE",          string  quién generó el fichero
  "binary": "bin/axpy_vector.exe" | null,
  "vlen_bits": 16384,           int     VLEN (bits) con el que se midió
  "trace_extended": true,       bool    si se tomaron las métricas extendidas
  "accumulate_regions": false,  bool    contadores acumulados en vez de delta
  "region_count": 1,            int     nº de elementos de regions[]
  "regions": [ <region>, ... ]  array
}
```

Cada `<region>`:

```
{
  "id": 0,                 int    índice de región (orden de creación)
  "name": "GLOBAL_REGION" | null,
  "nesting": 0,            int    profundidad (0 = raíz)
  "rank": 0,               int    rank MPI
  "thread": 0,             int    hilo que abrió la región
  "executions": 1,         int    veces que se ejecutó
  "parent": null | "nombre_region_padre",
  "counters": { <counters> },
  "instructions": { <categoría>: { <categoría> }, ... }   ver 1.4.1
}
```

`counters` son las **estadísticas** (y dentro, `instructions_previous_version`,
que es el bloque de recuentos de siempre); `instructions` son los **contadores
por instrucción**, con los mismos niveles que `docs/rvv instr.md`.

Los binarios que no abren regiones explícitas producen **una sola región**:
`name = "GLOBAL_REGION"`, `nesting = 0`, `parent = null`.

### 1.0 Regiones instrumentadas, `region_count` y `executions`

Si el programa está instrumentado (llama a `rave_begin_region`/`rave_end_region`),
`regions[]` es un **árbol**: `nesting` es la profundidad y `parent` el nombre de la
región que la contiene. Los nombres se repiten entre ejecuciones, así que hay que
distinguir dos números:

* **`region_count`** = número de regiones **ejecutadas** (cada entrada a una región
  cuenta una vez). Con la instrumentación de `lavaMD` son ~193.000 en `small`.
* **`regions[]`** = número de **entradas distintas**, que depende de
  `accumulate_regions`.

| `accumulate_regions` | `regions[]` | contadores de cada entrada | `executions` |
|---|---|---|---|
| `false` (por defecto) | un nodo **por cada ejecución** | delta de esa ejecución | `1` |
| `true` (`RAVE_ACCUM_REGIONS=1`) | un nodo **por nombre** | **media por ejecución** | nº de entradas |

**Consecuencia práctica:** sin acumulación, instrumentar un bucle caliente hace
que el JSON crezca sin límite (medido: `lavaMD -boxes1d 1`, 195 ejecuciones →
195 entradas y 2 MB; `-boxes1d 4`, 193.065 ejecuciones → ~2 GB estimados). Con
`RAVE_ACCUM_REGIONS=1` el mismo caso ocupa 53 KB y tiene 5 entradas.

Cuando `accumulate_regions` es `true`, `region_count` **no** coincide con
`len(regions)`: no es un error del fichero, es la diferencia entre ejecuciones y
nombres. (Un fichero sí incoherente sería aquel con menos `region_count` que
entradas en `regions[]`.)

### 1.1 `<counters>`

```
counters
├── moved_bytes      { total, scalar, vector }              bytes
├── flops            { total, scalar, vector }              FLOPs
├── global_stats     (ver 1.2)
├── dependencies     (ver 1.3)
├── masked           (ver 1.6, solo con trace_extended)
├── memory_addresses (ver 1.5, solo si hubo indexadas)
└── instructions_previous_version
    ├── total, scalar, vsetvl, vector                       instrucciones
    └── sew: [ <bloque_sew> x4 ]   índice 0..3 = SEW 8, 16, 32, 64
```

`instructions_previous_version.total = scalar + vsetvl + vector` y
`vector = Σ sew[i].instr`.

Aquí sólo hay **estadísticas y dependencias**; el nombre del bloque de
instrucciones es el de la versión anterior del contrato. Los contadores por
instrucción están en `region.instructions`, con los niveles del listado (ver
**§1.4.1**).

### 1.2 `global_stats`

| Campo | Unidad | Significado |
|---|---|---|
| `avg_vl_elems` | elementos | media de VL (elementos) sobre las instrucciones vectoriales |
| `avg_vl_bits` | bits | media de `min(VL·SEW, VLEN·LMUL)` |
| `avg_lmul` | factor | media de LMUL (1/8 … 8) |
| `avg_occupancy` | ratio 0..1 | media de `min(VL·SEW, VLEN·LMUL) / (VLEN·LMUL)`; 1.0 = usa todo el datapath |
| `vlen_bits` | bits | VLEN de la ejecución (duplica la raíz) |
| `avg_vlmax_elems` | elementos | media de `VLEN·LMUL/SEW` |
| `tu_effective` | nº instr | instrucciones con VL efectiva < VLMAX y política *tail-undisturbed* |
| `ta_effective` | nº instr | ídem con política *tail-agnostic* |
| `unique_vector_registers` | nº (0..32) | registros vectoriales distintos usados |

### 1.3 `dependencies`

```
dependencies
├── scalar { total, raw, waw, war }
└── vector { total,
            raw { count, avg_dist },
            waw { count, avg_dist },
            war { count, avg_dist } }
```

`avg_dist` = distancia media **en instrucciones** entre la escritura y el uso
(`count = 0` → `avg_dist = 0`, no es un error).

**Desde el 2026-10-12** los riesgos se clasifican por el registro implicado
(vectorial si `id >= 32`, si no escalar), así que las dos ramas se reparten los
mismos riesgos. En ficheros generados antes de esa fecha **todos** los riesgos
detectados caían en `vector`: no compares `scalar.*`/`vector.*` entre ficheros de
fechas distintas (medido en `axpy`: `scalar.raw` pasa de `0` a `8192`).

### 1.4 `<bloque_sew>`

```
sew_block
├── sew            8|16|32|64
├── instr          instrucciones vectoriales ejecutadas con ese SEW
├── elems          elementos procesados por esas instrucciones
├── avg_vl         elems / instr   (0 si instr = 0)
├── elems_by_kind  { arith, memory, reduction, mask }   elementos por tipo
├── avg_stride_b       stride medio (bytes) de los accesos strided
└── stddev_stride_b    desviación típica de ese stride
```

Son **estadísticas por SEW**: hasta el 2026-10-16 cada bloque llevaba además los
desgloses `arith` / `memory` / `reduction` / `mask` / `other`, que se han
eliminado porque se solapaban entre sí y con las categorías (§1.7). El detalle de
*qué instrucciones* se han ejecutado está en el **árbol de categorías** de la
región (§1.4.1), que sí es una partición.

Dos cosas **no** se pueden deducir del árbol (cuenta instrucciones, no elementos) y
por eso siguen aquí:

* `elems_by_kind`: elementos procesados por tipo de operación. Los tipos **se
  solapan** —una instrucción de máscara suma a la vez en `arith` y en `mask`—, así
  que la suma de los cuatro puede superar `elems`; cada uno por separado nunca
  pasa de `elems`.
* `avg_stride_b` / `stddev_stride_b`: medias internas de los accesos strided
  (`0` si no hubo ninguno).

Los siete campos se calculan siempre, con o sin `trace_extended`.

Estos cuatro campos se calculan siempre, con o sin `trace_extended`.

### 1.4.1 Contadores por instrucción (niveles del listado)

Desde el 2026-10-16 cada región lleva en `instructions` los contadores por
instrucción, agrupados con los mismos niveles que el listado
`docs/rvv instr.md`:

```json
{
  "id": 0,
  "name": "GLOBAL_REGION",
  "counters": { "...": "estadísticas y dependencias (1.1) + instructions_previous_version (1.4)" },
  "instructions": {
    "configuration_setting": { "vsetvli_counter": 5 },
    "vector_loads_and_stores": {
      "unit_stride": { "vle32_v_counter": 3, "vse32_v_counter": 3 }
    },
    "vector_integer_arithmetic": {
      "single_width_add_subtract": { "vadd_vx_counter": 1 }
    },
    "vector_floating_point": {
      "compare": { "vmfgt_vf_counter": 1 }
    }
  }
}
```

* **Clave de contador** = nombre del contador = la instrucción con todo lo que no
  sea `[a-zA-Z0-9_]` sustituido por `_` y el sufijo `_counter`:
  `vadd.vv` → `vadd_vv_counter`, `vfmv.f.s` → `vfmv_f_s_counter`,
  `vl1r.v` → `vl1r_v_counter`.
* **Clave de categoría** = el título del listado en `snake_case`
  (`vector_loads_and_stores`, `unit_stride`,
  `single_width_fused_multiply_add`, `alias_and_pseudo_instructions`…). El
  conjunto completo de claves posibles (630 contadores en 80 categorías) lo fija
  `utils/gen_instr_counters.py` a partir del listado; su versión para C es
  `rave_sources/include/rvv/instr_counters.h`.
* **Valor** = instrucciones de esa forma ejecutadas en la región, sumadas sobre
  los cuatro SEW.
* **Sólo se emiten las ramas con cuenta > 0**: un JSON normal trae unas pocas
  decenas de claves, no 630. Una clave ausente es `0`.
* Cada instrucción cuelga de **una sola** rama: no hay categorías solapadas, así
  que se puede sumar cualquier nivel sin duplicar nada (§1.7).
* **Indexadas colapsadas**: `vluxei*`/`vloxei*` se suman en `indexed.load` y
  `vsuxei*`/`vsoxei*` en `indexed.store`; las segmentadas indexadas
  (`vluxseg*`, `vsoxseg`…) van a las mismas dos sumas, así que la sección
  `indexed_segment` ya no aparece. Las distancias de esas instrucciones se
  agregan también por grupo en `memory_addresses` (§1.5).
* Los **`vset*`** (`vsetvli`, `vsetivli`, `vsetvl`) van en
  `configuration_setting` aunque no tengan SEW: por eso el árbol está a nivel de
  región y no dentro de cada `<bloque_sew>`. Si la región no ejecutó ninguna
  instrucción vectorial (ni `vset*`), `instructions` queda como objeto vacío.
* Estos contadores **no dependen de `trace_extended`**: se rellenan siempre.
* Los contadores **agregados** que existían antes (`vunit_instr`, `vstride_instr`,
  `vidx_instr`, `vspill_instr`, `vint_instr`, `vfp_instr`, `vseg_instr_*`,
  `vmove_instr`, `vmask_instr`, `vsetvl_instr`…) **ya no se emiten**: se
  recalculan sumando las categorías (p. ej. la memoria unit-stride es
  `Σ vector_loads_and_stores.unit_stride` y las máscaras `Σ vector_mask.*`).
* `utils/profiler_json_reader.py` aplana el árbol con `instruction_counters()` y
  suma por sección con `category_totals()` (`--summary` muestra esos totales y el
  *top 5*; `--dump` el árbol completo).

### 1.5 `memory_addresses` — distancias de las indexadas

Sólo se perfilan las instrucciones **indexadas**. Para cada ejecución se calculan
sus direcciones efectivas (`base (rs1) + índice × EEW`) y se toma el rango
`max − min`. Por instrucción se acumulan esos rangos; el JSON publica **una sola
media por grupo** (cargas y almacenamientos):

```json
"memory_addresses": {
  "load":  { "instr": 2, "executions": 101, "avg_range_b": 140 },
  "store": { "instr": 1, "executions": 2,   "avg_range_b": 160 }
}
```

* `avg_range_b` es la **media de las medias de cada instrucción**, dando el mismo
  peso a todas: primero se hace la media de cada instrucción con sus propias
  ejecuciones y luego se promedian esas medias. **No** es la media ponderada por
  ejecuciones.
* `instr` = cuántas instrucciones distintas entran en la media; `executions` = sus
  ejecuciones totales.
* Los grupos son los mismos del árbol: `load` = mnemónicos `vl*`, `store` = `vs*`
  (§1.4.1).
* Es **opcional** en el esquema: no aparece si la región no ejecutó indexadas.
* Coste: se lee el vector de índices en cada instrucción indexada (O(VL)).

### 1.6 `masked` — instrucciones enmascaradas y densidad de la máscara

Con `RAVE_TRACE_EXTENDED=1`, las instrucciones con `vm=0` se cuentan aparte y se
mide la **densidad** de la máscara: la fracción de lanes activos en `v0`, con su
media y desviación típica. Se separan dos categorías:

```json
"masked": {
  "arith":  { "instr": 1, "avg_density": 0.50, "stddev_density": 0.00 },
  "memory": { "instr": 1, "avg_density": 0.50, "stddev_density": 0.00 }
}
```

* `instr` = número de instrucciones enmascaradas de esa categoría (sumado sobre
  todos los SEW); `avg_density` ∈ [0,1]; `stddev_density` = dispersión de esas
  densidades.
* **Sólo hay datos con `trace_extended`** (la clasificación usa `type_ext`); sin
  él el bloque sale vacío.
* Opcional en el esquema: los JSON anteriores al 2026-10-18 no lo traen.

### 1.7 Sumas que se pueden hacer (y cuáles no)

El árbol de `region.instructions` (§1.4.1) **es una partición**: cada instrucción
ejecutada está en una única rama. Por eso cualquier suma cuenta exactamente lo que
cuelga de ahí, sin duplicados:

| Quiero… | Fórmula |
|---|---|
| todas las instrucciones contadas | `Σ region.instructions = prev.total − prev.scalar` |
| las vectoriales | `prev.vector = Σ prev.sew[].instr` |
| las de carga/almacenamiento | `Σ instructions.vector_loads_and_stores.*` |
| las unit-stride | `instructions.vector_loads_and_stores.unit_stride` |
| las segmentadas | `Σ instructions.vector_loads_and_stores.segment.*` |
| las máscaras | `Σ instructions.vector_mask.*` |
| las instrucciones `vset*` | `prev.vsetvl` |

(donde `prev` = `counters.instructions_previous_version`).

Lo que **no** tiene sentido es sumar entre sí los bloques de `counters`
(`moved_bytes`, `flops`, `global_stats`, `dependencies`): son magnitudes distintas
(bytes, FLOPs, medias y riesgos), no recuentos de instrucciones.

Las únicas desviaciones posibles son de redondeo con `accumulate_regions` (cada
contador es una media redondeada por separado, ver §2.7).

Hasta el 2026-10-16 sí había solapes: `arith` cubría dos ejes a la vez (forma y
función), `sew[s].mask.instr` duplicaba `sew[s].arith.mask`, y `other` era un
residuo que podía salir **negativo** (medido en `particlefilter_vector`:
`other = −17088`). Todo eso desaparece al pasar a las categorías del listado.

---

## 2. Reglas de parseo

1. **Todos los valores numéricos son números JSON**, nunca cadenas. Los enteros se
   emiten sin parte decimal (`%.0f`) y los reales con 6 decimales; un real que sea
   entero se emite como entero (`"avg_lmul": 1`). No asumas presencia/ausencia de `.0`.
2. **Los contadores se guardan internamente como `double`** y se imprimen con `%.0f`.
   Por encima de 2^53 (~9·10^15) perderían precisión; en la práctica no se llega.
   Aun así, usa enteros de 64 bits al leer (los valores superan 2^31: p. ej.
   `moved_bytes.total` de `somier` ≈ 3,2·10^8).
3. **`null` es posible** en `binary`, `name` y `parent`. Comprueba antes de usar.
4. **`sew` es siempre un array de 4 elementos** (SEW 8/16/32/64), aunque estén a cero.
   Usa el campo `sew` de cada bloque, no la posición.
5. **No hay campo de versión** del esquema. `generator` identifica quién lo escribió.
6. `region_count` debe ser igual a `len(regions)` **sin acumulación**; con
   `accumulate_regions` puede ser mayor (ver 1.0). Un fichero incoherente es aquel
   con `region_count` **menor** que `len(regions)`.
7. **Con `accumulate_regions` las descomposiciones pueden desviarse ±0,5 por sumando**:
   cada contador es una media por ejecución redondeada a entero de forma
   independiente. Medido en `large/lavaMD.json`: `instructions_previous_version.vector = 158054`
   frente a la suma de `sew[].instr = 158055`. No es un fichero corrupto; compara
   con tolerancia (el visor `profiler/` usa 0,5 por sumando) o usa JSONs sin
   acumulación para validar igualdades exactas.

---

## 3. Ejemplo de lectura

```python
import json

d = json.load(open("spmv_vector.json"))
print(d["generator"], d["binary"], d["vlen_bits"], d["region_count"])

def total(node):                      # suma un subárbol de categorías
    return sum(total(v) if isinstance(v, dict) else v for v in node.values())

for reg in d["regions"]:
    c = reg["counters"]
    g = c["global_stats"]
    print(f'{reg["name"]}: moved={c["moved_bytes"]["total"]} B, '
          f'vec_instr={c["instructions"]["vector"]}, '
          f'avgVL={g["avg_vl_elems"]:.2f}, ocupación={g["avg_occupancy"]*100:.2f}%')

    for s in c["instructions_previous_version"]["sew"]:
        if s["instr"]:
            print(f'  SEW{s["sew"]}: instr={s["instr"]} elems={s["elems"]} '
                  f'avg_vl={s["avg_vl"]:.2f}')

    # contadores por instrucción: total por sección del listado
    for name, node in reg["instructions"].items():
        print(f'  {name}: {total(node)}')

    # qué instrucciones concretas se han ejecutado
    print("  ", reg["instructions"].get("configuration_setting", {}))
```

Con `jq`:

```bash
# resumen por región
jq -r '.regions[] | [.name, .counters.moved_bytes.total,
                      .counters.instructions_previous_version.vector,
                      .counters.global_stats.avg_occupancy] | @tsv' f.json

# solo los SEW usados
jq '.regions[0].counters.instructions_previous_version.sew[] | select(.instr>0)
    | {sew, instr, avg_vl}' f.json

# comprobar que las dependencias están activas
jq '.regions[0].counters.dependencies.vector' f.json

# contadores por instrucción: un nivel del listado, o todo el árbol aplanado
jq '.regions[0].instructions.vector_loads_and_stores' f.json
jq '.regions[0].instructions | paths(scalars) | map(tostring) | join(".")' f.json
```

El lector de referencia ya trae esto hecho:

```bash
python3 utils/profiler_json_reader.py f.json --summary   # totales por sección y top 5
python3 utils/profiler_json_reader.py f.json --dump      # el árbol completo
```

---

## 4. Referencias

* **Cómo se genera el JSON** (opciones, variables de entorno, modos de traza) y
  **qué campos pueden variar entre ejecuciones**: `README_EXTENDED.md` §3 y §6.
* Visor de estos ficheros: `../profiler/` (pestañas por región, leyenda en texto y
  diagramas de tarta y de barras). Es un proyecto aparte, **fuera de este
  repositorio**; su única dependencia con RAVE es este contrato.
* **Esquema legible por máquina**: `schema/profiler_json_schema_v1.json`
  (este documento en formato JSON Schema; `additionalProperties: false` para
  detectar campos nuevos o desaparecidos).
* **Validador y lector de referencia**: `utils/profiler_json_reader.py`.

  ```bash
  # forma (esquema) + invariantes del contrato
  python3 utils/profiler_json_reader.py salida.json --validate
  # resumen por región y SEW
  python3 utils/profiler_json_reader.py salida.json --summary
  # solo invariantes (descomposiciones y orden de sew[])
  python3 utils/profiler_json_reader.py salida.json --check
  ```

  Comprueba, entre otras cosas, que `total = scalar + vsetvl + vector`, que
  `vector = Σ sew[].instr` y que la suma del árbol de contadores por instrucción
  es `total − scalar` (error si se pasa, aviso si se queda corta porque haya
  mnemónicos que la tabla aún no conoce).
* Pruebas del esquema y del validador: `test/profiler_json/`.

---

## 5. Historial del contrato

El documento **depende del generador**: cualquier cambio en los contadores o en el
escritor JSON tiene que reflejarse aquí (y en `schema/profiler_json_schema_v1.json`).

| Fecha | Cambio en RAVE | Efecto en el contrato |
|---|---|---|
| 2026-10-16 | `instr_counters.c` (nuevo) + `instr_data.c` + `callbacks.c`: contador propio por cada instrucción vectorial (630), con `_counter` en el nombre; `gen_instr_counters.py` los agrupa con los niveles de `docs/rvv instr.md` | **Rediseño del contrato (v2)**: los contadores por instrucción pasan a `region.instructions`, con las categorías del listado (§1.4.1), que forman una partición. El bloque de recuentos anterior se conserva en `counters.instructions_previous_version` (§1.1); desaparecen sus desgloses por SEW `arith`/`memory`/`reduction`/`mask`/`other` y los contadores agregados (`vunit_instr`, `vint_instr`…), que se recalculan sumando categorías (§1.7). El bloque SEW se queda con las estadísticas y con lo que **no** se deduce del árbol: `elems_by_kind` y `avg_stride_b`/`stddev_stride_b` (§1.4) |
| 2026-10-18 | merge de `24c83b9` (mask metrics) | **`counters.masked`** (§1.6): instrucciones enmascaradas y densidad media de la máscara por categoría (arith / memory), sólo con `trace_extended`. La rama original sólo lo sacaba por el informe de texto |
| 2026-10-18 | `gen_instr_counters.py` (sumas por prefijo) + `callbacks.c` (rango de direcciones) | **Indexadas colapsadas**: `indexed.load` / `indexed.store` suman todos los encodings (también las segmentadas indexadas, que pierden su nodo). Nuevo bloque opcional `counters.memory_addresses` (§1.5): media del rango `max−min` por instrucción indexada, con el mismo peso para todas al agregar |
| 2026-10-12 | `inst_trace.c`: nueva traza dinámica por instrucción (JSONL, otro fichero) | Ninguno: este contrato son los contadores agregados por región |
| 2026-10-12 | `instr_data.c`: las reducciones se marcan con su bit FP/INT | `reduction.int` deja de ser siempre `0`; las reducciones salen de `computation` (§1.4) |
| 2026-10-12 | `callbacks.c`: los riesgos se separan por registro escalar/vectorial | `dependencies.scalar.*` deja de ser siempre `0` (§1.3) |
| 2026-10-12 | `counters.c`: el bloque `memory` exporta `indexed_ordered` / `indexed_unordered` | Dos campos nuevos (§1.4); opcionales en el esquema para poder validar ficheros anteriores |
| 2026-10-12 | Este documento se mueve a `docs/` dentro del repositorio | Las referencias ya no apuntan fuera del repo |

