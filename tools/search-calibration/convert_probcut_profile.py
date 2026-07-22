#!/usr/bin/env python3
"""Convert reviewed MPC training/holdout reports to a scheduler-safe profile."""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import math
import sys
from pathlib import Path
from typing import Any, Sequence


REPORT_SCHEMA_VERSION = "mpc-shadow-calibration-report-v5"
ADOPTION_SCHEMA_VERSION = "probcut-profile-adoption-v3"
NODE_CLASS = "non_pv_scout_beta_only"
PROFILE_SCHEMA_VERSION = 3
SCORE_LOSS = -30_000
SCORE_WIN = 30_000
SCORE_SENTINEL_GUARD = 256
HEADER = (
    "schema_version",
    "profile_id",
    "source_checksum_sha256",
    "joint_holdout_checksum_sha256",
    "evaluator_family",
    "artifact_family",
    "node_class",
    "validated_maximum_probes_per_node",
    "joint_false_cut_count",
    "joint_cut_candidate_count",
    "joint_false_cut_rate_upper_bound",
    "scheduler_domain_evidence",
    "phase",
    "search_mode",
    "minimum_empties",
    "maximum_empties",
    "deep_depth",
    "shallow_depth",
    "exact_handoff_enabled",
    "exact_handoff_threshold",
    "minimum_exact_handoff_distance",
    "maximum_exact_handoff_distance",
    "regression_slope",
    "intercept",
    "residual_sigma",
    "confidence_multiplier",
    "minimum_shallow_score",
    "maximum_shallow_score",
    "minimum_beta",
    "maximum_beta",
)
JSON_COMMON_FIELDS = (
    "schema_version",
    "profile_id",
    "source_checksum_sha256",
    "joint_holdout_checksum_sha256",
    "evaluator_family",
    "artifact_family",
    "node_class",
    "validated_maximum_probes_per_node",
    "joint_false_cut_count",
    "joint_cut_candidate_count",
    "joint_false_cut_rate_upper_bound",
)
ENTRY_FIELDS = HEADER[12:]
SCHEDULER_EVIDENCE_FIELDS = (
    "pair_prefix_length",
    "maximum_probes_per_node",
    "phase",
    "search_mode",
    "minimum_empties",
    "maximum_empties",
    "deep_depth",
    "exact_handoff_enabled",
    "exact_handoff_threshold",
    "minimum_exact_handoff_distance",
    "maximum_exact_handoff_distance",
    "holdout_node_count",
    "false_cut_count",
    "cut_candidate_count",
    "false_cut_rate_upper_bound",
)
INTEGER_PROFILE_FIELDS = {
    "schema_version",
    "validated_maximum_probes_per_node",
    "joint_false_cut_count",
    "joint_cut_candidate_count",
    "pair_prefix_length",
    "maximum_probes_per_node",
    "phase",
    "minimum_empties",
    "maximum_empties",
    "deep_depth",
    "shallow_depth",
    "exact_handoff_threshold",
    "minimum_exact_handoff_distance",
    "maximum_exact_handoff_distance",
    "holdout_node_count",
    "false_cut_count",
    "cut_candidate_count",
    "minimum_shallow_score",
    "maximum_shallow_score",
    "minimum_beta",
    "maximum_beta",
}
FLOAT_PROFILE_FIELDS = {
    "joint_false_cut_rate_upper_bound",
    "false_cut_rate_upper_bound",
    "regression_slope",
    "intercept",
    "residual_sigma",
    "confidence_multiplier",
}
BOOLEAN_PROFILE_FIELDS = {"exact_handoff_enabled"}


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
    require(
        isinstance(value, (int, float)) and not isinstance(value, bool),
        f"{label} must be numeric",
    )
    converted = float(value)
    require(math.isfinite(converted), f"{label} must be finite")
    return converted


def integer(value: Any, label: str) -> int:
    require(
        isinstance(value, int) and not isinstance(value, bool),
        f"{label} must be an integer",
    )
    return value


def report_provenance(report: dict[str, Any], label: str) -> dict[str, Any]:
    require(report.get("schema_version") == REPORT_SCHEMA_VERSION, f"unsupported {label} schema")
    provenance = report.get("provenance_inventory")
    require(
        isinstance(provenance, list) and len(provenance) == 1,
        f"{label} must have one provenance tuple",
    )
    require(isinstance(provenance[0], dict), f"invalid {label} provenance")
    return provenance[0]


def ordered_report_pairs(report: dict[str, Any], label: str) -> list[tuple[int, int]]:
    rows = report.get("collection_depth_pairs")
    require(isinstance(rows, list) and bool(rows), f"{label} has no collection_depth_pairs")
    result: list[tuple[int, int]] = []
    for index, row in enumerate(rows):
        require(isinstance(row, dict), f"{label} collection_depth_pairs[{index}] is invalid")
        require(row.get("pair_index") == index, f"{label} pair order is not contiguous")
        pair = (
            integer(row.get("deep_depth"), f"{label} pair deep_depth"),
            integer(row.get("shallow_depth"), f"{label} pair shallow_depth"),
        )
        require(pair[0] > pair[1] > 0, f"{label} contains an invalid depth pair")
        require(pair not in result, f"{label} contains a duplicate depth pair")
        result.append(pair)
    return result


def parse_validated_pair_order(adoption: dict[str, Any]) -> list[tuple[int, int]]:
    preference = adoption.get("validated_pair_order")
    require(
        isinstance(preference, list) and bool(preference),
        "validated_pair_order must be non-empty",
    )
    result: list[tuple[int, int]] = []
    for index, row in enumerate(preference):
        label = f"validated_pair_order[{index}]"
        require(isinstance(row, dict), f"{label} must be an object")
        pair = (
            integer(row.get("deep_depth"), f"{label}.deep_depth"),
            integer(row.get("shallow_depth"), f"{label}.shallow_depth"),
        )
        require(pair[0] > pair[1] > 0, f"{label} depths are invalid")
        require(pair not in result, f"duplicate validated depth pair {pair[0]}/{pair[1]}")
        result.append(pair)
    return result


def observed_domain_contains(group: dict[str, Any], field: str, minimum: int, maximum: int) -> bool:
    domain = group.get("observed_domain")
    if not isinstance(domain, dict):
        return False
    bucket = domain.get(field)
    if not isinstance(bucket, dict):
        return False
    observed = bucket.get("observed_values")
    if not isinstance(observed, list) or not all(isinstance(value, int) for value in observed):
        return False
    return set(range(minimum, maximum + 1)).issubset(set(observed))


def select_training_group(
    report_groups: Sequence[Any], selection: dict[str, Any], label: str
) -> dict[str, Any]:
    keys = (
        "phase",
        "deep_depth",
        "shallow_depth",
        "search_mode",
        "exact_handoff_enabled",
        "exact_handoff_threshold",
    )
    matches = [
        group
        for group in report_groups
        if isinstance(group, dict)
        and group.get("search_role") == "non_pv_scout"
        and all(group.get(key) == selection[key] for key in keys)
        and observed_domain_contains(
            group,
            "empties_bucket",
            selection["minimum_empties"],
            selection["maximum_empties"],
        )
        and observed_domain_contains(
            group,
            "exact_handoff_distance_bucket",
            selection["minimum_exact_handoff_distance"],
            selection["maximum_exact_handoff_distance"],
        )
    ]
    require(
        len(matches) == 1,
        f"{label} does not select exactly one fully observed non_pv_scout group",
    )
    return matches[0]


def score_is_safely_heuristic(score: int) -> bool:
    return SCORE_LOSS + SCORE_SENTINEL_GUARD < score < SCORE_WIN - SCORE_SENTINEL_GUARD


def entry_matches_node(entry: dict[str, Any], node: dict[str, Any], pair: tuple[int, int]) -> bool:
    return (
        entry["phase"] == node.get("phase")
        and entry["search_mode"] == node.get("search_mode")
        and entry["minimum_empties"] <= node.get("empties", -1) <= entry["maximum_empties"]
        and entry["deep_depth"] == pair[0] == node.get("deep_depth")
        and entry["shallow_depth"] == pair[1]
        and entry["exact_handoff_enabled"] == node.get("exact_handoff_enabled")
        and entry["exact_handoff_threshold"] == node.get("exact_handoff_threshold")
        and entry["minimum_exact_handoff_distance"]
        <= node.get("exact_handoff_distance", -1)
        <= entry["maximum_exact_handoff_distance"]
    )


def confidence_accepts(entry: dict[str, Any], shallow_score: int, beta: int) -> bool:
    if (
        not score_is_safely_heuristic(shallow_score)
        or not entry["minimum_shallow_score"] <= shallow_score <= entry["maximum_shallow_score"]
        or not entry["minimum_beta"] <= beta <= entry["maximum_beta"]
    ):
        return False
    margin = math.ceil(entry["confidence_multiplier"] * entry["residual_sigma"])
    predicted_floor = math.floor(entry["regression_slope"] * shallow_score + entry["intercept"])
    conservative_lower = predicted_floor - margin
    return score_is_safely_heuristic(conservative_lower) and conservative_lower >= beta


def scheduler_domain(entry: dict[str, Any]) -> tuple[Any, ...]:
    return (
        entry["phase"],
        entry["search_mode"],
        entry["minimum_empties"],
        entry["maximum_empties"],
        entry["deep_depth"],
        entry["exact_handoff_enabled"],
        entry["exact_handoff_threshold"],
        entry["minimum_exact_handoff_distance"],
        entry["maximum_exact_handoff_distance"],
    )


def node_matches_scheduler_domain(node: dict[str, Any], domain: tuple[Any, ...]) -> bool:
    (
        phase,
        search_mode,
        minimum_empties,
        maximum_empties,
        deep_depth,
        exact_handoff_enabled,
        exact_handoff_threshold,
        minimum_exact_handoff_distance,
        maximum_exact_handoff_distance,
    ) = domain
    return (
        node.get("phase") == phase
        and node.get("search_mode") == search_mode
        and minimum_empties <= node.get("empties", -1) <= maximum_empties
        and node.get("deep_depth") == deep_depth
        and node.get("exact_handoff_enabled") == exact_handoff_enabled
        and node.get("exact_handoff_threshold") == exact_handoff_threshold
        and minimum_exact_handoff_distance
        <= node.get("exact_handoff_distance", -1)
        <= maximum_exact_handoff_distance
    )


def wilson_upper_bound(false_cuts: int, candidates: int) -> float:
    if candidates == 0:
        return 1.0
    z = 1.959963984540054
    rate = false_cuts / candidates
    denominator = 1.0 + z * z / candidates
    centre = rate + z * z / (2.0 * candidates)
    radius = z * math.sqrt((rate * (1.0 - rate) + z * z / (4.0 * candidates)) / candidates)
    return min(1.0, (centre + radius) / denominator)


def replay_joint_scheduler(
    holdout: dict[str, Any],
    entries: Sequence[dict[str, Any]],
    pair_order: Sequence[tuple[int, int]],
    maximum_probes: int,
    domain: tuple[Any, ...] | None = None,
) -> tuple[int, int, int, float]:
    observations = holdout.get("scheduler_observations")
    require(isinstance(observations, list) and bool(observations), "joint holdout has no scheduler observations")
    false_cuts = 0
    candidates = 0
    holdout_nodes = 0
    for node_index, node in enumerate(observations):
        require(isinstance(node, dict), f"joint holdout observation {node_index} is invalid")
        if domain is not None and not node_matches_scheduler_domain(node, domain):
            continue
        holdout_nodes += 1
        beta = integer(node.get("official_beta"), f"joint holdout observation {node_index}.official_beta")
        deep_score = integer(
            node.get("deep_verification_score"),
            f"joint holdout observation {node_index}.deep_verification_score",
        )
        pairs = node.get("pairs")
        require(isinstance(pairs, list), f"joint holdout observation {node_index}.pairs is invalid")
        scores = {
            (row.get("deep_depth"), row.get("shallow_depth")): row.get("shallow_verification_score")
            for row in pairs
            if isinstance(row, dict)
        }
        probes = 0
        for pair in pair_order:
            if pair[0] != node.get("deep_depth"):
                continue
            entry_matches = [entry for entry in entries if entry_matches_node(entry, node, pair)]
            if not entry_matches:
                continue
            require(len(entry_matches) == 1, "joint holdout matches overlapping profile entries")
            if probes >= maximum_probes:
                break
            probes += 1
            shallow_score = scores.get(pair)
            require(isinstance(shallow_score, int), f"joint holdout is missing pair {pair[0]}/{pair[1]}")
            if confidence_accepts(entry_matches[0], shallow_score, beta):
                candidates += 1
                false_cuts += deep_score < beta
                break
    return holdout_nodes, false_cuts, candidates, wilson_upper_bound(false_cuts, candidates)


def serialize_scheduler_evidence(evidence: Sequence[dict[str, Any]]) -> str:
    records: list[str] = []
    for item in evidence:
        domain = item["domain"]
        fields = (
            item["pair_prefix_length"],
            item["maximum_probes_per_node"],
            *domain[:5],
            "true" if domain[5] else "false",
            *domain[6:],
            item["holdout_node_count"],
            item["false_cut_count"],
            item["cut_candidate_count"],
            format(item["false_cut_rate_upper_bound"], ".17g"),
        )
        records.append(":".join(str(value) for value in fields))
    require(bool(records), "no scheduler/domain holdout configuration passed review")
    return ";".join(records)


def profile_value(field: str, value: str) -> Any:
    if field in INTEGER_PROFILE_FIELDS:
        return int(value)
    if field in FLOAT_PROFILE_FIELDS:
        converted = float(value)
        require(math.isfinite(converted), f"{field} must be finite")
        return converted
    if field in BOOLEAN_PROFILE_FIELDS:
        require(value in {"true", "false"}, f"{field} must be boolean")
        return value == "true"
    return value


def render_profile_json(rendered_tsv: str) -> str:
    try:
        rows = list(csv.DictReader(io.StringIO(rendered_tsv), delimiter="\t"))
    except csv.Error as error:
        raise ProfileConversionError(f"cannot parse converted TSV: {error}") from error
    require(bool(rows), "converted profile has no rows")
    first = rows[0]
    for field in JSON_COMMON_FIELDS:
        require(
            all(row.get(field) == first.get(field) for row in rows),
            f"profile rows disagree on {field}",
        )
    serialized_evidence = first.get("scheduler_domain_evidence", "")
    require(
        all(row.get("scheduler_domain_evidence") == serialized_evidence for row in rows),
        "profile rows disagree on scheduler_domain_evidence",
    )

    pair_order: list[dict[str, int]] = []
    seen_pairs: set[tuple[int, int]] = set()
    for row in rows:
        pair = (int(row["deep_depth"]), int(row["shallow_depth"]))
        if pair not in seen_pairs:
            pair_order.append({"deep_depth": pair[0], "shallow_depth": pair[1]})
            seen_pairs.add(pair)

    scheduler_evidence: list[dict[str, Any]] = []
    for record in serialized_evidence.split(";"):
        values = record.split(":")
        require(
            len(values) == len(SCHEDULER_EVIDENCE_FIELDS),
            f"invalid scheduler evidence: {record}",
        )
        scheduler_evidence.append(
            {
                field: profile_value(field, value)
                for field, value in zip(SCHEDULER_EVIDENCE_FIELDS, values, strict=True)
            }
        )

    payload = {
        "schema_version": profile_value("schema_version", first["schema_version"]),
        "profile_id": first["profile_id"],
        "source_checksum_sha256": first["source_checksum_sha256"],
        "joint_holdout_checksum_sha256": first["joint_holdout_checksum_sha256"],
        "evaluator_family": first["evaluator_family"],
        "artifact_family": first["artifact_family"],
        "node_class": first["node_class"],
        "validated_pair_order": pair_order,
        "validated_maximum_probes_per_node": profile_value(
            "validated_maximum_probes_per_node",
            first["validated_maximum_probes_per_node"],
        ),
        "joint_false_cut_count": profile_value(
            "joint_false_cut_count", first["joint_false_cut_count"]
        ),
        "joint_cut_candidate_count": profile_value(
            "joint_cut_candidate_count", first["joint_cut_candidate_count"]
        ),
        "joint_false_cut_rate_upper_bound": profile_value(
            "joint_false_cut_rate_upper_bound",
            first["joint_false_cut_rate_upper_bound"],
        ),
        "scheduler_domain_evidence": scheduler_evidence,
        "entries": [
            {field: profile_value(field, row[field]) for field in ENTRY_FIELDS}
            for row in rows
        ],
    }
    return json.dumps(payload, indent=2, ensure_ascii=False) + "\n"


def render_profile(
    report: dict[str, Any],
    report_raw: bytes,
    adoption: dict[str, Any],
    joint_holdout: dict[str, Any],
    joint_holdout_raw: bytes,
) -> str:
    provenance = report_provenance(report, "training report")
    holdout_provenance = report_provenance(joint_holdout, "joint holdout report")
    provenance_fields = ("repo_sha", "search_config_id", "evaluator_id", "artifact_id")
    require(
        all(provenance.get(field) == holdout_provenance.get(field) for field in provenance_fields),
        "training and joint holdout provenance must match exactly",
    )
    collection_inventory = report.get("collection_config_inventory")
    holdout_collection_inventory = joint_holdout.get("collection_config_inventory")
    require(
        isinstance(collection_inventory, list)
        and len(collection_inventory) == 1
        and isinstance(holdout_collection_inventory, list)
        and len(holdout_collection_inventory) == 1
        and collection_inventory[0].get("collection_config_id")
        == holdout_collection_inventory[0].get("collection_config_id"),
        "training and joint holdout collection policy must match exactly",
    )
    training_pairs = ordered_report_pairs(report, "training report")
    holdout_pairs = ordered_report_pairs(joint_holdout, "joint holdout report")
    require(training_pairs == holdout_pairs, "training and joint holdout depth-pair order must match")
    training_nodes = {
        node.get("node_id")
        for node in report.get("scheduler_observations", [])
        if isinstance(node, dict)
    }
    holdout_nodes = {
        node.get("node_id")
        for node in joint_holdout.get("scheduler_observations", [])
        if isinstance(node, dict)
    }
    require(not (training_nodes & holdout_nodes), "training and joint holdout contain duplicate sampled nodes")

    require(adoption.get("schema_version") == ADOPTION_SCHEMA_VERSION, "unsupported adoption schema")
    profile_id = adoption.get("profile_id")
    evaluator_family = adoption.get("evaluator_family")
    artifact_family = adoption.get("artifact_family")
    node_class = adoption.get("node_class")
    require(isinstance(profile_id, str) and bool(profile_id), "profile_id must be non-empty")
    require(evaluator_family == provenance.get("evaluator_id"), "evaluator_family must exactly match report evaluator_id")
    require(artifact_family == provenance.get("artifact_id"), "artifact_family must exactly match report artifact_id")
    require(node_class == NODE_CLASS, f"node_class must be {NODE_CLASS}")

    pair_order = parse_validated_pair_order(adoption)
    require(
        pair_order == training_pairs[: len(pair_order)],
        "validated_pair_order must be a prefix of the collected pair order",
    )
    maximum_probes = integer(
        adoption.get("validated_maximum_probes_per_node"),
        "validated_maximum_probes_per_node",
    )
    require(0 < maximum_probes <= len(pair_order), "validated maximum probes is invalid")
    minimum_joint_candidates = integer(
        adoption.get("minimum_joint_cut_candidates"), "minimum_joint_cut_candidates"
    )
    maximum_joint_upper = finite_number(
        adoption.get("maximum_joint_false_cut_rate_upper_bound"),
        "maximum_joint_false_cut_rate_upper_bound",
    )
    require(minimum_joint_candidates > 0, "minimum_joint_cut_candidates must be positive")
    require(0.0 <= maximum_joint_upper <= 1.0, "maximum joint false-cut upper bound is invalid")

    selections = adoption.get("entries")
    require(isinstance(selections, list) and bool(selections), "entries must be a non-empty array")
    report_groups = report.get("groups")
    require(isinstance(report_groups, list), "report groups must be an array")
    entries: list[dict[str, Any]] = []
    domains: list[tuple[Any, ...]] = []
    selected_pairs: set[tuple[int, int]] = set()
    search_modes = {"move", "analyze", "exact_score", "win_loss_draw"}
    for index, raw_selection in enumerate(selections):
        label = f"entries[{index}]"
        require(isinstance(raw_selection, dict), f"{label} must be an object")
        selection = dict(raw_selection)
        for field in (
            "phase",
            "deep_depth",
            "shallow_depth",
            "minimum_empties",
            "maximum_empties",
            "exact_handoff_threshold",
            "minimum_exact_handoff_distance",
            "maximum_exact_handoff_distance",
            "minimum_shallow_score",
            "maximum_shallow_score",
            "minimum_beta",
            "maximum_beta",
        ):
            selection[field] = integer(selection.get(field), f"{label}.{field}")
        search_mode = selection.get("search_mode")
        exact_enabled = selection.get("exact_handoff_enabled")
        require(search_mode in search_modes, f"{label}.search_mode is invalid")
        require(isinstance(exact_enabled, bool), f"{label}.exact_handoff_enabled must be boolean")
        require(0 <= selection["phase"] <= 12, f"{label}.phase is out of range")
        require(
            0 <= selection["minimum_empties"] <= selection["maximum_empties"] <= 60,
            f"{label} empties range is invalid",
        )
        pair = (selection["deep_depth"], selection["shallow_depth"])
        require(pair in pair_order, f"{label} pair is missing from validated_pair_order")
        require(
            exact_enabled == (selection["exact_handoff_threshold"] != 0),
            f"{label} exact handoff threshold/enabled mismatch",
        )
        require(
            0
            <= selection["minimum_exact_handoff_distance"]
            <= selection["maximum_exact_handoff_distance"]
            <= 60,
            f"{label} handoff range is invalid",
        )
        if not exact_enabled:
            require(
                selection["minimum_exact_handoff_distance"] == 0
                and selection["maximum_exact_handoff_distance"] == 0,
                f"{label} disabled exact handoff must use distance 0",
            )
        domain = (
            selection["phase"],
            search_mode,
            pair,
            exact_enabled,
            selection["exact_handoff_threshold"],
            selection["minimum_empties"],
            selection["maximum_empties"],
            selection["minimum_exact_handoff_distance"],
            selection["maximum_exact_handoff_distance"],
        )
        for previous in domains:
            same_key = previous[:5] == domain[:5]
            empties_overlap = previous[5] <= domain[6] and domain[5] <= previous[6]
            handoff_overlap = previous[7] <= domain[8] and domain[7] <= previous[8]
            require(not (same_key and empties_overlap and handoff_overlap), f"overlapping profile domain at {label}")
        domains.append(domain)
        selected_pairs.add(pair)

        group = select_training_group(report_groups, selection, label)
        require(group.get("recommendation_eligible") is True, f"{label} group is not recommendation eligible")
        require(
            observed_domain_contains(
                group,
                "empties_bucket",
                selection["minimum_empties"],
                selection["maximum_empties"],
            ),
            f"{label} empties range is not fully observed in the report",
        )
        require(
            observed_domain_contains(
                group,
                "exact_handoff_distance_bucket",
                selection["minimum_exact_handoff_distance"],
                selection["maximum_exact_handoff_distance"],
            ),
            f"{label} exact-handoff range is not fully observed in the report",
        )
        regression = group.get("linear_regression")
        require(isinstance(regression, dict), f"{label} regression is unavailable")
        selection["regression_slope"] = finite_number(regression.get("slope"), f"{label}.slope")
        selection["intercept"] = finite_number(regression.get("intercept"), f"{label}.intercept")
        selection["residual_sigma"] = finite_number(
            group.get("residual_standard_deviation"), f"{label}.residual_sigma"
        )
        selection["confidence_multiplier"] = finite_number(
            selection.get("confidence_multiplier"), f"{label}.confidence_multiplier"
        )
        require(
            selection["regression_slope"] > 0.0 and selection["residual_sigma"] >= 0.0,
            f"{label} regression is unsupported",
        )
        require(selection["confidence_multiplier"] > 0.0, f"{label}.confidence_multiplier must be positive")
        require(
            SCORE_LOSS < selection["minimum_shallow_score"] <= selection["maximum_shallow_score"] < SCORE_WIN,
            f"{label} shallow range is invalid",
        )
        require(
            SCORE_LOSS < selection["minimum_beta"] <= selection["maximum_beta"] < SCORE_WIN,
            f"{label} beta range is invalid",
        )
        entries.append(selection)

    require(selected_pairs == set(pair_order), "every validated depth pair must have an entry")
    _, false_cuts, candidates, joint_upper = replay_joint_scheduler(
        joint_holdout, entries, pair_order, maximum_probes
    )
    require(candidates >= minimum_joint_candidates, "joint holdout has too few cut candidates")
    require(joint_upper <= maximum_joint_upper, "joint scheduler false-cut upper bound exceeds the reviewed limit")

    exact_domains = sorted({scheduler_domain(entry) for entry in entries})
    scheduler_evidence: list[dict[str, Any]] = []
    for prefix_length in range(1, len(pair_order) + 1):
        prefix_pairs = set(pair_order[:prefix_length])
        for probe_count in range(1, min(prefix_length, maximum_probes) + 1):
            for domain in exact_domains:
                domain_enabled = any(
                    scheduler_domain(entry) == domain
                    and (entry["deep_depth"], entry["shallow_depth"]) in prefix_pairs
                    for entry in entries
                )
                if not domain_enabled:
                    continue
                holdout_node_count, domain_false_cuts, domain_candidates, domain_upper = (
                    replay_joint_scheduler(
                        joint_holdout,
                        entries,
                        pair_order[:prefix_length],
                        probe_count,
                        domain,
                    )
                )
                if (
                    domain_candidates >= minimum_joint_candidates
                    and domain_upper <= maximum_joint_upper
                ):
                    scheduler_evidence.append(
                        {
                            "pair_prefix_length": prefix_length,
                            "maximum_probes_per_node": probe_count,
                            "domain": domain,
                            "holdout_node_count": holdout_node_count,
                            "false_cut_count": domain_false_cuts,
                            "cut_candidate_count": domain_candidates,
                            "false_cut_rate_upper_bound": domain_upper,
                        }
                    )

    full_scheduler_domains = {
        item["domain"]
        for item in scheduler_evidence
        if item["pair_prefix_length"] == len(pair_order)
        and item["maximum_probes_per_node"] == maximum_probes
    }
    missing_full_domains = [domain for domain in exact_domains if domain not in full_scheduler_domains]
    require(
        not missing_full_domains,
        "joint holdout has an unaudited exact profile domain for the full scheduler",
    )
    serialized_scheduler_evidence = serialize_scheduler_evidence(scheduler_evidence)

    source_checksum = hashlib.sha256(report_raw).hexdigest()
    holdout_checksum = hashlib.sha256(joint_holdout_raw).hexdigest()
    rows: list[tuple[int, int, int, int, list[str]]] = []
    for entry in entries:
        pair = (entry["deep_depth"], entry["shallow_depth"])
        rows.append(
            (
                pair_order.index(pair),
                entry["phase"],
                entry["minimum_empties"],
                entry["minimum_exact_handoff_distance"],
                [
                    str(PROFILE_SCHEMA_VERSION),
                    profile_id,
                    source_checksum,
                    holdout_checksum,
                    evaluator_family,
                    artifact_family,
                    NODE_CLASS,
                    str(maximum_probes),
                    str(false_cuts),
                    str(candidates),
                    format(joint_upper, ".17g"),
                    serialized_scheduler_evidence,
                    str(entry["phase"]),
                    entry["search_mode"],
                    str(entry["minimum_empties"]),
                    str(entry["maximum_empties"]),
                    str(entry["deep_depth"]),
                    str(entry["shallow_depth"]),
                    "true" if entry["exact_handoff_enabled"] else "false",
                    str(entry["exact_handoff_threshold"]),
                    str(entry["minimum_exact_handoff_distance"]),
                    str(entry["maximum_exact_handoff_distance"]),
                    format(entry["regression_slope"], ".17g"),
                    format(entry["intercept"], ".17g"),
                    format(entry["residual_sigma"], ".17g"),
                    format(entry["confidence_multiplier"], ".17g"),
                    str(entry["minimum_shallow_score"]),
                    str(entry["maximum_shallow_score"]),
                    str(entry["minimum_beta"]),
                    str(entry["maximum_beta"]),
                ],
            )
        )
    lines = ["\t".join(HEADER)]
    lines.extend("\t".join(row) for *_, row in sorted(rows))
    return "\n".join(lines) + "\n"


def convert(
    report_path: Path,
    adoption_path: Path,
    joint_holdout_path: Path,
    output_path: Path,
    output_format: str = "tsv",
) -> None:
    report, report_raw = load_json(report_path, "training report")
    adoption, _ = load_json(adoption_path, "adoption specification")
    joint_holdout, joint_holdout_raw = load_json(joint_holdout_path, "joint holdout report")
    rendered_tsv = render_profile(
        report, report_raw, adoption, joint_holdout, joint_holdout_raw
    )
    if output_format == "tsv":
        rendered = rendered_tsv
    elif output_format == "json":
        rendered = render_profile_json(rendered_tsv)
    else:
        raise ProfileConversionError(f"unsupported output format: {output_format}")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(rendered, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("adoption", type=Path)
    parser.add_argument("--joint-holdout-report", type=Path, required=True)
    parser.add_argument("--output-format", choices=("tsv", "json"), default="tsv")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        convert(
            args.report,
            args.adoption,
            args.joint_holdout_report,
            args.output,
            args.output_format,
        )
    except ProfileConversionError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
