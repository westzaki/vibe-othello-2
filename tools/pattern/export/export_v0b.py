#!/usr/bin/env python3
"""Export pattern intermediate JSON weights to a tiny runtime artifact."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
import subprocess
import sys
import zlib
from pathlib import Path
from typing import Any

from pattern_sets import PatternSetSpec, resolve_pattern_set


FORMAT_VERSION = 1
SCORE_SCALE = 1
PHASE_COUNT = 13
EXPECTED_PHASE_MAPPING_ID = "disc-count-13-v1"
SCORE_UNIT = "disc-diff"
EXPECTED_INDEX_MODE = "raw"
WEIGHTS_SCHEMA_VERSION_V1 = "pattern-eval-weights-v1"
WEIGHTS_SCHEMA_VERSION_V2 = "pattern-eval-weights-v2"
REQUIRED_V2_FIELDS = {
    "weights_schema_version",
    "pattern_set_id",
    "pattern_contract_digest",
    "index_mode",
    "phase_count",
    "phase_mapping_id",
    "score_unit",
    "phase_bias",
    "pattern_weights",
}


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


def load_json_object(path: Path, label: str) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read {label}: {path}: {error}")
    if not isinstance(data, dict):
        fail(f"{label} root must be an object")
    return data


def load_catalog_dump(args: argparse.Namespace, pattern_set: PatternSetSpec) -> dict[str, Any]:
    if args.pattern_contract_json is not None and args.catalog_dump_exe is not None:
        fail("--pattern-contract-json and --catalog-dump-exe are mutually exclusive")
    if args.pattern_contract_json is not None:
        return load_json_object(args.pattern_contract_json, "pattern contract JSON")
    if args.catalog_dump_exe is None:
        fail("pattern-eval-weights-v2 requires --catalog-dump-exe or --pattern-contract-json")

    result = subprocess.run(
        [
            str(args.catalog_dump_exe),
            "--pattern-set",
            pattern_set.pattern_set_id,
            "--index-mode",
            EXPECTED_INDEX_MODE,
        ],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        fail(f"pattern catalog dump failed with exit code {result.returncode}")
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        fail(f"pattern catalog dump did not emit JSON: {error}")
    if not isinstance(payload, dict):
        fail("pattern catalog dump JSON root must be an object")
    return payload


def require_string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value:
        fail(f"{field} must be a non-empty string")
    return value


def require_int(value: Any, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        fail(f"{field} must be an integer")
    return value


def require_list(value: Any, field: str) -> list[Any]:
    if not isinstance(value, list):
        fail(f"{field} must be an array")
    return value


def selected_contract(
    catalog_payload: dict[str, Any], pattern_set: PatternSetSpec
) -> dict[str, Any]:
    if catalog_payload.get("schema_version") != 1:
        fail("pattern contract JSON schema_version must be 1")
    contracts = require_list(catalog_payload.get("pattern_sets"), "pattern_sets")
    matched: list[dict[str, Any]] = []
    for index, item in enumerate(contracts):
        if not isinstance(item, dict):
            fail(f"pattern_sets[{index}] must be an object")
        if (
            item.get("pattern_set_id") == pattern_set.pattern_set_id
            and item.get("index_mode") == EXPECTED_INDEX_MODE
        ):
            matched.append(item)
    if not matched:
        fail(
            "pattern contract JSON does not contain selected pattern_set_id "
            f"{pattern_set.pattern_set_id!r} with index_mode {EXPECTED_INDEX_MODE!r}"
        )
    if len(matched) > 1:
        fail(f"pattern contract JSON has duplicate selected contracts for {pattern_set.pattern_set_id}")
    contract = matched[0]
    require_string(contract.get("pattern_contract_digest"), "pattern_contract_digest")
    return contract


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


def validate_trained_phases(values: list[int] | None) -> list[int] | None:
    if values is None:
        return None
    if not values:
        fail("trained_phases must not be empty")
    if any(phase < 0 or phase >= PHASE_COUNT for phase in values):
        fail("trained_phases entries must be in [0, 12]")
    if len(set(values)) != len(values):
        fail("trained_phases entries must be unique")
    return sorted(values)


def phase_weight_diagnostics(
    phase_biases: list[int], pattern_weights: dict[tuple[int, str, int], int]
) -> list[dict[str, int | bool]]:
    diagnostics: list[dict[str, int | bool]] = []
    for phase, phase_bias in enumerate(phase_biases):
        phase_weights = [
            weight for (weight_phase, _pattern_id, _ternary_index), weight in pattern_weights.items()
            if weight_phase == phase
        ]
        diagnostics.append(
            {
                "phase": phase,
                "nonzero_pattern_weights": sum(weight != 0 for weight in phase_weights),
                "nonzero_phase_bias": phase_bias != 0,
                "max_absolute_weight": max((abs(weight) for weight in phase_weights), default=0),
            }
        )
    return diagnostics


def validate_v2_metadata(
    payload: dict[str, Any],
    pattern_set: PatternSetSpec,
    contract: dict[str, Any],
) -> None:
    missing = sorted(REQUIRED_V2_FIELDS - set(payload))
    if missing:
        fail("pattern-eval-weights-v2 missing required field(s): " + ", ".join(missing))
    if "trainer_algorithm" in payload:
        fail("trainer_algorithm must not be present in weights JSON")

    weights_pattern_set_id = require_string(payload.get("pattern_set_id"), "pattern_set_id")
    if weights_pattern_set_id != pattern_set.pattern_set_id:
        fail(
            "weights pattern_set_id does not match selected pattern set: "
            f"{weights_pattern_set_id!r} != {pattern_set.pattern_set_id!r}"
        )

    contract_pattern_set_id = require_string(
        contract.get("pattern_set_id"), "contract.pattern_set_id"
    )
    if contract_pattern_set_id != pattern_set.pattern_set_id:
        fail(
            "pattern contract pattern_set_id does not match selected pattern set: "
            f"{contract_pattern_set_id!r} != {pattern_set.pattern_set_id!r}"
        )

    weights_digest = require_string(
        payload.get("pattern_contract_digest"), "pattern_contract_digest"
    )
    contract_digest = require_string(
        contract.get("pattern_contract_digest"), "contract.pattern_contract_digest"
    )
    if weights_digest != contract_digest:
        fail(
            "weights pattern_contract_digest does not match runtime pattern contract: "
            f"{weights_digest!r} != {contract_digest!r}"
        )

    weights_index_mode = require_string(payload.get("index_mode"), "index_mode")
    contract_index_mode = require_string(contract.get("index_mode"), "contract.index_mode")
    if weights_index_mode != EXPECTED_INDEX_MODE:
        fail(
            "weights index_mode does not match exporter runtime payload contract: "
            f"{weights_index_mode!r} != {EXPECTED_INDEX_MODE!r}"
        )
    if contract_index_mode != EXPECTED_INDEX_MODE:
        fail(
            "pattern contract index_mode does not match exporter runtime payload contract: "
            f"{contract_index_mode!r} != {EXPECTED_INDEX_MODE!r}"
        )

    phase_count = require_int(payload.get("phase_count"), "phase_count")
    if phase_count != PHASE_COUNT:
        fail(
            "weights phase_count does not match runtime payload contract: "
            f"{phase_count!r} != {PHASE_COUNT!r}"
        )
    score_unit = require_string(payload.get("score_unit"), "score_unit")
    if score_unit != SCORE_UNIT:
        fail(
            "weights score_unit does not match runtime payload contract: "
            f"{score_unit!r} != {SCORE_UNIT!r}"
        )
    phase_mapping_id = require_string(payload.get("phase_mapping_id"), "phase_mapping_id")
    if phase_mapping_id != EXPECTED_PHASE_MAPPING_ID:
        fail(
            "weights phase_mapping_id does not match runtime payload contract: "
            f"{phase_mapping_id!r} != {EXPECTED_PHASE_MAPPING_ID!r}"
        )


def validate_weights(
    payload: dict[str, Any],
    pattern_set: PatternSetSpec,
    args: argparse.Namespace,
) -> tuple[str, list[int], dict[tuple[int, str, int], int]]:
    schema_version = payload.get("weights_schema_version")
    if schema_version == WEIGHTS_SCHEMA_VERSION_V1:
        if not args.allow_legacy_v1:
            fail("pattern-eval-weights-v1 is accepted only with --allow-legacy-v1")
        return (
            WEIGHTS_SCHEMA_VERSION_V1,
            validate_phase_bias(payload),
            validate_pattern_weights(payload, pattern_set),
        )
    if schema_version != WEIGHTS_SCHEMA_VERSION_V2:
        fail(f"weights_schema_version must be {WEIGHTS_SCHEMA_VERSION_V2}")

    contract_payload = load_catalog_dump(args, pattern_set)
    contract = selected_contract(contract_payload, pattern_set)
    validate_v2_metadata(payload, pattern_set, contract)
    return (
        WEIGHTS_SCHEMA_VERSION_V2,
        validate_phase_bias(payload),
        validate_pattern_weights(payload, pattern_set),
    )


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
    source_schema_version: str,
    source_checksum: str,
    checksum: int,
    artifact_size: int,
    phase_biases: list[int],
    phase_diagnostics: list[dict[str, int | bool]],
    trained_phases: list[int] | None,
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
        "source_weights_schema_version": source_schema_version,
        "source_weights_checksum": source_checksum,
        "nonzero_pattern_weights": sum(
            int(diagnostic["nonzero_pattern_weights"]) for diagnostic in phase_diagnostics
        ),
        "notes": pattern_set.note,
        "quantization": "round-half-away-from-zero to int32 search::Score",
        "phase_bias": phase_biases,
        "phase_weight_diagnostics": phase_diagnostics,
        "patterns": [
            {"pattern_id": pattern_id, "length": length, "weights": "sparse-v0b-import"}
            for pattern_id, length in pattern_set.patterns
        ],
    }
    if trained_phases is not None:
        manifest["trained_phases"] = trained_phases
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--weights-json", required=True, type=Path)
    parser.add_argument("--weights-out", required=True, type=Path)
    parser.add_argument("--manifest-out", required=True, type=Path)
    parser.add_argument(
        "--trained-phases",
        nargs="+",
        type=int,
        help="Explicit source-of-truth phases represented in the training data.",
    )
    parser.add_argument("--pattern-set", default="fixed-pattern-fixture-v1")
    parser.add_argument(
        "--catalog-dump-exe",
        type=Path,
        help="Runtime pattern catalog dump executable used to validate v2 weights.",
    )
    parser.add_argument(
        "--pattern-contract-json",
        type=Path,
        help="Precomputed runtime pattern catalog dump JSON used to validate v2 weights.",
    )
    parser.add_argument(
        "--allow-legacy-v1",
        action="store_true",
        help="Accept pattern-eval-weights-v1 for legacy smoke compatibility.",
    )
    args = parser.parse_args()

    try:
        pattern_set = resolve_pattern_set(args.pattern_set)
        payload, source_bytes = load_weights(args.weights_json)
        source_schema_version, phase_biases, pattern_weights = validate_weights(
            payload, pattern_set, args
        )
        trained_phases = validate_trained_phases(args.trained_phases)
        phase_diagnostics = phase_weight_diagnostics(phase_biases, pattern_weights)
        artifact, checksum = make_artifact(phase_biases, pattern_weights, pattern_set)
        args.weights_out.parent.mkdir(parents=True, exist_ok=True)
        args.manifest_out.parent.mkdir(parents=True, exist_ok=True)
        args.weights_out.write_bytes(artifact)
        source_checksum = f"sha256:{hashlib.sha256(source_bytes).hexdigest()}"
        write_manifest(
            args.manifest_out,
            args.weights_out,
            source_schema_version,
            source_checksum,
            checksum,
            len(artifact),
            phase_biases,
            phase_diagnostics,
            trained_phases,
            pattern_set,
        )
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 1

    print(f"format_version={FORMAT_VERSION}")
    print("bit_order=a1-lsb")
    print(f"score_unit={SCORE_UNIT}")
    print(f"score_scale={SCORE_SCALE}")
    print(f"phase_count={PHASE_COUNT}")
    print(f"pattern_set_id={pattern_set.pattern_set_id}")
    print(f"weights_checksum=0x{checksum:08x}")
    print(f"weights_size_bytes={len(artifact)}")
    print(f"source_weights_schema_version={source_schema_version}")
    print(f"source_weights_checksum={source_checksum}")
    print(
        "nonzero_pattern_weights="
        f"{sum(int(diagnostic['nonzero_pattern_weights']) for diagnostic in phase_diagnostics)}"
    )
    print(f"notes={pattern_set.note}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
