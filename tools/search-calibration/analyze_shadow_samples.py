#!/usr/bin/env python3
"""Analyze local-only MPC shadow calibration samples deterministically."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any, Sequence


SAMPLE_SCHEMA_VERSION = 1
REPORT_SCHEMA_VERSION = "mpc-shadow-calibration-report-v1"
HEX64 = re.compile(r"^[0-9a-f]{16}$")
REPO_SHA = re.compile(r"^[0-9a-f]{7,64}$")
MOVE = re.compile(r"^(?:pass|[a-h][1-8])$")
REQUIRED_FIELDS = {
    "schema_version",
    "repo_sha",
    "search_config_id",
    "evaluator_id",
    "artifact_id",
    "canonical_position_hash",
    "phase",
    "occupied_count",
    "empties",
    "ply",
    "node_type",
    "pv_node",
    "cut_node",
    "all_node",
    "deep_depth",
    "shallow_depth",
    "alpha",
    "beta",
    "shallow_score",
    "deep_score",
    "deep_bound",
    "shallow_best_move",
    "deep_best_move",
    "best_move_agreement",
    "pass_state",
    "terminal_state",
    "exact_handoff_eligible",
    "actual_deep_result",
    "hypothetical_cut_high",
    "hypothetical_cut_low",
    "false_cut_high_candidate",
    "false_cut_low_candidate",
    "sampling_seed",
    "search_identity",
}


class CalibrationInputError(ValueError):
    """Raised for invalid sample input."""


def _is_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _require(condition: bool, location: str, message: str) -> None:
    if not condition:
        raise CalibrationInputError(f"{location}: {message}")


def validate_sample(sample: Any, location: str) -> dict[str, Any]:
    _require(isinstance(sample, dict), location, "sample must be a JSON object")
    missing = sorted(REQUIRED_FIELDS - sample.keys())
    _require(not missing, location, f"missing required field(s): {', '.join(missing)}")

    _require(
        _is_int(sample["schema_version"])
        and sample["schema_version"] == SAMPLE_SCHEMA_VERSION,
        location,
        f"schema_version must be {SAMPLE_SCHEMA_VERSION}",
    )
    for field in ("repo_sha", "search_config_id", "evaluator_id", "artifact_id"):
        _require(
            isinstance(sample[field], str) and bool(sample[field]),
            location,
            f"{field} must be a non-empty string",
        )
    _require(
        REPO_SHA.fullmatch(sample["repo_sha"]) is not None,
        location,
        "repo_sha must be a lowercase hexadecimal Git SHA",
    )
    _require(
        isinstance(sample["canonical_position_hash"], str)
        and HEX64.fullmatch(sample["canonical_position_hash"]) is not None,
        location,
        "canonical_position_hash must be 16 lowercase hexadecimal characters",
    )
    for field, lower, upper in (
        ("phase", 0, 12),
        ("occupied_count", 0, 64),
        ("empties", 0, 64),
        ("ply", 0, 127),
    ):
        _require(_is_int(sample[field]) and lower <= sample[field] <= upper, location, f"{field} is out of range")
    _require(sample["occupied_count"] + sample["empties"] == 64, location, "occupied_count + empties must equal 64")

    _require(sample["node_type"] in ("pv", "cut", "all"), location, "invalid node_type")
    for field in (
        "pv_node",
        "cut_node",
        "all_node",
        "best_move_agreement",
        "pass_state",
        "terminal_state",
        "exact_handoff_eligible",
        "hypothetical_cut_high",
        "hypothetical_cut_low",
        "false_cut_high_candidate",
        "false_cut_low_candidate",
    ):
        _require(isinstance(sample[field], bool), location, f"{field} must be boolean")
    _require(sample["pv_node"] == (sample["node_type"] == "pv"), location, "pv_node disagrees with node_type")
    _require(sample["cut_node"] == (sample["node_type"] == "cut"), location, "cut_node disagrees with node_type")
    _require(sample["all_node"] == (sample["node_type"] == "all"), location, "all_node disagrees with node_type")

    for field in ("deep_depth", "shallow_depth", "alpha", "beta", "shallow_score", "deep_score"):
        _require(_is_int(sample[field]), location, f"{field} must be an integer")
    _require(sample["deep_depth"] > sample["shallow_depth"] >= 0, location, "depths must satisfy deep_depth > shallow_depth >= 0")
    _require(sample["alpha"] < sample["beta"], location, "alpha must be less than beta")
    _require(sample["deep_bound"] in ("upper", "exact", "lower"), location, "invalid deep_bound")
    _require(sample["actual_deep_result"] in ("fail_low", "exact", "fail_high"), location, "invalid actual_deep_result")
    if sample["deep_score"] <= sample["alpha"]:
        expected_result = "fail_low"
    elif sample["deep_score"] >= sample["beta"]:
        expected_result = "fail_high"
    else:
        expected_result = "exact"
    expected_bound = {"fail_low": "upper", "exact": "exact", "fail_high": "lower"}[expected_result]
    _require(sample["actual_deep_result"] == expected_result, location, "actual_deep_result disagrees with score/window")
    _require(sample["deep_bound"] == expected_bound, location, "deep_bound disagrees with score/window")

    for field in ("shallow_best_move", "deep_best_move"):
        value = sample[field]
        _require(value is None or (isinstance(value, str) and MOVE.fullmatch(value) is not None), location, f"invalid {field}")
    expected_agreement = sample["shallow_best_move"] is not None and sample["shallow_best_move"] == sample["deep_best_move"]
    _require(sample["best_move_agreement"] == expected_agreement, location, "best_move_agreement disagrees with moves")
    _require(_is_int(sample["sampling_seed"]) and sample["sampling_seed"] >= 0, location, "sampling_seed must be non-negative")
    _require(isinstance(sample["search_identity"], str) and HEX64.fullmatch(sample["search_identity"]) is not None, location, "search_identity must be 16 lowercase hexadecimal characters")

    _require(sample["hypothetical_cut_high"] == (sample["shallow_score"] >= sample["beta"]), location, "hypothetical_cut_high disagrees with score/window")
    _require(sample["hypothetical_cut_low"] == (sample["shallow_score"] <= sample["alpha"]), location, "hypothetical_cut_low disagrees with score/window")
    _require(sample["false_cut_high_candidate"] == (sample["hypothetical_cut_high"] and sample["deep_score"] < sample["beta"]), location, "false_cut_high_candidate disagrees with scores")
    _require(sample["false_cut_low_candidate"] == (sample["hypothetical_cut_low"] and sample["deep_score"] > sample["alpha"]), location, "false_cut_low_candidate disagrees with scores")
    return sample


def discover_inputs(paths: Sequence[Path]) -> list[Path]:
    files: list[Path] = []
    for path in paths:
        if path.is_dir():
            files.extend(item for item in path.rglob("*") if item.is_file() and item.suffix in (".json", ".jsonl"))
        else:
            files.append(path)
    return sorted(set(files), key=lambda item: str(item))


def load_samples(paths: Sequence[Path]) -> tuple[list[dict[str, Any]], str, int]:
    files = discover_inputs(paths)
    samples: list[dict[str, Any]] = []
    content_digests: list[bytes] = []
    for path in files:
        try:
            raw = path.read_bytes()
        except OSError as error:
            raise CalibrationInputError(f"{path}: cannot read input: {error}") from error
        content_digests.append(hashlib.sha256(raw).digest())
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError as error:
            raise CalibrationInputError(f"{path}: input is not UTF-8") from error

        if path.suffix == ".jsonl":
            for line_number, line in enumerate(text.splitlines(), start=1):
                if not line.strip():
                    continue
                try:
                    value = json.loads(line)
                except json.JSONDecodeError as error:
                    raise CalibrationInputError(f"{path}:{line_number}: malformed JSON: {error.msg}") from error
                samples.append(validate_sample(value, f"{path}:{line_number}"))
        else:
            if not text.strip():
                continue
            try:
                value = json.loads(text)
            except json.JSONDecodeError as error:
                raise CalibrationInputError(f"{path}: malformed JSON: {error.msg}") from error
            if isinstance(value, dict) and "samples" in value:
                value = value["samples"]
            _require(isinstance(value, list), str(path), "JSON input must be an array or an object containing samples")
            for index, sample in enumerate(value):
                samples.append(validate_sample(sample, f"{path}:samples[{index}]"))

    checksum = hashlib.sha256()
    checksum.update(b"mpc-shadow-input-v1\0")
    for digest in sorted(content_digests):
        checksum.update(digest)
    return samples, checksum.hexdigest(), len(files)


def percentile(values: Sequence[float], probability: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    location = (len(ordered) - 1) * probability
    lower = math.floor(location)
    upper = math.ceil(location)
    if lower == upper:
        return ordered[lower]
    weight = location - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def clean_float(value: float) -> float:
    rounded = round(value, 12)
    return 0.0 if rounded == 0 else rounded


def ratio(numerator: int, denominator: int) -> float:
    return clean_float(numerator / denominator) if denominator else 0.0


def analyze_group(key: tuple[int, int, int, str], rows: Sequence[dict[str, Any]]) -> dict[str, Any]:
    ordered = sorted(rows, key=lambda row: json.dumps(row, sort_keys=True, separators=(",", ":")))
    xs = [float(row["shallow_score"]) for row in ordered]
    ys = [float(row["deep_score"]) for row in ordered]
    mean_x = sum(xs) / len(xs)
    mean_y = sum(ys) / len(ys)
    denominator = sum((value - mean_x) ** 2 for value in xs)
    slope = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys)) / denominator if denominator else 0.0
    intercept = mean_y - slope * mean_x
    residuals = [y - (intercept + slope * x) for x, y in zip(xs, ys)]
    residual_mean = sum(residuals) / len(residuals)
    residual_stddev = math.sqrt(sum((value - residual_mean) ** 2 for value in residuals) / len(residuals))
    absolute = [abs(value) for value in residuals]

    hypothetical_high = sum(bool(row["hypothetical_cut_high"]) for row in ordered)
    hypothetical_low = sum(bool(row["hypothetical_cut_low"]) for row in ordered)
    false_high = sum(bool(row["false_cut_high_candidate"]) for row in ordered)
    false_low = sum(bool(row["false_cut_low_candidate"]) for row in ordered)
    agreements = sum(bool(row["best_move_agreement"]) for row in ordered)

    confidence_coverage = []
    for confidence in (0.90, 0.95, 0.99):
        margin = percentile(absolute, confidence)
        cut_high = 0
        cut_low = 0
        false_cuts = 0
        for row in ordered:
            predicted = intercept + slope * float(row["shallow_score"])
            high = predicted - margin >= row["beta"]
            low = predicted + margin <= row["alpha"]
            cut_high += high
            cut_low += low
            false_cuts += high and row["deep_score"] < row["beta"]
            false_cuts += low and row["deep_score"] > row["alpha"]
        cut_count = cut_high + cut_low
        confidence_coverage.append(
            {
                "confidence": confidence,
                "margin": clean_float(margin),
                "cut_high_count": cut_high,
                "cut_low_count": cut_low,
                "cut_count": cut_count,
                "coverage": ratio(cut_count, len(ordered)),
                "false_cut_count": false_cuts,
                "false_cut_rate": ratio(false_cuts, cut_count),
            }
        )

    phase, deep_depth, shallow_depth, node_type = key
    return {
        "phase": phase,
        "deep_depth": deep_depth,
        "shallow_depth": shallow_depth,
        "node_type": node_type,
        "sample_count": len(ordered),
        "linear_regression": {"a": clean_float(intercept), "b": clean_float(slope)},
        "residual_mean": clean_float(residual_mean),
        "residual_standard_deviation": clean_float(residual_stddev),
        "mae": clean_float(sum(absolute) / len(absolute)),
        "rmse": clean_float(math.sqrt(sum(value * value for value in residuals) / len(residuals))),
        "residual_percentiles": {
            name: clean_float(percentile(residuals, probability))
            for name, probability in (("p01", 0.01), ("p05", 0.05), ("p50", 0.50), ("p95", 0.95), ("p99", 0.99))
        },
        "absolute_residual_percentiles": {
            name: clean_float(percentile(absolute, probability))
            for name, probability in (("p90", 0.90), ("p95", 0.95), ("p99", 0.99))
        },
        "best_move_agreement": {"count": agreements, "rate": ratio(agreements, len(ordered))},
        "hypothetical_cuts": {
            "high_count": hypothetical_high,
            "low_count": hypothetical_low,
            "count": hypothetical_high + hypothetical_low,
        },
        "false_cut_estimate": {
            "high_count": false_high,
            "low_count": false_low,
            "count": false_high + false_low,
            "rate": ratio(false_high + false_low, hypothetical_high + hypothetical_low),
        },
        "confidence_cut_coverage": confidence_coverage,
        "recommended_conservative_margin": math.ceil(percentile(absolute, 0.99)),
    }


def build_report(samples: Sequence[dict[str, Any]], checksum: str, input_file_count: int) -> dict[str, Any]:
    groups: dict[tuple[int, int, int, str], list[dict[str, Any]]] = defaultdict(list)
    for sample in samples:
        key = (sample["phase"], sample["deep_depth"], sample["shallow_depth"], sample["node_type"])
        groups[key].append(sample)
    analyzed = [analyze_group(key, groups[key]) for key in sorted(groups)]
    return {
        "schema_version": REPORT_SCHEMA_VERSION,
        "sample_schema_version": SAMPLE_SCHEMA_VERSION,
        "input_checksum_sha256": checksum,
        "input_file_count": input_file_count,
        "sample_count": len(samples),
        "group_by": ["phase", "deep_depth", "shallow_depth", "node_type"],
        "group_count": len(analyzed),
        "groups": analyzed,
        "notes": [
            "diagnostic only; no coefficient or margin is applied by runtime search",
            "recommended_conservative_margin is ceil(p99 absolute fitted residual)",
            "generated reports and source samples are local-only",
        ],
    }


def markdown_summary(report: dict[str, Any]) -> str:
    lines = [
        "# MPC Shadow Calibration Summary",
        "",
        f"- Input checksum: `{report['input_checksum_sha256']}`",
        f"- Samples: {report['sample_count']}",
        f"- Groups: {report['group_count']}",
        "- Status: diagnostic only; runtime search uses no fitted coefficient or margin.",
        "",
    ]
    if not report["groups"]:
        lines.extend(["No valid samples were supplied.", ""])
        return "\n".join(lines)
    lines.extend(
        [
            "| Phase | Deep | Shallow | Node | N | a | b | RMSE | Move agree | Hyp. cuts | False-cut est. | Margin |",
            "| ---: | ---: | ---: | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
    )
    for group in report["groups"]:
        regression = group["linear_regression"]
        lines.append(
            f"| {group['phase']} | {group['deep_depth']} | {group['shallow_depth']} | {group['node_type']} "
            f"| {group['sample_count']} | {regression['a']:.4f} | {regression['b']:.4f} | {group['rmse']:.4f} "
            f"| {group['best_move_agreement']['rate']:.3f} | {group['hypothetical_cuts']['count']} "
            f"| {group['false_cut_estimate']['rate']:.3f} | {group['recommended_conservative_margin']} |"
        )
    lines.extend(["", "All samples and generated reports are local-only.", ""])
    return "\n".join(lines)


def analyze(inputs: Sequence[Path], json_output: Path, markdown_output: Path) -> dict[str, Any]:
    samples, checksum, input_file_count = load_samples(inputs)
    report = build_report(samples, checksum, input_file_count)
    json_output.parent.mkdir(parents=True, exist_ok=True)
    markdown_output.parent.mkdir(parents=True, exist_ok=True)
    json_output.write_text(json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")
    markdown_output.write_text(markdown_summary(report), encoding="utf-8")
    return report


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="*", type=Path, help="JSON/JSONL sample files or directories")
    parser.add_argument("--json-output", type=Path, required=True)
    parser.add_argument("--markdown-output", type=Path, required=True)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        analyze(args.inputs, args.json_output, args.markdown_output)
    except CalibrationInputError as error:
        print(f"shadow calibration input error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
