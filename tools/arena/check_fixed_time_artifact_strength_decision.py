#!/usr/bin/env python3
"""Validate the fixed-time artifact campaign decision schema."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


class SchemaError(RuntimeError):
    """Raised when a decision report does not satisfy the public schema."""


def require_fields(mapping: dict[str, Any], fields: set[str], context: str) -> None:
    missing = sorted(fields - mapping.keys())
    if missing:
        raise SchemaError(f"{context} missing fields: {', '.join(missing)}")


def require_object(mapping: dict[str, Any], field: str, context: str) -> dict[str, Any]:
    value = mapping.get(field)
    if not isinstance(value, dict):
        raise SchemaError(f"{context}.{field} must be an object")
    return value


def require_number(mapping: dict[str, Any], field: str, context: str) -> None:
    if not isinstance(mapping.get(field), (int, float)):
        raise SchemaError(f"{context}.{field} must be numeric")


def validate_decision(decision: dict[str, Any]) -> None:
    require_fields(
        decision,
        {
            "schema_version",
            "decision_version",
            "candidate_artifact",
            "baseline_artifact",
            "repo_sha",
            "repository",
            "executable",
            "opening_corpora",
            "selected_opening_checksum",
            "campaign_config",
            "overall",
            "phase_score_rate",
            "side_to_move_score_rate",
            "telemetry",
            "failed_games",
            "illegal_games",
            "same_artifact_sanity",
            "candidate_baseline_swap_consistency",
            "primary_fixed_time_cell",
            "promotion_checks",
            "cells",
            "suggested_decision",
        },
        "decision",
    )
    if decision["schema_version"] != 2:
        raise SchemaError("decision.schema_version must be 2")
    if decision["decision_version"] != "fixed-time-artifact-strength-decision-v2":
        raise SchemaError("decision.decision_version is unsupported")
    if not isinstance(decision["repo_sha"], str) or not decision["repo_sha"]:
        raise SchemaError("decision.repo_sha must be a non-empty string")
    for field in ("candidate_artifact", "baseline_artifact"):
        identity = require_object(decision, field, "decision")
        require_fields(
            identity,
            {
                "artifact_id",
                "pattern_set_id",
                "manifest_content_checksum",
                "weights_file_checksum",
                "runtime_identity_checksum",
            },
            f"decision.{field}",
        )
    executable = require_object(decision, "executable", "decision")
    require_fields(executable, {"path", "sha256", "size_bytes"}, "decision.executable")
    opening_corpora = require_object(decision, "opening_corpora", "decision")
    if not opening_corpora:
        raise SchemaError("decision.opening_corpora must not be empty")
    campaign_config = require_object(decision, "campaign_config", "decision")
    promotion_config = require_object(campaign_config, "promotion", "decision.campaign_config")
    require_fields(
        promotion_config,
        {
            "score_threshold",
            "ci_lower_threshold",
            "confidence_level",
            "minimum_completed_depth_ratio",
            "minimum_opening_pairs",
            "minimum_distinct_time_limits",
        },
        "decision.campaign_config.promotion",
    )
    if promotion_config["confidence_level"] != 0.95:
        raise SchemaError("decision promotion confidence level must be fixed at 95%")
    if not isinstance(promotion_config["minimum_opening_pairs"], int) or promotion_config[
        "minimum_opening_pairs"
    ] < 100:
        raise SchemaError("decision promotion minimum opening pairs must be at least 100")
    require_object(decision, "phase_score_rate", "decision")
    require_object(decision, "side_to_move_score_rate", "decision")
    overall = require_object(decision, "overall", "decision")
    require_fields(
        overall,
        {
            "games",
            "candidate_wins",
            "candidate_losses",
            "draws",
            "candidate_score_rate",
            "average_disc_difference",
            "median_disc_difference",
            "failed_games",
            "illegal_games",
            "descriptive_only",
            "heterogeneous_matrix_aggregate",
            "confidence_interval",
            "confidence_interval_reason",
        },
        "decision.overall",
    )
    for field in (
        "games",
        "candidate_wins",
        "candidate_losses",
        "draws",
        "candidate_score_rate",
        "average_disc_difference",
        "median_disc_difference",
    ):
        require_number(overall, field, "decision.overall")
    if (
        overall["descriptive_only"] is not True
        or overall["heterogeneous_matrix_aggregate"] is not True
        or overall["confidence_interval"] is not None
    ):
        raise SchemaError("decision.overall must keep the heterogeneous aggregate descriptive-only")
    telemetry = require_object(decision, "telemetry", "decision")
    telemetry_fields = {
        "completed_depth_percentiles",
        "nodes_per_sec",
        "evals_per_sec",
        "exact_completion_rate",
        "exact_handoff_rate",
        "incremental_evaluation_enabled_search_count",
        "incremental_eval_calls",
        "stateless_eval_calls",
        "incremental_updates",
    }
    for role in ("candidate", "baseline"):
        role_data = require_object(telemetry, role, "decision.telemetry")
        require_fields(role_data, telemetry_fields, f"decision.telemetry.{role}")
    if not isinstance(decision["cells"], list) or not decision["cells"]:
        raise SchemaError("decision.cells must be a non-empty array")
    passing_cells: list[dict[str, Any]] = []
    for index, cell in enumerate(decision["cells"]):
        if not isinstance(cell, dict):
            raise SchemaError(f"decision.cells[{index}] must be an object")
        require_fields(
            cell,
            {
                "opening_set",
                "time_limit_ms",
                "exact_threshold",
                "selected_opening_checksum",
                "opening_pair_count",
                "overall",
                "bootstrap_95_ci",
                "bootstrap_confidence_interval",
                "completed_depth_ratio_p50",
                "same_artifact_diagnostics",
                "argument_order_diagnostic",
                "strength_gate",
                "promotion_checks",
                "promotion_passed",
            },
            f"decision.cells[{index}]",
        )
        interval = require_object(cell, "bootstrap_95_ci", f"decision.cells[{index}]")
        if interval.get("confidence_level") != 0.95:
            raise SchemaError(f"decision.cells[{index}].bootstrap_95_ci must be fixed at 95%")
        if not isinstance(cell["promotion_passed"], bool):
            raise SchemaError(f"decision.cells[{index}].promotion_passed must be boolean")
        if not isinstance(cell["opening_pair_count"], int) or cell["opening_pair_count"] < 0:
            raise SchemaError(f"decision.cells[{index}].opening_pair_count must be non-negative")
        cell_promotion = require_object(cell, "promotion_checks", f"decision.cells[{index}]")
        require_fields(
            cell_promotion,
            {
                "strength_gate_eligible",
                "score_rate_passed",
                "ci95_lower_bound_passed",
                "completed_depth_passed",
                "minimum_opening_pairs_passed",
                "promotion_passed",
            },
            f"decision.cells[{index}].promotion_checks",
        )
        if cell_promotion["promotion_passed"] != cell["promotion_passed"]:
            raise SchemaError(f"decision.cells[{index}] promotion result is inconsistent")
        cell_overall = require_object(cell, "overall", f"decision.cells[{index}]")
        cell_strength_gate = require_object(cell, "strength_gate", f"decision.cells[{index}]")
        depth_ratio = cell["completed_depth_ratio_p50"]
        score_rate = cell_overall.get("candidate_score_rate")
        ci95_lower = interval.get("lower")
        promotion_recomputed = (
            cell_strength_gate.get("eligible") is True
            and isinstance(score_rate, (int, float))
            and score_rate > promotion_config["score_threshold"]
            and isinstance(ci95_lower, (int, float))
            and ci95_lower > promotion_config["ci_lower_threshold"]
            and isinstance(depth_ratio, (int, float))
            and depth_ratio >= promotion_config["minimum_completed_depth_ratio"]
            and cell["opening_pair_count"] >= promotion_config["minimum_opening_pairs"]
        )
        if cell["promotion_passed"] != promotion_recomputed:
            raise SchemaError(f"decision.cells[{index}] promotion gates do not match cell data")
        if promotion_recomputed:
            passing_cells.append(cell)
    same = require_object(decision, "same_artifact_sanity", "decision")
    swap = require_object(decision, "candidate_baseline_swap_consistency", "decision")
    require_fields(
        same,
        {"diagnostic_only", "all_exactly_neutral", "all_ci95_include_neutral", "checks"},
        "decision.same_artifact_sanity",
    )
    require_fields(
        swap,
        {
            "diagnostic_only",
            "all_selected_openings_match",
            "all_exactly_complementary",
            "checks",
        },
        "decision.candidate_baseline_swap_consistency",
    )
    if same["diagnostic_only"] is not True or swap["diagnostic_only"] is not True:
        raise SchemaError("fixed-time same-artifact and swap results must be diagnostic-only")
    primary = require_object(decision, "primary_fixed_time_cell", "decision")
    require_fields(
        primary,
        {
            "opening_set",
            "time_limit_ms",
            "exact_threshold",
            "selected_opening_checksum",
            "opening_pair_count",
            "overall",
            "bootstrap_95_ci",
            "bootstrap_confidence_interval",
            "completed_depth_ratio_p50",
            "same_artifact_diagnostics",
            "argument_order_diagnostic",
            "strength_gate",
            "promotion_checks",
            "promotion_passed",
        },
        "decision.primary_fixed_time_cell",
    )
    promotion_checks = require_object(decision, "promotion_checks", "decision")
    require_fields(
        promotion_checks,
        {
            "failed_games",
            "illegal_games",
            "primary_strength_gate_eligible",
            "primary_opening_pair_count",
            "minimum_promotion_opening_pairs",
            "primary_minimum_opening_pairs_passed",
            "primary_ci95_lower",
            "primary_ci95_upper",
            "primary_promotion_passed",
            "promotion_passed_cell_count",
            "promotion_passed_time_limits",
            "promotion_time_limit_count",
            "minimum_promotion_time_limits",
        },
        "decision.promotion_checks",
    )
    decision_set = campaign_config.get("primary_opening_set")
    decision_passing_cells = [
        cell for cell in passing_cells if cell.get("opening_set") == decision_set
    ]
    passing_time_limits = sorted(
        {cell["time_limit_ms"] for cell in decision_passing_cells}
    )
    if promotion_checks["promotion_passed_cell_count"] != len(decision_passing_cells):
        raise SchemaError("decision promotion passed cell count is inconsistent")
    if promotion_checks["promotion_passed_time_limits"] != passing_time_limits:
        raise SchemaError("decision promotion passed time limits are inconsistent")
    if promotion_checks["promotion_time_limit_count"] != len(passing_time_limits):
        raise SchemaError("decision promotion time-limit count is inconsistent")
    primary_matches = [
        cell
        for cell in decision["cells"]
        if cell.get("opening_set") == primary.get("opening_set")
        and cell.get("time_limit_ms") == primary.get("time_limit_ms")
        and cell.get("exact_threshold") == primary.get("exact_threshold")
    ]
    if len(primary_matches) != 1 or primary_matches[0]["promotion_passed"] != primary[
        "promotion_passed"
    ]:
        raise SchemaError("decision primary fixed-time cell is inconsistent with the matrix")
    suggested = require_object(decision, "suggested_decision", "decision")
    allowed = {
        "promote",
        "continue_validation",
        "reject_strength",
        "reject_correctness",
        "inconclusive",
    }
    if suggested.get("category") not in allowed:
        raise SchemaError("decision.suggested_decision.category is invalid")
    if not isinstance(suggested.get("reasons"), list) or not suggested["reasons"]:
        raise SchemaError("decision.suggested_decision.reasons must be a non-empty array")
    if suggested["category"] == "promote":
        promote_valid = (
            promotion_checks["failed_games"] == 0
            and promotion_checks["illegal_games"] == 0
            and promotion_checks["primary_strength_gate_eligible"] is True
            and promotion_checks["primary_minimum_opening_pairs_passed"] is True
            and promotion_checks["primary_promotion_passed"] is True
            and promotion_checks["promotion_time_limit_count"]
            >= promotion_checks["minimum_promotion_time_limits"]
        )
        if not promote_valid:
            raise SchemaError("decision suggests promote without satisfying fixed promotion gates")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--decision", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        value = json.loads(args.decision.read_text(encoding="utf-8"))
        if not isinstance(value, dict):
            raise SchemaError("decision root must be an object")
        validate_decision(value)
    except (OSError, json.JSONDecodeError, SchemaError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print("fixed-time artifact decision schema: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
