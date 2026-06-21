#!/usr/bin/env python3
"""CTest wrapper for the local training run analyzer."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


def stable_json(data: Any) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(stable_json(data), encoding="utf-8")


def smoke_report(
    run_id: str,
    created_at: str,
    source_kind: str,
    counts_by_split: dict[str, int],
    counts_by_phase: dict[str, int],
    artifact_checksum: str | None,
    evaluation_smoke_summary: dict[str, Any] | None,
    search_smoke_summary: dict[str, Any] | None,
    sequence_cache: dict[str, Any] | None = None,
    stage_timings: dict[str, Any] | None = None,
    dataset_output_format: str | None = None,
    trainer_version: str = "pattern-sgd-v0b",
    measurement_split_policy: str = "preserve",
    board_collision_count_after: int | None = None,
    game_collision_count_after: int | None = None,
) -> dict[str, Any]:
    report = {
        "schema_version": 1,
        "run_id": run_id,
        "created_at_utc": created_at,
        "git_commit": "smoke",
        "source_dataset_id": "tiny-smoke",
        "source_kind": source_kind,
        "input_mode": "normalized-tsv",
        "sample_policy": {
            "method": "synthetic smoke fixture",
            "split_policy": "preserve",
            "measurement_split_policy": measurement_split_policy,
        },
        "measurement_split_policy": measurement_split_policy,
        "measurement_split_policy_version": f"{measurement_split_policy}-v1",
        "sample_counts_by_split": counts_by_split,
        "sample_counts_by_phase": counts_by_phase,
        "sample_report_checksum": "sha256:sample",
        "dataset_report_checksum": "sha256:dataset",
        "trainer_version": trainer_version,
        "trainer_mode": trainer_version,
        "trainer_args": {
            "epochs": 8,
            "learning_rate": 0.1,
            "l2": 0.0,
            "seed": 7,
        },
        "trainer_report_checksum": "sha256:trainer",
        "weights_checksum": "sha256:weights",
        "artifact_checksum": artifact_checksum,
        "evaluation_smoke_summary": evaluation_smoke_summary,
        "search_smoke_summary": search_smoke_summary,
        "output_files": {
            "trainer_report_json": "v0b-trainer-report.json",
            "v0b_trainer_report_json": "v0b-trainer-report.json",
        },
        "notes": [
            "local run only",
            "not production benchmark",
            "not strength claim",
            "Egaroucid-derived artifacts are not committed",
            "publication remains gated / unknown",
        ],
    }
    if sequence_cache is not None:
        report["sequence_cache"] = sequence_cache
    if stage_timings is not None:
        report["stage_timings"] = stage_timings
    if dataset_output_format is not None:
        report["dataset_output_format"] = dataset_output_format
    if board_collision_count_after is not None:
        report["board_leakage_audit_after"] = {
            "unique_board_count": 10,
            "cross_split_board_collision_count": board_collision_count_after,
            "cross_split_board_collision_counts_by_pair": {
                "train__test": board_collision_count_after
            }
            if board_collision_count_after
            else {},
        }
        report["board_leakage_audit"] = report["board_leakage_audit_after"]
    if game_collision_count_after is not None:
        report["game_group_leakage_audit_after"] = {
            "unique_game_group_count": 8,
            "cross_split_game_group_collision_count": game_collision_count_after,
            "cross_split_game_group_collision_counts_by_pair": {
                "train__validation": game_collision_count_after
            }
            if game_collision_count_after
            else {},
        }
        report["game_group_leakage_audit"] = report["game_group_leakage_audit_after"]
    if measurement_split_policy == "connected-board-game":
        report["connected_component_count"] = 6
    return report


def sequence_cache(status: str) -> dict[str, Any]:
    return {
        "enabled": True,
        "cache_dir": "local-cache",
        "cache_key": "abc123",
        "status": status,
        "metadata_path": "local-cache/abc123/metadata.json",
        "normalized_tsv_sha256": "sha256:normalized",
        "import_report_sha256": "sha256:report",
        "notes": ["sequence replay cache is local-only and must not be committed"],
    }


def stage_timings() -> dict[str, Any]:
    return {
        "sequence_import_or_cache_restore": {"wall_time_sec": 1.25, "status": "hit"},
        "pattern_dataset_generation": {"wall_time_sec": 2.5, "status": "ok"},
        "trainer": {"wall_time_sec": 3.75, "status": "ok"},
        "trainer_v0b": {"wall_time_sec": 3.75, "status": "ok"},
        "v0b_export": {"wall_time_sec": 0.5, "status": "ok"},
        "evaluation_smoke": {"wall_time_sec": 0.25, "status": "ok"},
        "search_smoke": {"wall_time_sec": 0.75, "status": "ok"},
    }


def trainer_report() -> dict[str, Any]:
    return {
        "schema_version": 1,
        "trainer_version": "pattern-sgd-v0b",
        "checksum": "sha256:trainer",
        "weights_checksum": "sha256:weights",
        "final_pattern_sgd_metrics": {
            "metrics_by_split": {
                "train": {"rows": 12, "MAE": 1.25, "RMSE": 1.5, "sign_accuracy": 1.0},
                "validation": {
                    "rows": 1,
                    "MAE": 2.0,
                    "RMSE": 2.0,
                    "sign_accuracy": 0.0,
                },
                "test": {"rows": 1, "MAE": 3.0, "RMSE": 3.0, "sign_accuracy": 1.0},
            },
            "metrics_by_phase": {
                "0": {"rows": 13, "MAE": 1.5, "RMSE": 1.8, "sign_accuracy": 0.9},
                "12": {"rows": 1, "MAE": 3.0, "RMSE": 3.0, "sign_accuracy": 1.0},
            },
        },
    }


def trainer_report_v0c() -> dict[str, Any]:
    report = trainer_report()
    report["trainer_version"] = "pattern-sgd-v0c"
    report["best_validation_MAE"] = 1.75
    report["final_validation_MAE"] = 2.0
    report["nonzero_weight_count"] = 4
    report["weight_l2_norm"] = 2.5
    report["max_abs_weight"] = 1.25
    report["baseline_phase_bias_metrics"] = {
        "metrics_by_split_phase": {
            "train": {"0": {"examples": 12}, "1": {"examples": 0}},
            "validation": {"0": {"examples": 2, "MAE": 2.5}, "1": {"examples": 1, "MAE": 1.0}},
            "test": {"0": {"examples": 2, "MAE": 4.0}, "1": {"examples": 0, "MAE": None}},
        }
    }
    report["final_pattern_sgd_metrics"]["metrics_by_split_phase"] = {
        "train": {"0": {"examples": 12}, "1": {"examples": 0}},
        "validation": {"0": {"examples": 2, "MAE": 2.0}, "1": {"examples": 1, "MAE": 1.5}},
        "test": {"0": {"examples": 2, "MAE": 3.0}, "1": {"examples": 0, "MAE": None}},
    }
    return report


def trainer_report_v0d() -> dict[str, Any]:
    report = trainer_report_v0c()
    report["trainer_version"] = "pattern-sgd-v0d"
    report["phase_balance"] = "sqrt-inverse-count"
    report["phase_weight_floor"] = 0.25
    report["phase_weight_cap"] = 4.0
    report["phase_train_counts"] = {str(phase): 0 for phase in range(13)}
    report["phase_train_counts"]["0"] = 120
    report["phase_train_counts"]["1"] = 20
    report["phase_weights"] = {str(phase): 0.0 for phase in range(13)}
    report["phase_weights"]["0"] = 0.8
    report["phase_weights"]["1"] = 2.2
    report["weighted_train_residual_MAE"] = 1.5
    report["phase_balance_notes"] = ["phase balance scheme: sqrt-inverse-count"]
    return report


def create_fixtures(root: Path) -> Path:
    runs_dir = root / "runs"
    first_dir = runs_dir / "01-good"
    second_dir = runs_dir / "02-warning"
    third_dir = runs_dir / "03-sequence"
    fourth_dir = runs_dir / "04-sequence-miss"
    fifth_dir = runs_dir / "05-v0c"
    sixth_dir = runs_dir / "06-v0d"
    write_json(first_dir / "v0b-trainer-report.json", trainer_report())
    write_json(second_dir / "v0b-trainer-report.json", trainer_report())
    write_json(third_dir / "v0b-trainer-report.json", trainer_report())
    write_json(fourth_dir / "v0b-trainer-report.json", trainer_report())
    write_json(fifth_dir / "v0b-trainer-report.json", trainer_report_v0c())
    write_json(sixth_dir / "v0b-trainer-report.json", trainer_report_v0d())
    write_json(
        first_dir / "local-training-run-report.json",
        smoke_report(
            "smoke-10k",
            "2026-01-02T03:04:05Z",
            "egaroucid-local",
            {"train": 12, "validation": 1, "test": 1},
            {"0": 13, "12": 1},
            "0xartifact",
            {
                "summary": {
                    "positions_count": "5",
                    "v0a_v0b_different_count": "2",
                },
                "report_checksum": "0xeval",
            },
            {
                "summary": {
                    "positions_count": "1",
                    "v0a_v0b_score_different_count": "1",
                    "v0a_v0b_best_move_different_count": "0",
                },
                "report_checksum": "0xsearch",
            },
            None,
            stage_timings(),
            "expanded-tsv",
        ),
    )
    write_json(
        second_dir / "local-training-run-report.json",
        smoke_report(
            "smoke-warning",
            "2026-01-02T03:04:06Z",
            "synthetic-local",
            {"train": 0, "validation": 0, "test": 0},
            {"0": 0},
            "",
            {
                "summary": {
                    "positions_count": "3",
                    "v0a_v0b_different_count": "0",
                },
                "report_checksum": "0xeval-zero",
            },
            None,
            None,
            None,
            "expanded-tsv",
            "pattern-sgd-v0b",
            "preserve",
            2,
        ),
    )
    write_json(
        third_dir / "local-training-run-report.json",
        smoke_report(
            "smoke-sequence",
            "2026-01-02T03:04:07Z",
            "egaroucid-sequence-local",
            {"train": 120, "validation": 10, "test": 10},
            {"1": 50, "2": 45, "3": 45},
            "0xsequence",
            {
                "summary": {
                    "positions_count": "10",
                    "v0a_v0b_different_count": "5",
                },
                "report_checksum": "0xeval-sequence",
            },
            {
                "summary": {
                    "positions_count": "8",
                    "v0a_v0b_score_different_count": "3",
                    "v0a_v0b_best_move_different_count": "1",
                },
                "report_checksum": "0xsearch-sequence",
            },
            sequence_cache("hit"),
            stage_timings(),
            "compact-tsv",
            "pattern-sgd-v0b",
            "connected-board-game",
            0,
            0,
        ),
    )
    write_json(
        fourth_dir / "local-training-run-report.json",
        smoke_report(
            "smoke-sequence-miss",
            "2026-01-02T03:04:08Z",
            "egaroucid-sequence-local",
            {"train": 120, "validation": 10, "test": 10},
            {"1": 50, "2": 45, "3": 45},
            "0xsequence-miss",
            {
                "summary": {
                    "positions_count": "10",
                    "v0a_v0b_different_count": "5",
                },
                "report_checksum": "0xeval-sequence-miss",
            },
            {
                "summary": {
                    "positions_count": "8",
                    "v0a_v0b_score_different_count": "3",
                    "v0a_v0b_best_move_different_count": "1",
                },
                "report_checksum": "0xsearch-sequence-miss",
            },
            sequence_cache("miss"),
            stage_timings(),
            None,
        ),
    )
    write_json(
        fifth_dir / "local-training-run-report.json",
        smoke_report(
            "smoke-v0c",
            "2026-01-02T03:04:09Z",
            "egaroucid-local",
            {"train": 120, "validation": 10, "test": 10},
            {"0": 120, "1": 20},
            "0xv0c",
            {
                "summary": {
                    "positions_count": "10",
                    "v0a_v0b_different_count": "5",
                },
                "report_checksum": "0xeval-v0c",
            },
            {
                "summary": {
                    "positions_count": "8",
                    "v0a_v0b_score_different_count": "3",
                    "v0a_v0b_best_move_different_count": "1",
                },
                "report_checksum": "0xsearch-v0c",
            },
            None,
            stage_timings(),
            "expanded-tsv",
            "pattern-sgd-v0c",
        ),
    )
    write_json(
        sixth_dir / "local-training-run-report.json",
        smoke_report(
            "smoke-v0d",
            "2026-01-02T03:04:10Z",
            "egaroucid-local",
            {"train": 120, "validation": 10, "test": 10},
            {"0": 120, "1": 20},
            "0xv0d",
            {
                "summary": {
                    "positions_count": "10",
                    "v0a_v0b_different_count": "5",
                },
                "report_checksum": "0xeval-v0d",
            },
            {
                "summary": {
                    "positions_count": "8",
                    "v0a_v0b_score_different_count": "3",
                    "v0a_v0b_best_move_different_count": "1",
                },
                "report_checksum": "0xsearch-v0d",
            },
            None,
            stage_timings(),
            "expanded-tsv",
            "pattern-sgd-v0d",
        ),
    )
    return runs_dir


def run_analyzer(analyzer: Path, runs_dir: Path, output_dir: Path) -> dict[str, Any] | None:
    json_out = output_dir / "summary.json"
    markdown_out = output_dir / "summary.md"
    command = [
        sys.executable,
        str(analyzer),
        "--reports-dir",
        str(runs_dir),
        "--json-out",
        str(json_out),
        "--markdown-out",
        str(markdown_out),
        "--min-train-rows",
        "100",
    ]
    result = subprocess.run(command, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return None
    try:
        return {
            "json": json.loads(json_out.read_text(encoding="utf-8")),
            "json_text": json_out.read_text(encoding="utf-8"),
            "markdown_text": markdown_out.read_text(encoding="utf-8"),
        }
    except (OSError, json.JSONDecodeError) as error:
        print(f"could not read analyzer output: {error}", file=sys.stderr)
        return None


def warning_codes(summary: dict[str, Any]) -> dict[str, list[str]]:
    return {
        run["run_id"]: [warning["code"] for warning in run["warnings"]]
        for run in summary["runs"]
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--analyzer", required=True, type=Path)
    parser.add_argument("--source-dir", required=True, type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        runs_dir = create_fixtures(temp_dir)
        first = run_analyzer(args.analyzer, runs_dir, temp_dir / "first")
        second = run_analyzer(args.analyzer, runs_dir, temp_dir / "second")
        if first is None or second is None:
            return 1
        if first["json_text"] != second["json_text"]:
            print("analyzer JSON output is not deterministic", file=sys.stderr)
            return 1
        if first["markdown_text"] != second["markdown_text"]:
            print("analyzer Markdown output is not deterministic", file=sys.stderr)
            return 1
        summary = first["json"]
        if summary.get("run_count") != 6:
            print(f"unexpected run count: {summary.get('run_count')!r}", file=sys.stderr)
            return 1
        if [run.get("run_id") for run in summary.get("runs", [])] != [
            "smoke-10k",
            "smoke-warning",
            "smoke-sequence",
            "smoke-sequence-miss",
            "smoke-v0c",
            "smoke-v0d",
        ]:
            print(f"unexpected run order: {summary.get('runs')!r}", file=sys.stderr)
            return 1
        expected_warnings = {
            "smoke-10k": [
                "train_rows_too_small",
                "phase_coverage_biased",
                "dataset_output_format_mixed_comparison",
                "trainer_mode_mixed_comparison",
            ],
            "smoke-warning": [
                "train_rows_too_small",
                "validation_empty",
                "test_empty",
                "phase_coverage_biased",
                "evaluation_score_difference_zero",
                "search_smoke_missing",
                "artifact_checksum_missing",
                "source_kind_unknown",
                "exact_board_cross_split_collision",
                "telemetry_missing",
                "dataset_output_format_mixed_comparison",
                "trainer_mode_mixed_comparison",
            ],
            "smoke-sequence": [
                "cache_status_mixed_comparison",
                "dataset_output_format_mixed_comparison",
                "trainer_mode_mixed_comparison",
            ],
            "smoke-sequence-miss": [
                "cache_status_mixed_comparison",
                "trainer_mode_mixed_comparison",
            ],
            "smoke-v0c": [
                "phase_coverage_biased",
                "validation_phase_count_tiny",
                "phase_missing_train_examples",
                "phase_mae_regressed_vs_phase_bias",
                "dataset_output_format_mixed_comparison",
                "trainer_mode_mixed_comparison",
            ],
            "smoke-v0d": [
                "phase_coverage_biased",
                "validation_phase_count_tiny",
                "phase_missing_train_examples",
                "phase_mae_regressed_vs_phase_bias",
                "dataset_output_format_mixed_comparison",
                "trainer_mode_mixed_comparison",
            ],
        }
        if warning_codes(summary) != expected_warnings:
            print(f"unexpected warnings: {warning_codes(summary)!r}", file=sys.stderr)
            return 1
        first_run = summary["runs"][0]
        if first_run.get("metrics", {}).get("train", {}).get("MAE") != 1.25:
            print(f"trainer metrics were not extracted: {first_run!r}", file=sys.stderr)
            return 1
        v0c_run = summary["runs"][4]
        if v0c_run.get("best_validation_MAE") != 1.75:
            print(f"v0c best validation MAE was not extracted: {v0c_run!r}", file=sys.stderr)
            return 1
        if v0c_run.get("weight_diagnostics", {}).get("nonzero_weight_count") != 4:
            print(f"v0c weight diagnostics were not extracted: {v0c_run!r}", file=sys.stderr)
            return 1
        v0d_run = summary["runs"][5]
        phase_balance = v0d_run.get("phase_balance_diagnostics")
        if not isinstance(phase_balance, dict) or phase_balance.get("phase_balance") != "sqrt-inverse-count":
            print(f"v0d phase-balance diagnostics were not extracted: {v0d_run!r}", file=sys.stderr)
            return 1
        if "phase_balance" not in first["markdown_text"]:
            print("Markdown summary is missing phase balance column", file=sys.stderr)
            return 1
        sequence_run = summary["runs"][2]
        if sequence_run.get("cache_status") != "hit":
            print(f"cache status was not extracted: {sequence_run!r}", file=sys.stderr)
            return 1
        if sequence_run.get("measurement_split_policy") != "connected-board-game":
            print(f"measurement split policy was not extracted: {sequence_run!r}", file=sys.stderr)
            return 1
        if sequence_run.get("board_cross_split_collision_count_after") != 0:
            print(f"board collision count was not extracted: {sequence_run!r}", file=sys.stderr)
            return 1
        if sequence_run.get("game_group_cross_split_collision_count_after") != 0:
            print(f"game collision count was not extracted: {sequence_run!r}", file=sys.stderr)
            return 1
        if sequence_run.get("stage_wall_times", {}).get("evaluation_search_smoke") != 1.0:
            print(f"stage telemetry was not summarized: {sequence_run!r}", file=sys.stderr)
            return 1
        if "cache_status" not in first["markdown_text"]:
            print("Markdown summary is missing cache status column", file=sys.stderr)
            return 1
        if "split_policy" not in first["markdown_text"]:
            print("Markdown summary is missing split policy column", file=sys.stderr)
            return 1
        git_result = subprocess.run(
            [
                "git",
                "-C",
                str(args.source_dir),
                "ls-files",
                "--",
                ":(glob)**/local-training-run-report.json",
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        if git_result.returncode != 0:
            sys.stderr.write(git_result.stderr)
            return 1
        committed_reports = git_result.stdout.splitlines()
        if committed_reports:
            print(f"local run reports must not be committed: {committed_reports}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
