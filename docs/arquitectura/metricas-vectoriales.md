# Métricas Vectoriales Avanzadas y Estadísticas

Además de la clasificación extendida y el rastreo de dependencias, la rama `feature/extended` incorpora nuevas métricas en [`counters.h`](../../rave_sources/include/rvv/counters.h) y [`callbacks.c`](../../rave_sources/src/rvv/callbacks.c#L564).

---

## 1. Longitud Vectorial (VL) y LMUL Fraccional

En la rama `main`, RAVE solo calculaba un promedio simple de elementos procesados por instrucción. En `feature/extended` se implementa un modelo preciso que distingue el factor de agrupación de registros (**LMUL**).

### Decodificación de LMUL Dinámico

Implementado en [`callbacks.c`](../../rave_sources/src/rvv/callbacks.c#L571):
```c
if (lmul < 4) {
    real_lmul = (double)(1 << lmul);
} 
#ifndef RVV_07
else {
    real_lmul = 1.0 / (double)(1 << (8 - lmul)); 
}
#endif
```

---

## 2. Ocupación Efectiva del Registro Vectorial

El perfilador calcula cuánta capacidad del registro vectorial está siendo aprovechada por cada instrucción con respecto al límite teórico arquitectural (**VLMAX**):

$$\text{VLMAX} = \frac{\text{VLEN} \times \text{real\_lmul}}{\text{SEW\_bits}}$$

$$\text{effective\_vl} = \min(\text{vl} \times \text{SEW\_bits},\; \text{RAVE\_VLMAX} \times \text{real\_lmul})$$

$$\text{Occupancy} = \frac{\text{effective\_vl}}{\text{RAVE\_VLMAX} \times \text{real\_lmul}}$$

La media acumulada se reporta como porcentaje. Un valor de ocupación bajo (ej. < 50%) indica que el algoritmo procesa vectores cortos, desaprovechando los lanes físicos del coprocesador vectorial.

---

## 3. Afectación de Políticas de Cola: `ta` vs `tu`

En la especificación RISC-V Vector, cuando la longitud activa $\text{vl} < \text{VLMAX}$, los elementos restantes se denominan elementos de cola (*tail elements*). La política seleccionada en `vtype` puede ser:
- **`ta` (Tail Agnostic):** No requiere conservar el contenido previo.
- **`tu` (Tail Undisturbed):** El hardware **debe preservar** intactos los elementos previos del registro destino.

En `feature/extended`, se contabiliza cuántas veces esta política tiene efecto real (es decir, cuando $\text{effective\_vl} < \text{real\_lmul} \times \text{VLEN}$):

```c
if (effective_vl < real_lmul * RAVE_VLMAX) {
    if (ta)
        state->accum_counters.ta_count++;
    else
        state->accum_counters.tu_count++;
}
```
---

## 4. Mapa de Uso de Registros Vectoriales

Para estudiar la presión sobre el banco de registros vectoriales (RF) y optimizar el coloreado de registros en compiladores, RAVE mantiene un mapa de bits de acceso sobre los 32 registros vectoriales:

```c
#define NUM_VECTOR_REGS 32
double vector_register_usage[NUM_VECTOR_REGS];
```

Cada vez que una instrucción lee o escribe en `v0..v31`, la función `record_vector_register()` marca el registro como activo. El informe indica el número total de registros vectoriales que se han utilizado en la ejecución de ese programa.

---

## 5. Estadísticas de Dispersión de Stride (Memoria con Salto)

En `feature/extended`, se añade la desviación típica [`agg_strides_squared`](../../rave_sources/include/rvv/counters.h#L32):
Implementado en [`counters.c`](../../rave_sources/src/rvv/counters.c#L182):
```c
double avg_stride = counters->agg_strides[s] / counters->vstride_instr[s];
double stride_variance = counters->agg_strides_squared[s] / counters->vstride_instr[s] - avg_stride * avg_stride;
double std_dev_stride = sqrt(fmax(0.0, stride_variance));
```

### Utilidad
- Si $\sigma \approx 0$, el acceso strided es perfectamente uniforme (ej. acceso por columnas a una matriz fija).
- Si $\sigma > 0$, existen patrones de paso variable.