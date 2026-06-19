#!/usr/bin/env python3
"""CTest wrapper for local training runner sequence-input mode."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


def parse_key_values(text: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in text.splitlines():
        key, separator, value = line.partition("=")
        if not separator:
            raise ValueError(f"line is missing '=': {line}")
        values[key] = value
    return values


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def run_runner(args: argparse.Namespace, output_dir: Path) -> tuple[dict[str, str], dict[str, Any]] | None:
    command = [
        sys.executable,
        str(args.runner),
        "--sequence-input",
        str(args.sequence_fixture),
        "--sequence-manifest",
        str(args.sequence_manifest),
        "--output-dir",
        str(output_dir),
        "--run-id",
        "local-sequence-runner-smoke",
        "--created-at-utc",
        "2026-01-02T03:04:05Z",
        "--max-examples",
        "100",
        "--max-per-phase",
        "100",
        "--epochs",
        "8",
        "--learning-rate",
        "0.2",
        "--l2",
        "0.0",
        "--seed",
        "7",
        "--sequence-min-ply",
        "4",
        "--sequence-max-ply",
        "58",
        "--sequence-ply-stride",
        "1",
        "--sequence-max-positions",
        "80",
        "--sequence-no-emit-terminal",
        "--eval-smoke-max-positions",
        "10",
        "--search-smoke-max-positions",
        "3",
        "--dataset-exe",
        str(args.dataset_exe),
        "--eval-smoke-exe",
        str(args.eval_smoke_exe),
        "--search-smoke-exe",
        str(args.search_smoke_exe),
    ]
    result = subprocess.run(command, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return None
    try:
        summary = parse_key_values(result.stdout)
    except ValueError as error:
        print(error, file=sys.stderr)
        return None
    return summary, load_json(output_dir / "local-training-run-report.json")


def check_report(report: dict[str, Any], output_dir: Path) -> bool:
    expected = {
        "schema_version": 1,
        "run_id": "local-sequence-runner-smoke",
        "created_at_utc": "2026-01-02T03:04:05Z",
        "source_dataset_id": "egaroucid-sequence-v0002-local",
        "source_kind": "egaroucid-sequence-local",
        "input_mode": "sequence-input",
        "trainer_version": "pattern-sgd-v0b",
    }
    for key, value in expected.items():
        if report.get(key) != value:
            print(f"report mismatch for {key}: {report.get(key)!r}", file=sys.stderr)
            return False
    if report.get("trainer_args") != {"epochs": 8, "learning_rate": 0.2, "l2": 0.0, "seed": 7}:
        print(f"unexpected trainer args: {report.get('trainer_args')!r}", file=sys.stderr)
        return False
    sample_counts = report.get("sample_counts_by_split")
    if not isinstance(sample_counts, dict) or sum(sample_counts.values()) != 80:
        print(f"sequence import cap did not bound sampled rows: {sample_counts!r}", file=sys.stderr)
        return False
    if report.get("eval_smoke_input_positions", 0) <= report.get("eval_smoke_used_positions", 0):
        print("eval smoke cap did not reduce positions", file=sys.stderr)
        return False
    if report.get("search_smoke_input_positions", 0) <= report.get("search_smoke_used_positions", 0):
        print("search smoke cap did not reduce positions", file=sys.stderr)
        return False
    if report.get("eval_smoke_used_positions") != 10:
        print(f"unexpected eval smoke used positions: {report.get('eval_smoke_used_positions')}", file=sys.stderr)
        return False
    if report.get("search_smoke_used_positions") != 3:
        print(
            f"unexpected search smoke used positions: {report.get('search_smoke_used_positions')}",
            file=sys.stderr,
        )
        return False
    policy = report.get("smoke_position_sample_policy")
    if not isinstance(policy, dict):
        print("missing smoke position sample policy", file=sys.stderr)
        return False
    if policy.get("search", {}).get("method") != "deterministic position_id sha256 top-k":
        print(f"unexpected search smoke policy: {policy!r}", file=sys.stderr)
        return False
    output_files = report.get("output_files")
    required = {
        "sequence_import_report_json",
        "evaluation_smoke_positions_tsv",
        "search_smoke_positions_tsv",
        "evaluation_smoke_report_json",
        "search_smoke_report_json",
    }
    if not isinstance(output_files, dict) or not required.issubset(output_files):
        print(f"missing output files: {output_files!r}", file=sys.stderr)
        return False
    for relative in output_files.values():
        path = output_dir / relative
        if not path.exists():
            print(f"missing generated file: {path}", file=sys.stderr)
            return False
    sequence_report = load_json(output_dir / output_files["sequence_import_report_json"])
    if sequence_report.get("source_kind") != "egaroucid-sequence-local":
        print(f"bad sequence import report: {sequence_report!r}", file=sys.stderr)
        return False
    emit_policy = sequence_report.get("emit_policy")
    expected_emit_policy = {
        "emit_terminal": False,
        "max_ply": 58,
        "max_positions": 80,
        "min_ply": 4,
        "ply_stride": 1,
        "sample_policy": "deterministic position_id sha256 top-k when max_positions is set",
        "seed": 7,
    }
    if emit_policy != expected_emit_policy:
        print(f"unexpected sequence emit policy: {emit_policy!r}", file=sys.stderr)
        return False
    if sequence_report.get("emitted_positions") != 80:
        print(f"sequence import cap did not bound emitted positions: {sequence_report!r}", file=sys.stderr)
        return False
    if sequence_report.get("rejected_games") != 1 or sequence_report.get("pass_count", 0) < 3:
        print(f"sequence importer did not report expected reject/pass counts: {sequence_report!r}", file=sys.stderr)
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--dataset-exe", required=True, type=Path)
    parser.add_argument("--eval-smoke-exe", required=True, type=Path)
    parser.add_argument("--search-smoke-exe", required=True, type=Path)
    parser.add_argument("--sequence-fixture", required=True, type=Path)
    parser.add_argument("--sequence-manifest", required=True, type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        first = run_runner(args, temp_dir / "first")
        second = run_runner(args, temp_dir / "second")
        if first is None or second is None:
            return 1
        first_summary, first_report = first
        second_summary, second_report = second
        if first_summary.get("trainer_report_checksum") != second_summary.get(
            "trainer_report_checksum"
        ):
            print("trainer checksum is not deterministic", file=sys.stderr)
            return 1
        if first_summary.get("artifact_checksum") != second_summary.get("artifact_checksum"):
            print("artifact checksum is not deterministic", file=sys.stderr)
            return 1
        if first_report != second_report:
            print("local sequence run report is not deterministic", file=sys.stderr)
            return 1
        if not check_report(first_report, temp_dir / "first"):
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
