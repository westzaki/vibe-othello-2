#!/usr/bin/env python3
"""Convert an explicitly reviewed MPC report selection to a ProbCut profile TSV."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from pathlib import Path
from typing import Any


REPORT_SCHEMA_VERSION = "mpc-shadow-calibration-report-v4"
ADOPTION_SCHEMA_VERSION = "probcut-profile-adoption-v1"
NODE_CLASS = "non_pv_scout_beta_only"
HEADER = (
    "schema_version",
    "profile_id",
    "source_checksum_sha256",
    "evaluator_family",
    "artifact_family",
    "node_class",
    "phase",
    "deep_depth",
    "shallow_depth",
    "regression_slope",
    "intercept",
    "residual_sigma",
    "confidence_multiplier",
    "minimum_shallow_score",
    "maximum_shallow_score",
    "minimum_beta",
    "maximum_beta",
)


class ProfileConversionError(ValueError):
    """Raised when reviewed adoption input cannot be mapped exactly."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ProfileConversionError(message)


def load_json(path: Path, label: str) -> tuple[dict[str, Any], bytes]:
    try:
        raw = path.read_bytes()
        value = json.loads(raw)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ProfileConversionError(f"cannot read {label}: {error}") from error
    require(isinstance(value, dict), f"{label} must be a JSON object")
    return value, raw


def finite_number(value: Any, label: str) -> float:
    require(isinstance(value, (int, float)) and not isinstance(value, bool), f"{label} must be numeric")
    converted = float(value)
    require(math.isfinite(converted), f"{label} must be finite")
    return converted


def integer(value: Any, label: str) -> int:
    require(isinstance(value, int) and not isinstance(value, bool), f"{label} must be an integer")
    return value


def render_profile(report: dict[str, Any], report_raw: bytes, adoption: dict[str, Any]) -> str:
    require(report.get("schema_version") == REPORT_SCHEMA_VERSION, "unsupported report schema")
    provenance = report.get("provenance_inventory")
    require(isinstance(provenance, list) and len(provenance) == 1, "report must have one provenance tuple")
    provenance_row = provenance[0]
    require(isinstance(provenance_row, dict), "invalid report provenance")

    require(adoption.get("schema_version") == ADOPTION_SCHEMA_VERSION, "unsupported adoption schema")
    profile_id = adoption.get("profile_id")
    evaluator_family = adoption.get("evaluator_family")
    artifact_family = adoption.get("artifact_family")
    node_class = adoption.get("node_class")
    require(isinstance(profile_id, str) and bool(profile_id), "profile_id must be non-empty")
    require(
        evaluator_family == provenance_row.get("evaluator_id"),
        "evaluator_family must exactly match report evaluator_id",
    )
    require(
        artifact_family == provenance_row.get("artifact_id"),
        "artifact_family must exactly match report artifact_id",
    )
    require(node_class == NODE_CLASS, f"node_class must be {NODE_CLASS}")

    selections = adoption.get("entries")
    require(isinstance(selections, list) and bool(selections), "entries must be a non-empty array")
    report_groups = report.get("groups")
    require(isinstance(report_groups, list), "report groups must be an array")
    source_checksum = hashlib.sha256(report_raw).hexdigest()

    output_rows: list[tuple[int, list[str]]] = []
    depth_pairs: set[tuple[int, int]] = set()
    phases: set[int] = set()
    for index, selection in enumerate(selections):
        label = f"entries[{index}]"
        require(isinstance(selection, dict), f"{label} must be an object")
        phase = integer(selection.get("phase"), f"{label}.phase")
        deep_depth = integer(selection.get("deep_depth"), f"{label}.deep_depth")
        shallow_depth = integer(selection.get("shallow_depth"), f"{label}.shallow_depth")
        require(0 <= phase <= 12, f"{label}.phase is out of range")
        require(deep_depth > shallow_depth > 0, f"{label} depths are invalid")
        require(phase not in phases, f"duplicate phase {phase}")
        phases.add(phase)
        depth_pairs.add((deep_depth, shallow_depth))

        matches = [
            group
            for group in report_groups
            if isinstance(group, dict)
            and group.get("phase") == phase
            and group.get("deep_depth") == deep_depth
            and group.get("shallow_depth") == shallow_depth
            and group.get("search_role") == "non_pv_scout"
        ]
        require(len(matches) == 1, f"{label} does not select exactly one non_pv_scout group")
        group = matches[0]
        require(group.get("recommendation_eligible") is True, f"{label} group is not recommendation eligible")
        regression = group.get("linear_regression")
        require(isinstance(regression, dict), f"{label} regression is unavailable")
        slope = finite_number(regression.get("slope"), f"{label}.slope")
        intercept = finite_number(regression.get("intercept"), f"{label}.intercept")
        sigma = finite_number(group.get("residual_standard_deviation"), f"{label}.residual_sigma")
        require(slope > 0.0 and sigma >= 0.0, f"{label} regression is unsupported")

        confidence = finite_number(selection.get("confidence_multiplier"), f"{label}.confidence_multiplier")
        minimum_shallow = integer(selection.get("minimum_shallow_score"), f"{label}.minimum_shallow_score")
        maximum_shallow = integer(selection.get("maximum_shallow_score"), f"{label}.maximum_shallow_score")
        minimum_beta = integer(selection.get("minimum_beta"), f"{label}.minimum_beta")
        maximum_beta = integer(selection.get("maximum_beta"), f"{label}.maximum_beta")
        require(confidence > 0.0, f"{label}.confidence_multiplier must be positive")
        require(-30_000 < minimum_shallow <= maximum_shallow < 30_000, f"{label} shallow range is invalid")
        require(-30_000 < minimum_beta <= maximum_beta < 30_000, f"{label} beta range is invalid")

        output_rows.append(
            (
                phase,
                [
                    "1",
                    profile_id,
                    source_checksum,
                    evaluator_family,
                    artifact_family,
                    NODE_CLASS,
                    str(phase),
                    str(deep_depth),
                    str(shallow_depth),
                    format(slope, ".17g"),
                    format(intercept, ".17g"),
                    format(sigma, ".17g"),
                    format(confidence, ".17g"),
                    str(minimum_shallow),
                    str(maximum_shallow),
                    str(minimum_beta),
                    str(maximum_beta),
                ],
            )
        )

    require(len(depth_pairs) == 1, "ProbCut profile v1 supports exactly one depth pair")
    lines = ["\t".join(HEADER)]
    lines.extend("\t".join(row) for _, row in sorted(output_rows))
    return "\n".join(lines) + "\n"


def convert(report_path: Path, adoption_path: Path, output_path: Path) -> None:
    report, report_raw = load_json(report_path, "report")
    adoption, _ = load_json(adoption_path, "adoption specification")
    rendered = render_profile(report, report_raw, adoption)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(rendered, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("adoption", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        convert(args.report, args.adoption, args.output)
    except ProfileConversionError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
