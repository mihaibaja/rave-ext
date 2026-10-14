# Arquitectura del Perfilador en RAVE Extended

En esta sección se introducen e indexan las modificaciones añadidas en el núcleo de RAVE dando lugar a RAVE-EXT.

---

## Submódulos de esta Sección

1. **[Clasificación Extendida de Instrucciones (`type_ext`)](clasificacion-instrucciones.md)**
   - Explicación del campo `type_ext` de 16 bits añadido a [`instr_data`](../../rave_sources/include/rvv/instr_data.h).
   - Máscaras de bits y jerarquía de clasificación (Widening, Narrowing, Fused, Reductions, Mask, Permutations, Segmented Memory).

2. **[Rastreo de Riesgos y Dependencias de Datos (RAW, WAR, WAW)](rastreo-dependencias.md)**
   - Estructuras [`inst_compressed_t`](../../rave_sources/include/common/state.h) y arrays de seguimiento sobre los 96 registros arquitecturales (32 escalares enteros, 32 flotantes y 32 vectoriales).
   - Definición de distancias umbral (`RAVE_RAW_DIST`, `RAVE_WAR_DIST`, `RAVE_WAW_DIST`).
   - Algoritmo de detección y distinción de dependencias vector-vector (`VRAW`, `VWAR`, `VWAW`) vs escalar-vector (`RAW`, `WAR`, `WAW`).

3. **[Métricas Vectoriales Avanzadas y Estadísticas](metricas-vectoriales.md)**
   - Ocupación efectiva del registro vectorial respecto a VLMAX.
   - Manejo del LMUL fraccional (`mf2`, `mf4`, `mf8`) de la especificación RVV 1.0.
   - Contabilización de políticas de cola y máscara (`ta` frente a `tu`).
   - Muestreo de dispersión en accesos con salto: media y desviación estándar de strides.
   - Mapa de calor/uso de registros vectoriales individuales.

---

## Archivos Fuente Clave Modificados

- [`rave_sources/include/rvv/instr_data.h`](../../rave_sources/include/rvv/instr_data.h): Definición de máscaras y constantes `TYPE_EXT_*`.
- [`rave_sources/src/rvv/instr_data.c`](../../rave_sources/src/rvv/instr_data.c): Implementación de `instr_set_vector_type_ext()`, es decir, donde clasificamos de manera estática las instrucciones.
- [`rave_sources/include/rvv/counters.h`](../../rave_sources/include/rvv/counters.h): Campos añadidos en `struct rave_counters`.
- [`rave_sources/src/rvv/counters.c`](../../rave_sources/src/rvv/counters.c): Generación del informe detallado de estos campos.
- [`rave_sources/src/rvv/callbacks.c`](../../rave_sources/src/rvv/callbacks.c): Callbacks de ejecución donde se actualizan contadores y se comprueban riesgos.
- [`rave_sources/include/common/state.h`](../../rave_sources/include/common/state.h): Espacio añadido de 96 registros en `thread_state_t`, para así poder detectar dependencias de datos.
- [`rave_sources/src/common/rave2prv.c`](../../rave_sources/src/common/rave2prv.c): Función `reg2id()` que mapea nombres de registros (`x0-x31`, `f0-f31`, `v0-v31`) a IDs 0..95.
