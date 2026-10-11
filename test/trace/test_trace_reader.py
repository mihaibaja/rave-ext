#!/usr/bin/env python3
"""
Unit tests for RAVE Dynamic Instruction Trace schema validation and reference reader.
"""

import os
import sys
import unittest
from pathlib import Path

# Ensure utils directory is on path
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT / "utils"))

from trace_reader import (
    AuxRecord,
    IncompatibleSchemaError,
    InstructionRecord,
    OperandDependency,
    TraceFormatError,
    TraceHeader,
    TraceReader,
    simulate_bank_conflicts,
    simulate_lane_workload,
    validate_trace_file,
)


FIXTURES_DIR = Path(__file__).resolve().parent / "fixtures"


class TestTraceReader(unittest.TestCase):

    def setUp(self):
        self.sample_file = FIXTURES_DIR / "sample_trace.jsonl"
        self.missing_header_file = FIXTURES_DIR / "invalid_missing_header.jsonl"
        self.incompatible_version_file = FIXTURES_DIR / "invalid_incompatible_version.jsonl"
        self.missing_field_file = FIXTURES_DIR / "invalid_missing_inst_field.jsonl"
        self.type_mismatch_file = FIXTURES_DIR / "invalid_type_mismatch.jsonl"
        self.orphaned_aux_file = FIXTURES_DIR / "invalid_orphaned_aux.jsonl"

    def test_valid_trace_parsing(self):
        with TraceReader(str(self.sample_file)) as reader:
            self.assertIsNotNone(reader.header)
            header = reader.header
            self.assertEqual(header.schema_version, "1.0.0")
            self.assertEqual(header.rave_version, "2.0-extended")
            self.assertEqual(header.isa_revision, "RVV-1.0")
            self.assertEqual(header.vlen, 16384)
            self.assertEqual(header.elen, 64)
            self.assertEqual(header.workload, "axpy_vector")
            self.assertEqual(header.num_harts, 1)
            self.assertEqual(header.addr_normalization, "base_relative")

            insts = reader.read_all()
            self.assertEqual(len(insts), 9)  # seqno 1 to 9, total 9 insts

            # Check vsetvli
            vset = insts[0]
            self.assertEqual(vset.seqno, 1)
            self.assertEqual(vset.op, "vsetvli")
            self.assertEqual(vset.decoded_op, "VSETVL")
            self.assertEqual(vset.vl, 64)
            self.assertEqual(vset.sew, 32)
            self.assertEqual(vset.lmul, "m1")
            self.assertFalse(vset.masked)

            # Check unit load with mem_addrs aux record
            load_inst = insts[1]
            self.assertEqual(load_inst.seqno, 2)
            self.assertEqual(load_inst.op, "vle32.v")
            self.assertEqual(load_inst.decoded_op, "VECTOR_LOAD_UNIT")
            self.assertEqual(load_inst.dst, ["v8"])
            self.assertEqual(load_inst.srcs, ["x10"])
            self.assertEqual(len(load_inst.aux_records), 1)
            aux_mem = load_inst.get_aux("mem_addrs")
            self.assertIsNotNone(aux_mem)
            self.assertEqual(aux_mem.data["mem_type"], "unit")
            self.assertEqual(aux_mem.data["stride_bytes"], 4)
            self.assertEqual(len(load_inst.get_effective_deltas()), 16)
            self.assertEqual(load_inst.get_effective_deltas()[0], 0)
            self.assertEqual(load_inst.get_effective_deltas()[1], 4)

            # Check dependencies on load
            self.assertEqual(len(load_inst.deps), 1)
            dep = load_inst.deps[0]
            self.assertEqual(dep.operand, "x10")
            self.assertEqual(dep.dep_type, "RAW")
            self.assertEqual(dep.producer_seqno, 1)
            self.assertEqual(dep.distance, 1)

            # Check strided load
            stride_inst = insts[2]
            self.assertEqual(stride_inst.seqno, 3)
            self.assertEqual(stride_inst.op, "vlse32.v")
            self.assertEqual(stride_inst.decoded_op, "VECTOR_LOAD_STRIDED")

            # Check vmseq.vv
            mseq_inst = insts[3]
            self.assertEqual(mseq_inst.seqno, 4)
            self.assertEqual(mseq_inst.op, "vmseq.vv")

            # Check masked vadd.vv with mask_bitmap aux record
            add_inst = insts[4]
            self.assertEqual(add_inst.seqno, 5)
            self.assertEqual(add_inst.op, "vadd.vv")
            self.assertTrue(add_inst.masked)
            self.assertEqual(add_inst.active_elements, 32)
            self.assertEqual(add_inst.get_mask_bitmap(), "0x5555555555555555")

            # Check vrgather.vv with gather_indices aux record
            gather_inst = insts[5]
            self.assertEqual(gather_inst.seqno, 6)
            self.assertEqual(gather_inst.op, "vrgather.vv")
            gather_indices = gather_inst.get_gather_indices()
            self.assertIsNotNone(gather_indices)
            self.assertEqual(gather_indices[:4], [0, 2, 4, 6])

            # Check vslideup.vi with slide aux record
            slide_inst = insts[6]
            self.assertEqual(slide_inst.seqno, 7)
            self.assertEqual(slide_inst.op, "vslideup.vi")
            slide_info = slide_inst.get_slide_compress_info()
            self.assertIsNotNone(slide_info)
            self.assertEqual(slide_info["sub_type"], "slideup")
            self.assertEqual(slide_info["offset"], 2)

            # Check vcompress.vm with compress aux record
            compress_inst = insts[7]
            self.assertEqual(compress_inst.seqno, 8)
            self.assertEqual(compress_inst.op, "vcompress.vm")
            comp_info = compress_inst.get_slide_compress_info()
            self.assertIsNotNone(comp_info)
            self.assertEqual(comp_info["sub_type"], "compress")
            self.assertEqual(comp_info["compacted_count"], 32)

    def test_incompatible_schema_version(self):
        with self.assertRaises(IncompatibleSchemaError) as ctx:
            with TraceReader(str(self.incompatible_version_file)) as reader:
                reader.read_all()
        self.assertIn("Unsupported schema version", str(ctx.exception))

    def test_missing_header(self):
        with self.assertRaises(TraceFormatError) as ctx:
            with TraceReader(str(self.missing_header_file)) as reader:
                reader.read_all()
        self.assertIn("record_type 'header'", str(ctx.exception))

    def test_missing_instruction_field(self):
        with self.assertRaises(TraceFormatError) as ctx:
            with TraceReader(str(self.missing_field_file)) as reader:
                reader.read_all()
        self.assertIn("missing required field", str(ctx.exception).lower())

    def test_type_mismatch(self):
        with self.assertRaises(TraceFormatError) as ctx:
            with TraceReader(str(self.type_mismatch_file)) as reader:
                reader.read_all()
        self.assertIn("must be int", str(ctx.exception))

    def test_orphaned_aux_record(self):
        with self.assertRaises(TraceFormatError) as ctx:
            with TraceReader(str(self.orphaned_aux_file)) as reader:
                reader.read_all()
        self.assertIn("Orphaned auxiliary record", str(ctx.exception))

    def test_validator_function(self):
        valid, msg, header, count = validate_trace_file(str(self.sample_file))
        self.assertTrue(valid)
        self.assertIn("SUCCESS", msg)
        self.assertEqual(count, 9)

        valid, msg, header, count = validate_trace_file(str(self.incompatible_version_file))
        self.assertFalse(valid)
        self.assertIn("FAILED", msg)

    def test_lane_workload_simulation(self):
        with TraceReader(str(self.sample_file)) as reader:
            insts = reader.read_all()

        for lanes in [2, 4, 8]:
            res = simulate_lane_workload(insts, lanes)
            self.assertEqual(res["num_lanes"], lanes)
            self.assertEqual(len(res["lane_elements"]), lanes)
            self.assertGreater(res["total_active_elements"], 0)
            self.assertGreaterEqual(res["cross_lane_movements"], 0)

    def test_bank_conflict_simulation(self):
        with TraceReader(str(self.sample_file)) as reader:
            insts = reader.read_all()

        for banks in [4, 8, 16]:
            res = simulate_bank_conflicts(insts, num_banks=banks, bank_width_bytes=4)
            self.assertEqual(res["num_banks"], banks)
            self.assertGreater(res["total_mem_instructions"], 0)
            self.assertGreaterEqual(res["total_conflicts"], 0)

    def test_html_visualizer_generation(self):
        from trace_viewer import generate_html_content, load_trace_json_lines
        records = load_trace_json_lines(str(self.sample_file))
        self.assertEqual(len(records), 17)  # header + 9 insts + 7 aux records
        html = generate_html_content(records)
        self.assertIn("<!DOCTYPE html>", html)
        self.assertIn("RAVE Trace Explorer", html)
        self.assertIn("Lane Workload & Crossbar Movement", html)
        self.assertIn("Memory Bank Access & Conflicts", html)
        self.assertIn("axpy_vector", html)

    def test_cli_summary_format(self):
        import subprocess
        proc = subprocess.run(
            [sys.executable, str(REPO_ROOT / "utils" / "trace_reader.py"), str(self.sample_file), "--summary"],
            capture_output=True,
            text=True,
            check=True,
        )
        lines = [line.strip() for line in proc.stdout.strip().splitlines() if line.strip()]
        self.assertEqual(len(lines), 4)
        self.assertEqual(lines[0], "ISA:       RVV-1.0")
        self.assertEqual(lines[1], "VLEN / ELEN:        16384 / 64")
        self.assertEqual(lines[2], "Workload:           axpy_vector")
        self.assertEqual(lines[3], "Total Instructions: 9")

    def test_cli_validate_and_dump(self):
        import subprocess
        proc_val = subprocess.run(
            [sys.executable, str(REPO_ROOT / "utils" / "trace_reader.py"), str(self.sample_file), "--validate"],
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertIn("[OK] Validation SUCCESS: 9 instructions parsed.", proc_val.stdout)

        proc_dump = subprocess.run(
            [sys.executable, str(REPO_ROOT / "utils" / "trace_reader.py"), str(self.sample_file), "--dump"],
            capture_output=True,
            text=True,
            check=True,
        )
        dump_lines = proc_dump.stdout.strip().splitlines()
        self.assertEqual(len(dump_lines), 9)
        self.assertIn("vsetvli", dump_lines[0])


if __name__ == "__main__":
    unittest.main()
