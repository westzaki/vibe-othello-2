#!/usr/bin/env python3
"""Export pattern intermediate JSON weights to a tiny runtime artifact."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
import sys
import zlib
from pathlib import Path
from typing import Any

from pattern_sets import PatternSetSpec, resolve_pattern_set


FORMAT_VERSION = 1
SCORE_SCALE = 1
PHASE_COUNT = 13
WEIGHTS_SCHEMA_VERSION = "pattern-eval-weights-v1"


def checked_pattern_size(length: int) -> int:
    return 3**length


def fail(message: str) -> None:
    raise RuntimeError(message)


def is_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def quantize_weight(value: float) -> int:
    if value >= 0:
        rounded = math.floor(value + 0.5)
    else:
        rounded = math.ceil(value - 0.5)
    if rounded < -(2**31) or rounded > 2**31 - 1:
        fail(f"quantized weight is outside int32 range: {value!r}")
    return int(rounded)


def load_weights(path: Path) -> tuple[dict[str, Any], bytes]:
    try:
        source_bytes = path.read_bytes()
    except OSError as error:
        fail(f"cannot read weights JSON: {path}: {error}")
    try:
        payload = json.loads(source_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        fail(f"weights JSON is invalid: {error}")
    if not isinstance(payload, dict):
        fail("weights JSON root must be an object")
    return payload, source_bytes


def validate_phase_bias(payload: dict[str, Any]) -> list[int]:
    phase_bias = payload.get("phase_bias")
    if not isinstance(phase_bias, dict):
        fail("phase_bias must be an object")
    expected_keys = {str(phase) for phase in range(PHASE_COUNT)}
    if set(phase_bias) != expected_keys:
        fail("phase_bias must contain exactly phases 0..12")

    values: list[int] = []
    for phase in range(PHASE_COUNT):
        value = phase_bias[str(phase)]
        if not is_number(value):
            fail(f"phase_bias[{phase}] must be numeric")
        values.append(quantize_weight(float(value)))
    return values


def validate_pattern_weights(
    payload: dict[str, Any], pattern_set: PatternSetSpec
) -> dict[tuple[int, str, int], int]:
    pattern_weights = payload.get("pattern_weights")
    if not isinstance(pattern_weights, list):
        fail("pattern_weights must be an array")

    pattern_sizes = {
        pattern_id: checked_pattern_size(length) for pattern_id, length in pattern_set.patterns
    }
    weights: dict[tuple[int, str, int], int] = {}
    for row_index, row in enumerate(pattern_weights):
        if not isinstance(row, dict):
            fail(f"pattern_weights[{row_index}] must be an object")
        if set(row) != {"phase", "pattern_id", "ternary_index", "weight"}:
            fail(f"pattern_weights[{row_index}] has unexpected fields")

        phase = row["phase"]
        pattern_id = row["pattern_id"]
        ternary_index = row["ternary_index"]
        weight = row["weight"]
        if not isinstance(phase, int) or isinstance(phase, bool) or phase < 0 or phase > 12:
            fail(f"pattern_weights[{row_index}].phase must be in [0, 12]")
        if not isinstance(pattern_id, str) or pattern_id not in pattern_sizes:
            fail(f"pattern_weights[{row_index}].pattern_id is not in the selected pattern set")
        if (
            not isinstance(ternary_index, int)
            or isinstance(ternary_index, bool)
            or ternary_index < 0
            or ternary_index >= pattern_sizes[pattern_id]
        ):
            fail(f"pattern_weights[{row_index}].ternary_index is outside the table range")
        if not is_number(weight):
            fail(f"pattern_weights[{row_index}].weight must be numeric")

        key = (phase, pattern_id, ternary_index)
        if key in weights:
            fail(f"duplicate pattern weight entry: {key}")
        weights[key] = quantize_weight(float(weight))
    return weights


def validate_weights(
    payload: dict[str, Any], pattern_set: PatternSetSpec
) -> tuple[list[int], dict[tuple[int, str, int], int]]:
    if payload.get("weights_schema_version") != WEIGHTS_SCHEMA_VERSION:
        fail(f"weights_schema_version must be {WEIGHTS_SCHEMA_VERSION}")
    return validate_phase_bias(payload), validate_pattern_weights(payload, pattern_set)


def append_header(output: bytearray, weight_count: int, pattern_set: PatternSetSpec) -> None:
    output.extend(b"VOPWGT\0\0")
    output.extend(
        struct.pack(
            "<HHHHHHHHI",
            FORMAT_VERSION,
            1,
            1,
            SCORE_SCALE,
            PHASE_COUNT,
            len(pattern_set.patterns),
            len(pattern_set.pattern_set_id),
            0,
            weight_count,
        )
    )
    output.extend(pattern_set.pattern_set_id.encode("utf-8"))


def make_artifact(
    phase_biases: list[int],
    pattern_weights: dict[tuple[int, str, int], int],
    pattern_set: PatternSetSpec,
) -> tuple[bytes, int]:
    pattern_offsets: dict[str, int] = {}
    stride = 1
    for pattern_id, length in pattern_set.patterns:
        pattern_offsets[pattern_id] = stride
        stride += checked_pattern_size(length)
    weight_count = stride * PHASE_COUNT

    output = bytearray()
    append_header(output, weight_count, pattern_set)
    weights = [0] * weight_count
    for phase, bias in enumerate(phase_biases):
        weights[phase * stride] = bias
    for (phase, pattern_id, ternary_index), weight in pattern_weights.items():
        weights[phase * stride + pattern_offsets[pattern_id] + ternary_index] = weight
    for weight in weights:
        output.extend(struct.pack("<i", weight))

    checksum = zlib.crc32(output) & 0xFFFFFFFF
    output.extend(struct.pack("<I", checksum))
    return bytes(output), checksum


def write_manifest(
    path: Path,
    weights_path: Path,
    source_checksum: str,
    checksum: int,
    artifact_size: int,
    phase_biases: list[int],
    nonzero_pattern_weights: int,
    pattern_set: PatternSetSpec,
) -> None:
    manifest = {
        "artifact_id": pattern_set.v0b_artifact_id,
        "format": "vibe-othello-pattern-eval",
        "format_version": FORMAT_VERSION,
        "bit_order": "a1-lsb",
        "score_unit": "disc-diff",
        "score_scale": SCORE_SCALE,
        "phase_count": PHASE_COUNT,
        "pattern_set_id": pattern_set.pattern_set_id,
        "weights_file": weights_path.name,
        "weights_size_bytes": artifact_size,
        "weights_checksum": f"0x{checksum:08x}",
        "source_weights_schema_version": WEIGHTS_SCHEMA_VERSION,
        "source_weights_checksum": source_checksum,
        "nonzero_pattern_weights": nonzero_pattern_weights,
        "notes": pattern_set.note,
        "quantization": "round-half-away-from-zero to int32 search::Score",
        "phase_bias": phase_biases,
        "patterns": [
            {"pattern_id": pattern_id, "length": length, "weights": "sparse-v0b-import"}
            for pattern_id, length in pattern_set.patterns
        ],
    }
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--weights-json", required=True, type=Path)
    parser.add_argument("--weights-out", required=True, type=Path)
    parser.add_argument("--manifest-out", required=True, type=Path)
    parser.add_argument("--pattern-set", default="fixed-pattern-fixture-v1")
    args = parser.parse_args()

    try:
        pattern_set = resolve_pattern_set(args.pattern_set)
        payload, source_bytes = load_weights(args.weights_json)
        phase_biases, pattern_weights = validate_weights(payload, pattern_set)
        artifact, checksum = make_artifact(phase_biases, pattern_weights, pattern_set)
        args.weights_out.parent.mkdir(parents=True, exist_ok=True)
        args.manifest_out.parent.mkdir(parents=True, exist_ok=True)
        args.weights_out.write_bytes(artifact)
        source_checksum = f"sha256:{hashlib.sha256(source_bytes).hexdigest()}"
        write_manifest(
            args.manifest_out,
            args.weights_out,
            source_checksum,
            checksum,
            len(artifact),
            phase_biases,
            len(pattern_weights),
            pattern_set,
        )
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 1

    print(f"format_version={FORMAT_VERSION}")
    print("bit_order=a1-lsb")
    print("score_unit=disc-diff")
    print(f"score_scale={SCORE_SCALE}")
    print(f"phase_count={PHASE_COUNT}")
    print(f"pattern_set_id={pattern_set.pattern_set_id}")
    print(f"weights_checksum=0x{checksum:08x}")
    print(f"weights_size_bytes={len(artifact)}")
    print(f"source_weights_schema_version={WEIGHTS_SCHEMA_VERSION}")
    print(f"source_weights_checksum={source_checksum}")
    print(f"nonzero_pattern_weights={len(pattern_weights)}")
    print(f"notes={pattern_set.note}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
