#!/usr/bin/env python3
"""Export trainer v0a phase-bias TSV weights to a tiny runtime artifact."""

from __future__ import annotations

import argparse
import csv
import json
import math
import struct
import sys
import zlib
from pathlib import Path


FORMAT_VERSION = 1
SCORE_SCALE = 1
PHASE_COUNT = 13
PATTERN_SET_ID = "fixed-pattern-fixture-v1"
PATTERNS = (
    ("edge-8", 8),
    ("corner-3x3", 9),
)
NOTE = "local smoke artifact; not production"


def fail(message: str) -> None:
    raise RuntimeError(message)


def checked_pattern_size(length: int) -> int:
    return 3**length


def quantize_weight(value: float) -> int:
    if not math.isfinite(value):
        fail(f"phase bias is not finite: {value!r}")
    if value >= 0:
        rounded = math.floor(value + 0.5)
    else:
        rounded = math.ceil(value - 0.5)
    if rounded < -(2**31) or rounded > 2**31 - 1:
        fail(f"quantized phase bias is outside int32 range: {value!r}")
    return int(rounded)


def load_phase_biases(path: Path) -> list[int]:
    try:
        input_file = path.open(newline="", encoding="utf-8")
    except OSError as error:
        fail(f"cannot read v0a weights TSV: {path}: {error}")

    with input_file:
        reader = csv.DictReader(input_file, delimiter="\t")
        if reader.fieldnames != ["phase", "bias"]:
            fail("v0a weights TSV header must be phase<TAB>bias")
        values: dict[int, int] = {}
        for row_index, row in enumerate(reader, start=2):
            if set(row) != {"phase", "bias"} or None in row:
                fail(f"line {row_index}: expected 2 TSV fields")
            try:
                phase = int(row["phase"])
                bias = float(row["bias"])
            except ValueError as error:
                fail(f"line {row_index}: phase and bias must be numeric: {error}")
            if str(phase) != row["phase"] or phase < 0 or phase >= PHASE_COUNT:
                fail(f"line {row_index}: phase must be an integer in [0, 12]")
            if phase in values:
                fail(f"line {row_index}: duplicate phase {phase}")
            values[phase] = quantize_weight(bias)

    if set(values) != set(range(PHASE_COUNT)):
        fail("v0a weights TSV must contain exactly phases 0..12")
    return [values[phase] for phase in range(PHASE_COUNT)]


def append_header(output: bytearray, weight_count: int) -> None:
    output.extend(b"VOPWGT\0\0")
    output.extend(
        struct.pack(
            "<HHHHHHHHI",
            FORMAT_VERSION,
            1,
            1,
            SCORE_SCALE,
            PHASE_COUNT,
            len(PATTERNS),
            len(PATTERN_SET_ID),
            0,
            weight_count,
        )
    )
    output.extend(PATTERN_SET_ID.encode("utf-8"))


def make_artifact(phase_biases: list[int]) -> tuple[bytes, int]:
    stride = 1 + sum(checked_pattern_size(length) for _, length in PATTERNS)
    weight_count = stride * PHASE_COUNT

    output = bytearray()
    append_header(output, weight_count)
    for phase in range(PHASE_COUNT):
        output.extend(struct.pack("<i", phase_biases[phase]))
        for _ in range(1, stride):
            output.extend(struct.pack("<i", 0))

    checksum = zlib.crc32(output) & 0xFFFFFFFF
    output.extend(struct.pack("<I", checksum))
    return bytes(output), checksum


def write_manifest(
    path: Path,
    weights_path: Path,
    checksum: int,
    artifact_size: int,
    phase_biases: list[int],
) -> None:
    manifest = {
        "artifact_id": "tiny-smoke-phase-bias-v0a-artifact-v1",
        "format": "vibe-othello-pattern-eval",
        "format_version": FORMAT_VERSION,
        "bit_order": "a1-lsb",
        "score_unit": "disc-diff",
        "score_scale": SCORE_SCALE,
        "phase_count": PHASE_COUNT,
        "pattern_set_id": PATTERN_SET_ID,
        "weights_file": weights_path.name,
        "weights_size_bytes": artifact_size,
        "weights_checksum": f"0x{checksum:08x}",
        "trainer_version": "phase-bias-v0a",
        "notes": NOTE,
        "quantization": "round-half-away-from-zero to int32 search::Score",
        "phase_bias": phase_biases,
        "patterns": [
            {"pattern_id": pattern_id, "length": length, "weights": "all-zero"}
            for pattern_id, length in PATTERNS
        ],
    }
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--weights-tsv", required=True, type=Path)
    parser.add_argument("--weights-out", required=True, type=Path)
    parser.add_argument("--manifest-out", required=True, type=Path)
    args = parser.parse_args()

    try:
        phase_biases = load_phase_biases(args.weights_tsv)
        artifact, checksum = make_artifact(phase_biases)
        args.weights_out.parent.mkdir(parents=True, exist_ok=True)
        args.manifest_out.parent.mkdir(parents=True, exist_ok=True)
        args.weights_out.write_bytes(artifact)
        write_manifest(args.manifest_out, args.weights_out, checksum, len(artifact), phase_biases)
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 1

    print(f"format_version={FORMAT_VERSION}")
    print("bit_order=a1-lsb")
    print("score_unit=disc-diff")
    print(f"score_scale={SCORE_SCALE}")
    print(f"phase_count={PHASE_COUNT}")
    print(f"pattern_set_id={PATTERN_SET_ID}")
    print(f"weights_checksum=0x{checksum:08x}")
    print(f"weights_size_bytes={len(artifact)}")
    print("trainer_version=phase-bias-v0a")
    print(f"notes={NOTE}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
