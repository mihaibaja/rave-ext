# Rastreo de Riesgos y Dependencias de Datos (RAW, WAR, WAW)

Una de las extensiones más críticas añadidas en esta versión extendida es el motor de análisis y rastreo dinámico de dependencias de datos entre instrucciones vectoriales y escalares.

---

## Modelo Unificado de 96 Registros

Para monitorizar de forma homogénea todas las dependencias arquitecturales del procesador RISC-V, RAVE mapea todos los registros a un espacio continuo de índices `[0 .. 95]`:

```
   Índice 0                    Índice 32                   Índice 64            Índice 95
┌───────────────────────────┬───────────────────────────┬───────────────────────────┐
│     Registros Escalares   │     Registros Flotantes   │     Registros Vectoriales │
│        Enteros (x0..x31)  │         (f0..f31)         │           (v0..v31)       │
│      RAVE_INI_REG_S = 0   │      RAVE_INI_REG_F = 32  │      RAVE_INI_REG_V = 64  │
└───────────────────────────┴───────────────────────────┴───────────────────────────┘
```

Definido en [`rave_sources/include/common/state.h`](../../rave_sources/include/common/state.h#L60):

```c
#define RAVE_INI_REG_S 0
#define RAVE_INI_REG_F 32
#define RAVE_INI_REG_V 64
```

### Conversión de Nombres y Alias ABI

La función [`reg2id()`](../../rave_sources/src/common/rave2prv.c#L125) mapea tanto los identificadores arquitecturales estándar (`x0..x31`, `f0..f31`, `v0..v31`) como todos los alias del ABI estándar de RISC-V:
- Enteros: `zero`, `ra`, `sp`, `gp`, `tp`, `t0..t6`, `s0..s11`, `a0..a7`.
- Flotantes: `ft0..ft11`, `fs0..fs11`, `fa0..fa7`.
- Soporte para alias como `fp` ($\rightarrow$ `s0` / `x8`).

---

## Estructuras de Datos y Estado

Cada hilo emulado mantiene en su estructura [`thread_state_t`](../../rave_sources/include/common/state.h#L73) la información de las últimas escrituras y lecturas:

```c
typedef struct inst_compressed_t inst_compressed_t;
struct inst_compressed_t {
    char * asm_string; // Cadena ensambladora de la instrucción
    uint64_t pos;      // Posición secuencial global en el flujo dinámico
    uint16_t type;     // Tipo de instrucción (escalar o vectorial)
};

struct thread_state_t {
    ...
    // Tabla para cálculo de estadísticas acumuladas (RAW_DIST, etc.)
    struct inst_compressed_t write_reg_deps[96];
    struct inst_compressed_t read_reg_deps[96];

    // Tabla para cálculo de distancias en la traza dinámica JSONL
    uint64_t inst_trace_last_write[96];
    uint64_t inst_trace_last_read[96];
    ...
};
```

---

## Algoritmo de Detección de Riesgos

Durante la ejecución de cada instrucción vectorial en [`callbacks.c`](../../rave_sources/src/rvv/callbacks.c#L280):

1. **Posición Global Dinámica**:
   Se calcula la posición acumulada `pos` de la instrucción.

2. **Detección WAW (Escritura tras Escritura)**:
   Si el registro destino `dst_d` es válido y fue escrito previamente en `write_reg_deps[dst_d].pos != 0`:
   $$\Delta_{\text{WAW}} = \text{pos} - \text{write\_reg\_deps}[dst\_d].\text{pos}$$
   Si $\Delta_{\text{WAW}} \le \text{WAW\_DIST}$:
   - Si $dst\_d \ge \text{RAVE\_INI\_REG\_V}$ (registro vectorial): se incrementa `VWAW_deps` y se acumula la distancia en `avg_VWAW_deps`.
   - En caso contrario: se incrementa `WAW_deps`.

3. **Detección WAR (Escritura tras Lectura)**:
   Si el registro destino `dst_d` fue leído previamente en `read_reg_deps[dst_d].pos != 0`:
   $$\Delta_{\text{WAR}} = \text{pos} - \text{read\_reg\_deps}[dst\_d].\text{pos}$$
   Si $\Delta_{\text{WAR}} \le \text{WAR\_DIST}$:
   - Si $dst\_d \ge \text{RAVE\_INI\_REG\_V}$: se incrementa `VWAR_deps` y se acumula en `avg_VWAR_deps`.
   - En caso contrario: se incrementa `WAR_deps`.

4. **Detección RAW (Lectura tras Escritura)**:
   Para cada registro fuente leído (`src1_d`, `src2_d`, `src3_d`):
   $$\Delta_{\text{RAW}} = \text{pos} - \text{write\_reg\_deps}[src_i\_d].\text{pos}$$
   Si $\Delta_{\text{RAW}} \le \text{RAW\_DIST}$:
   - Si $src_i\_d \ge \text{RAVE\_INI\_REG\_V}$: se incrementa `VRAW_deps` y se acumula en `avg_VRAW_deps`.
   - En caso contrario: se incrementa `RAW_deps`.

5. **Actualización del Estado**:
   Tras evaluar los riesgos, se registran la lectura y escritura para las instrucciones siguientes:
   ```c
   if (valid_reg_id(instr->dst_d))  state->write_reg_deps[instr->dst_d]  = instr_c;
   if (valid_reg_id(instr->src1_d)) state->read_reg_deps[instr->src1_d]  = instr_c;
   if (valid_reg_id(instr->src2_d)) state->read_reg_deps[instr->src2_d]  = instr_c;
   if (valid_reg_id(instr->src3_d)) state->read_reg_deps[instr->src3_d]  = instr_c;
   ```

---

## Ventanas de Distancia Configurables

Las ventanas máximas de inspección se configuran mediante variables de entorno o argumentos del script de lanzamiento:

| Variable | Script / CLI Arg | Valor por Defecto | Propósito |
| :--- | :--- | :--- | :--- |
| `RAVE_RAW_DIST` | `RAW_DIST=N` | `10` | Ventana máxima en instrucciones para considerar una dependencia RAW relevante. |
| `RAVE_WAR_DIST` | `WAR_DIST=N` | `10` | Ventana máxima en instrucciones para considerar un riesgo WAR. |
| `RAVE_WAW_DIST` | `WAW_DIST=N` | `10` | Ventana máxima en instrucciones para considerar un riesgo WAW. |
| `RAVE_DEBUG_INFO` | `DEBUG_INFO=on` | `0` | Si se activa (`1`), imprime por `stdout` cada dependencia detectada en tiempo real. |

---

## Integración en el Informe Final

Al finalizar la ejecución, el informe de RAVE muestra las métricas de dependencias calculadas:

```text
Dependencies:
  VRAW (vector-vector): 1420 (avg distance: 3.42)
  VWAR (vector-vector): 215  (avg distance: 5.12)
  VWAW (vector-vector): 88   (avg distance: 4.80)
  RAW  (scalar-vector): 650
  WAR  (scalar-vector): 120
  WAW  (scalar-vector): 42
```
Esto permite evaluar inmediatamente la calidad del entrelazado de instrucciones generado por el compilador o programador (ILP vectorial).

**Importante: RAW, WAR y WAW son dependencias de instrucciones vectoriales con instrucciones anteriores escalares, no en el otro sentido.**
