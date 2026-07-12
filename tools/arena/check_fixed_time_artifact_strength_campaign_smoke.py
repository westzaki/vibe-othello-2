#!/usr/bin/env python3
"""Tiny real-tool smoke for the fixed-time artifact strength campaign."""

from __future__ import annotations

import argparse
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


FORCED_PASS_MOVES = (
    "e6 d6 c5 f6 c4 c3 d3 b4 c6 b3 b2 a1 f5 c7 a3 g5 b7 a2 c1 "
    "a7 a8 e2 d2 e1 e7 d7 h5 e8 a6 b5 c2 f4 f2 f1 g7 h4 d8 h8 "
    "c8 d1 f3 g2 h1 a4 g6 b1 g8 h6 g3 g4 f8 h2 h3 f7 e3 b8 h7 b6 a5"
)


def load_module(path: Path, name: str) -> Any:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load module: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(command: list[str], *, success: bool = True) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(command, check=False, capture_output=True, text=True)
    if success and completed.returncode != 0:
        raise AssertionError(
            f"command failed ({completed.returncode}): {command}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    if not success and completed.returncode == 0:
        raise AssertionError(f"command unexpectedly succeeded: {command}")
    return completed


def campaign_command(
    runner: Path,
    exe: Path,
    root: Path,
    output_dir: Path,
) -> list[str]:
    return [
        sys.executable,
        str(runner),
        "--candidate-manifest",
        str(root / "candidate.manifest.json"),
        "--candidate-weights",
        str(root / "candidate.weights.bin"),
        "--baseline-manifest",
        str(root / "baseline.manifest.json"),
        "--baseline-weights",
        str(root / "baseline.weights.bin"),
        "--opening-corpus",
        str(root / "openings.txt"),
        "--holdout-opening-corpus",
        str(root / "holdout-openings.txt"),
        "--arena-executable",
        str(exe),
        "--output-dir",
        str(output_dir),
        "--campaign-seed",
        "17",
        "--opening-count",
        "1",
        "--holdout-opening-count",
        "1",
        "--time-limits-ms",
        "1,2",
        "--exact-thresholds",
        "1",
        "--tt-bytes",
        "1048576",
        "--persistent-session",
        "--bootstrap-iterations",
        "100",
        "--confidence-level",
        "0.95",
    ]


def assert_decision(decision: dict[str, Any]) -> None:
    if decision["same_artifact_sanity"] != {"neutral": True}:
        raise AssertionError(f"same-artifact sanity was not neutral: {decision!r}")
    if decision["candidate_baseline_swap_consistency"] != {"passed": True}:
        raise AssertionError(f"candidate/baseline swap sanity failed: {decision!r}")
    if decision["failed_games"] != 0 or decision["illegal_games"] != 0:
        raise AssertionError(f"campaign contains failed or illegal games: {decision!r}")
    if decision["campaign_config"]["persistent_session"] is not True:
        raise AssertionError("campaign did not retain explicit persistent-session config")
    if decision["campaign_config"]["tt_bytes"] != 1048576:
        raise AssertionError("campaign did not retain explicit TT byte config")
    if decision["campaign_config"]["primary_opening_set"] != "holdout":
        raise AssertionError("independent holdout did not drive the primary decision cell")
    if decision["suggested_decision"]["category"] == "promote":
        raise AssertionError("neutral tiny smoke campaign must not suggest promotion")
    for role in ("candidate", "baseline"):
        telemetry = decision["telemetry"][role]
        for field in (
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
        ):
            if field not in telemetry:
                raise AssertionError(f"decision telemetry missing {role}.{field}")


def assert_promotion_guards(runner_module: Any) -> None:
    passing = {
        "failed_games": 0,
        "illegal_games": 0,
        "same_artifact_neutral": True,
        "swap_passed": True,
        "primary_eligible": True,
        "primary_score_rate": 0.6,
        "primary_ci_lower": 0.51,
        "primary_ci_upper": 0.7,
        "score_threshold": 0.5,
        "ci_lower_threshold": 0.5,
        "depth_regression_passed": True,
        "eligible_cell_count": 2,
        "minimum_promotion_cells": 2,
    }
    if runner_module.suggest_decision(passing)["category"] != "promote":
        raise AssertionError("synthetic passing campaign was not promotable")
    failed = dict(passing, failed_games=1)
    if runner_module.suggest_decision(failed)["category"] != "reject_correctness":
        raise AssertionError("failed game did not reject promotion for correctness")
    illegal = dict(passing, illegal_games=1)
    if runner_module.suggest_decision(illegal)["category"] != "reject_correctness":
        raise AssertionError("illegal game did not reject promotion for correctness")
    weak_ci = dict(passing, primary_ci_lower=0.5)
    if runner_module.suggest_decision(weak_ci)["category"] == "promote":
        raise AssertionError("CI lower bound at 50% did not reject promotion")
    single_cell = dict(passing, eligible_cell_count=1)
    if runner_module.suggest_decision(single_cell)["category"] == "promote":
        raise AssertionError("single-cell campaign unexpectedly suggested promotion")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--schema-checker", type=Path, required=True)
    parser.add_argument("--artifact-fixture", type=Path, required=True)
    args = parser.parse_args(argv)
    fixture_module = load_module(args.artifact_fixture, "full_game_arena_fixture")
    runner_module = load_module(args.runner, "fixed_time_campaign")
    assert_promotion_guards(runner_module)

    with tempfile.TemporaryDirectory(prefix="fixed-time-campaign-smoke-") as temp:
        root = Path(temp)
        fixture_module.make_tiny_artifact(
            root / "candidate.weights.bin", root / "candidate.manifest.json"
        )
        fixture_module.make_tiny_artifact(
            root / "baseline.weights.bin", root / "baseline.manifest.json"
        )
        (root / "openings.txt").write_text(
            f"near-terminal: {FORCED_PASS_MOVES}\n", encoding="utf-8"
        )
        (root / "holdout-openings.txt").write_text(
            f"independent-holdout: {FORCED_PASS_MOVES}\n", encoding="utf-8"
        )

        first_dir = root / "first"
        first_command = campaign_command(args.runner, args.exe, root, first_dir)
        run(first_command)
        for name in (
            "decision.json",
            "summary.md",
            "arena-report-inventory.json",
            "campaign-manifest.json",
            "campaign.resume.json",
        ):
            if not (first_dir / name).is_file():
                raise AssertionError(f"campaign did not write required output: {name}")
        first = json.loads((first_dir / "decision.json").read_text(encoding="utf-8"))
        assert_decision(first)
        run(
            [
                sys.executable,
                str(args.schema_checker),
                "--decision",
                str(first_dir / "decision.json"),
            ]
        )

        inventory = json.loads(
            (first_dir / "arena-report-inventory.json").read_text(encoding="utf-8")
        )
        if len(inventory["reports"]) != 16:
            raise AssertionError(
                f"tiny primary+holdout 2x1 matrices did not produce sixteen reports: {inventory!r}"
            )
        selected_by_set: dict[str, set[str]] = {}
        for report in inventory["reports"]:
            selected_by_set.setdefault(report["opening_set"], set()).add(
                report["selected_openings_checksum"]
            )
        if set(selected_by_set) != {"primary", "holdout"} or any(
            len(checksums) != 1 for checksums in selected_by_set.values()
        ):
            raise AssertionError(
                f"matrix variants did not reuse deterministic opening selections: {selected_by_set!r}"
            )

        resume = run([*first_command, "--resume"])
        if "skipped-resume-validated" not in (
            first_dir / "arena-report-inventory.json"
        ).read_text(encoding="utf-8"):
            raise AssertionError(f"resume did not validate and skip stages: {resume.stdout}")
        mismatch = run([*first_command, "--resume", "--tt-bytes", "2097152"], success=False)
        if "resume metadata mismatch" not in mismatch.stderr:
            raise AssertionError(f"resume config mismatch was not explicit: {mismatch.stderr}")

        second_dir = root / "second"
        run(campaign_command(args.runner, args.exe, root, second_dir))
        second = json.loads((second_dir / "decision.json").read_text(encoding="utf-8"))
        if first["selected_opening_checksum"] != second["selected_opening_checksum"]:
            raise AssertionError("deterministic rerun selected different openings")
        if first["suggested_decision"] != second["suggested_decision"]:
            raise AssertionError("deterministic neutral rerun changed the suggested decision")

        missing = dict(first)
        missing.pop("telemetry")
        missing_path = root / "missing-field.json"
        missing_path.write_text(json.dumps(missing), encoding="utf-8")
        rejected = run(
            [sys.executable, str(args.schema_checker), "--decision", str(missing_path)],
            success=False,
        )
        if "missing fields" not in rejected.stderr:
            raise AssertionError(f"schema checker did not reject missing field: {rejected.stderr}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
