#!/usr/bin/env python3
"""
Reference Reader and Validator for RAVE Dynamic Instruction Traces.

This module implements:
- TraceHeader, InstructionRecord, OperandDependency, and AuxRecord data classes.
- TraceReader: Streaming and buffered parser for RAVE JSON Lines (.jsonl) traces.
- Schema Validator: Detects incompatible schema versions, missing fields, type errors,
  and orphaned auxiliary records.
- Post-processing Reference Models: Demonstrates lane distribution and bank conflict
  calculations from lane/bank-independent trace records.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Iterator, List, Optional, Sequence, Set, Tuple


SUPPORTED_SCHEMA_VERSIONS = {"1.0.0", "1.0"}


class TraceFormatError(Exception):
    """Raised when a trace violates schema or format constraints."""
    pass


class IncompatibleSchemaError(TraceFormatError):
    """Raised when the trace schema version is incompatible with this reader."""
    pass


@dataclass
class OperandDependency:
    operand: str
    dep_type: str
    producer_seqno: int
    distance: int

    @classmethod
    def from_dict(cls, data: Dict[str, Any], record_seqno: int) -> OperandDependency:
        if not isinstance(data, dict):
            raise TraceFormatError(f"Dependency entry must be an object, got {type(data).__name__}")
        for req in ("operand", "dep_type", "producer_seqno", "distance"):
            if req not in data:
                raise TraceFormatError(f"Dependency missing required field '{req}' at seqno {record_seqno}")
        if not isinstance(data["producer_seqno"], int):
            raise TraceFormatError(f"producer_seqno must be int, got {type(data['producer_seqno']).__name__}")
        if not isinstance(data["distance"], int):
            raise TraceFormatError(f"distance must be int, got {type(data['distance']).__name__}")
        return cls(
            operand=str(data["operand"]),
            dep_type=str(data["dep_type"]),
            producer_seqno=data["producer_seqno"],
            distance=data["distance"],
        )


@dataclass
class AuxRecord:
    seqno: int
    aux_type: str
    data: Dict[str, Any]

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> AuxRecord:
        if "seqno" not in data:
            raise TraceFormatError("Auxiliary record missing 'seqno'")
        if "aux_type" not in data:
            raise TraceFormatError("Auxiliary record missing 'aux_type'")
        if not isinstance(data["seqno"], int):
            raise TraceFormatError(f"Auxiliary seqno must be int, got {type(data['seqno']).__name__}")
        aux_data = {k: v for k, v in data.items() if k not in ("record_type", "seqno", "aux_type")}
        return cls(seqno=data["seqno"], aux_type=str(data["aux_type"]), data=aux_data)


@dataclass
class TraceHeader:
    schema_version: str
    rave_version: str
    isa_revision: str
    vlen: int
    elen: int
    workload: str
    config: Dict[str, Any]
    num_harts: int
    addr_normalization: str

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> TraceHeader:
        required = [
            "schema_version",
            "rave_version",
            "isa_revision",
            "vlen",
            "elen",
            "workload",
            "config",
            "num_harts",
            "addr_normalization",
        ]
        for req in required:
            if req not in data:
                raise TraceFormatError(f"Header missing required field: '{req}'")

        schema_ver = str(data["schema_version"])
        major = schema_ver.split(".")[0]
        if major != "1":
            raise IncompatibleSchemaError(
                f"Unsupported schema version: '{schema_ver}'. Supported major version: 1 (e.g. {SUPPORTED_SCHEMA_VERSIONS})"
            )

        if not isinstance(data["vlen"], int) or data["vlen"] <= 0:
            raise TraceFormatError(f"Header 'vlen' must be positive integer, got {data['vlen']}")
        if not isinstance(data["elen"], int) or data["elen"] <= 0:
            raise TraceFormatError(f"Header 'elen' must be positive integer, got {data['elen']}")
        if not isinstance(data["num_harts"], int) or data["num_harts"] <= 0:
            raise TraceFormatError(f"Header 'num_harts' must be positive integer, got {data['num_harts']}")
        if not isinstance(data["config"], dict):
            raise TraceFormatError(f"Header 'config' must be an object, got {type(data['config']).__name__}")

        return cls(
            schema_version=schema_ver,
            rave_version=str(data["rave_version"]),
            isa_revision=str(data["isa_revision"]),
            vlen=data["vlen"],
            elen=data["elen"],
            workload=str(data["workload"]),
            config=data["config"],
            num_harts=data["num_harts"],
            addr_normalization=str(data["addr_normalization"]),
        )


@dataclass
class InstructionRecord:
    seqno: int
    hart: int
    pc: str
    binary: str
    op: str
    asm: str
    decoded_op: str
    dst: List[str]
    srcs: List[str]
    vl: int
    sew: int
    lmul: str
    vta: int
    vma: int
    masked: bool
    deps: List[OperandDependency]
    # `vstart` y `active_elements` son obligatorios en schema/trace_schema_v1.json y
    # estan presentes en los fixtures, pero el emisor actual (inst_trace.c) todavia
    # no los escribe; se aceptan como opcionales para no romper esas trazas.
    vstart: int = 0
    active_elements: int = 0
    aux_records: List[AuxRecord] = field(default_factory=list)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> InstructionRecord:
        required_fields = [
            "seqno",
            "hart",
            "pc",
            "binary",
            "op",
            "asm",
            "decoded_op",
            "dst",
            "srcs",
            "vl",
            "sew",
            "lmul",
            "vta",
            "vma",
            "masked",
            "deps",
        ]
        for req in required_fields:
            if req not in data:
                raise TraceFormatError(f"Instruction record missing required field: '{req}'")

        seqno = data["seqno"]
        if not isinstance(seqno, int):
            raise TraceFormatError(f"seqno must be int, got {type(seqno).__name__}")
        if not isinstance(data["hart"], int):
            raise TraceFormatError(f"hart must be int, got {type(data['hart']).__name__}")
        if not isinstance(data["dst"], list):
            raise TraceFormatError(f"dst must be a list of strings, got {type(data['dst']).__name__}")
        if not isinstance(data["srcs"], list):
            raise TraceFormatError(f"srcs must be a list of strings, got {type(data['srcs']).__name__}")
        if not isinstance(data["vl"], int):
            raise TraceFormatError(f"vl must be int, got {type(data['vl']).__name__}")
        if not isinstance(data["sew"], int):
            raise TraceFormatError(f"sew must be int, got {type(data['sew']).__name__}")
        if not isinstance(data["masked"], bool):
            raise TraceFormatError(f"masked must be bool, got {type(data['masked']).__name__}")
        for optional_int_field in ("vstart", "active_elements"):
            if optional_int_field in data and not isinstance(data[optional_int_field], int):
                raise TraceFormatError(
                    f"{optional_int_field} must be int, "
                    f"got {type(data[optional_int_field]).__name__}"
                )

        deps_list = [
            OperandDependency.from_dict(d, seqno) for d in data.get("deps", [])
        ]

        return cls(
            seqno=seqno,
            hart=data["hart"],
            pc=str(data["pc"]),
            binary=str(data["binary"]),
            op=str(data["op"]),
            asm=str(data["asm"]),
            decoded_op=str(data["decoded_op"]),
            dst=[str(x) for x in data["dst"]],
            srcs=[str(x) for x in data["srcs"]],
            vl=data["vl"],
            sew=data["sew"],
            lmul=str(data["lmul"]),
            vta=int(data["vta"]),
            vma=int(data["vma"]),
            masked=data["masked"],
            deps=deps_list,
            vstart=data.get("vstart", 0),
            active_elements=data.get("active_elements", 0),
        )

    def is_vector(self) -> bool:
        return self.decoded_op.startswith("VECTOR_")

    def is_memory(self) -> bool:
        return "LOAD" in self.decoded_op or "STORE" in self.decoded_op

    def get_aux(self, aux_type: str) -> Optional[AuxRecord]:
        for aux in self.aux_records:
            if aux.aux_type == aux_type:
                return aux
        return None

    def get_mask_bitmap(self) -> Optional[str]:
        aux = self.get_aux("mask_bitmap")
        return aux.data.get("mask_bitmap") if aux else None

    def get_effective_deltas(self) -> Optional[List[int]]:
        aux = self.get_aux("mem_addrs")
        if aux and "deltas" in aux.data:
            return [int(x) for x in aux.data["deltas"]]
        return None

    def get_gather_indices(self) -> Optional[List[int]]:
        aux = self.get_aux("gather_indices")
        if aux and "indices" in aux.data:
            return [int(x) for x in aux.data["indices"]]
        return None

    def get_slide_compress_info(self) -> Optional[Dict[str, Any]]:
        aux = self.get_aux("slide_compress")
        return aux.data if aux else None


class TraceReader:
    """
    Parser and stream iterator for RAVE instruction traces.
    """

    def __init__(self, filename_or_stream: Any):
        if isinstance(filename_or_stream, str):
            self.file_path = filename_or_stream
            self._handle = open(filename_or_stream, "r", encoding="utf-8")
            self._owns_handle = True
        else:
            self.file_path = "<stream>"
            self._handle = filename_or_stream
            self._owns_handle = False

        self.header: Optional[TraceHeader] = None
        self._pending_instructions: Dict[int, InstructionRecord] = {}
        self._parsed_records: List[InstructionRecord] = []
        try:
            self._read_header()
        except Exception:
            self.close()
            raise

    def close(self):
        if self._owns_handle and self._handle:
            self._handle.close()
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def _read_header(self):
        first_line = self._handle.readline()
        if not first_line:
            raise TraceFormatError("Trace file is empty, missing header record")
        try:
            data = json.loads(first_line.strip())
        except json.JSONDecodeError as e:
            raise TraceFormatError(f"Header line is not valid JSON: {e}") from e

        if not isinstance(data, dict):
            raise TraceFormatError("Header must be a JSON object")
        if data.get("record_type") != "header":
            raise TraceFormatError(
                f"First line must have record_type 'header', got '{data.get('record_type')}'"
            )

        self.header = TraceHeader.from_dict(data)

    def iter_instructions(self) -> Iterator[InstructionRecord]:
        """
        Yields fully populated InstructionRecord objects with linked auxiliary records.
        """
        current_inst: Optional[InstructionRecord] = None
        line_num = 1

        for line in self._handle:
            line_num += 1
            line = line.strip()
            if not line:
                continue

            try:
                record = json.loads(line)
            except json.JSONDecodeError as e:
                raise TraceFormatError(f"Line {line_num} is not valid JSON: {e}") from e

            if not isinstance(record, dict):
                raise TraceFormatError(f"Line {line_num} must be a JSON object")

            rtype = record.get("record_type")
            if rtype == "inst":
                if current_inst is not None:
                    yield current_inst
                current_inst = InstructionRecord.from_dict(record)
                self._pending_instructions[current_inst.seqno] = current_inst

            elif rtype == "aux":
                aux = AuxRecord.from_dict(record)
                if aux.seqno not in self._pending_instructions:
                    raise TraceFormatError(
                        f"Line {line_num}: Orphaned auxiliary record referencing unknown seqno {aux.seqno}"
                    )
                target = self._pending_instructions[aux.seqno]
                target.aux_records.append(aux)

            else:
                raise TraceFormatError(f"Line {line_num}: Unknown record_type '{rtype}'")

        if current_inst is not None:
            yield current_inst

    def read_all(self) -> List[InstructionRecord]:
        """Reads all instructions and caches them in memory."""
        self._parsed_records = list(self.iter_instructions())
        return self._parsed_records


# =====================================================================
# Post-Processing Reference Models (Lane & Bank Mappings)
# =====================================================================

def simulate_lane_workload(
    instructions: Sequence[InstructionRecord],
    num_lanes: int,
) -> Dict[str, Any]:
    """
    Computes per-lane active elements distribution and cross-lane movement.
    This demonstrates why the trace is lane-independent: any number of lanes
    can be evaluated in post-processing without modifying the simulation trace.
    """
    if num_lanes <= 0:
        raise ValueError("num_lanes must be positive")

    lane_elements = [0] * num_lanes
    cross_lane_movements = 0
    total_active_elements = 0

    for inst in instructions:
        if not inst.is_vector() or inst.vl <= 0:
            continue

        active = inst.active_elements
        total_active_elements += active

        # Standard cyclic lane mapping: element i -> lane (i % num_lanes)
        for i in range(inst.vstart, inst.vstart + active):
            lane = i % num_lanes
            lane_elements[lane] += 1

        # Check cross-lane movements for gathers or slides
        gather_indices = inst.get_gather_indices()
        if gather_indices:
            for dst_elem, src_elem in enumerate(gather_indices):
                if dst_elem >= inst.vl:
                    break
                dst_lane = dst_elem % num_lanes
                src_lane = src_elem % num_lanes
                if dst_lane != src_lane:
                    cross_lane_movements += 1

        slide_info = inst.get_slide_compress_info()
        if slide_info and "offset" in slide_info:
            offset = int(slide_info["offset"])
            for dst_elem in range(inst.active_elements):
                src_elem = dst_elem + offset
                if (dst_elem % num_lanes) != (src_elem % num_lanes):
                    cross_lane_movements += 1

    return {
        "num_lanes": num_lanes,
        "lane_elements": lane_elements,
        "max_lane_load": max(lane_elements) if lane_elements else 0,
        "min_lane_load": min(lane_elements) if lane_elements else 0,
        "total_active_elements": total_active_elements,
        "cross_lane_movements": cross_lane_movements,
    }


def simulate_bank_conflicts(
    instructions: Sequence[InstructionRecord],
    num_banks: int = 16,
    bank_width_bytes: int = 8,
) -> Dict[str, Any]:
    """
    Simulates cache/scratchpad bank conflicts for vector memory accesses
    using the normalized relative deltas recorded in the auxiliary records.
    """
    if num_banks <= 0 or bank_width_bytes <= 0:
        raise ValueError("num_banks and bank_width_bytes must be positive")

    total_mem_instructions = 0
    total_conflicts = 0
    bank_access_counts = [0] * num_banks

    for inst in instructions:
        deltas = inst.get_effective_deltas()
        if deltas is None or len(deltas) == 0:
            continue

        total_mem_instructions += 1
        banks_hit_this_inst = defaultdict(int)

        for delta in deltas:
            bank_id = (delta // bank_width_bytes) % num_banks
            bank_access_counts[bank_id] += 1
            banks_hit_this_inst[bank_id] += 1

        for bank_id, hits in banks_hit_this_inst.items():
            if hits > 1:
                total_conflicts += hits - 1

    return {
        "num_banks": num_banks,
        "bank_width_bytes": bank_width_bytes,
        "total_mem_instructions": total_mem_instructions,
        "total_conflicts": total_conflicts,
        "bank_access_counts": bank_access_counts,
    }


def validate_trace_file(file_path: str) -> Tuple[bool, str, Optional[TraceHeader], int]:
    """
    Validates a trace file against schema rules.
    Returns (is_valid, message, header, instruction_count).
    """
    try:
        with TraceReader(file_path) as reader:
            count = 0
            for _ in reader.iter_instructions():
                count += 1
            return True, f"Validation SUCCESS: {count} instructions parsed.", reader.header, count
    except Exception as e:
        return False, f"Validation FAILED: {e}", None, 0


def main():
    parser = argparse.ArgumentParser(
        description="Reference reader and validator for RAVE instruction traces."
    )
    parser.add_argument("trace_file", help="Path to trace file (.jsonl)")
    parser.add_argument("--validate", action="store_true", help="Validate schema and linkages")
    parser.add_argument("--dump", action="store_true", help="Dump parsed instructions")
    parser.add_argument("--summary", action="store_true", help="Print summary statistics")
    args = parser.parse_args()

    if not os.path.exists(args.trace_file):
        print(f"Error: file not found: {args.trace_file}", file=sys.stderr)
        sys.exit(1)

    is_valid, msg, header, count = validate_trace_file(args.trace_file)
    if not is_valid:
        print(f"[ERROR] {msg}", file=sys.stderr)
        sys.exit(2)

    # If no specific action is specified, default to summary
    if not (args.validate or args.dump or args.summary):
        args.summary = True

    if args.validate and not (args.summary or args.dump):
        print(f"[OK] {msg}")
        sys.exit(0)

    with TraceReader(args.trace_file) as reader:
        insts = reader.read_all()

    if args.summary:
        print(f"ISA:       {reader.header.isa_revision}")
        print(f"VLEN / ELEN:        {reader.header.vlen} / {reader.header.elen}")
        print(f"Workload:           {reader.header.workload}")
        print(f"Total Instructions: {len(insts)}")

    if args.dump:
        for inst in insts:
            aux_str = f" [aux: {len(inst.aux_records)}]" if inst.aux_records else ""
            deps_str = f" [deps: {len(inst.deps)}]" if inst.deps else ""
            print(f"#{inst.seqno:06d} PC={inst.pc} {inst.op:<12} dst={inst.dst} srcs={inst.srcs} vl={inst.vl} sew={inst.sew}{aux_str}{deps_str}")


if __name__ == "__main__":
    main()

