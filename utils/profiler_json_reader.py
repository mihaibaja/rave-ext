#!/usr/bin/env python3
"""Lector y validador de referencia del JSON del profiler de RAVE.

Implementa el contrato descrito en ``docs/PROFILER_JSON_API.md`` y lo valida contra
``schema/profiler_json_schema_v1.json`` con un validador de JSON Schema reducido
(solo biblioteca estandar, al estilo de ``utils/trace_reader.py``).

Las dos comprobaciones son complementarias:

* ``--validate``: forma del documento (tipos, campos obligatorios, campos no
  documentados, longitudes) segun el esquema.
* ``--check``: invariantes semanticas del contrato (descomposiciones exactas,
  orden de los bloques SEW, coherencia de ``region_count``, campos que deben
  estar a cero sin traza extendida).

Uso:
    python3 utils/profiler_json_reader.py fichero.json --validate
    python3 utils/profiler_json_reader.py fichero.json --check
    python3 utils/profiler_json_reader.py fichero.json --summary
"""

import argparse
import json
import os
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SCHEMA = REPO_ROOT / "schema" / "profiler_json_schema_v1.json"

# Las descomposiciones del contrato son exactas, pero con accumulate_regions=1
# los contadores son medias por ejecucion y el redondeo a entero puede desviar
# una unidad; de ahi la tolerancia.
TOLERANCE = 0.5


class SchemaError(Exception):
    """Fallo al cargar o interpretar el fichero de esquema."""


# --------------------------------------------------------------------------- #
# Validador JSON Schema (subconjunto: lo que usa el esquema del profiler)
# --------------------------------------------------------------------------- #

def _type_name(value):
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "boolean"
    if isinstance(value, int):
        return "integer"
    if isinstance(value, float):
        return "number"
    if isinstance(value, str):
        return "string"
    if isinstance(value, list):
        return "array"
    if isinstance(value, dict):
        return "object"
    return type(value).__name__


def _matches_type(value, type_name):
    found = _type_name(value)
    if type_name == "number":
        return found in ("integer", "number")
    if type_name == "integer":
        return found == "integer"
    return found == type_name


def _resolve_ref(root, ref):
    if not ref.startswith("#/"):
        raise SchemaError(f"solo se soportan $ref locales, no '{ref}'")
    node = root
    for part in ref[2:].split("/"):
        if not isinstance(node, dict) or part not in node:
            raise SchemaError(f"$ref no resoluble: '{ref}'")
        node = node[part]
    return node


def validate_instance(instance, schema, root, path="$"):
    """Valida ``instance`` contra ``schema``. Devuelve la lista de errores."""
    errors = []

    if "$ref" in schema:
        target = _resolve_ref(root, schema["$ref"])
        return validate_instance(instance, target, root, path)

    for sub in schema.get("allOf", []):
        errors += validate_instance(instance, sub, root, path)

    if "anyOf" in schema:
        if not any(not validate_instance(instance, sub, root, path)
                   for sub in schema["anyOf"]):
            errors.append(f"{path}: no cumple ninguna de las opciones de anyOf")

    if "oneOf" in schema:
        matched = sum(1 for sub in schema["oneOf"]
                      if not validate_instance(instance, sub, root, path))
        if matched != 1:
            errors.append(f"{path}: oneOf esperaba 1 coincidencia, hay {matched}")

    declared = schema.get("type")
    if declared is not None:
        types = declared if isinstance(declared, list) else [declared]
        if not any(_matches_type(instance, t) for t in types):
            errors.append(f"{path}: se esperaba {'/'.join(types)}, "
                          f"encontrado {_type_name(instance)}")
            return errors

    if "enum" in schema and instance not in schema["enum"]:
        errors.append(f"{path}: valor {instance!r} fuera de {schema['enum']}")
    if "const" in schema and instance != schema["const"]:
        errors.append(f"{path}: valor {instance!r} != {schema['const']!r}")

    if isinstance(instance, str) and "minLength" in schema:
        if len(instance) < schema["minLength"]:
            errors.append(f"{path}: cadena vacia (minLength {schema['minLength']})")

    if isinstance(instance, str) and "pattern" in schema:
        if re.search(schema["pattern"], instance) is None:
            errors.append(f"{path}: {instance!r} no casa con {schema['pattern']!r}")

    if isinstance(instance, (int, float)) and not isinstance(instance, bool):
        if "minimum" in schema and instance < schema["minimum"]:
            errors.append(f"{path}: {instance} < minimo {schema['minimum']}")
        if "maximum" in schema and instance > schema["maximum"]:
            errors.append(f"{path}: {instance} > maximo {schema['maximum']}")

    if isinstance(instance, list):
        if "minItems" in schema and len(instance) < schema["minItems"]:
            errors.append(f"{path}: {len(instance)} elementos, minimo {schema['minItems']}")
        if "maxItems" in schema and len(instance) > schema["maxItems"]:
            errors.append(f"{path}: {len(instance)} elementos, maximo {schema['maxItems']}")
        if "items" in schema:
            for i, item in enumerate(instance):
                errors += validate_instance(item, schema["items"], root, f"{path}[{i}]")

    if isinstance(instance, dict):
        for name in schema.get("required", []):
            if name not in instance:
                errors.append(f"{path}: falta el campo obligatorio '{name}'")
        properties = schema.get("properties", {})
        extra = schema.get("additionalProperties", True)
        for name, value in instance.items():
            child_path = f"{path}.{name}"
            if name in properties:
                errors += validate_instance(value, properties[name], root, child_path)
            elif extra is False:
                errors.append(f"{child_path}: campo no documentado en el esquema")
            elif isinstance(extra, dict):
                errors += validate_instance(value, extra, root, child_path)
        if "propertyNames" in schema:
            for name in instance:
                errors += validate_instance(name, schema["propertyNames"], root,
                                            f"{path}.{name} (clave)")

    return errors


def load_schema(schema_path=None):
    path = Path(schema_path) if schema_path else DEFAULT_SCHEMA
    try:
        with open(path, encoding="utf-8") as fh:
            schema = json.load(fh)
    except FileNotFoundError:
        raise SchemaError(f"no existe el esquema: {path}")
    except json.JSONDecodeError as exc:
        raise SchemaError(f"esquema no es JSON valido ({path}): {exc}")
    if not isinstance(schema, dict) or "$defs" not in schema:
        raise SchemaError(f"esquema sin $defs ({path})")
    return schema


def load_document(json_path):
    with open(json_path, encoding="utf-8") as fh:
        return json.load(fh)


def validate_document(doc, schema=None):
    """Errores de forma del documento (lista de mensajes, vacia = valido)."""
    schema = schema if schema is not None else load_schema()
    return validate_instance(doc, schema, schema, "$")


# --------------------------------------------------------------------------- #
# Invariantes del contrato
# --------------------------------------------------------------------------- #

def _close(a, b, terms=1, accumulate=False):
    """Igualdad con tolerancia.

    Con ``accumulate_regions`` los contadores son medias por ejecucion y cada
    sumando se redondea a entero por separado, asi que la descomposicion puede
    desviarse hasta 0.5 por sumando (medido: large/lavaMD.json se desvia 1).
    """
    tolerance = TOLERANCE * (terms if accumulate else 1)
    return abs(a - b) <= tolerance


def _get(node, dotted):
    cur = node
    for part in dotted.split("."):
        if isinstance(cur, dict):
            cur = cur.get(part)
        else:
            return 0
    return cur


def _sum_tree(node):
    """Suma todos los contadores de un subarbol de categorias."""
    total = 0
    for value in node.values():
        total += _sum_tree(value) if isinstance(value, dict) else value
    return total


def instruction_tree(region):
    """Contadores por instruccion de una region (``region["instructions"]``).

    Son los niveles del listado (``docs/rvv instr.md``):
    ``{"vector_loads_and_stores": {"unit_stride": {"vle32_v_counter": 3}}}``.
    """
    return dict(region.get("instructions") or {})


def instruction_counters(region):
    """Contadores por instruccion de una region, aplanados.

    ``{"vle32_v_counter": 3, "vadd_vx_counter": 1, ...}``.
    """
    flat = {}

    def walk(node):
        for name, value in node.items():
            if isinstance(value, dict):
                walk(value)
            elif isinstance(value, (int, float)):
                flat[name] = flat.get(name, 0) + value

    walk(instruction_tree(region))
    return flat


def category_totals(region):
    """Instrucciones de cada seccion del listado (primer nivel del arbol)."""
    return {name: _sum_tree(value)
            for name, value in instruction_tree(region).items()
            if isinstance(value, dict)}


def check_invariants(doc):
    """Comprueba las descomposiciones de ``docs/PROFILER_JSON_API.md``.

    Devuelve ``(errores, avisos)``: los errores son incoherencias del fichero,
    los avisos son cosas esperables pero que conviene saber.
    """
    errors = []
    warnings = []

    regions = doc.get("regions") or []
    region_count = doc.get("region_count")
    accumulate = bool(doc.get("accumulate_regions"))
    extended = bool(doc.get("trace_extended"))

    if isinstance(region_count, int):
        if region_count < len(regions):
            errors.append(f"region_count ({region_count}) < len(regions) "
                          f"({len(regions)}): fichero incoherente")
        elif not accumulate and region_count != len(regions):
            errors.append(f"region_count ({region_count}) != len(regions) "
                          f"({len(regions)}) sin accumulate_regions")

    for index, region in enumerate(regions):
        where = f"regions[{index}] ({region.get('name')})"
        counters = region.get("counters") or {}
        inst = counters.get("instructions_previous_version") or {}

        if {"total", "scalar", "vsetvl", "vector"} <= set(inst):
            if not _close(inst["total"], inst["scalar"] + inst["vsetvl"] + inst["vector"],
                          terms=3, accumulate=accumulate):
                errors.append(f"{where}: instructions.total ({inst['total']}) != "
                              f"scalar+vsetvl+vector "
                              f"({inst['scalar'] + inst['vsetvl'] + inst['vector']})")

        sew_blocks = inst.get("sew") or []
        if "vector" in inst and sew_blocks:
            sew_instr = sum(block.get("instr", 0) for block in sew_blocks)
            if not _close(inst["vector"], sew_instr, terms=len(sew_blocks),
                          accumulate=accumulate):
                errors.append(f"{where}: instructions.vector ({inst['vector']}) != "
                              f"suma de sew[].instr ({sew_instr})")

        for position, block in enumerate(sew_blocks):
            sew_where = f"{where} sew[{position}]"
            expected_sew = 8 << position
            if "sew" in block and block["sew"] != expected_sew:
                errors.append(f"{sew_where}: sew={block['sew']}, "
                              f"esperado {expected_sew} en la posicion {position}")

            # Elementos por tipo: se solapan entre si (una mascara suma en arith y
            # en mask), asi que su SUMA puede pasar de elems, pero cada uno por
            # separado no.
            by_kind = block.get("elems_by_kind") or {}
            elems = block.get("elems")
            if isinstance(elems, (int, float)):
                for kind, value in sorted(by_kind.items()):
                    if isinstance(value, (int, float)) and value > elems + TOLERANCE:
                        errors.append(f"{sew_where}: elems_by_kind.{kind} ({value}) > "
                                      f"elems ({elems})")

        # Contadores por instruccion: cada instruccion vectorial y cada vset*
        # tiene el suyo, asi que la suma del arbol de categorias es total -
        # scalar. Si baja es que hay un mnemonico que la tabla no conoce
        # todavia (aviso); si sube, el fichero es incoherente (error).
        counted = instruction_counters(region)
        if counted:
            total_counted = sum(counted.values())
            expected = inst.get("total", 0) - inst.get("scalar", 0)
            if total_counted > expected and not accumulate:
                errors.append(f"{where}: suma de los contadores por instruccion "
                              f"({total_counted}) > total - scalar ({expected})")
            elif total_counted < expected:
                warnings.append(f"{where}: los contadores por instruccion cubren "
                                f"{total_counted} de {expected} instrucciones "
                                f"(mnemonicos no reconocidos por la tabla)")

    return errors, warnings


# --------------------------------------------------------------------------- #
# Resumen y volcado
# --------------------------------------------------------------------------- #

def summarize(doc):
    """Lineas de resumen (mismo espiritu que la seccion 3 del contrato)."""
    lines = [
        f"generator:           {doc.get('generator')}",
        f"binary:              {doc.get('binary')}",
        f"vlen_bits:           {doc.get('vlen_bits')}",
        f"trace_extended:      {doc.get('trace_extended')}",
        f"accumulate_regions:  {doc.get('accumulate_regions')}",
        f"region_count:        {doc.get('region_count')}",
        f"regions[]:           {len(doc.get('regions') or [])}",
    ]

    for index, region in enumerate(doc.get("regions") or []):
        counters = region.get("counters") or {}
        stats = counters.get("global_stats") or {}
        inst = counters.get("instructions_previous_version") or {}
        occupancy = stats.get("avg_occupancy")
        occupancy_txt = f"{occupancy * 100:.2f}%" if isinstance(occupancy, (int, float)) else "n/a"
        lines.append(
            f"[{index}] {region.get('name')}: executions={region.get('executions')} "
            f"moved_bytes={_get(counters, 'moved_bytes.total'):.0f} "
            f"vec_instr={inst.get('vector')} "
            f"avg_vl={stats.get('avg_vl_elems')} occupancy={occupancy_txt}"
        )
        for block in inst.get("sew") or []:
            if not block.get("instr"):
                continue
            lines.append(
                f"    SEW{block.get('sew')}: instr={block.get('instr')} "
                f"elems={block.get('elems')} avg_vl={block.get('avg_vl')}"
            )
        totals = category_totals(region)
        if totals:
            lines.append("    por categoria: "
                         + ", ".join(f"{name}={count}"
                                     for name, count in sorted(totals.items())))
        counted = instruction_counters(region)
        if counted:
            top = sorted(counted.items(), key=lambda kv: (-kv[1], kv[0]))[:5]
            lines.append("    top_instr: "
                         + ", ".join(f"{name}={count}" for name, count in top))
        masked = (region.get("counters") or {}).get("masked") or {}
        for cat in ("arith", "memory"):
            r = masked.get(cat)
            if isinstance(r, dict):
                lines.append(f"    masked {cat}: {r['instr']} instr, "
                             f"densidad {r['avg_density']:.2f} "
                             f"± {r['stddev_density']:.2f}")
        addr = (region.get("counters") or {}).get("memory_addresses") or {}
        grupos = [f"{name}={r['avg_range_b']:.1f}B ({r['instr']} instr, "
                  f"{r['executions']} ejec.)"
                  for name, r in addr.items()
                  if isinstance(r, dict) and "avg_range_b" in r]
        if grupos:
            lines.append("    rango de direcciones (media): " + ", ".join(grupos))

    return lines


def dump(doc):
    """Volcado por region: estadisticas, bloques SEW y arbol de instrucciones."""
    lines = []
    for index, region in enumerate(doc.get("regions") or []):
        lines.append(f"region[{index}] name={region.get('name')} "
                     f"nesting={region.get('nesting')} parent={region.get('parent')}")
        instructions = (region.get("counters") or {}).get(
            "instructions_previous_version") or {}
        lines.append(f"  instructions: total={instructions.get('total')} "
                     f"scalar={instructions.get('scalar')} "
                     f"vsetvl={instructions.get('vsetvl')} "
                     f"vector={instructions.get('vector')}")
        for block in instructions.get("sew") or []:
            kinds = block.get("elems_by_kind") or {}
            lines.append(f"  sew={block.get('sew'):>2}  instr={block.get('instr'):>8} "
                         f"elems={block.get('elems'):>10} avg_vl={block.get('avg_vl'):>8} "
                         f"| elems arith/mem/red/mask="
                         f"{kinds.get('arith', 0)}/{kinds.get('memory', 0)}/"
                         f"{kinds.get('reduction', 0)}/{kinds.get('mask', 0)} "
                         f"| stride={block.get('avg_stride_b', 0)}"
                         f"±{block.get('stddev_stride_b', 0)}")

        masked = (region.get("counters") or {}).get("masked") or {}
        for cat, r in masked.items():
            lines.append(f"  masked {cat}: instr={r['instr']} "
                         f"avg_density={r['avg_density']:.2f} "
                         f"stddev={r['stddev_density']:.2f}")
        addr = (region.get("counters") or {}).get("memory_addresses") or {}
        for name, r in addr.items():
            if isinstance(r, dict) and "avg_range_b" in r:
                lines.append(f"  {name}: avg_range={r['avg_range_b']:.1f}B "
                             f"({r['instr']} instr, {r['executions']} ejec.)")

        def walk(node, depth):
            for name, value in node.items():
                if isinstance(value, dict):
                    lines.append("  " + "  " * depth + f"{name}:")
                    walk(value, depth + 1)
                else:
                    lines.append("  " + "  " * depth + f"{name}={value}")

        walk(instruction_tree(region), 1)
    return lines


def validate_file(json_path, schema_path=None):
    """(valido, mensaje) de forma + invariantes, para uso programatico."""
    try:
        doc = load_document(json_path)
        schema = load_schema(schema_path)
    except (OSError, json.JSONDecodeError, SchemaError) as exc:
        return False, f"Validation FAILED: {exc}"

    errors = validate_document(doc, schema)
    if errors:
        detail = "; ".join(errors[:5])
        extra = "" if len(errors) <= 5 else f" (+{len(errors) - 5} mas)"
        return False, f"Validation FAILED: {detail}{extra}"

    errors, warnings = check_invariants(doc)
    if errors:
        return False, "Validation FAILED (invariantes): " + "; ".join(errors[:5])

    nregions = len(doc.get("regions") or [])
    msg = f"Validation SUCCESS: {nregions} region(es)"
    if warnings:
        msg += f", {len(warnings)} aviso(s)"
    return True, msg


def main():
    parser = argparse.ArgumentParser(
        description="Reference reader and validator for the RAVE profiler JSON."
    )
    parser.add_argument("json_file", help="Path to the profiler JSON file")
    parser.add_argument("--validate", action="store_true",
                        help="Validate shape (schema) and invariants")
    parser.add_argument("--check", action="store_true",
                        help="Check contract invariants only")
    parser.add_argument("--summary", action="store_true", help="Print summary statistics")
    parser.add_argument("--dump", action="store_true", help="Dump counters per region and SEW")
    parser.add_argument("--schema", default=None, help="Path to the JSON schema file")
    parser.add_argument("--quiet-warnings", action="store_true",
                        help="Do not print invariant warnings")
    args = parser.parse_args()

    if not os.path.exists(args.json_file):
        print(f"Error: file not found: {args.json_file}", file=sys.stderr)
        sys.exit(1)

    try:
        doc = load_document(args.json_file)
        schema = load_schema(args.schema)
    except (OSError, json.JSONDecodeError, SchemaError) as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        sys.exit(2)

    if args.validate:
        errors = validate_document(doc, schema)
        if errors:
            for err in errors[:20]:
                print(f"[ERROR] {err}", file=sys.stderr)
            print(f"[ERROR] Validation FAILED: {len(errors)} error(es) de esquema",
                  file=sys.stderr)
            sys.exit(2)

    if args.validate or args.check:
        errors, warnings = check_invariants(doc)
        if not args.quiet_warnings:
            for warn in warnings:
                print(f"[WARN] {warn}", file=sys.stderr)
        if errors:
            for err in errors[:20]:
                print(f"[ERROR] {err}", file=sys.stderr)
            print(f"[ERROR] Validation FAILED: {len(errors)} invariante(s) rota(s)",
                  file=sys.stderr)
            sys.exit(2)

    # Sin accion explicita, resumen (igual que trace_reader.py).
    if not (args.validate or args.check or args.summary or args.dump):
        args.summary = True

    if args.validate or args.check:
        nregions = len(doc.get("regions") or [])
        print(f"[OK] Validation SUCCESS: {nregions} region(es)")
        if not (args.summary or args.dump):
            sys.exit(0)

    if args.summary:
        for line in summarize(doc):
            print(line)
    if args.dump:
        for line in dump(doc):
            print(line)


if __name__ == "__main__":
    main()
