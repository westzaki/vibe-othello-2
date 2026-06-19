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
) -> dict[str, Any]:
    return {
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
        },
        "sample_counts_by_split": counts_by_split,
        "sample_counts_by_phase": counts_by_phase,
        "sample_report_checksum": "sha256:sample",
        "dataset_report_checksum": "sha256:dataset",
        "trainer_version": "pattern-sgd-v0b",
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


def create_fixtures(root: Path) -> Path:
    runs_dir = root / "runs"
    first_dir = runs_dir / "01-good"
    second_dir = runs_dir / "02-warning"
    write_json(first_dir / "v0b-trainer-report.json", trainer_report())
    write_json(second_dir / "v0b-trainer-report.json", trainer_report())
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
        if summary.get("run_count") != 2:
            print(f"unexpected run count: {summary.get('run_count')!r}", file=sys.stderr)
            return 1
        if [run.get("run_id") for run in summary.get("runs", [])] != [
            "smoke-10k",
            "smoke-warning",
        ]:
            print(f"unexpected run order: {summary.get('runs')!r}", file=sys.stderr)
            return 1
        expected_warnings = {
            "smoke-10k": ["train_rows_too_small", "phase_coverage_biased"],
            "smoke-warning": [
                "train_rows_too_small",
                "validation_empty",
                "test_empty",
                "phase_coverage_biased",
                "evaluation_score_difference_zero",
                "search_smoke_missing",
                "artifact_checksum_missing",
                "source_kind_not_egaroucid_local",
            ],
        }
        if warning_codes(summary) != expected_warnings:
            print(f"unexpected warnings: {warning_codes(summary)!r}", file=sys.stderr)
            return 1
        first_run = summary["runs"][0]
        if first_run.get("metrics", {}).get("train", {}).get("MAE") != 1.25:
            print(f"trainer metrics were not extracted: {first_run!r}", file=sys.stderr)
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
