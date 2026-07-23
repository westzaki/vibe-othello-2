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


SAMPLE_SCHEMA_VERSION = 5
REPORT_SCHEMA_VERSION = "mpc-shadow-calibration-report-v6"
DEFAULT_MINIMUM_EXACT_PAIRS = 30
DOMAIN_BUCKET_WIDTH = 4
VERIFICATION_ALPHA = -30_000
VERIFICATION_BETA = 30_000
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
    "search_role",
    "node_type",
    "pv_node",
    "cut_node",
    "all_node",
    "collection_pair_index",
    "collection_pair_count",
    "same_deep_pair_index",
    "same_deep_pair_count",
    "deep_depth",
    "shallow_depth",
    "official_alpha",
    "official_beta",
    "official_deep_score",
    "official_deep_bound",
    "shallow_verification_score",
    "deep_verification_score",
    "shallow_verification_bound",
    "deep_verification_bound",
    "shallow_verification_best_move",
    "deep_verification_best_move",
    "verification_best_move_agreement",
    "pass_state",
    "terminal_state",
    "search_mode",
    "exact_handoff_enabled",
    "exact_handoff_threshold",
    "exact_handoff_distance",
    "exact_handoff_eligible",
    "actual_official_deep_result",
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
        ("occupied_count", 4, 64),
        ("empties", 0, 60),
        ("ply", 0, 127),
    ):
        _require(_is_int(sample[field]) and lower <= sample[field] <= upper, location, f"{field} is out of range")
    _require(sample["occupied_count"] + sample["empties"] == 64, location, "occupied_count + empties must equal 64")

    _require(
        sample["search_role"] in ("pv", "non_pv_scout", "other"),
        location,
        "invalid search_role",
    )
    _require(sample["node_type"] in ("pv", "cut", "all"), location, "invalid node_type")
    for field in (
        "pv_node",
        "cut_node",
        "all_node",
        "verification_best_move_agreement",
        "pass_state",
        "terminal_state",
        "exact_handoff_enabled",
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
    _require(
        sample["pv_node"] == (sample["search_role"] == "pv"),
        location,
        "pv_node disagrees with search_role",
    )
    _require(
        sample["search_mode"] in ("move", "analyze", "exact_score", "win_loss_draw"),
        location,
        "invalid search_mode",
    )
    for field in (
        "collection_pair_index",
        "collection_pair_count",
        "same_deep_pair_index",
        "same_deep_pair_count",
        "exact_handoff_threshold",
        "exact_handoff_distance",
    ):
        _require(_is_int(sample[field]), location, f"{field} must be an integer")
    _require(
        0 < sample["collection_pair_count"] <= 65_535
        and 0 <= sample["collection_pair_index"] < sample["collection_pair_count"],
        location,
        "collection pair index/count is invalid",
    )
    _require(
        0 < sample["same_deep_pair_count"] <= sample["collection_pair_count"]
        and 0 <= sample["same_deep_pair_index"] < sample["same_deep_pair_count"],
        location,
        "same-deep pair index/count is invalid",
    )
    _require(
        0 <= sample["exact_handoff_threshold"] <= 60
        and 0 <= sample["exact_handoff_distance"] <= 60,
        location,
        "exact handoff threshold/distance is out of range",
    )
    if sample["exact_handoff_enabled"]:
        _require(
            sample["exact_handoff_threshold"] > 0,
            location,
            "enabled exact handoff requires a positive threshold",
        )
        expected_distance = max(0, sample["empties"] - sample["exact_handoff_threshold"])
        _require(
            sample["exact_handoff_distance"] == expected_distance,
            location,
            "exact_handoff_distance disagrees with empties/threshold",
        )
        _require(
            sample["exact_handoff_eligible"]
            == (sample["empties"] <= sample["exact_handoff_threshold"]),
            location,
            "exact_handoff_eligible disagrees with empties/threshold",
        )
    else:
        _require(
            sample["exact_handoff_threshold"] == 0
            and sample["exact_handoff_distance"] == 0
            and sample["exact_handoff_eligible"] is False,
            location,
            "disabled exact handoff must use zero threshold/distance and be ineligible",
        )

    for field in (
        "deep_depth",
        "shallow_depth",
        "official_alpha",
        "official_beta",
        "official_deep_score",
        "shallow_verification_score",
        "deep_verification_score",
    ):
        _require(_is_int(sample[field]), location, f"{field} must be an integer")
    _require(sample["deep_depth"] > sample["shallow_depth"] >= 0, location, "depths must satisfy deep_depth > shallow_depth >= 0")
    _require(
        sample["official_alpha"] < sample["official_beta"],
        location,
        "official_alpha must be less than official_beta",
    )
    if sample["search_role"] == "non_pv_scout":
        _require(
            sample["official_beta"] - sample["official_alpha"] == 1,
            location,
            "non_pv_scout must use an integer null window",
        )
    official_score = sample["official_deep_score"]
    if official_score <= sample["official_alpha"]:
        expected_official_result = "fail_low"
    elif official_score >= sample["official_beta"]:
        expected_official_result = "fail_high"
    else:
        expected_official_result = "exact"
    expected_official_bound = {
        "fail_low": "upper",
        "exact": "exact",
        "fail_high": "lower",
    }[expected_official_result]
    _require(
        sample["actual_official_deep_result"] == expected_official_result,
        location,
        "actual_official_deep_result disagrees with score/window",
    )
    _require(
        sample["official_deep_bound"] == expected_official_bound,
        location,
        "official_deep_bound disagrees with score/window",
    )
    expected_node_type = (
        "pv"
        if sample["pv_node"]
        else ("cut" if expected_official_result == "fail_high" else "all")
    )
    _require(
        sample["node_type"] == expected_node_type,
        location,
        "node_type disagrees with PV state and official result",
    )
    for prefix in ("shallow", "deep"):
        score_field = f"{prefix}_verification_score"
        bound_field = f"{prefix}_verification_bound"
        score = sample[score_field]
        _require(
            sample[bound_field] in ("upper", "exact", "lower"),
            location,
            f"invalid {bound_field}",
        )
        expected_bound = (
            "upper"
            if score <= VERIFICATION_ALPHA
            else ("lower" if score >= VERIFICATION_BETA else "exact")
        )
        _require(
            sample[bound_field] == expected_bound,
            location,
            f"{bound_field} disagrees with the full verification window",
        )

    for field in ("shallow_verification_best_move", "deep_verification_best_move"):
        value = sample[field]
        _require(value is None or (isinstance(value, str) and MOVE.fullmatch(value) is not None), location, f"invalid {field}")
    expected_agreement = (
        sample["shallow_verification_best_move"] is not None
        and sample["shallow_verification_best_move"]
        == sample["deep_verification_best_move"]
    )
    _require(
        sample["verification_best_move_agreement"] == expected_agreement,
        location,
        "verification_best_move_agreement disagrees with moves",
    )
    _require(_is_int(sample["sampling_seed"]) and sample["sampling_seed"] >= 0, location, "sampling_seed must be non-negative")
    _require(isinstance(sample["search_identity"], str) and HEX64.fullmatch(sample["search_identity"]) is not None, location, "search_identity must be 16 lowercase hexadecimal characters")

    _require(
        sample["hypothetical_cut_high"]
        == (sample["shallow_verification_score"] >= sample["official_beta"]),
        location,
        "hypothetical_cut_high disagrees with verification score/official window",
    )
    _require(
        sample["hypothetical_cut_low"]
        == (sample["shallow_verification_score"] <= sample["official_alpha"]),
        location,
        "hypothetical_cut_low disagrees with verification score/official window",
    )
    _require(
        sample["false_cut_high_candidate"]
        == (
            sample["hypothetical_cut_high"]
            and sample["deep_verification_score"] < sample["official_beta"]
        ),
        location,
        "false_cut_high_candidate disagrees with verification scores",
    )
    _require(
        sample["false_cut_low_candidate"]
        == (
            sample["hypothetical_cut_low"]
            and sample["deep_verification_score"] > sample["official_alpha"]
        ),
        location,
        "false_cut_low_candidate disagrees with verification scores",
    )
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
    checksum.update(b"mpc-shadow-input-v5\0")
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


def value_bucket(value: int) -> tuple[int, int]:
    minimum = (value // DOMAIN_BUCKET_WIDTH) * DOMAIN_BUCKET_WIDTH
    return minimum, min(60, minimum + DOMAIN_BUCKET_WIDTH - 1)


def analysis_key(sample: dict[str, Any], classification_field: str) -> tuple[Any, ...]:
    empties_minimum, empties_maximum = value_bucket(sample["empties"])
    if sample["exact_handoff_enabled"]:
        distance_minimum, distance_maximum = value_bucket(sample["exact_handoff_distance"])
    else:
        distance_minimum, distance_maximum = 0, 0
    return (
        sample["phase"],
        sample["deep_depth"],
        sample["shallow_depth"],
        sample[classification_field],
        sample["search_mode"],
        sample["exact_handoff_enabled"],
        sample["exact_handoff_threshold"],
        empties_minimum,
        empties_maximum,
        distance_minimum,
        distance_maximum,
    )


def analyze_group(
    key: tuple[Any, ...],
    rows: Sequence[dict[str, Any]],
    minimum_exact_pairs: int,
    classification_field: str,
    allow_recommendation: bool,
) -> dict[str, Any]:
    ordered = sorted(rows, key=lambda row: json.dumps(row, sort_keys=True, separators=(",", ":")))
    exact_pairs = [
        row
        for row in ordered
        if row["shallow_verification_bound"] == "exact"
        and row["deep_verification_bound"] == "exact"
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
        xs = [float(row["shallow_verification_score"]) for row in exact_pairs]
        ys = [float(row["deep_verification_score"]) for row in exact_pairs]
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
            regression = {
                "intercept": clean_float(intercept),
                "slope": clean_float(slope),
            }
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
    agreements = sum(bool(row["verification_best_move_agreement"]) for row in ordered)

    insufficient_samples = len(exact_pairs) < minimum_exact_pairs
    recommendation_eligible = (
        allow_recommendation and not insufficient_samples and regression is not None
    )
    confidence_coverage = []
    if recommendation_eligible:
        for confidence in (0.90, 0.95, 0.99):
            margin = percentile(absolute, confidence)
            cut_high = 0
            cut_low = 0
            false_cuts = 0
            for row in ordered:
                predicted = intercept + slope * float(row["shallow_verification_score"])
                high = predicted - margin >= row["official_beta"]
                low = predicted + margin <= row["official_alpha"]
                cut_high += high
                cut_low += low
                false_cuts += (
                    high and row["deep_verification_score"] < row["official_beta"]
                )
                false_cuts += (
                    low and row["deep_verification_score"] > row["official_alpha"]
                )
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

    (
        phase,
        deep_depth,
        shallow_depth,
        classification,
        search_mode,
        exact_handoff_enabled,
        exact_handoff_threshold,
        minimum_empties,
        maximum_empties,
        minimum_handoff_distance,
        maximum_handoff_distance,
    ) = key
    observed_empties = sorted({int(row["empties"]) for row in ordered})
    observed_handoff_distances = sorted(
        {int(row["exact_handoff_distance"]) for row in ordered}
    )
    return {
        "phase": phase,
        "deep_depth": deep_depth,
        "shallow_depth": shallow_depth,
        classification_field: classification,
        "search_mode": search_mode,
        "exact_handoff_enabled": exact_handoff_enabled,
        "exact_handoff_threshold": exact_handoff_threshold,
        "observed_domain": {
            "empties_bucket": {
                "minimum": minimum_empties,
                "maximum": maximum_empties,
                "observed_values": observed_empties,
            },
            "exact_handoff_distance_bucket": {
                "minimum": minimum_handoff_distance,
                "maximum": maximum_handoff_distance,
                "observed_values": observed_handoff_distances,
            },
        },
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


def scheduler_node_key(sample: dict[str, Any]) -> tuple[Any, ...]:
    return (
        sample["canonical_position_hash"],
        sample["phase"],
        sample["occupied_count"],
        sample["empties"],
        sample["ply"],
        sample["search_role"],
        sample["deep_depth"],
        sample["official_alpha"],
        sample["official_beta"],
        sample["search_mode"],
        sample["exact_handoff_enabled"],
        sample["exact_handoff_threshold"],
        sample["exact_handoff_distance"],
    )


def validate_pair_population(samples: Sequence[dict[str, Any]]) -> list[dict[str, int]]:
    if not samples:
        return []
    pair_counts = {int(sample["collection_pair_count"]) for sample in samples}
    _require(len(pair_counts) == 1, "inputs", "collection_pair_count must be consistent")
    pair_count = next(iter(pair_counts))
    indexed_pairs: dict[int, tuple[int, int]] = {}
    for sample in samples:
        index = int(sample["collection_pair_index"])
        pair = (int(sample["deep_depth"]), int(sample["shallow_depth"]))
        previous = indexed_pairs.get(index)
        _require(
            previous is None or previous == pair,
            "inputs",
            f"collection pair index {index} maps to multiple depth pairs",
        )
        indexed_pairs[index] = pair
    _require(
        sorted(indexed_pairs) == list(range(pair_count)),
        "inputs",
        "not every ordered collection depth pair was observed",
    )

    nodes: dict[tuple[Any, ...], list[dict[str, Any]]] = defaultdict(list)
    for sample in samples:
        nodes[scheduler_node_key(sample)].append(sample)
    for key, rows in nodes.items():
        expected_count = int(rows[0]["same_deep_pair_count"])
        indices = [int(row["same_deep_pair_index"]) for row in rows]
        _require(
            len(rows) == expected_count and sorted(indices) == list(range(expected_count)),
            "inputs",
            f"sampled node {key[0]} does not contain its complete same-deep pair population",
        )
        _require(
            len(set(indices)) == len(indices),
            "inputs",
            f"sampled node {key[0]} contains a duplicate pair observation",
        )
        for field in (
            "official_deep_score",
            "official_deep_bound",
            "deep_verification_score",
            "deep_verification_bound",
            "deep_verification_best_move",
        ):
            _require(
                len({json.dumps(row[field], sort_keys=True) for row in rows}) == 1,
                "inputs",
                f"sampled node {key[0]} has inconsistent shared deep verification field {field}",
            )
    return [
        {
            "pair_index": index,
            "deep_depth": indexed_pairs[index][0],
            "shallow_depth": indexed_pairs[index][1],
        }
        for index in range(pair_count)
    ]


def scheduler_observations(samples: Sequence[dict[str, Any]]) -> list[dict[str, Any]]:
    nodes: dict[tuple[Any, ...], list[dict[str, Any]]] = defaultdict(list)
    for sample in samples:
        if sample["search_role"] == "non_pv_scout":
            nodes[scheduler_node_key(sample)].append(sample)
    result: list[dict[str, Any]] = []
    for key in sorted(nodes):
        rows = sorted(nodes[key], key=lambda row: int(row["same_deep_pair_index"]))
        first = rows[0]
        identity_payload = json.dumps(key, separators=(",", ":"), ensure_ascii=True)
        result.append(
            {
                "node_id": hashlib.sha256(identity_payload.encode("utf-8")).hexdigest(),
                "canonical_position_hash": first["canonical_position_hash"],
                "phase": first["phase"],
                "empties": first["empties"],
                "search_mode": first["search_mode"],
                "exact_handoff_enabled": first["exact_handoff_enabled"],
                "exact_handoff_threshold": first["exact_handoff_threshold"],
                "exact_handoff_distance": first["exact_handoff_distance"],
                "deep_depth": first["deep_depth"],
                "official_alpha": first["official_alpha"],
                "official_beta": first["official_beta"],
                "deep_verification_score": first["deep_verification_score"],
                "deep_verification_bound": first["deep_verification_bound"],
                "pairs": [
                    {
                        "collection_pair_index": row["collection_pair_index"],
                        "same_deep_pair_index": row["same_deep_pair_index"],
                        "deep_depth": row["deep_depth"],
                        "shallow_depth": row["shallow_depth"],
                        "shallow_verification_score": row["shallow_verification_score"],
                        "shallow_verification_bound": row["shallow_verification_bound"],
                    }
                    for row in rows
                ],
            }
        )
    return result


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
    collection_depth_pairs = validate_pair_population(samples)
    groups: dict[tuple[Any, ...], list[dict[str, Any]]] = defaultdict(list)
    result_groups: dict[tuple[Any, ...], list[dict[str, Any]]] = defaultdict(list)
    for sample in samples:
        key = analysis_key(sample, "search_role")
        groups[key].append(sample)
        result_key = analysis_key(sample, "node_type")
        result_groups[result_key].append(sample)
    analyzed = [
        analyze_group(
            key,
            groups[key],
            minimum_exact_pairs,
            "search_role",
            key[3] == "non_pv_scout",
        )
        for key in sorted(groups)
    ]
    result_diagnostics = [
        analyze_group(
            key,
            result_groups[key],
            minimum_exact_pairs,
            "node_type",
            False,
        )
        for key in sorted(result_groups)
    ]
    exact_pair_count = sum(group["exact_pair_count"] for group in analyzed)
    eligible_group_count = sum(bool(group["recommendation_eligible"]) for group in analyzed)
    probcut_groups = [group for group in analyzed if group["search_role"] == "non_pv_scout"]
    insufficient_group_count = sum(bool(group["insufficient_samples"]) for group in probcut_groups)
    return {
        "schema_version": REPORT_SCHEMA_VERSION,
        "sample_schema_version": SAMPLE_SCHEMA_VERSION,
        "input_checksum_sha256": checksum,
        "input_file_count": input_file_count,
        "sample_count": len(samples),
        "exact_pair_count": exact_pair_count,
        "minimum_exact_pairs_per_group": minimum_exact_pairs,
        "verification_window": {"alpha": VERIFICATION_ALPHA, "beta": VERIFICATION_BETA},
        "insufficient_samples": not probcut_groups or insufficient_group_count > 0,
        "recommendation_eligible": eligible_group_count > 0,
        "eligible_group_count": eligible_group_count,
        "insufficient_group_count": insufficient_group_count,
        "provenance_inventory": provenance_inventory,
        "collection_config_inventory": collection_config_inventory,
        "collection_depth_pairs": collection_depth_pairs,
        "group_by": [
            "phase",
            "deep_depth",
            "shallow_depth",
            "search_role",
            "search_mode",
            "exact_handoff_enabled",
            "exact_handoff_threshold",
            "empties_bucket",
            "exact_handoff_distance_bucket",
        ],
        "group_count": len(analyzed),
        "groups": analyzed,
        "result_diagnostic_group_by": [
            "phase",
            "deep_depth",
            "shallow_depth",
            "node_type",
            "search_mode",
            "exact_handoff_enabled",
            "exact_handoff_threshold",
            "empties_bucket",
            "exact_handoff_distance_bucket",
        ],
        "result_diagnostic_group_count": len(result_diagnostics),
        "result_node_type_groups": result_diagnostics,
        "scheduler_observations": scheduler_observations(samples),
        "notes": [
            "diagnostic only; no coefficient or margin is applied by runtime search",
            "value regression uses only rows where shallow_verification_bound and deep_verification_bound are both exact",
            "official bounds are retained only for node classification and window/cut diagnostics",
            "ProbCut recommendations are fitted only to the result-independent non_pv_scout population, including fail-high and fail-low outcomes",
            "pv/other search roles and post-result pv/cut/all groups are diagnostic only",
            "verification scores are collected by isolated full-window searches",
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
        f"- Verification window: [{report['verification_window']['alpha']}, {report['verification_window']['beta']}]",
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
            "| Phase | Deep | Shallow | Search role | N | Exact | Intercept | Slope | RMSE | Move agree | Hyp. cuts | False-cut est. | Margin |",
            "| ---: | ---: | ---: | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
    )
    for group in report["groups"]:
        regression = group["linear_regression"]
        intercept = f"{regression['intercept']:.4f}" if regression is not None else "—"
        slope = f"{regression['slope']:.4f}" if regression is not None else "—"
        rmse = f"{group['rmse']:.4f}" if group["rmse"] is not None else "—"
        margin = (
            str(group["recommended_conservative_margin"])
            if group["recommended_conservative_margin"] is not None
            else "—"
        )
        lines.append(
            f"| {group['phase']} | {group['deep_depth']} | {group['shallow_depth']} | {group['search_role']} "
            f"| {group['sample_count']} | {group['exact_pair_count']} | {intercept} | {slope} | {rmse} "
            f"| {group['best_move_agreement']['rate']:.3f} | {group['hypothetical_cuts']['count']} "
            f"| {group['false_cut_estimate']['rate']:.3f} | {margin} |"
        )
    lines.extend(
        [
            "",
            "## Post-result node-type diagnostics",
            "",
            "These `pv`/`cut`/`all` groups are diagnostic only and are never profile-adoption inputs.",
            "",
            "| Phase | Deep | Shallow | Node type | N | Exact | False-cut est. |",
            "| ---: | ---: | ---: | :--- | ---: | ---: | ---: |",
        ]
    )
    for group in report["result_node_type_groups"]:
        lines.append(
            f"| {group['phase']} | {group['deep_depth']} | {group['shallow_depth']} "
            f"| {group['node_type']} | {group['sample_count']} | {group['exact_pair_count']} "
            f"| {group['false_cut_estimate']['rate']:.3f} |"
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
