#!/usr/bin/env python3
"""Genera la tabla de contadores por instruccion (una por forma de encoding).

La fuente de verdad es el listado `docs/rvv instr.md`: una fila por instruccion
con el nombre del contador en la columna `Counter`, agrupadas bajo los titulos
del documento. A partir de el se generan:

  * ``rave_sources/include/rvv/instr_counters.h`` - indice (RAVE_VINSTR_LIST),
    enum con el nombre de cada contador, arbol de categorias y API de busqueda.
  * ``rave_sources/src/rvv/instr_counters.c``     - tablas de nombres,
    mnemonicos y categorias + tabla hash (calculada aqui, para que la busqueda
    sea O(1) en ejecucion).

Uso:
    utils/gen_instr_counters.py [ruta/al/rvv instr.md]

Sin argumento usa ``<repo>/../docs/rvv instr.md``. Tras regenerar hay que
recompilar el plugin (``make -f makefiles/make_RAVE.mk ...``): el numero de
contadores y su orden cambian, y ``rave_counters.vinstr[][]`` se redimensiona.
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HDR = REPO / "rave_sources/include/rvv/instr_counters.h"
SRC = REPO / "rave_sources/src/rvv/instr_counters.c"
DEFAULT_MD = REPO.parent / "docs/rvv instr.md"

HASH_SIZE = 2048          # potencia de dos, >= 2 * nº de instrucciones

# Titulo del .md -> clave de la categoria en el JSON. Si se anade un titulo
# nuevo al listado hay que anadirlo aqui (el generador falla si falta).
CATEGORY_KEYS = {
    # --- nivel ## --------------------------------------------------------
    "Configuration-Setting Instructions": "configuration_setting",
    "Vector Loads and Stores": "vector_loads_and_stores",
    "Vector Integer Arithmetic Instructions": "vector_integer_arithmetic",
    "Vector Fixed-Point Arithmetic Instructions": "vector_fixed_point_arithmetic",
    "Vector Floating-Point Instructions": "vector_floating_point",
    "Vector Reduction Operations": "vector_reduction",
    "Vector Mask Instructions": "vector_mask",
    "Vector Permutation Instructions": "vector_permutation",
    "Alias y pseudo-instrucciones": "alias_and_pseudo_instructions",
    # --- cargas y almacenamientos ----------------------------------------
    "Vector Unit-Stride Instructions": "unit_stride",
    "Vector Strided Instructions": "strided",
    "Vector Indexed Instructions": "indexed",
    "Unit-stride Fault-Only-First Loads": "fault_only_first",
    "Vector Load/Store Segment Instructions": "segment",
    "Vector Unit-Stride Segment Loads and Stores": "unit_stride_segment",
    "Vector Strided Segment Loads and Stores": "strided_segment",
    "Vector Indexed Segment Loads and Stores": "indexed_segment",
    "Vector Load/Store Whole Register Instructions": "whole_register",
    # --- enteras ----------------------------------------------------------
    "Vector Single-Width Integer Add and Subtract": "single_width_add_subtract",
    "Vector Widening Integer Add/Subtract": "widening_add_subtract",
    "Vector Integer Extension": "integer_extension",
    "Vector Integer Add-with-Carry / Subtract-with-Borrow Instructions":
        "add_with_carry_subtract_with_borrow",
    "Vector Bitwise Logical Instructions": "bitwise_logical",
    "Vector Single-Width Shift Instructions": "single_width_shift",
    "Vector Narrowing Integer Right Shift Instructions": "narrowing_right_shift",
    "Vector Integer Compare Instructions": "integer_compare",
    "Vector Integer Min/Max Instructions": "integer_min_max",
    "Vector Single-Width Integer Multiply Instructions": "single_width_multiply",
    "Vector Integer Divide Instructions": "integer_divide",
    "Vector Widening Integer Multiply Instructions": "widening_multiply",
    "Vector Single-Width Integer Multiply-Add Instructions": "single_width_multiply_add",
    "Vector Widening Integer Multiply-Add Instructions": "widening_multiply_add",
    "Vector Integer Merge Instructions": "integer_merge",
    "Vector Integer Move Instructions": "integer_move",
    # --- coma fija --------------------------------------------------------
    "Vector Single-Width Saturating Add and Subtract": "single_width_saturating_add_subtract",
    "Vector Single-Width Averaging Add and Subtract": "single_width_averaging_add_subtract",
    "Vector Single-Width Fractional Multiply with Rounding and Saturation":
        "single_width_fractional_multiply",
    "Vector Single-Width Scaling Shift Instructions": "single_width_scaling_shift",
    "Vector Narrowing Fixed-Point Clip Instructions": "narrowing_fixed_point_clip",
    # --- coma flotante ----------------------------------------------------
    "Vector Single-Width Floating-Point Add/Subtract Instructions": "single_width_add_subtract",
    "Vector Widening Floating-Point Add/Subtract Instructions": "widening_add_subtract",
    "Vector Single-Width Floating-Point Multiply/Divide Instructions":
        "single_width_multiply_divide",
    "Vector Widening Floating-Point Multiply": "widening_multiply",
    "Vector Single-Width Floating-Point Fused Multiply-Add Instructions":
        "single_width_fused_multiply_add",
    "Vector Widening Floating-Point Fused Multiply-Add Instructions":
        "widening_fused_multiply_add",
    "Vector Floating-Point Square-Root Instruction": "square_root",
    "Vector Floating-Point Reciprocal Square-Root Estimate Instruction":
        "reciprocal_square_root_estimate",
    "Vector Floating-Point Reciprocal Estimate Instruction": "reciprocal_estimate",
    "Vector Floating-Point MIN/MAX Instructions": "min_max",
    "Vector Floating-Point Sign-Injection Instructions": "sign_injection",
    "Vector Floating-Point Compare Instructions": "compare",
    "Vector Floating-Point Classify Instruction": "classify",
    "Vector Floating-Point Merge Instruction": "merge",
    "Vector Floating-Point Move Instruction": "move",
    "Single-Width Floating-Point/Integer Type-Convert Instructions":
        "single_width_type_convert",
    "Widening Floating-Point/Integer Type-Convert Instructions": "widening_type_convert",
    "Narrowing Floating-Point/Integer Type-Convert Instructions": "narrowing_type_convert",
    # --- reducciones ------------------------------------------------------
    "Vector Single-Width Integer Reduction Instructions": "single_width_integer",
    "Vector Widening Integer Reduction Instructions": "widening_integer",
    "Vector Single-Width Floating-Point Reduction Instructions": "single_width_floating_point",
    "Vector Widening Floating-Point Reduction Instructions": "widening_floating_point",
    # --- mascaras ---------------------------------------------------------
    "Vector Mask-Register Logical Instructions": "logical",
    "Vector count population in mask vcpop.m": "vcpop",
    "vfirst find-first-set mask bit": "vfirst",
    "vmsbf.m set-before-first mask bit": "vmsbf",
    "vmsif.m set-including-first mask bit": "vmsif",
    "vmsof.m set-only-first mask bit": "vmsof",
    "Vector Iota Instruction": "iota",
    "Vector Element Index Instruction": "element_index",
    # --- permutaciones ----------------------------------------------------
    "Integer Scalar Move Instructions": "integer_scalar_move",
    "Floating-Point Scalar Move Instructions": "floating_point_scalar_move",
    "Vector Slide Instructions": "slide",
    "Vector Register Gather Instructions": "register_gather",
    "Vector Compress Instruction": "compress",
    "Whole Vector Register Move": "whole_register_move",
    # --- alias y pseudo-instrucciones -------------------------------------
    "Pseudo-instrucciones aritméticas y lógicas": "arithmetic_and_logical",
    "Pseudo-instrucciones de comparación entera": "integer_compare",
    "Pseudo-instrucciones de comparación FP": "floating_point_compare",
    "Pseudo-instrucciones de lógica de máscaras": "mask_logic",
    "Alias de compatibilidad conservados por la especificación": "compatibility",
}


def c_id(mnemonic):
    """vadd.vv -> vadd_vv_counter ; vl1r.v -> vl1r_v_counter"""
    return re.sub(r"[^0-9a-zA-Z_]", "_", mnemonic) + "_counter"


def fnv1a(text):
    h = 2166136261
    for ch in text.encode("utf-8"):
        h ^= ch
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def read_listing(path):
    """Devuelve (mnemonicos, categoria de cada uno, arbol, indice de indexed).

    El arbol es una lista de (clave, indice del padre) en orden de aparicion;
    las categorias de nivel superior llevan padre -1, de forma que los padres
    siempre van antes que sus hijos.

    Las instrucciones indexadas (las de "Vector Indexed Instructions" y las de
    "Vector Indexed Segment Loads and Stores") cuelgan todas de la categoria
    `indexed`: el emisor del JSON las suma por prefijo del mnemonico en dos
    claves, `load` (`vl*`) y `store` (`vs*`), sin tablas auxiliares. La
    categoria `indexed_segment` se crea pero se queda sin contadores, asi que
    el emisor no la imprime (solo saca categorias con total > 0).
    """
    mnemonics, instr_cat, categories = [], [], []
    stack = []                       # (nivel del titulo, indice en categories)
    seen_titles = []
    idx_indexed = None

    for line in path.read_text(encoding="utf-8").splitlines():
        heading = re.match(r"^(#+) (.+?)\s*$", line)
        if heading:
            level, title = len(heading.group(1)), heading.group(2)
            if title not in CATEGORY_KEYS:
                sys.exit(f"ERROR: el titulo {title!r} no esta en CATEGORY_KEYS")
            seen_titles.append(title)
            while stack and stack[-1][0] >= level:
                stack.pop()
            parent = stack[-1][1] if stack else -1
            categories.append((CATEGORY_KEYS[title], parent))
            stack.append((level, len(categories) - 1))
            if CATEGORY_KEYS[title] == "indexed":
                idx_indexed = len(categories) - 1
            continue
        row = re.match(r"^\|[☑☒☐]\|`([^`]+)`\|", line)
        if not row:
            continue
        mnemonic = row.group(1)
        found = re.findall(r"([a-z0-9_]+_counter)", line)
        if not found or found[0] != c_id(mnemonic):
            sys.exit(f"ERROR: la fila de {mnemonic} no lleva su <mnemonico>_counter")
        if not stack:
            sys.exit(f"ERROR: {mnemonic} no esta bajo ningun titulo")

        abiertas = {categories[idx][0] for _, idx in stack}
        if "indexed" in abiertas or "indexed_segment" in abiertas:
            if idx_indexed is None:
                sys.exit("ERROR: fila indexada antes de la seccion 'indexed'")
            if not (mnemonic.startswith("vl") or mnemonic.startswith("vs")):
                sys.exit(f"ERROR: no se si {mnemonic} es load o store")
            # cuelga de `indexed`: el emisor lo suma en load o store
            mnemonics.append(mnemonic)
            instr_cat.append(idx_indexed)
            continue

        mnemonics.append(mnemonic)
        instr_cat.append(stack[-1][1])

    if len(mnemonics) != len(set(mnemonics)):
        sys.exit("ERROR: hay mnemonicos repetidos en el listado")
    if len(seen_titles) != len(set(seen_titles)):
        sys.exit("ERROR: hay titulos repetidos en el listado")
    if set(seen_titles) != set(CATEGORY_KEYS):
        sys.exit("ERROR: CATEGORY_KEYS y el listado no coinciden: faltan "
                 f"{sorted(set(CATEGORY_KEYS) - set(seen_titles))}, sobran "
                 f"{sorted(set(seen_titles) - set(CATEGORY_KEYS))}")
    # Las claves solo tienen que ser unicas entre hermanas: cada categoria se
    # anida dentro de su padre en el JSON, asi que `integer_compare` puede
    # existir a la vez en enteras y en las pseudo-instrucciones de comparacion.
    siblings = {}
    for key, parent in categories:
        seen = siblings.setdefault(parent, set())
        if key in seen:
            sys.exit(f"ERROR: clave de categoria repetida en el mismo nivel: {key}")
        seen.add(key)
    return (mnemonics, instr_cat, categories,
            idx_indexed if idx_indexed is not None else -1)


def build_hash(mnemonics):
    """Tabla hash abierta con probing lineal, resuelta al generar el fichero."""
    slots = [-1] * HASH_SIZE
    for idx, mn in enumerate(mnemonics):
        h = fnv1a(mn) & (HASH_SIZE - 1)
        while slots[h] != -1:
            h = (h + 1) & (HASH_SIZE - 1)
        slots[h] = idx
    return slots


def header_banner():
    return ("/*********************************************************/\n"
            "// Contador propio por instruccion vectorial (RVV 1.0).\n"
            "// FICHERO GENERADO por utils/gen_instr_counters.py desde\n"
            '// "docs/rvv instr.md" -- no editar a mano.\n'
            "/*********************************************************/\n")


def write_header(mnemonics, categories, idx_indexed):
    ids = [c_id(m) for m in mnemonics]
    with open(HDR, "w", encoding="utf-8") as f:
        f.write(header_banner())
        f.write("#pragma once\n\n#include <stdint.h>\n\n")
        f.write(f"#define RAVE_VINSTR_COUNT {len(mnemonics)}\n")
        f.write(f"#define RAVE_VINSTR_NCAT {len(categories)}\n")
        f.write(f"#define RAVE_VINSTR_HASH_SIZE {HASH_SIZE}\n\n")
        f.write("/* Una entrada por forma de encoding: (identificador, mnemonico). */\n")
        f.write("#define RAVE_VINSTR_LIST(X) \\\n")
        for name, mn in zip(ids, mnemonics):
            f.write(f'    X({name}, "{mn}") \\\n')
        f.write("\n")
        f.write("/* Indice de cada contador: es el indice dentro de rave_counters.vinstr[][]. */\n")
        f.write("enum rave_vinstr_id {\n")
        f.write("#define RAVE_VINSTR_ENUM(name, mnem) name,\n")
        f.write("    RAVE_VINSTR_LIST(RAVE_VINSTR_ENUM)\n")
        f.write("#undef RAVE_VINSTR_ENUM\n};\n\n")
        f.write("/* Arbol de categorias: los mismos niveles que docs/rvv instr.md.\n")
        f.write(" * Cada nodo es (clave en el JSON, indice del padre); -1 = nivel superior. */\n")
        f.write("typedef struct { const char *name; short parent; } rave_vinstr_category;\n")
        f.write("extern const rave_vinstr_category rave_vinstr_categories[RAVE_VINSTR_NCAT];\n\n")
        f.write("/* Indice de la categoria `indexed`: sus contadores se suman en el\n")
        f.write(" * JSON en dos claves, load (mnemonicos vl*) y store (vs*). */\n")
        f.write(f"#define RAVE_VINSTR_CAT_INDEXED {idx_indexed}\n\n")
        f.write('/* Indice de la instruccion a partir de su mnemonico ("vadd.vv"),\n')
        f.write(" * o -1 si no es una instruccion vectorial conocida. */\n")
        f.write("int rave_vinstr_lookup(const char *mnemonic);\n\n")
        f.write('/* Nombre del contador ("vadd_vv_counter") y mnemonico del indice. */\n')
        f.write("const char *rave_vinstr_name(int idx);\n")
        f.write("const char *rave_vinstr_mnemonic(int idx);\n\n")
        f.write("/* Categoria hoja del contador (indice en rave_vinstr_categories). */\n")
        f.write("short rave_vinstr_category_of(int idx);\n")


def write_source(mnemonics, instr_cat, categories, slots):
    with open(SRC, "w", encoding="utf-8") as f:
        f.write(header_banner() + "\n")
        f.write('#include "instr_counters.h"\n#include <string.h>\n\n')
        f.write("static const char * const rave_vinstr_names[RAVE_VINSTR_COUNT] = {\n")
        f.write("#define RAVE_VINSTR_NAME(name, mnem) #name,\n")
        f.write("    RAVE_VINSTR_LIST(RAVE_VINSTR_NAME)\n")
        f.write("#undef RAVE_VINSTR_NAME\n};\n\n")
        f.write("static const char * const rave_vinstr_mnemonics[RAVE_VINSTR_COUNT] = {\n")
        f.write("#define RAVE_VINSTR_MNEM(name, mnem) mnem,\n")
        f.write("    RAVE_VINSTR_LIST(RAVE_VINSTR_MNEM)\n")
        f.write("#undef RAVE_VINSTR_MNEM\n};\n\n")

        f.write("/* Arbol de categorias del listado (los padres van antes que los hijos). */\n")
        f.write("const rave_vinstr_category rave_vinstr_categories[RAVE_VINSTR_NCAT] = {\n")
        for key, parent in categories:
            f.write(f'    {{ "{key}", {parent} }},\n')
        f.write("};\n\n")

        f.write("/* Categoria hoja de cada contador. */\n")
        f.write("static const short rave_vinstr_instr_category[RAVE_VINSTR_COUNT] = {\n")
        for i in range(0, len(instr_cat), 20):
            chunk = ", ".join(str(v) for v in instr_cat[i:i + 20])
            f.write(f"    {chunk},\n")
        f.write("};\n\n")

        f.write("/* Indice dentro de la tabla para cada cubo (probing lineal), calculado\n")
        f.write(" * al generar el fichero para que la busqueda sea O(1) en ejecucion. */\n")
        f.write("static const short rave_vinstr_slots[RAVE_VINSTR_HASH_SIZE] = {\n")
        for i in range(0, HASH_SIZE, 16):
            chunk = ", ".join(str(v) for v in slots[i:i + 16])
            f.write(f"    {chunk},\n")
        f.write("};\n\n")

        f.write("static uint32_t rave_vinstr_hash(const char *s){\n")
        f.write("    uint32_t h = 2166136261u;   /* FNV-1a */\n")
        f.write("    for (; *s; ++s) { h ^= (unsigned char)*s; h *= 16777619u; }\n")
        f.write("    return h;\n}\n\n")

        f.write("int rave_vinstr_lookup(const char *mnemonic){\n")
        f.write("    uint32_t slot;\n    int probe;\n")
        f.write("    if (mnemonic == NULL || mnemonic[0] != 'v') return -1;\n")
        f.write("    slot = rave_vinstr_hash(mnemonic) & (RAVE_VINSTR_HASH_SIZE - 1);\n")
        f.write("    for (probe = 0; probe < RAVE_VINSTR_HASH_SIZE; ++probe) {\n")
        f.write("        short idx = rave_vinstr_slots[(slot + probe) & (RAVE_VINSTR_HASH_SIZE - 1)];\n")
        f.write("        if (idx < 0) return -1;\n")
        f.write("        if (strcmp(rave_vinstr_mnemonics[idx], mnemonic) == 0) return idx;\n")
        f.write("    }\n    return -1;\n}\n\n")

        f.write("const char *rave_vinstr_name(int idx){\n")
        f.write("    return (idx >= 0 && idx < RAVE_VINSTR_COUNT) ? rave_vinstr_names[idx] : NULL;\n}\n\n")
        f.write("const char *rave_vinstr_mnemonic(int idx){\n")
        f.write("    return (idx >= 0 && idx < RAVE_VINSTR_COUNT) ? rave_vinstr_mnemonics[idx] : NULL;\n}\n\n")
        f.write("short rave_vinstr_category_of(int idx){\n")
        f.write("    return (idx >= 0 && idx < RAVE_VINSTR_COUNT) ? rave_vinstr_instr_category[idx] : -1;\n}\n")


def main():
    md = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_MD
    if not md.is_file():
        sys.exit(f"ERROR: no encuentro el listado de instrucciones: {md}")
    mnemonics, instr_cat, categories, idx_indexed = read_listing(md)
    write_header(mnemonics, categories, idx_indexed)
    write_source(mnemonics, instr_cat, categories, build_hash(mnemonics))
    print(f"{len(mnemonics)} contadores en {len(categories)} categorias desde {md}")
    print(" ", HDR.relative_to(REPO))
    print(" ", SRC.relative_to(REPO))


if __name__ == "__main__":
    main()
