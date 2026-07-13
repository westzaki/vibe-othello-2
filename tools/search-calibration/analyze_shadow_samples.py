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


SAMPLE_SCHEMA_VERSION = 2
REPORT_SCHEMA_VERSION = "mpc-shadow-calibration-report-v2"
DEFAULT_MINIMUM_EXACT_PAIRS = 30
HEX64 = re.compile(r"^[0-9a-f]{16}$")
REPO_SHA = re.compile(r"^[0-9a-f]{7,64}$")
MOVE = re.compile(r"^(?:pass|[a-h][1-8])$")
PROVENANCE_FIELDS = ("repo_sha", "search_config_id", "evaluator_id", "artifact_id")
REQUIRED_FIELDS = {
    "schema_version",
    "repo_sha",
    "search_config_id",
    "evaluator_id",
    "artifact_id",
    "collection_config_id",
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
    "shallow_bound",
    "deep_bound",
    "shallow_best_move",
    "deep_best_move",
    "best_move_agreement",
    "pass_state",
    "terminal_state",
    "exact_handoff_eligible",
    "actual_shallow_result",
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
    for field in PROVENANCE_FIELDS:
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
    _require(
        isinstance(sample["collection_config_id"], str)
        and HEX64.fullmatch(sample["collection_config_id"]) is not None,
        location,
        "collection_config_id must be 16 lowercase hexadecimal characters",
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
    for prefix in ("shallow", "deep"):
        bound_field = f"{prefix}_bound"
        result_field = f"actual_{prefix}_result"
        score = sample[f"{prefix}_score"]
        _require(sample[bound_field] in ("upper", "exact", "lower"), location, f"invalid {bound_field}")
        _require(
            sample[result_field] in ("fail_low", "exact", "fail_high"),
            location,
            f"invalid {result_field}",
        )
        if score <= sample["alpha"]:
            expected_result = "fail_low"
        elif score >= sample["beta"]:
            expected_result = "fail_high"
        else:
            expected_result = "exact"
        expected_bound = {"fail_low": "upper", "exact": "exact", "fail_high": "lower"}[
            expected_result
        ]
        _require(
            sample[result_field] == expected_result,
            location,
            f"{result_field} disagrees with score/window",
        )
        _require(
            sample[bound_field] == expected_bound,
            location,
            f"{bound_field} disagrees with score/window",
        )

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
    checksum.update(b"mpc-shadow-input-v2\0")
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


def analyze_group(
    key: tuple[int, int, int, str],
    rows: Sequence[dict[str, Any]],
    minimum_exact_pairs: int,
) -> dict[str, Any]:
    ordered = sorted(rows, key=lambda row: json.dumps(row, sort_keys=True, separators=(",", ":")))
    exact_pairs = [
        row
        for row in ordered
        if row["shallow_bound"] == "exact" and row["deep_bound"] == "exact"
    ]
    regression: dict[str, float] | None = None
    residual_mean: float | None = None
    residual_stddev: float | None = None
    mae: float | None = None
    rmse: float | None = None
    residual_percentiles: dict[str, float] | None = None
    absolute_residual_percentiles: dict[str, float] | None = None
    absolute: list[float] = []
    intercept = 0.0
    slope = 0.0
    if len(exact_pairs) >= 2:
        xs = [float(row["shallow_score"]) for row in exact_pairs]
        ys = [float(row["deep_score"]) for row in exact_pairs]
        mean_x = sum(xs) / len(xs)
        mean_y = sum(ys) / len(ys)
        denominator = sum((value - mean_x) ** 2 for value in xs)
        if denominator:
            slope = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys)) / denominator
            intercept = mean_y - slope * mean_x
            residuals = [y - (intercept + slope * x) for x, y in zip(xs, ys)]
            residual_mean = sum(residuals) / len(residuals)
            residual_stddev = math.sqrt(
                sum((value - residual_mean) ** 2 for value in residuals) / len(residuals)
            )
            absolute = [abs(value) for value in residuals]
            regression = {"a": clean_float(intercept), "b": clean_float(slope)}
            residual_mean = clean_float(residual_mean)
            residual_stddev = clean_float(residual_stddev)
            mae = clean_float(sum(absolute) / len(absolute))
            rmse = clean_float(math.sqrt(sum(value * value for value in residuals) / len(residuals)))
            residual_percentiles = {
                name: clean_float(percentile(residuals, probability))
                for name, probability in (
                    ("p01", 0.01),
                    ("p05", 0.05),
                    ("p50", 0.50),
                    ("p95", 0.95),
                    ("p99", 0.99),
                )
            }
            absolute_residual_percentiles = {
                name: clean_float(percentile(absolute, probability))
                for name, probability in (("p90", 0.90), ("p95", 0.95), ("p99", 0.99))
            }

    hypothetical_high = sum(bool(row["hypothetical_cut_high"]) for row in ordered)
    hypothetical_low = sum(bool(row["hypothetical_cut_low"]) for row in ordered)
    false_high = sum(bool(row["false_cut_high_candidate"]) for row in ordered)
    false_low = sum(bool(row["false_cut_low_candidate"]) for row in ordered)
    agreements = sum(bool(row["best_move_agreement"]) for row in ordered)

    insufficient_samples = len(exact_pairs) < minimum_exact_pairs
    recommendation_eligible = not insufficient_samples and regression is not None
    confidence_coverage = []
    if recommendation_eligible:
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
        "exact_pair_count": len(exact_pairs),
        "bound_observation_count": len(ordered) - len(exact_pairs),
        "insufficient_samples": insufficient_samples,
        "fit_available": regression is not None,
        "recommendation_eligible": recommendation_eligible,
        "linear_regression": regression,
        "residual_mean": residual_mean,
        "residual_standard_deviation": residual_stddev,
        "mae": mae,
        "rmse": rmse,
        "residual_percentiles": residual_percentiles,
        "absolute_residual_percentiles": absolute_residual_percentiles,
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
        "recommended_conservative_margin": (
            math.ceil(percentile(absolute, 0.99)) if recommendation_eligible else None
        ),
    }


def _inventory(
    samples: Sequence[dict[str, Any]], fields: Sequence[str]
) -> list[dict[str, Any]]:
    counts: dict[tuple[Any, ...], int] = defaultdict(int)
    for sample in samples:
        counts[tuple(sample[field] for field in fields)] += 1
    return [
        {**dict(zip(fields, values)), "sample_count": counts[values]}
        for values in sorted(counts)
    ]


def build_report(
    samples: Sequence[dict[str, Any]],
    checksum: str,
    input_file_count: int,
    minimum_exact_pairs: int = DEFAULT_MINIMUM_EXACT_PAIRS,
) -> dict[str, Any]:
    _require(minimum_exact_pairs >= 2, "analyzer", "minimum_exact_pairs must be at least 2")
    provenance_inventory = _inventory(samples, PROVENANCE_FIELDS)
    _require(
        len(provenance_inventory) <= 1,
        "inputs",
        "mixed provenance is not allowed; analyze each "
        "(repo_sha, search_config_id, evaluator_id, artifact_id) tuple separately",
    )
    collection_config_inventory = _inventory(samples, ("collection_config_id",))
    _require(
        len(collection_config_inventory) <= 1,
        "inputs",
        "mixed collection_config_id values are not allowed; analyze each collection policy separately",
    )
    groups: dict[tuple[int, int, int, str], list[dict[str, Any]]] = defaultdict(list)
    for sample in samples:
        key = (sample["phase"], sample["deep_depth"], sample["shallow_depth"], sample["node_type"])
        groups[key].append(sample)
    analyzed = [analyze_group(key, groups[key], minimum_exact_pairs) for key in sorted(groups)]
    exact_pair_count = sum(group["exact_pair_count"] for group in analyzed)
    eligible_group_count = sum(bool(group["recommendation_eligible"]) for group in analyzed)
    insufficient_group_count = sum(bool(group["insufficient_samples"]) for group in analyzed)
    return {
        "schema_version": REPORT_SCHEMA_VERSION,
        "sample_schema_version": SAMPLE_SCHEMA_VERSION,
        "input_checksum_sha256": checksum,
        "input_file_count": input_file_count,
        "sample_count": len(samples),
        "exact_pair_count": exact_pair_count,
        "minimum_exact_pairs_per_group": minimum_exact_pairs,
        "insufficient_samples": not analyzed or insufficient_group_count > 0,
        "recommendation_eligible": eligible_group_count > 0,
        "eligible_group_count": eligible_group_count,
        "insufficient_group_count": insufficient_group_count,
        "provenance_inventory": provenance_inventory,
        "collection_config_inventory": collection_config_inventory,
        "group_by": ["phase", "deep_depth", "shallow_depth", "node_type"],
        "group_count": len(analyzed),
        "groups": analyzed,
        "notes": [
            "diagnostic only; no coefficient or margin is applied by runtime search",
            "value regression uses only rows where shallow_bound and deep_bound are both exact",
            "bound observations are retained only for window and cut diagnostics",
            "recommended_conservative_margin is null below the exact-pair threshold",
            "recommendations are in-sample diagnostics and require separate-seed or holdout validation before runtime adoption",
            "generated reports and source samples are local-only",
        ],
    }


def markdown_summary(report: dict[str, Any]) -> str:
    lines = [
        "# MPC Shadow Calibration Summary",
        "",
        f"- Input checksum: `{report['input_checksum_sha256']}`",
        f"- Samples: {report['sample_count']}",
        f"- Exact pairs: {report['exact_pair_count']}",
        f"- Groups: {report['group_count']}",
        f"- Minimum exact pairs per group: {report['minimum_exact_pairs_per_group']}",
        "- Status: diagnostic only; runtime search uses no fitted coefficient or margin.",
        "",
    ]
    if report["provenance_inventory"]:
        provenance = report["provenance_inventory"][0]
        lines.extend(
            [
                "## Provenance",
                "",
                f"- Repo SHA: `{provenance['repo_sha']}`",
                f"- Search config: `{provenance['search_config_id']}`",
                f"- Evaluator: `{provenance['evaluator_id']}`",
                f"- Artifact: `{provenance['artifact_id']}`",
                f"- Collection config: `{report['collection_config_inventory'][0]['collection_config_id']}`",
                "",
            ]
        )
    if not report["groups"]:
        lines.extend(["No valid samples were supplied.", ""])
        return "\n".join(lines)
    lines.extend(
        [
            "| Phase | Deep | Shallow | Node | N | Exact | a | b | RMSE | Move agree | Hyp. cuts | False-cut est. | Margin |",
            "| ---: | ---: | ---: | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
    )
    for group in report["groups"]:
        regression = group["linear_regression"]
        intercept = f"{regression['a']:.4f}" if regression is not None else "—"
        slope = f"{regression['b']:.4f}" if regression is not None else "—"
        rmse = f"{group['rmse']:.4f}" if group["rmse"] is not None else "—"
        margin = (
            str(group["recommended_conservative_margin"])
            if group["recommended_conservative_margin"] is not None
            else "—"
        )
        lines.append(
            f"| {group['phase']} | {group['deep_depth']} | {group['shallow_depth']} | {group['node_type']} "
            f"| {group['sample_count']} | {group['exact_pair_count']} | {intercept} | {slope} | {rmse} "
            f"| {group['best_move_agreement']['rate']:.3f} | {group['hypothetical_cuts']['count']} "
            f"| {group['false_cut_estimate']['rate']:.3f} | {margin} |"
        )
    lines.extend(
        [
            "",
            "Margins are withheld for groups below the exact-pair threshold. Separate-seed or holdout validation is required before runtime adoption.",
            "",
            "All samples and generated reports are local-only.",
            "",
        ]
    )
    return "\n".join(lines)


def analyze(
    inputs: Sequence[Path],
    json_output: Path,
    markdown_output: Path,
    minimum_exact_pairs: int = DEFAULT_MINIMUM_EXACT_PAIRS,
) -> dict[str, Any]:
    samples, checksum, input_file_count = load_samples(inputs)
    report = build_report(samples, checksum, input_file_count, minimum_exact_pairs)
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
    parser.add_argument(
        "--minimum-exact-pairs",
        type=int,
        default=DEFAULT_MINIMUM_EXACT_PAIRS,
        help=f"minimum exact/exact pairs per group for a margin (default: {DEFAULT_MINIMUM_EXACT_PAIRS})",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        analyze(
            args.inputs,
            args.json_output,
            args.markdown_output,
            args.minimum_exact_pairs,
        )
    except CalibrationInputError as error:
        print(f"shadow calibration input error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
