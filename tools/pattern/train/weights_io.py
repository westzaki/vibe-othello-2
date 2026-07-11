"""Pattern trainer weight serialization and checksum helpers."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from dataset_contract import (
    PHASES,
    PHASE_COUNT,
    PHASE_MAPPING_ID,
    SCORE_UNIT,
    TRAINER_ALGORITHM_V0A,
    WEIGHTS_SCHEMA_VERSION_V1,
    WEIGHTS_SCHEMA_VERSION_V2,
)
from objectives import PatternWeights


@dataclass(frozen=True)
class InitialWeights:
    phase_bias: dict[str, float]
    pattern_weights: PatternWeights
    checksum: str
    schema_version: str

@dataclass(frozen=True)
class WeightsMetadata:
    pattern_set_id: str
    pattern_contract_digest: str
    index_mode: str
    phase_count: int
    phase_mapping_id: str
    score_unit: str

def load_json_object(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"cannot read JSON object: {path}: {error}") from error
    if not isinstance(data, dict):
        raise RuntimeError(f"JSON root must be an object: {path}")
    return data


def load_initial_weights(path: Path, expected_metadata: WeightsMetadata | None) -> InitialWeights:
    try:
        source_bytes = path.read_bytes()
        payload = json.loads(source_bytes.decode("utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise RuntimeError(f"cannot read initial weights: {path}: {error}") from error
    if not isinstance(payload, dict):
        raise RuntimeError("initial weights JSON root must be an object")
    phase_bias_payload = payload.get("phase_bias")
    if not isinstance(phase_bias_payload, dict) or set(phase_bias_payload) != {
        str(phase) for phase in PHASES
    }:
        raise RuntimeError("initial weights phase_bias must contain exactly phases 0..12")
    phase_bias: dict[str, float] = {}
    for phase in PHASES:
        value = phase_bias_payload[str(phase)]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
            raise RuntimeError(f"initial weights phase_bias[{phase}] must be finite numeric")
        phase_bias[str(phase)] = float(value)

    rows = payload.get("pattern_weights")
    if not isinstance(rows, list):
        raise RuntimeError("initial weights pattern_weights must be an array")
    pattern_weights: PatternWeights = {}
    for index, row in enumerate(rows):
        if not isinstance(row, dict) or set(row) != {
            "phase",
            "pattern_id",
            "ternary_index",
            "weight",
        }:
            raise RuntimeError(f"initial weights pattern_weights[{index}] has invalid fields")
        phase = row["phase"]
        pattern_id = row["pattern_id"]
        ternary_index = row["ternary_index"]
        weight = row["weight"]
        if isinstance(phase, bool) or not isinstance(phase, int) or phase not in PHASES:
            raise RuntimeError(f"initial weights pattern_weights[{index}].phase is invalid")
        if not isinstance(pattern_id, str) or not pattern_id:
            raise RuntimeError(f"initial weights pattern_weights[{index}].pattern_id is invalid")
        if isinstance(ternary_index, bool) or not isinstance(ternary_index, int) or ternary_index < 0:
            raise RuntimeError(f"initial weights pattern_weights[{index}].ternary_index is invalid")
        if isinstance(weight, bool) or not isinstance(weight, (int, float)) or not math.isfinite(weight):
            raise RuntimeError(f"initial weights pattern_weights[{index}].weight must be finite numeric")
        key = (phase, pattern_id, ternary_index)
        if key in pattern_weights:
            raise RuntimeError(f"initial weights contain duplicate key: {key}")
        if float(weight) != 0.0:
            pattern_weights[key] = float(weight)

    schema_version = payload.get("weights_schema_version")
    if schema_version == WEIGHTS_SCHEMA_VERSION_V2:
        if expected_metadata is None:
            raise RuntimeError("v2 initial weights require current pattern metadata")
        for field, expected in metadata_json(expected_metadata).items():
            if payload.get(field) != expected:
                raise RuntimeError(
                    f"initial weights {field} mismatch: {payload.get(field)!r} != {expected!r}"
                )
    elif payload.get("schema_version") == 1 and isinstance(payload.get("trainer_version"), str):
        schema_version = "legacy-pattern-weights-v1"
    else:
        raise RuntimeError("initial weights schema is unsupported")

    return InitialWeights(
        phase_bias=phase_bias,
        pattern_weights=pattern_weights,
        checksum=f"sha256:{hashlib.sha256(source_bytes).hexdigest()}",
        schema_version=schema_version,
    )


def optional_string(value: Any, field: str) -> str | None:
    if value is None:
        return None
    if not isinstance(value, str) or not value:
        raise RuntimeError(f"{field} must be a non-empty string")
    return value


def optional_int(value: Any, field: str) -> int | None:
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, int):
        raise RuntimeError(f"{field} must be an integer")
    return value


def merged_metadata_field(
    name: str,
    explicit: str | int | None,
    report: dict[str, Any] | None,
    *,
    default: str | int | None = None,
) -> str | int | None:
    report_value = None if report is None else report.get(name)
    if isinstance(default, int):
        report_value = optional_int(report_value, f"dataset_report.{name}")
    else:
        report_value = optional_string(report_value, f"dataset_report.{name}")
    value = explicit if explicit is not None else report_value
    if value is None:
        value = default
    if explicit is not None and report_value is not None and explicit != report_value:
        raise RuntimeError(
            f"{name} mismatch between CLI ({explicit!r}) and dataset report ({report_value!r})"
        )
    return value


def weights_metadata_for(args: argparse.Namespace) -> WeightsMetadata | None:
    if args.mode == TRAINER_ALGORITHM_V0A:
        return None
    if args.weights_schema_version == WEIGHTS_SCHEMA_VERSION_V1:
        return None

    report = load_json_object(args.dataset_report) if args.dataset_report is not None else None
    pattern_set_id = merged_metadata_field("pattern_set_id", args.pattern_set_id, report)
    pattern_contract_digest = merged_metadata_field(
        "pattern_contract_digest", args.pattern_contract_digest, report
    )
    index_mode = merged_metadata_field("index_mode", args.index_mode, report)
    phase_count = merged_metadata_field(
        "phase_count", args.phase_count, report, default=PHASE_COUNT
    )
    phase_mapping_id = merged_metadata_field(
        "phase_mapping_id", args.phase_mapping_id, report, default=PHASE_MAPPING_ID
    )
    score_unit = merged_metadata_field("score_unit", args.score_unit, report, default=SCORE_UNIT)

    missing = [
        name
        for name, value in (
            ("pattern_set_id", pattern_set_id),
            ("pattern_contract_digest", pattern_contract_digest),
            ("index_mode", index_mode),
            ("phase_count", phase_count),
            ("phase_mapping_id", phase_mapping_id),
            ("score_unit", score_unit),
        )
        if value is None
    ]
    if missing:
        raise RuntimeError(
            "pattern-eval-weights-v2 requires metadata fields: " + ", ".join(missing)
        )
    if index_mode not in {"raw", "canonical"}:
        raise RuntimeError("index_mode must be raw or canonical")
    if phase_count != PHASE_COUNT:
        raise RuntimeError(f"phase_count must be {PHASE_COUNT}")
    if not isinstance(pattern_set_id, str):
        raise RuntimeError("pattern_set_id must be a string")
    if not isinstance(pattern_contract_digest, str):
        raise RuntimeError("pattern_contract_digest must be a string")
    if not isinstance(index_mode, str):
        raise RuntimeError("index_mode must be a string")
    if not isinstance(phase_mapping_id, str):
        raise RuntimeError("phase_mapping_id must be a string")
    if not isinstance(score_unit, str):
        raise RuntimeError("score_unit must be a string")

    return WeightsMetadata(
        pattern_set_id=pattern_set_id,
        pattern_contract_digest=pattern_contract_digest,
        index_mode=index_mode,
        phase_count=phase_count,
        phase_mapping_id=phase_mapping_id,
        score_unit=score_unit,
    )

def stable_float(value: Any) -> Any:
    if isinstance(value, float):
        return float(f"{value:.12g}")
    if isinstance(value, dict):
        return {key: stable_float(nested) for key, nested in value.items()}
    if isinstance(value, list):
        return [stable_float(nested) for nested in value]
    return value


def nonzero_pattern_weight_items(pattern_weights: PatternWeights) -> list[dict[str, Any]]:
    return [
        {
            "phase": phase,
            "pattern_id": pattern_id,
            "ternary_index": ternary_index,
            "weight": weight,
        }
        for (phase, pattern_id, ternary_index), weight in sorted(pattern_weights.items())
        if weight != 0.0
    ]


def weight_count_by_phase(pattern_weights: PatternWeights) -> dict[str, int]:
    counts = {str(phase): 0 for phase in PHASES}
    for phase, _pattern_id, _ternary_index in pattern_weights:
        counts[str(phase)] += 1
    return counts


def weight_count_by_pattern(pattern_weights: PatternWeights) -> dict[str, int]:
    counts: dict[str, int] = {}
    for _phase, pattern_id, _ternary_index in pattern_weights:
        counts[pattern_id] = counts.get(pattern_id, 0) + 1
    return {pattern_id: counts[pattern_id] for pattern_id in sorted(counts)}


def top_abs_weights(pattern_weights: PatternWeights, limit: int = 20) -> list[dict[str, Any]]:
    rows = nonzero_pattern_weight_items(pattern_weights)
    rows.sort(
        key=lambda row: (
            -abs(float(row["weight"])),
            int(row["phase"]),
            str(row["pattern_id"]),
            int(row["ternary_index"]),
        )
    )
    return rows[:limit]


def metadata_json(metadata: WeightsMetadata) -> dict[str, Any]:
    return {
        "pattern_set_id": metadata.pattern_set_id,
        "pattern_contract_digest": metadata.pattern_contract_digest,
        "index_mode": metadata.index_mode,
        "phase_count": metadata.phase_count,
        "phase_mapping_id": metadata.phase_mapping_id,
        "score_unit": metadata.score_unit,
    }


def pattern_weights_json(
    phase_bias: dict[str, float],
    pattern_weights: PatternWeights,
    args: argparse.Namespace,
    metadata: WeightsMetadata | None,
) -> str:
    payload: dict[str, Any] = {
        "weights_schema_version": args.weights_schema_version,
        "phase_bias": {str(phase): phase_bias[str(phase)] for phase in PHASES},
        "pattern_weights": nonzero_pattern_weight_items(pattern_weights),
    }
    if args.weights_schema_version == WEIGHTS_SCHEMA_VERSION_V2:
        if metadata is None:
            raise RuntimeError("pattern-eval-weights-v2 requires pattern contract metadata")
        payload.update(metadata_json(metadata))
    return json.dumps(stable_float(payload), indent=2, sort_keys=True) + "\n"


def weights_checksum_for(weights_text: str) -> str:
    return f"sha256:{hashlib.sha256(weights_text.encode('utf-8')).hexdigest()}"


def checksum_for(report: dict[str, Any], weights_text: str) -> str:
    payload = json.dumps(report, sort_keys=True, separators=(",", ":")).encode("utf-8")
    digest = hashlib.sha256(payload + b"\n" + weights_text.encode("utf-8")).hexdigest()
    return f"sha256:{digest}"


def weights_tsv(phase_bias: dict[str, float]) -> str:
    lines = ["phase\tbias"]
    for phase in PHASES:
        lines.append(f"{phase}\t{phase_bias[str(phase)]:.12g}")
    return "\n".join(lines) + "\n"
