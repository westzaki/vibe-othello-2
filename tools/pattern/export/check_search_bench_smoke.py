#!/usr/bin/env python3
"""CTest wrapper for fixed-position learned pattern search smoke."""

from __future__ import annotations

import argparse
import json
import sys
import tempfile
from pathlib import Path
from typing import Any

from smoke_pipeline import build_v0a_v0b_artifacts, parse_key_values, run_or_report


def check_artifact_checksum(
    report: dict[str, Any], v0a_export: dict[str, str], v0b_export: dict[str, str]
) -> bool:
    artifact_checksums = report.get("artifact_checksums")
    if not isinstance(artifact_checksums, dict):
        print("report artifact_checksums must be an object", file=sys.stderr)
        return False
    expected = {
        "v0a": v0a_export.get("weights_checksum"),
        "v0b": v0b_export.get("weights_checksum"),
    }
    if artifact_checksums != expected:
        print(
            f"artifact checksums mismatch: report={artifact_checksums!r} expected={expected!r}",
            file=sys.stderr,
        )
        return False
    return True


def smoke_source_for(pattern_set: str) -> str:
    if pattern_set == "fixed-pattern-fixture-v1" or pattern_set == "tiny":
        return "tiny-synthetic-v0b-search-smoke"
    if pattern_set == "pattern-v2-endgame-lite" or pattern_set == "endgame-lite":
        return "endgame-lite-synthetic-v0b-search-smoke"
    return "buro-lite-synthetic-v0b-search-smoke"


def canonical_pattern_set_id(pattern_set: str) -> str:
    aliases = {
        "tiny": "fixed-pattern-fixture-v1",
        "buro-lite": "pattern-v1-buro-lite",
        "endgame-lite": "pattern-v2-endgame-lite",
    }
    return aliases.get(pattern_set, pattern_set)


def check_report(
    report_path: Path,
    first_summary: dict[str, str],
    v0a_export: dict[str, str],
    v0b_export: dict[str, str],
    pattern_set: str,
) -> bool:
    try:
        report: dict[str, Any] = json.loads(report_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"could not read search report JSON: {error}", file=sys.stderr)
        return False

    expected_notes = [
        "local smoke only",
        "not production benchmark",
        "not strength claim",
        "depth 1 only checks evaluator signal propagation into search score",
        "synthetic artifacts are temp-only",
        "publication remains gated / unknown",
    ]
    expected_fields: dict[str, Any] = {
        "schema_version": 1,
        "source": smoke_source_for(pattern_set),
        "pattern_set_id": canonical_pattern_set_id(pattern_set),
        "phase_count": 13,
        "notes": expected_notes,
    }
    for key, expected in expected_fields.items():
        if report.get(key) != expected:
            print(f"report field mismatch for {key}: {report.get(key)!r}", file=sys.stderr)
            return False

    if not check_artifact_checksum(report, v0a_export, v0b_export):
        return False

    search_config = report.get("search_config")
    expected_search_config = {
        "mode": "fixed_depth",
        "entry_point": "search_iterative",
        "depth": 1,
        "depth_policy": "smoke-only leaf-evaluator propagation",
        "time_control": "off",
        "midgame_tt": "off",
        "endgame_exact": "off",
        "endgame_tt": "off",
        "endgame_parity_ordering": "off",
        "threading": "single",
        "elapsed_ms_policy": "fixed-zero-for-deterministic-smoke",
    }
    if search_config != expected_search_config:
        print(f"unexpected search_config: {search_config!r}", file=sys.stderr)
        return False

    result_rows = report.get("result_rows")
    if not isinstance(result_rows, list) or not result_rows:
        print("report result_rows must be a non-empty array", file=sys.stderr)
        return False
    positions_count = report.get("positions_count")
    if not isinstance(positions_count, int) or positions_count <= 0:
        print(f"invalid positions_count: {positions_count!r}", file=sys.stderr)
        return False
    if len(result_rows) != positions_count * 2:
        print("result_rows must contain v0a and v0b rows for every position", file=sys.stderr)
        return False
    if str(positions_count) != first_summary.get("positions_count"):
        print("summary positions_count does not match report", file=sys.stderr)
        return False
    if str(len(result_rows)) != first_summary.get("result_rows"):
        print("summary result_rows does not match report", file=sys.stderr)
        return False

    score_diff_count = report.get("v0a_v0b_score_different_count")
    best_move_diff_count = report.get("v0a_v0b_best_move_different_count")
    if not isinstance(score_diff_count, int) or score_diff_count <= 0:
        print("report did not record any v0a/v0b search score differences", file=sys.stderr)
        return False
    if not isinstance(best_move_diff_count, int) or best_move_diff_count < 0:
        print("best-move difference count must be a non-negative integer", file=sys.stderr)
        return False
    if str(score_diff_count) != first_summary.get("v0a_v0b_score_different_count"):
        print("summary score difference count does not match report", file=sys.stderr)
        return False
    if str(best_move_diff_count) != first_summary.get("v0a_v0b_best_move_different_count"):
        print("summary best-move difference count does not match report", file=sys.stderr)
        return False

    checksum = report.get("checksum")
    if not isinstance(checksum, str) or not checksum.startswith("0x"):
        print(f"report checksum is invalid: {checksum!r}", file=sys.stderr)
        return False
    if checksum != first_summary.get("checksum"):
        print("summary checksum does not match report", file=sys.stderr)
        return False

    required_row_fields = {
        "position_id",
        "disc_count",
        "phase",
        "evaluator",
        "best_move",
        "score",
        "nodes",
        "depth",
        "elapsed_ms",
    }
    rows_by_position: dict[str, dict[str, dict[str, Any]]] = {}
    for row in result_rows:
        if not isinstance(row, dict) or set(row) != required_row_fields:
            print(f"unexpected result row shape: {row!r}", file=sys.stderr)
            return False
        if row["evaluator"] not in {"v0a", "v0b"}:
            print(f"unexpected evaluator in row: {row!r}", file=sys.stderr)
            return False
        if row["depth"] != 1 or row["elapsed_ms"] != 0:
            print(f"unexpected deterministic search row fields: {row!r}", file=sys.stderr)
            return False
        if not isinstance(row["nodes"], int) or row["nodes"] <= 0:
            print(f"nodes must be positive: {row!r}", file=sys.stderr)
            return False
        rows_by_position.setdefault(row["position_id"], {})[row["evaluator"]] = row

    position_diffs = report.get("position_diffs")
    if not isinstance(position_diffs, list) or len(position_diffs) != positions_count:
        print("position_diffs must contain one row per position", file=sys.stderr)
        return False
    score_delta_nonzero = 0
    for diff in position_diffs:
        if not isinstance(diff, dict) or set(diff) != {
            "position_id",
            "score_delta",
            "best_move_different",
        }:
            print(f"unexpected position diff shape: {diff!r}", file=sys.stderr)
            return False
        row_pair = rows_by_position.get(diff["position_id"], {})
        if set(row_pair) != {"v0a", "v0b"}:
            print(f"missing v0a/v0b row pair for {diff['position_id']!r}", file=sys.stderr)
            return False
        expected_delta = row_pair["v0b"]["score"] - row_pair["v0a"]["score"]
        if diff["score_delta"] != expected_delta:
            print(f"score_delta mismatch: {diff!r}", file=sys.stderr)
            return False
        if diff["score_delta"] != 0:
            score_delta_nonzero += 1
        expected_best_move_diff = row_pair["v0b"]["best_move"] != row_pair["v0a"]["best_move"]
        if diff["best_move_different"] != expected_best_move_diff:
            print(f"best_move_different mismatch: {diff!r}", file=sys.stderr)
            return False
    if score_delta_nonzero != score_diff_count:
        print("score difference count does not match position_diffs", file=sys.stderr)
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bench-exe", required=True, type=Path)
    parser.add_argument("--v0a-exporter", required=True, type=Path)
    parser.add_argument("--v0b-exporter", required=True, type=Path)
    parser.add_argument("--trainer", required=True, type=Path)
    parser.add_argument("--dataset-exe", required=True, type=Path)
    parser.add_argument("--normalized-tsv", required=True, type=Path)
    parser.add_argument("--pattern-set", default="fixed-pattern-fixture-v1")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        artifacts = build_v0a_v0b_artifacts(
            trainer=args.trainer,
            dataset_exe=args.dataset_exe,
            normalized_tsv=args.normalized_tsv,
            temp_dir=temp_dir,
            pattern_set=args.pattern_set,
            v0a_exporter=args.v0a_exporter,
            v0b_exporter=args.v0b_exporter,
            v0b_extra_args=["--epochs", "8", "--learning-rate", "0.5", "--l2", "0.0", "--seed", "7"],
        )
        if artifacts is None:
            return 1
        v0a_weights, v0b_weights, v0a_export, v0b_export = artifacts

        first_report = temp_dir / "fixed-position-search-report.json"
        second_report = temp_dir / "fixed-position-search-report-second.json"
        command = [
            str(args.bench_exe),
            "--positions-tsv",
            str(args.normalized_tsv),
            "--v0a-weights",
            str(v0a_weights),
            "--v0b-weights",
            str(v0b_weights),
            "--v0a-artifact-checksum",
            v0a_export["weights_checksum"],
            "--v0b-artifact-checksum",
            v0b_export["weights_checksum"],
            "--pattern-set",
            args.pattern_set,
        ]
        first = run_or_report([*command, "--report-out", str(first_report)])
        if first is None:
            return 1
        second = run_or_report([*command, "--report-out", str(second_report)])
        if second is None:
            return 1

        try:
            first_summary = parse_key_values(first.stdout)
            second_summary = parse_key_values(second.stdout)
        except ValueError as error:
            print(error, file=sys.stderr)
            return 1
        if first_summary != second_summary:
            print("search bench summary is not deterministic", file=sys.stderr)
            return 1
        if first_report.read_bytes() != second_report.read_bytes():
            print("search bench report JSON is not deterministic", file=sys.stderr)
            return 1
        if not check_report(first_report, first_summary, v0a_export, v0b_export, args.pattern_set):
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
