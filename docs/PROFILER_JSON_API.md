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
  "counters": { <counters> }
}
```

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
└── instructions
    ├── total, scalar, vsetvl, vector                       instrucciones
    └── sew: [ <bloque_sew> x4 ]   índice 0..3 = SEW 8, 16, 32, 64
```

`instructions.total = scalar + vsetvl + vector` y `vector = Σ sew[i].instr`.

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
├── instr, elems, avg_vl
├── arith          { instr, elems, avg_vl, fp, int,
│                    narrowing, widening,
│                    widening_reductions { total, fp, int }, widening_fused, normal,
│                    permutations { total,
│                                   moves { total, read_from_scalar, write_to_scalar,
│                                           whole_register, others },
│                                   slides, gather, compress },
│                    mask, reductions { total, fp, int },
│                    computation { total, fused },
│                    scalar_deps { total, read_from_scalar, write_to_scalar } }
├── memory         { instr, elems, avg_vl, unit, segmented_unit,
│                    strided, avg_stride_b, stddev_stride_b,
│                    segmented_strided, indexed, indexed_ordered, indexed_unordered,
│                    segmented_indexed, whole_register }
├── reduction      { instr, elems, fp, int }
├── mask           { instr, elems, avg_vl }
└── other          { instr, elems, avg_vl }
```

Descomposiciones (verificadas con `utils/profiler_json_reader.py --check` sobre los
25 JSON de `resultados/`):

* `arith.instr  = fp + int`                                    ← exacto
* `arith.instr  = narrowing + widening + normal`               ← exacto
* `memory.instr = unit + strided + indexed + whole_register`   ← exacto (excluyentes)
* `memory.indexed = indexed_ordered + indexed_unordered`       ← exacto (todo `vle.v`/`vse.v`
  indexado es ordered u unordered, nunca ambos)
* `other.instr  = instr − memory.instr − arith.instr − reduction.instr − mask.instr`
  → **es un residuo, no una categoría**, y **puede salir negativo** (ver 1.5)

`indexed_ordered` / `indexed_unordered` son **subconjuntos** de `indexed`, igual que
`segmented_*` lo es de su categoría. `memory.avg_stride_b` / `stddev_stride_b` están
en **bytes**; si no hay accesos strided valen `0`.

Todo está en `0` cuando `trace_extended` es `false`, **salvo**: `sew`, `instr`,
`elems`, `arith.instr/elems/fp/int`, `reduction.*`, `memory.*` (excepto
`segmented_*`, `indexed_ordered`, `indexed_unordered` y `stddev_stride_b`),
`mask.*` y `other.*`, que se calculan siempre.

`indexed_ordered` / `indexed_unordered` se añadieron el 2026-10-12: en ficheros
generados antes de esa fecha **no aparecen** (léelos como `0`, que es lo que hace
`../profiler/` con cualquier campo ausente).

También desde el 2026-10-12 el decodificador marca las **reducciones** con su bit
FP/INT: `vredsum`/`vredmax`… (enteras) y `vfredsum`/`vfredosum` (FP) dejan de
contarse dentro de `computation` y pasan a `reduction.*` / `arith.reductions.*`,
con `reduction.int` y `reduction.fp` poblados (antes `reduction.int` era siempre
`0`). Consecuencia: en un benchmark con reducciones, `other.instr` puede bajar y
`computation.total` también; no compares esos campos entre fechas.

### 1.5 Categorías solapadas y valores duplicados (importante)

`arith` cubre dos ejes a la vez y **no son disjuntos**:

* Eje "forma" (suman exactamente `arith.instr`): `narrowing`, `widening`, `normal`.
* Eje "función" (**subconjuntos** de `arith.instr`; no los sumes con el anterior):
  `permutations`, `mask`, `reductions`, `computation`, `scalar_deps`.

Además hay **valores duplicados** en dos sitios del mismo JSON (mismo contador interno):

| Campo A | Campo B | Contador interno |
|---|---|---|
| `sew[s].mask.instr` | `sew[s].arith.mask` | `vmask_instr` |
| `sew[s].reduction.*` | `sew[s].arith.widening_reductions.*` | `vfp_reductions + vint_reductions` |

**Consecuencia: `other.instr` puede ser negativo.** Las instrucciones de máscara y
las reducciones se cuentan *dentro* de `arith` (una `vmadc` incrementa `vint_instr`
**y** `vmask_instr`) y después se restan otra vez en la fórmula de `other`. Medido en
`particlefilter_vector` (SEW 64): `instr=73939`, `memory=180`, `arith=73759`,
`mask=17088` → `other = 73939 − 180 − 73759 − 0 − 17088 = −17088`. No interpretes
`other` como "instrucciones sin clasificar"; si lo necesitas, calcula el residuo
excluyendo `mask` y `reduction`.

Ejemplo sin solape (axpy, SEW 64): `instr = 2048`, `normal = 2048`,
`computation.total = 2048` (toda la aritmética es fused/FMA),
`scalar_deps.read_from_scalar = 2048`.

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
   independiente. Medido en `large/lavaMD.json`: `instructions.vector = 158054`
   frente a la suma de `sew[].instr = 158055`. No es un fichero corrupto; compara
   con tolerancia (el visor `profiler/` usa 0,5 por sumando) o usa JSONs sin
   acumulación para validar igualdades exactas.

---

## 3. Ejemplo de lectura

```python
import json

d = json.load(open("spmv_vector.json"))
print(d["generator"], d["binary"], d["vlen_bits"], d["region_count"])

for reg in d["regions"]:
    c = reg["counters"]
    g = c["global_stats"]
    print(f'{reg["name"]}: moved={c["moved_bytes"]["total"]} B, '
          f'vec_instr={c["instructions"]["vector"]}, '
          f'avgVL={g["avg_vl_elems"]:.2f}, ocupación={g["avg_occupancy"]*100:.2f}%')

    for s in c["instructions"]["sew"]:
        if s["instr"]:
            print(f'  SEW{s["sew"]}: instr={s["instr"]} elems={s["elems"]} '
                  f'avg_vl={s["avg_vl"]:.2f} deps={s["arith"]["scalar_deps"]["total"]}')
```

Con `jq`:

```bash
# resumen por región
jq -r '.regions[] | [.name, .counters.moved_bytes.total,
                      .counters.instructions.vector,
                      .counters.global_stats.avg_occupancy] | @tsv' f.json

# solo los SEW usados
jq '.regions[0].counters.instructions.sew[] | select(.instr>0) | {sew, instr, avg_vl}' f.json

# comprobar que las dependencias están activas
jq '.regions[0].counters.dependencies.vector' f.json
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
  # solo invariantes (descomposiciones, orden de sew[], campos extendidos)
  python3 utils/profiler_json_reader.py salida.json --check
  ```

  Comprueba, entre otras cosas, que `indexed = indexed_ordered + indexed_unordered`,
  que `memory.instr = unit + strided + indexed + whole_register` y que los campos
  extendidos están a cero cuando `trace_extended` es `false`.
* Pruebas del esquema y del validador: `test/profiler_json/`.

---

## 5. Historial del contrato

El documento **depende del generador**: cualquier cambio en los contadores o en el
escritor JSON tiene que reflejarse aquí (y en `schema/profiler_json_schema_v1.json`).

| Fecha | Cambio en RAVE | Efecto en el contrato |
|---|---|---|
| 2026-10-12 | `inst_trace.c`: nueva traza dinámica por instrucción (JSONL, otro fichero) | Ninguno: este contrato son los contadores agregados por región |
| 2026-10-12 | `instr_data.c`: las reducciones se marcan con su bit FP/INT | `reduction.int` deja de ser siempre `0`; las reducciones salen de `computation` (§1.4) |
| 2026-10-12 | `callbacks.c`: los riesgos se separan por registro escalar/vectorial | `dependencies.scalar.*` deja de ser siempre `0` (§1.3) |
| 2026-10-12 | `counters.c`: el bloque `memory` exporta `indexed_ordered` / `indexed_unordered` | Dos campos nuevos (§1.4); opcionales en el esquema para poder validar ficheros anteriores |
| 2026-10-12 | Este documento se mueve a `docs/` dentro del repositorio | Las referencias ya no apuntan fuera del repo |

