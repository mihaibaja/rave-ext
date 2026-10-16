#!/usr/bin/env python3
"""Tests del lector/validador del JSON del profiler (utils/profiler_json_reader.py).

Los casos invalidos se construyen mutando `fixtures/minimal.json` (documento
valido canonico) para que la mutacion quede explicita en el propio test; ademas
se valida una salida REAL de RAVE (`fixtures/sample_spmv.json`), que es la que
detecta desviaciones del contrato cuando cambian los contadores.

Estructura del documento (contrato 2.0, 2026-10-16):

    regions[]                     id/nombre/... + counters + instructions
    counters                      moved_bytes, flops, global_stats, dependencies,
                                  instructions_previous_version
                                  { total, scalar, vsetvl, vector, sew[] }
    instructions                  un objeto por seccion de docs/rvv instr.md, con
                                  los contadores "<mnemonico>_counter" y sus
                                  subcategorias
"""

import copy
import json
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT / "utils"))

from profiler_json_reader import (  # noqa: E402
    category_totals,
    check_invariants,
    dump,
    instruction_counters,
    instruction_tree,
    load_document,
    load_schema,
    summarize,
    validate_document,
    validate_file,
)

FIXTURES_DIR = Path(__file__).resolve().parent / "fixtures"


def minimal():
    return copy.deepcopy(load_document(FIXTURES_DIR / "minimal.json"))


def sample():
    return copy.deepcopy(load_document(FIXTURES_DIR / "sample_spmv.json"))


def region(doc):
    return doc["regions"][0]


def counters(doc):
    return region(doc)["counters"]


def instructions(doc):
    """Estadisticas de instrucciones (bloque heredado)."""
    return counters(doc)["instructions_previous_version"]


def sew_blocks(doc):
    return instructions(doc)["sew"]


def tree(doc):
    return instruction_tree(region(doc))


def unit_stride(doc):
    return tree(doc)["vector_loads_and_stores"]["unit_stride"]


class TestProfilerJsonSchema(unittest.TestCase):

    def setUp(self):
        self.schema = load_schema()
        self.minimal = minimal()
        self.sample = sample()

    def test_schema_loads_with_defs(self):
        self.assertIn("$defs", self.schema)
        for name in ("region", "counters", "sewBlock", "instructionsTree",
                     "instructionsPreviousVersion", "category"):
            self.assertIn(name, self.schema["$defs"])

    def test_minimal_fixture_is_valid(self):
        self.assertEqual(validate_document(self.minimal, self.schema), [])

    def test_real_output_is_valid(self):
        # 25 ficheros reales de resultados/ pasan; aqui se fija uno como referencia.
        self.assertEqual(validate_document(self.sample, self.schema), [])
        errors, _ = check_invariants(self.sample)
        self.assertEqual(errors, [])

    def test_missing_required_top_level_field(self):
        del self.minimal["region_count"]
        errors = validate_document(self.minimal, self.schema)
        self.assertTrue(any("region_count" in e and "obligatorio" in e for e in errors),
                        errors)

    def test_missing_required_nested_field(self):
        del self.minimal["regions"][0]["counters"]["dependencies"]["scalar"]["war"]
        errors = validate_document(self.minimal, self.schema)
        self.assertTrue(any("war" in e and "obligatorio" in e for e in errors), errors)

    def test_type_mismatch(self):
        self.minimal["regions"][0]["counters"]["global_stats"]["avg_lmul"] = "1"
        errors = validate_document(self.minimal, self.schema)
        self.assertTrue(any("avg_lmul" in e and "number" in e for e in errors), errors)

    def test_counter_blocks_are_split(self):
        # counters.instructions_previous_version = estadisticas; region.instructions
        # = contadores por instruccion (niveles del listado).
        doc = minimal()
        self.assertIn("instructions_previous_version", counters(doc))
        self.assertNotIn("instructions", counters(doc))
        self.assertIn("instructions", region(doc))
        self.assertEqual(sorted(region(doc)["instructions"])[0],
                         "configuration_setting")

    def test_unknown_field_is_rejected(self):
        counters(self.minimal)["campo_inventado"] = 1
        errors = validate_document(self.minimal, self.schema)
        self.assertTrue(any("campo_inventado" in e for e in errors), errors)

    def test_unknown_category_is_rejected(self):
        # Las claves de primer nivel son las secciones del listado: una de mas no
        # cuela porque la region tiene additionalProperties=false.
        region(self.minimal)["vector_inventado"] = {"sub": {"vadd_vv_counter": 1}}
        errors = validate_document(self.minimal, self.schema)
        self.assertTrue(any("vector_inventado" in e for e in errors), errors)

    def test_sew_array_must_have_four_blocks(self):
        doc = self.minimal
        instructions(doc)["sew"] = sew_blocks(doc)[:2]
        errors = validate_document(doc, self.schema)
        self.assertTrue(any("minimo 4" in e for e in errors), errors)

    def test_sew_value_must_be_8_16_32_64(self):
        doc = self.minimal
        sew_blocks(doc)[1]["sew"] = 24
        errors = validate_document(doc, self.schema)
        self.assertTrue(any("fuera de" in e for e in errors), errors)

    def test_sew_block_only_has_stats(self):
        # Los desgloses por SEW (arith/memory/reduction/mask/other) ya no existen:
        # el detalle por instruccion esta en las categorias de la region.
        sew_blocks(self.minimal)[0]["arith"] = {"instr": 1}
        errors = validate_document(self.minimal, self.schema)
        self.assertTrue(any("arith" in e and "no documentado" in e for e in errors),
                        errors)

    def test_nullable_fields(self):
        doc = self.minimal
        doc["binary"] = None
        doc["regions"][0]["name"] = None
        doc["regions"][0]["parent"] = "REGION_PADRE"
        self.assertEqual(validate_document(doc, self.schema), [])

    def test_count_field_rejects_real_number(self):
        sew_blocks(self.minimal)[0]["instr"] = 4.5
        errors = validate_document(self.minimal, self.schema)
        self.assertTrue(any("instr" in e and "integer" in e for e in errors), errors)

    def test_counter_key_must_be_a_counter_name(self):
        unit_stride(self.minimal)["vle8.v"] = 1     # falta el sufijo _counter
        self.assertTrue(validate_document(self.minimal, self.schema),
                        "una clave con punto no es un contador valido")

    def test_counter_value_must_be_a_non_negative_integer(self):
        unit_stride(self.minimal)["vle8_v_counter"] = -2
        self.assertTrue(validate_document(self.minimal, self.schema))

    def test_category_can_nest(self):
        # Un nodo nuevo con subcategorias dentro sigue siendo valido mientras las
        # claves y los valores tengan la forma del contrato.
        tree(self.minimal)["vector_mask"] = {
            "logical": {"vmand_mm_counter": 3},
            "vcpop": {"vcpop_m_counter": 1},
        }
        self.assertEqual(validate_document(self.minimal, self.schema), [])

    def test_sew_block_requires_the_element_breakdown(self):
        del sew_blocks(self.minimal)[0]["elems_by_kind"]
        errors = validate_document(self.minimal, self.schema)
        self.assertTrue(any("elems_by_kind" in e and "obligatorio" in e
                            for e in errors), errors)

    def test_overlapping_element_kinds_are_allowed(self):
        # arith y mask se solapan (una mascara suma en los dos), asi que la suma
        # puede pasar de elems: el esquema no lo prohibe y las invariantes
        # tampoco (cada tipo por separado si esta acotado).
        doc = minimal()
        block = sew_blocks(doc)[0]
        block["elems_by_kind"] = {"arith": block["elems"], "memory": 0,
                                  "reduction": 0, "mask": block["elems"]}
        self.assertEqual(validate_document(doc, self.schema), [])
        self.assertEqual(check_invariants(doc), ([], []))


class TestProfilerJsonInvariants(unittest.TestCase):

    def test_broken_total_decomposition(self):
        doc = minimal()
        instructions(doc)["total"] = 99
        errors, _ = check_invariants(doc)
        self.assertTrue(any("instructions.total" in e for e in errors), errors)

    def test_broken_vector_sum(self):
        doc = minimal()
        sew_blocks(doc)[3]["instr"] = 99
        errors, _ = check_invariants(doc)
        self.assertTrue(any("instructions.vector" in e for e in errors), errors)

    def test_sew_blocks_must_be_in_order(self):
        doc = minimal()
        sew_blocks(doc)[0], sew_blocks(doc)[1] = sew_blocks(doc)[1], sew_blocks(doc)[0]
        errors, _ = check_invariants(doc)
        self.assertTrue(any("esperado 8" in e for e in errors), errors)

    def test_elems_by_kind_cannot_exceed_elems(self):
        doc = minimal()
        sew_blocks(doc)[0]["elems_by_kind"]["memory"] = 999
        errors, _ = check_invariants(doc)
        self.assertTrue(any("elems_by_kind.memory" in e for e in errors), errors)

    def test_region_count_mismatch_without_accumulation(self):
        doc = minimal()
        doc["region_count"] = 3
        errors, _ = check_invariants(doc)
        self.assertTrue(any("region_count" in e for e in errors), errors)

    def test_region_count_allows_accumulation(self):
        # Con accumulate_regions no coincide: no debe ser un error.
        doc = minimal()
        doc["accumulate_regions"] = True
        doc["region_count"] = 193065
        errors, _ = check_invariants(doc)
        self.assertEqual(errors, [])

    def test_rounded_means_are_tolerated(self):
        # Con accumulate_regions los sumandos se redondean por separado (medido en
        # large/lavaMD.json: se desvia 1 unidad). No debe ser un error.
        doc = minimal()
        doc["accumulate_regions"] = True
        sew_blocks(doc)[2]["instr"] = 9   # vector queda desviado 1
        errors, _ = check_invariants(doc)
        self.assertNotIn("instructions.vector", " ".join(errors))

    def test_counters_cover_everything_but_scalar(self):
        # Cada instruccion vectorial y cada vset* tiene su contador: la suma del
        # arbol es exactamente total - scalar en los dos fixtures.
        for doc in (minimal(), sample()):
            inst = instructions(doc)
            self.assertEqual(sum(instruction_counters(region(doc)).values()),
                             inst["total"] - inst["scalar"])
            self.assertEqual(check_invariants(doc), ([], []))

    def test_counters_cannot_exceed_countable_instr(self):
        doc = minimal()
        unit_stride(doc)["vle8_v_counter"] = 999
        errors, _ = check_invariants(doc)
        self.assertTrue(any("contadores por instruccion" in e for e in errors), errors)

    def test_missing_coverage_is_a_warning_not_an_error(self):
        # Un mnemonico que la tabla no reconoce deja la suma por debajo de
        # total - scalar: es un aviso, no un fichero corrupto.
        doc = minimal()
        unit_stride(doc)["vle8_v_counter"] = 1
        errors, warnings = check_invariants(doc)
        self.assertEqual(errors, [])
        self.assertTrue(any("cubren" in w for w in warnings), warnings)


class TestReaderApi(unittest.TestCase):

    def test_validate_file_ok(self):
        valid, msg = validate_file(FIXTURES_DIR / "minimal.json")
        self.assertTrue(valid, msg)
        self.assertIn("Validation SUCCESS", msg)

    def test_validate_file_ko(self):
        bad = Path(self._write_temp({"generator": "RAVE"}))
        valid, msg = validate_file(bad)
        self.assertFalse(valid)
        self.assertIn("Validation FAILED", msg)
        bad.unlink()

    def test_validate_file_missing(self):
        valid, msg = validate_file(FIXTURES_DIR / "no_existe.json")
        self.assertFalse(valid)
        self.assertIn("FAILED", msg)

    def _write_temp(self, doc):
        path = FIXTURES_DIR / "_tmp_test.json"
        path.write_text(json.dumps(doc), encoding="utf-8")
        return path

    def test_summary_includes_regions_and_sew(self):
        lines = summarize(sample())
        self.assertTrue(any("GLOBAL_REGION" in line for line in lines), lines)
        self.assertTrue(any("SEW64" in line for line in lines), lines)

    def test_summary_includes_categories_and_top_instructions(self):
        lines = summarize(sample())
        self.assertTrue(any("por categoria:" in line for line in lines), lines)
        self.assertTrue(any("vector_loads_and_stores=22008" in line for line in lines),
                        lines)
        self.assertTrue(any("top_instr:" in line for line in lines), lines)

    def test_dump_prints_one_line_per_sew(self):
        lines = dump(sample())
        sew_lines = [line for line in lines if line.startswith("  sew=")]
        self.assertEqual(len(sew_lines), 4)

    def test_dump_prints_the_category_tree(self):
        lines = dump(sample())
        self.assertTrue(any(line.strip() == "vector_loads_and_stores:" for line in lines),
                        lines)
        self.assertTrue(any("vle64_v_counter=22008" in line for line in lines), lines)

    def test_instruction_tree_has_only_the_categories(self):
        self.assertNotIn("counters", tree(sample()))
        self.assertNotIn("id", tree(sample()))
        self.assertEqual(sorted(tree(minimal())),
                         ["configuration_setting", "vector_floating_point",
                          "vector_integer_arithmetic", "vector_loads_and_stores"])

    def test_instruction_counters_flattens_the_tree(self):
        counted = instruction_counters(region(sample()))
        self.assertEqual(counted["vle64_v_counter"], 22008)
        self.assertEqual(counted["vfmacc_vf_counter"], 10000)
        self.assertEqual(len(counted), 6)

    def test_category_totals_sums_each_section(self):
        totals = category_totals(region(sample()))
        self.assertEqual(totals["configuration_setting"], 22009)
        self.assertEqual(totals["vector_floating_point"], 30000)
        self.assertEqual(sum(totals.values()), 80698)


if __name__ == "__main__":
    unittest.main(verbosity=2)
