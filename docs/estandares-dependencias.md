# Estándares de Medición de Dependencias (RAVE)

Criterios y reglas de cálculo de dependencias de datos (RAW, WAR, WAW) para instrucciones vectoriales y escalares en RAVE.

---

## 1. Métrica de Distancia
- **Unidad:** Número de instrucciones dinámicas entre productor y consumidor.
- **Fórmula:**
  $$\Delta = \text{pos}_{\text{consumidor}} - \text{pos}_{\text{productor}}$$
- **Modelo microarquitectónico:** Emisión en orden (*in-order issue*) con *chaining*.

---

## 2. Tratamiento de LMUL y Tipos de Operandos

> [!NOTE]
> **Granularidad a nivel de registro (sin seguimiento a nivel de byte/máscara):**
> El análisis de dependencias opera a nivel de registro vectorial completo (o grupo de registros contiguos según $\text{LMUL}$). **No se tienen en cuenta dependencias a nivel de byte ni de elemento individual mediante máscaras**. Si una instrucción escribe en un registro bajo una máscara y otra posterior lee o escribe en él, se asume dependencia total sobre el registro completo, sin evaluar si los elementos o bytes activos eran disjuntos.

| Tipo de Registro | Rango de Índices | Comportamiento con LMUL | Tamaño del Grupo |
| :--- | :--- | :--- | :--- |
| **Vectorial** | $\ge 64$ (`v0` a `v31`) | Se expande según $\text{LMUL}$ | $1, 2, 4 \text{ u } 8$ registros contiguos |
| **Escalar** | $< 64$ (`x0..x31`, `f0..f31`) | Inmune a $\text{LMUL}$ | **1 registro** fijo |

- $\text{LMUL} < 1$ (fraccional: `mf2`, `mf4`, `mf8`): tamaño = 1 registro.
- $\text{LMUL} \ge 1$ (`m1`, `m2`, `m4`, `m8`): tamaño = $\text{LMUL}$ registros.
- **Widening ($2\times\text{SEW}$):** El operando destino `dst` ocupa $2 \times \text{LMUL}$ registros (máximo 8).
- **Narrowing ($\text{SEW}/2$):** El vector de entrada ancho (`src1`) ocupa $2 \times \text{LMUL}$ registros (máximo 8).
- **Widening Fused MAC (`vwmacc*`, `vfwmacc*`):** El acumulador `src3` (que coincide con `dst`) ocupa $2 \times \text{LMUL}$ registros (máximo 8).
- **Operaciones de Máscara (`TYPE_EXT_ARITH_MASK` - `vmseq`, `vmsne`, etc.):** El operando destino `dst` escribe un vector de máscara (1 bit por elemento), por lo que siempre ocupa **1 solo registro** (`count = 1`), independientemente de $\text{LMUL}$.
- **Whole Register Move (`TYPE_EXT_WHOLE_REGISTER` - `vmv1r`, `vmv2r`, `vmv4r`, `vmv8r`):** Ignoran el $\text{LMUL}$ de `vsetvl`. Mueven $N \in \{1, 2, 4, 8\}$ registros contiguos tanto en lectura (`src1`) como en escritura (`dst`).
- **Whole Register Load/Store (`TYPE_EXT_MEMORY_WHOLE_REGISTER` - `vl<N>re*`, `vs<N>r`):** Ignoran el $\text{LMUL}$ de `vsetvl`. Transfieren $N \in \{1, 2, 4, 8\}$ registros contiguos entre memoria y registros vectoriales (`dst` para load, `src3` para store).
- **Reducciones Vectoriales (`TYPE_EXT_ARITH_REDUCTION` - `vredsum.vs`, `vfredusum.vs`, etc.):**
  - El vector a reducir (`src1` / `vs2`) ocupa $\text{LMUL}$ registros.
  - El acumulador escalar (`src2` / `vs1`) y el resultado escalar (`dst` / `vd`) **siempre ocupan 1 solo registro** (`count = 1`), independientemente de $\text{LMUL}$.

---

## 3. Mapeo de Operandos RISC-V a RAVE (`src1_d`, `src2_d`, `src3_d`)

El parser de RAVE (`instr_data.c`) lee los tokens del ensamblador en orden posicional estricto:

| Token Ensamblador | Campo Interno RAVE | Rol Arquitectónico Típico |
| :--- | :--- | :--- |
| `instr_fields[1]` | `dst_d` | Registro destino `vd` |
| `instr_fields[2]` | `src1_d` | Primer operando fuente textual (en RISC-V es `vs2`) |
| `instr_fields[3]` | `src2_d` | Segundo operando fuente textual (en RISC-V es `vs1` o `rs1`/`imm`) |
| *(Interno)* | `src3_d` | Tercer operando / acumulador (`vd` previo en *fused*, o dato en *stores*) |

### Consecuencia en Narrowing y Fused:
- **Narrowing (`vnsrl.wv vd, vs2, vs1`):**
  - El vector ancho de entrada es `vs2`.
  - Por sintaxis ensambladora va en el primer campo fuente (`instr_fields[2]`), por lo que en RAVE se almacena en **`src1_d`**.
  - **Por esta razón, en narrowing el factor $2 \times \text{LMUL}$ se aplica a `src1_d` y no a `src2_d`**.
- **Fused Multiply-Add (`vfmacc`, `vwmacc`):**
  - El acumulador es `vd`, y RAVE lo asigna a **`src3_d`** para comprobar la dependencia RAW sobre el valor previo acumulado.
  - En variantes con ensanchamiento (*widening*, ej. `vwmacc`), `src3_d` ocupa **$2 \times \text{LMUL}$ registros** (`dst_factor`), igual que `dst_d`. En variantes estándar (`vmacc`, `vfmacc`), ocupa $\text{LMUL}$ registros.
- **Stores (`vse32.v vs3, (rs1)`):**
  - El vector a escribir en memoria (`vs3`) se asigna a **`src3_d`**, dejando `dst_d = -1`.
- **Reducciones Vectoriales (`vredsum.vs vd, vs2, vs1`):**
  - El vector a reducir es `vs2` (`src1_d`) y ocupa $\text{LMUL}$ registros.
  - El escalar inicial es `vs1` (`src2_d`) y solo ocupa **1 registro** (elemento 0).
  - El destino `vd` (`dst_d`) solo escribe el resultado escalar y ocupa **1 registro** (elemento 0).
- **Instrucciones Enmascaradas (`vm = 0` / `TYPE_EXT_MASKED`):**
  - Leen implícitamente el registro de máscara **`v0`** (`RAVE_INI_REG_V + 0`).
  - La máscara ocupa siempre **1 registro** (`count = 1`), independientemente de $\text{LMUL}$.
  - Se comprueba como operando fuente adicional y se registra en `read_reg_deps[v0]`.

---

## 4. Criterio de Productor Más Reciente ($\max(\text{pos})$)

Para evitar multiplicar los contadores (*overcounting*) cuando $\text{LMUL} > 1$:

### RAW (Lectura tras Escritura)
- **Regla:** Para cada operando fuente (`src1`, `src2`, `src3`), se evalúan los registros del grupo y se selecciona el que tenga la escritura más tardía:
  $$\text{pos}_{\text{crítica}} = \max_{i \in [0, \text{count}-1]} \left(\text{write\_reg\_deps}[\text{base} + i].\text{pos}\right)$$
- **Conteo:** **Exactamente 1 dependencia RAW por operando fuente**.
- **Distancia:** $\Delta_{\text{RAW}} = \text{pos}_{\text{actual}} - \text{pos}_{\text{crítica}}$.

### WAW (Escritura tras Escritura)
- **Regla:** Se busca la última escritura previa en el grupo destino `dst`:
  $$\text{pos}_{\text{waw}} = \max_{i \in [0, \text{count}-1]} \left(\text{write\_reg\_deps}[\text{dst} + i].\text{pos}\right)$$
- **Conteo:** **Máximo 1 dependencia WAW por instrucción**.

### WAR (Escritura tras Lectura)
- **Regla:** Se busca la última lectura previa en el grupo destino `dst`:
  $$\text{pos}_{\text{war}} = \max_{i \in [0, \text{count}-1]} \left(\text{read\_reg\_deps}[\text{dst} + i].\text{pos}\right)$$
- **Conteo:** **Máximo 1 dependencia WAR por instrucción**.

---

## 5. Actualización del Estado

- **Al escribir (`dst`):** Se actualizan las entradas individuales de todos los registros del grupo en `write_reg_deps[dst + i]`.
- **Al leer (`src` y máscara `v0`):** Se actualizan las entradas individuales de todos los registros del grupo en `read_reg_deps[src + i]`, y para instrucciones enmascaradas en `read_reg_deps[v0]`.
- **Ventaja:** Si una instrucción posterior utiliza un $\text{LMUL}$ diferente o sobrescribe un solo registro del grupo, cada registro físico mantiene su propio estado de forma independiente.

---

## 6. Paralelismo Disponible en Ventana (`track_vector_parallelism_available`)

Evalúa el número de instrucciones listas para ejecutarse en paralelo dentro de una ventana deslizante de instrucciones vectoriales consecutivas de tamaño $W \in \{2, 4, 8, 16\}$ (Figura 3.5), bajo un modelo de emisión/ejecución FIFO en orden (*in-order*):

- **Estructura en `state->rvv_raw_win`:** Búfer circular de hasta 16 instrucciones vectoriales recientes. Cada entrada almacena:
  - `dst_mask`: Máscara de bits (`uint32_t`) con los registros vectoriales destino escritos ($v_0 \dots v_{31}$).
  - `src_mask`: Máscara de bits (`uint32_t`) con los registros vectoriales fuente leídos ($v_0 \dots v_{31}$).
- **Cálculo de Máscaras:**
  - Se activan todos los registros correspondientes al grupo según $\text{LMUL}$, *widening*, *narrowing*, *whole-register* o reducciones.
  - Para instrucciones enmascaradas (`is_masked`), se incluye el bit 0 en `src_mask` para modelar la lectura de `v0`.
- **Modelo de Evaluación FIFO ($O(W)$):**
  - La comprobación de dependencias y colisiones se realiza a nivel de registro completo ($v_0 \dots v_{31}$), sin desagregar a nivel de byte o elemento según el valor de las máscaras.
  - Se recorren las $W$ instrucciones de la ventana en orden de programa (de más antigua $i=0$ a más reciente $i=W-1$).
  - Se mantiene una máscara acumulada de registros sucios (`dirty_mask`) producidos por las instrucciones precedentes en la ventana.
  - Una instrucción se considera **lista** (`ready_insns++`) si ninguna de sus fuentes colisiona con escrituras de instrucciones previas:
    $$(\text{src\_mask}_i \ \& \ \text{dirty\_mask}) == 0$$
  - Tras evaluar la instrucción, se acumula su destino en la máscara sucia:
    $$\text{dirty\_mask} \leftarrow \text{dirty\_mask} \mid \text{dst\_mask}_i$$
  - El contador acumulado de la ventana suma `ready_insns`, reflejando cuántas instrucciones de la ventana pueden comenzar inmediatamente sin quedar bloqueadas por dependencias RAW previas dentro de la misma.
