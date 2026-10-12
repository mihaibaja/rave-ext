#!/usr/bin/env python3
"""Tests del lector/validador del JSON del profiler (utils/profiler_json_reader.py).

Los casos invalidos se construyen mutando `fixtures/minimal.json` (documento
valido canonico) para que la mutacion quede explicita en el propio test; ademas
se valida una salida REAL de RAVE (`fixtures/sample_spmv.json`), que es la que
detecta desviaciones del contrato cuando cambian los contadores.
"""

import copy
import json
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT / "utils"))

from profiler_json_reader import (  # noqa: E402
    EXTENDED_ONLY_FIELDS,
    check_invariants,
    dump,
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


def sew_blocks(doc):
    return doc["regions"][0]["counters"]["instructions"]["sew"]


class TestProfilerJsonSchema(unittest.TestCase):

    def setUp(self):
        self.schema = load_schema()
        self.minimal = minimal()
        self.sample = sample()

    def test_schema_loads_with_defs(self):
        self.assertIn("$defs", self.schema)
        for name in ("region", "counters", "sewBlock", "arith", "memory", "instructions"):
            self.assertIn(name, self.schema["$defs"])

    def test_minimal_fixture_is_valid(self):
        self.assertEqual(validate_document(self.minimal, self.schema), [])

    def test_real_output_is_valid(self):
        # 25 ficheros reales de resultados/ pasan; aqui se fija uno como referencia.
        self.assertEqual(validate_document(self.sample, self.schema), [])
        errors, warnings = check_invariants(self.sample)
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

    def test_unknown_field_is_rejected(self):
        sew_blocks(self.minimal)[0]["memory"]["campo_inventado"] = 1
        errors = validate_document(self.minimal, self.schema)
        self.assertTrue(any("campo_inventado" in e for e in errors), errors)

    def test_sew_array_must_have_four_blocks(self):
        doc = self.minimal
        doc["regions"][0]["counters"]["instructions"]["sew"] = sew_blocks(doc)[:2]
        errors = validate_document(doc, self.schema)
        self.assertTrue(any("minimo 4" in e for e in errors), errors)

    def test_sew_value_must_be_8_16_32_64(self):
        doc = self.minimal
        sew_blocks(doc)[1]["sew"] = 24
        errors = validate_document(doc, self.schema)
        self.assertTrue(any("fuera de" in e for e in errors), errors)

    def test_negative_values_are_allowed_in_other(self):
        doc = self.minimal
        sew_blocks(doc)[0]["other"] = {"instr": -17088, "elems": -17088, "avg_vl": -1.0}
        self.assertEqual(validate_document(doc, self.schema), [])
        errors, _ = check_invariants(doc)
        self.assertEqual(errors, [])

    def test_nullable_fields(self):
        doc = self.minimal
        doc["binary"] = None
        doc["regions"][0]["name"] = None
        doc["regions"][0]["parent"] = "REGION_PADRE"
        self.assertEqual(validate_document(doc, self.schema), [])

    def test_count_field_rejects_real_number(self):
        sew_blocks(self.minimal)[0]["memory"]["instr"] = 4.5
        errors = validate_document(self.minimal, self.schema)
        self.assertTrue(any("memory.instr" in e and "integer" in e for e in errors), errors)


class TestProfilerJsonInvariants(unittest.TestCase):

    def test_broken_memory_decomposition(self):
        doc = minimal()
        sew_blocks(doc)[3]["memory"]["instr"] = 99
        errors, _ = check_invariants(doc)
        self.assertTrue(any("memory.instr" in e for e in errors), errors)

    def test_broken_arith_decomposition(self):
        doc = minimal()
        sew_blocks(doc)[0]["arith"]["fp"] = 5
        errors, _ = check_invariants(doc)
        self.assertTrue(any("arith.instr" in e for e in errors), errors)

    def test_broken_indexed_ordered_pair(self):
        doc = minimal()
        sew_blocks(doc)[0]["memory"]["indexed_unordered"] = 7
        errors, _ = check_invariants(doc)
        self.assertTrue(any("indexed_ordered+indexed_unordered" in e for e in errors), errors)

    def test_sew_blocks_must_be_in_order(self):
        doc = minimal()
        sew_blocks(doc)[0], sew_blocks(doc)[1] = sew_blocks(doc)[1], sew_blocks(doc)[0]
        errors, _ = check_invariants(doc)
        self.assertTrue(any("esperado 8" in e for e in errors), errors)

    def test_region_count_mismatch_without_accumulation(self):
        doc = minimal()
        doc["region_count"] = 3
        errors, _ = check_invariants(doc)
        self.assertTrue(any("region_count" in e for e in errors), errors)

    def test_region_count_allows_accumulation(self):
        # Con accumulate_regions no coincide: no debe ser un error (contrato 1.0).
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
        blocks = sew_blocks(doc)
        blocks[2]["instr"] = 9  # vector y arith quedan desviados 1
        errors, _ = check_invariants(doc)
        self.assertNotIn("instructions.vector", " ".join(errors))

    def test_extended_fields_zero_without_extended_trace(self):
        doc = minimal()
        doc["trace_extended"] = False
        errors, warnings = check_invariants(doc)
        self.assertEqual(errors, [])
        self.assertTrue(any("campos extendidos" in w for w in warnings), warnings)
        self.assertTrue(any("arith.narrowing" in w for w in warnings), warnings)

    def test_extended_field_list_matches_schema(self):
        schema = load_schema()
        memory = schema["$defs"]["memory"]["properties"]
        for field in EXTENDED_ONLY_FIELDS:
            root, _, leaf = field.partition(".")
            if root == "memory":
                self.assertIn(leaf, memory, field)


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

    def test_summary_reports_ordered_when_present(self):
        doc = minimal()
        lines = summarize(doc)
        self.assertTrue(any("ordered=" in line for line in lines), lines)

    def test_dump_prints_one_line_per_sew(self):
        lines = dump(sample())
        sew_lines = [line for line in lines if line.startswith("  sew=")]
        self.assertEqual(len(sew_lines), 4)
        self.assertTrue(any("ordered/unordered=" in line for line in sew_lines))

    def test_new_fields_are_optional_for_old_files(self):
        # Los JSON generados antes del 2026-10-12 no traen indexed_ordered /
        # indexed_unordered: deben seguir validando (el visor los lee como 0).
        doc = sample()
        for block in sew_blocks(doc):
            block["memory"].pop("indexed_ordered", None)
            block["memory"].pop("indexed_unordered", None)
        self.assertEqual(validate_document(doc), [])
        errors, _ = check_invariants(doc)
        self.assertEqual(errors, [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
