# Clasificación Extendida de Instrucciones (`type_ext`)

En la rama `main`, RAVE clasifica las instrucciones vectoriales utilizando una palabra de 16 bits (`type`) basada en categorías genéricas:
- `T_VECTOR` / `T_SCALAR` / `T_VSETVL`
- Subtipos: `T_ARITH`, `T_MEMORY`, `T_REDUCTION`, `T_MASK`
- Subsubtipos de memoria: `T_UNIT`, `T_STRIDE`, `T_INDEX`, `T_SPILL`
- Subsubtipos aritméticos: `T_INT`, `T_FP`, `T_FUSED`

Para este análisis, esta clasificación resultaba insuficiente al no distinguir transformaciones de anchura de elemento (widening / narrowing), tipos de permutación ni accesos a memoria segmentados o desordenados, entre otros.

En la rama `feature/extended`, se introduce el campo `uint16_t type_ext` en [`struct instr_data`](../../rave_sources/include/rvv/instr_data.h#L73) y la función [`instr_set_vector_type_ext()`](../../rave_sources/src/rvv/instr_data.c#L175).

---

## Mapa de Bits de `type_ext`

El valor de `type_ext` codifica de forma ortogonal distintas dimensiones de la instrucción mediante el siguiente mapa de bits:

```
 15    13  12    11  10    9     8     7     6     5     4     3     2     1     0
┌────────┬─────┬─────┬─────┬─────┬───────────┬─────────────────┬───────────┬───────────┐
│ Reserv │ S1/ │ UP/ │ FP/ │ MSK │ Transfer  │ Operación Arit. │ Subtipo   │ Clase     │
│        │ SN1 │ DWN │ INT │     │ Registro  │ / Orden Memoria │ Arit/Mem  │ Vectorial │
└────────┴─────┴─────┴─────┴─────┴───────────┴─────────────────┴───────────┴───────────┘
```

| Rango de Bits | Nombre Campo | Máscara | Valores Posibles y Descripción |
| :--- | :--- | :--- | :--- |
| **Bits 1:0** | Clase Base | `MASK_EXT_BASE` (`0x0003`) | `TYPE_EXT_ARITH` (`00`-`0x0000`): Operación aritmética.<br>`TYPE_EXT_LOAD` (`01`-`0x0001`): Carga de memoria.<br>`TYPE_EXT_STORE` (`10`-`0x0002`): Almacenamiento en memoria. |
| **Bits 3:2** | Subtipo Aritmética / Memoria | `MASK_EXT_SUBTYPE` (`0x000C`) | **Aritmética:**<br>• `TYPE_EXT_ARITH_NORMAL` (`00`-`0x0000`): Operación estándar.<br>• `TYPE_EXT_ARITH_WIDENING` (`01`-`0x0004`): Operación ensanchadora (SEW destino = 2 * SEW origen).<br>• `TYPE_EXT_ARITH_NARROWING` (`10`-`0x0008`): Operación estrechadora (SEW destino = SEW origen / 2).<br><br>**Memoria:**<br>• `TYPE_EXT_MEMORY_NORMAL` (`0x0004`): Acceso simple regular.<br>• `TYPE_EXT_MEMORY_SEGMENTED` (`0x0004`): Acceso segmentado estructurado (`vlseg*` / `vsseg*`). |
| **Bits 6:4** | Operación Aritmética Específica / Orden de Memoria | `MASK_EXT_SUBSUBTYPE` (`0x0070`) | **Aritmética:**<br>• `TYPE_EXT_ARITH_COMPUTATION` (`000`-`0x0000`): ALU genérica (add, sub, etc.).<br>• `TYPE_EXT_ARITH_COMP_FUSED` (`101`-`0x0050`): Multiplicación acumulada fusionada (FMA / FMS).<br>• `TYPE_EXT_ARITH_REDUCTION` (`010`-`0x0020`): Reducción vectorial a escalar.<br>• `TYPE_EXT_ARITH_MASK` (`100`-`0x0040`): Conteo/operación sobre vector máscara (`vcpop.m`, `vfirst.m`).<br>• `TYPE_EXT_ARITH_MOVE` (`001`-`0x0010`): Movimiento de datos entre registros.<br>• `TYPE_EXT_ARITH_SLIDE` (`011`-`0x0030`): Desplazamiento (`vslideup`, `vslidedown`, etc.).<br>• `TYPE_EXT_ARITH_COMPRESS` (`110`-`0x0060`): Compactación condicional (`vcompress`).<br>• `TYPE_EXT_ARITH_GATHER` (`111`-`0x0070`): Lectura arbitraria por índice (`vrgather`, `vrgatherei16`).<br><br>**Memoria Indexada:**<br>• `TYPE_EXT_MEMORY_UNORDERED` (`000`-`0x0000`): Carga/almacenamiento no ordenado (`luxei*` / `suxei*`).<br>• `TYPE_EXT_MEMORY_ORDERED` (`001`-`0x0010`): Carga/almacenamiento ordenado (`loxei*` / `soxei*`). |
| **Bits 8:7** | Transferencia de Registros | `MASK_EXT_OPERAND` (`0x0180`) | • `TYPE_EXT_VECTOR_VECTOR` (`00`-`0x0000`): Operación exclusivamente entre vectores.<br>• `TYPE_EXT_WRITE_SCALAR` (`01`-`0x0080`): El destino es un registro escalar (`vmv.x.s`, `vfmv.f.s`, etc.).<br>• `TYPE_EXT_READ_SCALAR` (`10`-`0x0100`): Lee de un registro escalar (`vmv.s.x`, `vfmv.s.f`, etc.).<br>• `TYPE_EXT_WHOLE_REGISTER` (`11`-`0x0180`): Mueve registros vectoriales completos (`vmv1r.v`..`vmv8r.v`). |
| **Bit 9** | Máscara Activa | `TYPE_EXT_MASKED` (`0x0200`) | `1`: Instrucción enmascarada condicionalmente.<br>`0`: Instrucción no enmascarada. |
| **Bit 10** | Tipo de Dato | `TYPE_EXT_FP` (`0x0400`) / `TYPE_EXT_INT` (`0x0000`) | `1`: Operación en coma flotante.<br>`0`: Operación entera / lógica. |
| **Bit 11** | Dirección de Slide | `TYPE_EXT_SLIDE_UP` (`0x0800`) / `TYPE_EXT_SLIDE_DOWN` (`0x0000`) | `1`: Desplazamiento hacia arriba (`slideup`).<br>`0`: Desplazamiento hacia abajo (`slidedown`). |
| **Bit 12** | Amplitud de Slide | `TYPE_EXT_SLIDE_1` (`0x1000`) / `TYPE_EXT_SLIDE_N1` (`0x0000`) | `1`: Desplazamiento unitario de un solo elemento (`slide1up`, `slide1down`, `vfslide1up`, etc.).<br>`0`: Desplazamiento general por vector o inmediato. |

### Macro de Comprobación

Para consultar los atributos se utiliza la macro:

```c
#define is_type_ext(x, mask, y) (((x) & (mask)) == (y))
```

Por ejemplo:
```c
// ¿Es una instrucción de permutación de tipo gather?
if (is_type_ext(instr->type_ext, MASK_EXT_SUBSUBTYPE, TYPE_EXT_ARITH_GATHER)) { ... }

// ¿Es una instrucción widening?
if (is_type_ext(instr->type_ext, MASK_EXT_SUBTYPE, TYPE_EXT_ARITH_WIDENING)) { ... }
```

---

## Lógica de Decodificación y Desensamblado

La función [`instr_set_vector_type_ext()`](../../rave_sources/src/rvv/instr_data.c#L175) combina el análisis de los campos de bits del opcode binario con comprobaciones del string desensamblado (`instr_asm`):

1. **Widening y Narrowing**:
   - Se detectan comprobando los primeros caracteres del mnemónico:
     - `vn...` o `vfn...` $\rightarrow$ `TYPE_EXT_ARITH_NARROWING`.
     - `vw...` o `vfw...` $\rightarrow$ `TYPE_EXT_ARITH_WIDENING`.
     - Resto $\rightarrow$ `TYPE_EXT_ARITH_NORMAL`.

2. **Operaciones Fusionadas (FMA / FMS)**:
   - Se examina la presencia de subcadenas `"macc"`, `"madd"`, `"msac"`, `"msub"` en el nombre de la instrucción para clasificarlas como `TYPE_EXT_ARITH_COMP_FUSED`.

3. **Familia de Permutaciones**:
   - **Moves (`TYPE_EXT_ARITH_MOVE`)**: Identificadas mediante `funct6 == 0x10` (`vmv.x.s`, `vmv.s.x`, `vfmv`), `funct6 == 0x27` (`vmv1r.v`..`vmv8r.v` con `WHOLE_REGISTER`), y `funct6 == 0x17` (`vmv.v.v`, `vmv.v.x`).
   - **Slides (`TYPE_EXT_ARITH_SLIDE`)**: Identificadas con `funct6 == 14` o `15` (`vslideup`, `vslidedown`, `vslide1up`, `vslide1down`, `vfslide1up`, etc.).
   - **Gather (`TYPE_EXT_ARITH_GATHER`)**: Identificadas con `funct6 == 0b001100` (`vrgather`) y `funct6 == 0b001110` (`vrgatherei16`).
   - **Compress (`TYPE_EXT_ARITH_COMPRESS`)**: Identificadas con `funct6 == 0b010111` y `funct3 == 0b010` (`vcompress`).

4. **Memoria Segmentada**:
   - Se extrae el campo `nf` (número de campos del segmento) en los bits `31:29` de la instrucción (`MAJOR_LOAD` y `MAJOR_STORE`):
     ```c
     int nf = (insn_opcode >> 29) & 0x7;
     if (nf > 0) {
         type_ext |= TYPE_EXT_MEMORY_SEGMENTED;
     }
     ```
   - Esto permite clasificar correctamente instrucciones como `vlseg2e32.v`, `vsseg4e64.v`, etc.

5. **Memoria Indexada Ordenada vs Desordenada**:
   - En instrucciones indexadas se analiza el bit 20 (`mop`): si es ordenada (`loxei` / `soxei`), se marca `TYPE_EXT_MEMORY_ORDERED`; si es desordenada (`luxei` / `suxei`), se marca `TYPE_EXT_MEMORY_UNORDERED`.

---

## Impacto en el Informe de Estadísticas

Gracias a esta clasificación, el informe generado en [`counters.c`](../../rave_sources/src/rvv/counters.c#L125) desglosa jerárquicamente:

```text
Arith: X (Y%)
  FP: ...
  INT: ...
  Widening: X (Y%)
    Fused: ...
  Narrowing: X (Y%)
  Normal: X (Y%)
    Permutations: X (Y%)
      Moves: X (Y%)
        Read from scalar: ...
        Write to scalar: ...
        Whole register: ...
        Others: ...
      Slides: X (Y%)
        slideup: ...
        slide1up: ...
        slidedown: ...
        slide1down: ...
      Gather: ...
      Compress: ...
    Mask: ...
    Reductions: ...
      FP: ...
      INT: ...
    Computation: ...
      Fused: ...
  Scalar deps (excl. moves):
    Read from scalar: ...
    Write to scalar: ...
Memory: X (Y%)
  unit: X (Y%)
    Segmented: X (Y%)
  strided: X (Y%)
    Segmented: X (Y%)
    Avg. Stride (B): ...
    Std. Dev. Stride (B): ...
  indexed: X (Y%)
    Segmented: X (Y%)
    Ordered: X (Y%)
    Unordered: X (Y%)
  whole-register: X (Y%)
```
