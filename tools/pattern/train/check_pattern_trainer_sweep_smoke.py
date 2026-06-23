#!/usr/bin/env python3
"""CTest wrapper for the local pattern trainer sweep runner."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


CREATED_AT = "2026-01-02T03:04:05Z"
RUN_PREFIX = "sweep-smoke"
COMPACT_DATASET = """record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_features
train-a\t8\ttrain\t10\t1\tedge-8:0:1,corner-3x3:0:2
train-b\t16\ttrain\t-8\t3\tedge-8:0:2,corner-3x3:0:3
train-c\t24\ttrain\t14\t5\tedge-8:0:1,corner-3x3:0:4
train-d\t32\ttrain\t-12\t7\tedge-8:0:2,corner-3x3:0:5
validation-a\t20\tvalidation\t8\t4\tedge-8:0:1,corner-3x3:0:2
validation-b\t28\tvalidation\t-6\t6\tedge-8:0:2,corner-3x3:0:3
test-a\t36\ttest\t6\t8\tedge-8:0:1,corner-3x3:0:4
test-b\t44\ttest\t-4\t10\tedge-8:0:2,corner-3x3:0:5
"""
COMPACT_DATASET_VARIANT = """record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_features
train-a\t8\ttrain\t10\t1\tedge-8:0:1,corner-3x3:0:2
train-b\t16\ttrain\t-8\t3\tedge-8:0:2,corner-3x3:0:3
train-c\t24\ttrain\t14\t5\tedge-8:0:1,corner-3x3:0:4
train-d\t32\ttrain\t-12\t7\tedge-8:0:2,corner-3x3:0:5
train-e\t40\ttrain\t4\t9\tedge-8:0:6,corner-3x3:0:7
validation-a\t20\tvalidation\t8\t4\tedge-8:0:1,corner-3x3:0:2
validation-b\t28\tvalidation\t-6\t6\tedge-8:0:2,corner-3x3:0:3
test-a\t36\ttest\t6\t8\tedge-8:0:1,corner-3x3:0:4
test-b\t44\ttest\t-4\t10\tedge-8:0:2,corner-3x3:0:5
"""


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def run_capture(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, capture_output=True, text=True)


def base_command(args: argparse.Namespace, dataset: Path, output_dir: Path) -> list[str]:
    return [
        sys.executable,
        str(args.sweep_runner),
        "--dataset",
        str(dataset),
        "--output-dir",
        str(output_dir),
        "--trainer",
        str(args.trainer),
        "--created-at-utc",
        CREATED_AT,
        "--run-prefix",
        RUN_PREFIX,
        "--seed",
        "3",
    ]


def check_dry_run(args: argparse.Namespace, dataset: Path, root: Path) -> bool:
    output_dir = root / "dry-run"
    command = base_command(args, dataset, output_dir)
    command.extend(["--dry-run", "--max-configs", "3"])
    result = run_capture(command)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return False
    report = load_json(output_dir / "sweep-report.json")
    runs = report.get("runs")
    if not isinstance(runs, list) or len(runs) != 3:
        print(f"unexpected dry-run runs: {runs!r}", file=sys.stderr)
        return False
    if [run.get("config_id") for run in runs] != [
        "baseline_const_lr0.1",
        "const_lr0.05",
        "const_lr0.03",
    ]:
        print(f"unexpected planned config order: {runs!r}", file=sys.stderr)
        return False
    if any(run.get("status") != "planned" for run in runs):
        print(f"dry-run status mismatch: {runs!r}", file=sys.stderr)
        return False
    if any(Path(run["trainer_report_path"]).exists() for run in runs):
        print("dry-run unexpectedly created trainer report", file=sys.stderr)
        return False
    if report.get("dataset_format") != "compact-tsv" or report.get("dataset_row_count") != 8:
        print(f"unexpected dataset metadata: {report!r}", file=sys.stderr)
        return False
    if report.get("best_config_id") is not None:
        print(f"dry-run unexpectedly selected a best config: {report.get('best_config_id')!r}", file=sys.stderr)
        return False
    summary = (output_dir / "sweep-summary.md").read_text(encoding="utf-8")
    if "test metrics are reporting and tie-break only" not in summary:
        print("dry-run summary missing test metric policy note", file=sys.stderr)
        return False
    return True


def check_v0d_dry_run(args: argparse.Namespace, dataset: Path, root: Path) -> bool:
    output_dir = root / "v0d-dry-run"
    command = base_command(args, dataset, output_dir)
    command.extend(["--dry-run", "--sweep-preset", "v0d-100k-phase-core"])
    result = run_capture(command)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return False
    report = load_json(output_dir / "sweep-report.json")
    expected_ids = [
        "v0d_sqrt_bal_lr0.05",
        "v0d_sqrt_bal_lr0.1",
        "v0d_inv_bal_lr0.05",
        "v0d_none_lr0.05",
        "v0d_sqrt_bal_lr0.05_wd1e-4",
        "v0d_sqrt_bal_lr0.05_clip2",
    ]
    if [run.get("config_id") for run in report.get("runs", [])] != expected_ids:
        print(f"unexpected v0d dry-run config order: {report.get('runs')!r}", file=sys.stderr)
        return False
    configs = report.get("configs")
    if not isinstance(configs, list) or any(config.get("mode") != "pattern-sgd-v0d" for config in configs):
        print(f"v0d configs did not record trainer mode: {configs!r}", file=sys.stderr)
        return False
    phase_balances = [config.get("phase_balance") for config in configs]
    if phase_balances[:4] != [
        "sqrt-inverse-count",
        "sqrt-inverse-count",
        "inverse-count",
        "none",
    ]:
        print(f"unexpected v0d phase-balance args: {phase_balances!r}", file=sys.stderr)
        return False
    commands = [run.get("command") for run in report.get("runs", [])]
    if not all("--phase-balance" in command for command in commands if isinstance(command, list)):
        print(f"v0d dry-run commands did not include phase-balance args: {commands!r}", file=sys.stderr)
        return False
    return True


def check_execute(args: argparse.Namespace, dataset: Path, root: Path) -> bool:
    output_dir = root / "execute"
    command = base_command(args, dataset, output_dir)
    command.extend(["--max-configs", "2"])
    result = run_capture(command)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return False
    report = load_json(output_dir / "sweep-report.json")
    runs = report.get("runs")
    if not isinstance(runs, list) or len(runs) != 2:
        print(f"unexpected execute runs: {runs!r}", file=sys.stderr)
        return False
    if any(run.get("status") != "success" for run in runs):
        print(f"execute runs did not all succeed: {runs!r}", file=sys.stderr)
        return False
    if report.get("best_config_id") not in {run.get("config_id") for run in runs}:
        print(f"best config was not selected from successful runs: {report!r}", file=sys.stderr)
        return False
    policy = report.get("best_selection_policy")
    if not isinstance(policy, dict) or "validation_MAE" not in str(policy.get("primary")):
        print(f"missing best selection policy: {policy!r}", file=sys.stderr)
        return False
    for run in runs:
        for key in ("trainer_report_path", "weights_path", "stdout_log", "stderr_log"):
            if not Path(run[key]).exists():
                print(f"missing run output {key}: {run!r}", file=sys.stderr)
                return False
        if run.get("final_validation_MAE") is None or run.get("test_MAE") is None:
            print(f"run metrics were not extracted: {run!r}", file=sys.stderr)
            return False
        if run.get("trainer_report_checksum") is None or run.get("weights_checksum") is None:
            print(f"run checksums were not extracted: {run!r}", file=sys.stderr)
            return False
    summary = (output_dir / "sweep-summary.md").read_text(encoding="utf-8")
    if f"**{report.get('best_config_id')}**" not in summary:
        print("summary did not mark the best config", file=sys.stderr)
        return False
    return True


def check_resume(args: argparse.Namespace, dataset: Path, root: Path) -> bool:
    output_dir = root / "resume"
    first = base_command(args, dataset, output_dir)
    first.extend(["--max-configs", "2"])
    if run_capture(first).returncode != 0:
        print("initial resume fixture run failed", file=sys.stderr)
        return False
    second = base_command(args, dataset, output_dir)
    second.extend(["--max-configs", "2", "--resume"])
    result = run_capture(second)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return False
    report = load_json(output_dir / "sweep-report.json")
    if [run.get("status") for run in report.get("runs", [])] != ["skipped", "skipped"]:
        print(f"resume did not skip completed configs: {report.get('runs')!r}", file=sys.stderr)
        return False
    if report.get("best_config_id") is None:
        print("resume report lost best config selection", file=sys.stderr)
        return False
    return True


def check_resume_reruns_on_dataset_change(
    args: argparse.Namespace, dataset: Path, root: Path
) -> bool:
    output_dir = root / "resume-dataset-change"
    first = base_command(args, dataset, output_dir)
    first.extend(["--max-configs", "1"])
    if run_capture(first).returncode != 0:
        print("initial dataset-change fixture run failed", file=sys.stderr)
        return False
    variant = root / "variant-pattern-dataset.tsv"
    variant.write_text(COMPACT_DATASET_VARIANT, encoding="utf-8")
    second = base_command(args, variant, output_dir)
    second.extend(["--max-configs", "1", "--resume"])
    result = run_capture(second)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return False
    report = load_json(output_dir / "sweep-report.json")
    runs = report.get("runs")
    if not isinstance(runs, list) or len(runs) != 1:
        print(f"unexpected dataset-change runs: {runs!r}", file=sys.stderr)
        return False
    run = runs[0]
    if run.get("status") != "success":
        print(f"dataset change should rerun instead of skipping: {run!r}", file=sys.stderr)
        return False
    if "resume metadata mismatch" not in str(run.get("resume_validation_error")):
        print(f"dataset change did not record resume mismatch: {run!r}", file=sys.stderr)
        return False
    if report.get("dataset_row_count") != 9:
        print(f"dataset metadata was not refreshed: {report!r}", file=sys.stderr)
        return False
    metadata = load_json(Path(run["sweep_metadata_path"]))
    if metadata.get("dataset_row_count") != 9 or metadata.get("dataset_checksum") != report.get(
        "dataset_checksum"
    ):
        print(f"run metadata does not match refreshed dataset: {metadata!r}", file=sys.stderr)
        return False
    return True


def check_source_report(args: argparse.Namespace, dataset: Path, root: Path) -> bool:
    output_dir = root / "source-report"
    source_run_dir = root / "source-run"
    source_run_dir.mkdir()
    source_dataset = source_run_dir / "pattern-dataset.tsv"
    source_dataset.write_text(dataset.read_text(encoding="utf-8"), encoding="utf-8")
    source_report = source_run_dir / "local-training-run-report.json"
    source_report.write_text(
        json.dumps(
            {
                "run_id": "synthetic-connected-100k",
                "measurement_split_policy": "connected-board-game",
                "dataset_output_format": "compact-tsv",
                "sample_counts_by_split": {"train": 4, "validation": 2, "test": 2},
                "sample_counts_by_phase": {"1": 1},
                "dataset_example_rows": 8,
                "dataset_feature_occurrence_count": 16,
                "pattern_set_id": "fixed-pattern-fixture-v1",
                "source_kind": "synthetic-local",
                "trainer_mode": "pattern-sgd-v0c",
                "output_files": {"pattern_dataset_tsv": "pattern-dataset.tsv"},
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    command = [
        sys.executable,
        str(args.sweep_runner),
        "--output-dir",
        str(output_dir),
        "--trainer",
        str(args.trainer),
        "--created-at-utc",
        CREATED_AT,
        "--run-prefix",
        RUN_PREFIX,
        "--source-local-run-report",
        str(source_report),
        "--dry-run",
        "--max-configs",
        "1",
    ]
    result = run_capture(command)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return False
    report = load_json(output_dir / "sweep-report.json")
    if report.get("dataset_path") != str(source_dataset):
        print(f"source report did not resolve dataset: {report.get('dataset_path')!r}", file=sys.stderr)
        return False
    summary = report.get("source_run_summary")
    if not isinstance(summary, dict) or summary.get("source_run_id") != "synthetic-connected-100k":
        print(f"missing source summary: {summary!r}", file=sys.stderr)
        return False
    if report["runs"][0].get("warnings") != []:
        print(f"connected source report should not warn: {report['runs'][0]!r}", file=sys.stderr)
        return False
    return True


def check_failure(args: argparse.Namespace, root: Path) -> bool:
    output_dir = root / "failure"
    result = run_capture(base_command(args, root / "missing.tsv", output_dir))
    if result.returncode == 0:
        print("missing dataset unexpectedly succeeded", file=sys.stderr)
        return False
    if "dataset does not exist" not in result.stderr:
        print(f"missing dataset error was not helpful: {result.stderr!r}", file=sys.stderr)
        return False
    return True


def check_keep_going(args: argparse.Namespace, dataset: Path, root: Path) -> bool:
    output_dir = root / "keep-going"
    bad_trainer = root / "bad-trainer.py"
    bad_trainer.write_text(
        "import sys\n"
        "print('intentional trainer failure', file=sys.stderr)\n"
        "raise SystemExit(9)\n",
        encoding="utf-8",
    )
    command = [
        sys.executable,
        str(args.sweep_runner),
        "--dataset",
        str(dataset),
        "--output-dir",
        str(output_dir),
        "--trainer",
        str(bad_trainer),
        "--created-at-utc",
        CREATED_AT,
        "--run-prefix",
        RUN_PREFIX,
        "--max-configs",
        "2",
        "--keep-going",
    ]
    result = run_capture(command)
    if result.returncode == 0:
        print("failing keep-going fixture unexpectedly succeeded", file=sys.stderr)
        return False
    report = load_json(output_dir / "sweep-report.json")
    statuses = [run.get("status") for run in report.get("runs", [])]
    if statuses != ["failed", "failed"]:
        print(f"keep-going did not continue across failures: {statuses!r}", file=sys.stderr)
        return False
    if any("failed_run" not in run.get("warnings", []) for run in report.get("runs", [])):
        print(f"failed runs did not include failed_run warning: {report.get('runs')!r}", file=sys.stderr)
        return False
    return True


def check_docs_privacy_guard(args: argparse.Namespace) -> bool:
    repo_root = args.sweep_runner.resolve().parents[3]
    docs = (
        repo_root / "docs/progress/pattern-learning.md",
        repo_root / "docs/experiments/pattern-learning-early-diagnostics.md",
    )
    blocked = ("/Users/", "/home/", "~/Project/")
    for path in docs:
        text = path.read_text(encoding="utf-8")
        for needle in blocked:
            if needle in text:
                print(f"{path} contains personal local path marker {needle!r}", file=sys.stderr)
                return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sweep-runner", required=True, type=Path)
    parser.add_argument("--trainer", required=True, type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as temp_dir_name:
        root = Path(temp_dir_name)
        dataset = root / "pattern-dataset.tsv"
        dataset.write_text(COMPACT_DATASET, encoding="utf-8")
        checks = (
            check_dry_run,
            check_v0d_dry_run,
            check_execute,
            check_resume,
            check_resume_reruns_on_dataset_change,
            check_source_report,
        )
        for check in checks:
            if not check(args, dataset, root):
                return 1
        if not check_failure(args, root):
            return 1
        if not check_keep_going(args, dataset, root):
            return 1
        if not check_docs_privacy_guard(args):
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
