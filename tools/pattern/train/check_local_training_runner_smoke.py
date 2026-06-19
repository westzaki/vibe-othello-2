#!/usr/bin/env python3
"""CTest wrapper for the local Egaroucid training runner."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


def run_capture(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, capture_output=True, text=True)


def run_or_report(command: list[str]) -> subprocess.CompletedProcess[str] | None:
    result = run_capture(command)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return None
    return result


def parse_key_values(text: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in text.splitlines():
        key, separator, value = line.partition("=")
        if not separator:
            raise ValueError(f"line is missing '=': {line}")
        if key in values:
            raise ValueError(f"duplicate key: {key}")
        values[key] = value
    return values


def load_json(path: Path) -> dict[str, Any] | None:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"could not read report JSON: {error}", file=sys.stderr)
        return None
    if not isinstance(data, dict):
        print("report JSON root must be an object", file=sys.stderr)
        return None
    return data


def check_report(
    report: dict[str, Any],
    output_dir: Path,
    expected_eval_positions: int,
    expected_search_positions: int,
) -> bool:
    expected_scalars: dict[str, Any] = {
        "schema_version": 1,
        "run_id": "local-runner-smoke",
        "created_at_utc": "2026-01-02T03:04:05Z",
        "source_dataset_id": "egaroucid-train-data-board-score-v2025-02-02",
        "source_kind": "egaroucid-local",
        "input_mode": "raw-input",
        "trainer_version": "pattern-sgd-v0b",
        "notes": [
            "local run only",
            "not production benchmark",
            "not strength claim",
            "Egaroucid-derived artifacts are not committed",
            "publication remains gated / unknown",
        ],
    }
    for key, expected in expected_scalars.items():
        if report.get(key) != expected:
            print(f"report field mismatch for {key}: {report.get(key)!r}", file=sys.stderr)
            return False

    sample_policy = report.get("sample_policy")
    if not isinstance(sample_policy, dict):
        print("sample_policy must be an object", file=sys.stderr)
        return False
    if sample_policy.get("method") != "deterministic position_id sha256 top-k":
        print(f"unexpected sample method: {sample_policy!r}", file=sys.stderr)
        return False
    if sample_policy.get("split_policy") != "preserve":
        print(f"unexpected split policy: {sample_policy!r}", file=sys.stderr)
        return False

    sample_counts = report.get("sample_counts_by_split")
    if sample_counts != {"train": 5, "validation": 1, "test": 1}:
        print(f"unexpected sample split counts: {sample_counts!r}", file=sys.stderr)
        return False
    phase_counts = report.get("sample_counts_by_phase")
    if phase_counts != {"0": 6, "12": 1}:
        print(f"unexpected sample phase counts: {phase_counts!r}", file=sys.stderr)
        return False

    for key in (
        "sample_report_checksum",
        "dataset_report_checksum",
        "trainer_report_checksum",
        "weights_checksum",
        "artifact_checksum",
    ):
        value = report.get(key)
        if not isinstance(value, str) or not value:
            print(f"missing checksum-like field {key}: {value!r}", file=sys.stderr)
            return False

    trainer_args = report.get("trainer_args")
    if trainer_args != {"epochs": 8, "learning_rate": 0.9, "l2": 0.0, "seed": 7}:
        print(f"unexpected trainer_args: {trainer_args!r}", file=sys.stderr)
        return False

    eval_summary = report.get("evaluation_smoke_summary")
    search_summary = report.get("search_smoke_summary")
    if not isinstance(eval_summary, dict) or not isinstance(search_summary, dict):
        print("runner must include evaluation and search smoke summaries", file=sys.stderr)
        return False
    if eval_summary.get("summary", {}).get("positions_count") != str(expected_eval_positions):
        print(f"unexpected evaluation summary: {eval_summary!r}", file=sys.stderr)
        return False
    if search_summary.get("summary", {}).get("positions_count") != str(expected_search_positions):
        print(f"unexpected search summary: {search_summary!r}", file=sys.stderr)
        return False

    output_files = report.get("output_files")
    if not isinstance(output_files, dict):
        print("output_files must be an object", file=sys.stderr)
        return False
    required_files = {
        "normalized_tsv",
        "sampled_normalized_tsv",
        "sample_report_json",
        "pattern_dataset_tsv",
        "dataset_report_json",
        "v0b_weights_json",
        "v0b_trainer_report_json",
        "v0b_artifact_weights",
        "v0b_artifact_manifest",
        "v0a_weights_tsv",
        "v0a_trainer_report_json",
        "v0a_artifact_weights",
        "v0a_artifact_manifest",
        "evaluation_smoke_report_json",
        "search_smoke_report_json",
    }
    if set(output_files) != required_files:
        print(f"unexpected output_files keys: {set(output_files)!r}", file=sys.stderr)
        return False
    resolved_output_dir = output_dir.resolve()
    for label, relative_path in output_files.items():
        if not isinstance(relative_path, str) or Path(relative_path).is_absolute():
            print(f"output file path must be relative: {label}={relative_path!r}", file=sys.stderr)
            return False
        full_path = (output_dir / relative_path).resolve()
        if not full_path.exists() or resolved_output_dir not in full_path.parents:
            print(f"generated file is not under output dir: {full_path}", file=sys.stderr)
            return False
    return True


def run_runner(args: argparse.Namespace, output_dir: Path) -> tuple[dict[str, str], dict[str, Any]] | None:
    command = [
        sys.executable,
        str(args.runner),
        "--raw-input",
        str(args.egaroucid_fixture),
        "--manifest",
        str(args.egaroucid_manifest),
        "--output-dir",
        str(output_dir),
        "--run-id",
        "local-runner-smoke",
        "--created-at-utc",
        "2026-01-02T03:04:05Z",
        "--max-examples",
        "7",
        "--max-per-phase",
        "7",
        "--epochs",
        "8",
        "--learning-rate",
        "0.9",
        "--l2",
        "0.0",
        "--seed",
        "7",
        "--dataset-exe",
        str(args.dataset_exe),
        "--eval-smoke-exe",
        str(args.eval_smoke_exe),
        "--search-smoke-exe",
        str(args.search_smoke_exe),
    ]
    result = run_or_report(command)
    if result is None:
        return None
    try:
        summary = parse_key_values(result.stdout)
    except ValueError as error:
        print(error, file=sys.stderr)
        return None
    report_path = Path(summary.get("report", ""))
    if report_path != output_dir / "local-training-run-report.json":
        print(f"unexpected report path: {report_path}", file=sys.stderr)
        return None
    report = load_json(report_path)
    if report is None:
        return None
    return summary, report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--dataset-exe", required=True, type=Path)
    parser.add_argument("--eval-smoke-exe", required=True, type=Path)
    parser.add_argument("--search-smoke-exe", required=True, type=Path)
    parser.add_argument("--egaroucid-fixture", required=True, type=Path)
    parser.add_argument("--egaroucid-manifest", required=True, type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        first = run_runner(args, temp_dir / "first")
        second = run_runner(args, temp_dir / "second")
        if first is None or second is None:
            return 1
        first_summary, first_report = first
        second_summary, second_report = second
        if first_summary.get("sampled_rows") != "7":
            print(f"unexpected sampled rows: {first_summary!r}", file=sys.stderr)
            return 1
        if first_summary.get("trainer_report_checksum") != second_summary.get(
            "trainer_report_checksum"
        ):
            print("trainer report checksum is not deterministic", file=sys.stderr)
            return 1
        if first_summary.get("artifact_checksum") != second_summary.get("artifact_checksum"):
            print("artifact checksum is not deterministic", file=sys.stderr)
            return 1
        if first_report != second_report:
            print("local training run report JSON is not deterministic", file=sys.stderr)
            return 1
        if not check_report(
            first_report,
            temp_dir / "first",
            expected_eval_positions=5,
            expected_search_positions=1,
        ):
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
