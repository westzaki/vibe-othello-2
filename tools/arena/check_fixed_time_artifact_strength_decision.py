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
            "bootstrap_95_ci",
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
    if decision["schema_version"] != 1:
        raise SchemaError("decision.schema_version must be 1")
    if decision["decision_version"] != "fixed-time-artifact-strength-decision-v1":
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
    require_object(decision, "campaign_config", "decision")
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
    interval = require_object(decision, "bootstrap_95_ci", "decision")
    require_fields(
        interval,
        {"confidence_level", "point_estimate", "lower", "upper", "iterations", "seed"},
        "decision.bootstrap_95_ci",
    )
    if interval["confidence_level"] != 0.95:
        raise SchemaError("decision.bootstrap_95_ci.confidence_level must be 0.95")
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
        "touched_pattern_instances",
    }
    for role in ("candidate", "baseline"):
        role_data = require_object(telemetry, role, "decision.telemetry")
        require_fields(role_data, telemetry_fields, f"decision.telemetry.{role}")
    if not isinstance(decision["cells"], list) or not decision["cells"]:
        raise SchemaError("decision.cells must be a non-empty array")
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
                "overall",
                "bootstrap_confidence_interval",
                "completed_depth_ratio_p50",
                "same_artifact_neutral",
                "swap_consistency_passed",
                "strength_gate_eligible",
            },
            f"decision.cells[{index}]",
        )
    same = require_object(decision, "same_artifact_sanity", "decision")
    swap = require_object(decision, "candidate_baseline_swap_consistency", "decision")
    if not isinstance(same.get("neutral"), bool):
        raise SchemaError("decision.same_artifact_sanity.neutral must be boolean")
    if not isinstance(swap.get("passed"), bool):
        raise SchemaError("decision.candidate_baseline_swap_consistency.passed must be boolean")
    primary = require_object(decision, "primary_fixed_time_cell", "decision")
    require_fields(
        primary,
        {
            "opening_set",
            "time_limit_ms",
            "exact_threshold",
            "overall",
            "bootstrap_95_ci",
            "bootstrap_confidence_interval",
            "completed_depth_ratio_p50",
            "strength_gate",
        },
        "decision.primary_fixed_time_cell",
    )
    require_object(decision, "promotion_checks", "decision")
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
