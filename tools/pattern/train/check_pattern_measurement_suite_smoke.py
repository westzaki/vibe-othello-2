#!/usr/bin/env python3
"""CTest wrapper for the local pattern measurement suite runner."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


CREATED_AT = "2026-01-02T03:04:05Z"
RUN_PREFIX = "suite-smoke"
LOCAL_ENV_NAMES = (
    "VIBE_OTHELLO_LOCAL",
    "VIBE_OTHELLO_CORPORA",
    "VIBE_OTHELLO_SEQUENCE_CACHE",
    "VIBE_OTHELLO_MEASUREMENTS",
)


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def clean_env(extra: dict[str, str] | None = None) -> dict[str, str]:
    env = os.environ.copy()
    for name in LOCAL_ENV_NAMES:
        env.pop(name, None)
    if extra is not None:
        env.update(extra)
    return env


def run_capture(command: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, capture_output=True, text=True, env=env)


def base_command(
    args: argparse.Namespace,
    output_dir: Path | None,
    cache_dir: Path | None,
) -> list[str]:
    command = [
        sys.executable,
        str(args.suite_runner),
        "--sequence-input",
        str(args.sequence_fixture),
        "--sequence-manifest",
        str(args.sequence_manifest),
        "--runner",
        str(args.runner),
        "--analyzer",
        str(args.analyzer),
        "--dataset-exe",
        str(args.dataset_exe),
        "--eval-smoke-exe",
        str(args.eval_smoke_exe),
        "--search-smoke-exe",
        str(args.search_smoke_exe),
        "--created-at-utc",
        CREATED_AT,
        "--run-prefix",
        RUN_PREFIX,
        "--seed",
        "7",
    ]
    if output_dir is not None:
        command.extend(["--suite-output-dir", str(output_dir)])
    if cache_dir is not None:
        command.extend(["--sequence-cache-dir", str(cache_dir)])
    return command


def run_suite(
    args: argparse.Namespace,
    output_dir: Path,
    cache_dir: Path,
    extra_args: list[str],
) -> subprocess.CompletedProcess[str]:
    command = base_command(args, output_dir, cache_dir)
    command.extend(extra_args)
    result = run_capture(command, env=clean_env())
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
    return result


def command_contains(command: list[str], *needles: str) -> bool:
    return all(needle in command for needle in needles)


def command_value(command: list[str], flag: str) -> str | None:
    try:
        return command[command.index(flag) + 1]
    except (ValueError, IndexError):
        return None


def check_dry_run(args: argparse.Namespace, root: Path) -> bool:
    output_dir = root / "dry-run"
    result = run_suite(args, output_dir, root / "dry-cache", ["--preset", "smoke", "--dry-run"])
    if result.returncode != 0:
        return False
    report = load_json(output_dir / "suite-report.json")
    runs = report.get("runs")
    if not isinstance(runs, list) or len(runs) != 1:
        print(f"unexpected dry-run runs: {runs!r}", file=sys.stderr)
        return False
    run = runs[0]
    if run.get("status") != "planned":
        print(f"dry-run status mismatch: {run!r}", file=sys.stderr)
        return False
    command = run.get("command")
    if not isinstance(command, list) or not command_contains(
        command,
        "--dataset-output-format",
        "compact-tsv",
        "--trainer-mode",
        "pattern-sgd-v0c",
        "--sequence-sampling-mode",
        "bounded-dev",
        "--sequence-file-order",
        "hash",
        "--sequence-progress-every-games",
        "1",
        "--trainer-eval-every-epoch",
        "--trainer-progress-every-examples",
        "10",
    ):
        print(f"dry-run command does not include compact v0c defaults: {command!r}", file=sys.stderr)
        return False
    if Path(run["local_training_report"]).exists():
        print("dry-run unexpectedly created a local training report", file=sys.stderr)
        return False
    if not (output_dir / "suite-summary.md").exists():
        print("dry-run did not write suite-summary.md", file=sys.stderr)
        return False
    return True


def check_measurement_split_policy_dry_run(args: argparse.Namespace, root: Path) -> bool:
    output_dir = root / "connected-split-dry-run"
    result = run_suite(
        args,
        output_dir,
        root / "connected-split-cache",
        [
            "--preset",
            "smoke",
            "--measurement-split-policy",
            "connected-board-game",
            "--dry-run",
        ],
    )
    if result.returncode != 0:
        return False
    report = load_json(output_dir / "suite-report.json")
    if report.get("measurement_split_policy") != "connected-board-game":
        print(f"suite report missing measurement split policy: {report!r}", file=sys.stderr)
        return False
    runs = report.get("runs")
    if not isinstance(runs, list) or len(runs) != 1:
        print(f"unexpected connected dry-run runs: {runs!r}", file=sys.stderr)
        return False
    command = runs[0].get("command")
    if not isinstance(command, list) or not command_contains(
        command,
        "--measurement-split-policy",
        "connected-board-game",
    ):
        print(f"dry-run command did not include measurement split flag: {command!r}", file=sys.stderr)
        return False
    if runs[0].get("measurement_split_policy") != "connected-board-game":
        print(f"run entry missing measurement split policy: {runs[0]!r}", file=sys.stderr)
        return False
    summary = (output_dir / "suite-summary.md").read_text(encoding="utf-8")
    if "measurement_split_policy" not in summary or "connected-board-game" not in summary:
        print("suite summary is missing measurement split policy", file=sys.stderr)
        return False
    return True


def check_teacher_label_dry_run(args: argparse.Namespace, root: Path) -> bool:
    output_dir = root / "teacher-dry-run"
    teacher_labels = root / "synthetic-teacher-labels.tsv"
    result = run_suite(
        args,
        output_dir,
        root / "teacher-cache",
        [
            "--preset",
            "smoke",
            "--teacher-labels",
            str(teacher_labels),
            "--teacher-label-missing-policy",
            "keep-observed",
            "--teacher-label-conflict-policy",
            "fail",
            "--dry-run",
        ],
    )
    if result.returncode != 0:
        return False
    report = load_json(output_dir / "suite-report.json")
    if report.get("teacher_labels_enabled") is not True:
        print(f"suite report missing teacher label flag: {report!r}", file=sys.stderr)
        return False
    if report.get("teacher_label_missing_policy") != "keep-observed":
        print(f"suite report missing teacher missing policy: {report!r}", file=sys.stderr)
        return False
    runs = report.get("runs")
    if not isinstance(runs, list) or len(runs) != 1:
        print(f"unexpected teacher dry-run runs: {runs!r}", file=sys.stderr)
        return False
    command = runs[0].get("command")
    if not isinstance(command, list) or not command_contains(
        command,
        "--teacher-labels",
        str(teacher_labels),
        "--teacher-label-missing-policy",
        "keep-observed",
        "--teacher-label-conflict-policy",
        "fail",
    ):
        print(f"dry-run command did not include teacher label flags: {command!r}", file=sys.stderr)
        return False
    if runs[0].get("teacher_labels_enabled") is not True:
        print(f"run entry missing teacher label flag: {runs[0]!r}", file=sys.stderr)
        return False
    return True


def check_local_root_env_defaults(args: argparse.Namespace, root: Path) -> bool:
    local_root = root / "local-root"
    output_dir = local_root / "measurements" / RUN_PREFIX
    cache_dir = local_root / "sequence-cache"
    command = base_command(args, None, None)
    command.extend(["--preset", "smoke", "--dry-run"])
    result = run_capture(
        command,
        env=clean_env({"VIBE_OTHELLO_LOCAL": str(local_root)}),
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return False
    report = load_json(output_dir / "suite-report.json")
    if report.get("suite_output_dir") != str(output_dir):
        print(f"VIBE_OTHELLO_LOCAL output mismatch: {report.get('suite_output_dir')!r}", file=sys.stderr)
        return False
    if report.get("sequence_cache_dir") != str(cache_dir):
        print(f"VIBE_OTHELLO_LOCAL cache mismatch: {report.get('sequence_cache_dir')!r}", file=sys.stderr)
        return False
    run = report["runs"][0]
    resolved_cache = command_value(run.get("command") or [], "--sequence-cache-dir")
    if resolved_cache != str(cache_dir):
        print(f"planned command did not use local-root cache: {run.get('command')!r}", file=sys.stderr)
        return False
    return True


def check_specific_env_overrides_local_root(args: argparse.Namespace, root: Path) -> bool:
    local_root = root / "local-root-unused"
    measurements_root = root / "specific-measurements"
    output_dir = measurements_root / RUN_PREFIX
    cache_dir = root / "specific-cache"
    command = base_command(args, None, None)
    command.extend(["--preset", "smoke", "--dry-run"])
    result = run_capture(
        command,
        env=clean_env(
            {
                "VIBE_OTHELLO_LOCAL": str(local_root),
                "VIBE_OTHELLO_SEQUENCE_CACHE": str(cache_dir),
                "VIBE_OTHELLO_MEASUREMENTS": str(measurements_root),
            }
        ),
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return False
    report = load_json(output_dir / "suite-report.json")
    if report.get("suite_output_dir") != str(output_dir):
        print(f"specific measurements env mismatch: {report.get('suite_output_dir')!r}", file=sys.stderr)
        return False
    if report.get("sequence_cache_dir") != str(cache_dir):
        print(f"specific cache env mismatch: {report.get('sequence_cache_dir')!r}", file=sys.stderr)
        return False
    return True


def check_cli_overrides_env(args: argparse.Namespace, root: Path) -> bool:
    output_dir = root / "cli-output"
    cache_dir = root / "cli-cache"
    command = base_command(args, output_dir, cache_dir)
    command.extend(["--preset", "smoke", "--dry-run"])
    result = run_capture(
        command,
        env=clean_env(
            {
                "VIBE_OTHELLO_LOCAL": str(root / "env-local"),
                "VIBE_OTHELLO_SEQUENCE_CACHE": str(root / "env-cache"),
                "VIBE_OTHELLO_MEASUREMENTS": str(root / "env-measurements"),
            }
        ),
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return False
    report = load_json(output_dir / "suite-report.json")
    if report.get("suite_output_dir") != str(output_dir):
        print(f"CLI suite output did not override env: {report.get('suite_output_dir')!r}", file=sys.stderr)
        return False
    if report.get("sequence_cache_dir") != str(cache_dir):
        print(f"CLI sequence cache did not override env: {report.get('sequence_cache_dir')!r}", file=sys.stderr)
        return False
    return True


def check_missing_output_location(args: argparse.Namespace) -> bool:
    command = base_command(args, None, None)
    command.extend(["--preset", "smoke", "--dry-run"])
    result = run_capture(command, env=clean_env())
    if result.returncode == 0:
        print("missing output location unexpectedly succeeded", file=sys.stderr)
        return False
    expected = "either --suite-output-dir, VIBE_OTHELLO_MEASUREMENTS, or VIBE_OTHELLO_LOCAL is required"
    if expected not in result.stderr:
        print(f"missing output error was not helpful: {result.stderr!r}", file=sys.stderr)
        return False
    return True


def check_execute(args: argparse.Namespace, root: Path) -> bool:
    output_dir = root / "execute"
    cache_dir = root / "execute-cache"
    result = run_suite(args, output_dir, cache_dir, ["--preset", "smoke"])
    if result.returncode != 0:
        return False
    suite = load_json(output_dir / "suite-report.json")
    runs = suite.get("runs")
    if not isinstance(runs, list) or len(runs) != 1:
        print(f"unexpected execute runs: {runs!r}", file=sys.stderr)
        return False
    run = runs[0]
    if run.get("status") != "success":
        print(f"execute run did not succeed: {run!r}", file=sys.stderr)
        return False
    local_report_path = Path(run["local_training_report"])
    if not local_report_path.exists():
        print(f"missing local training report: {local_report_path}", file=sys.stderr)
        return False
    local = load_json(local_report_path)
    expected_scalars = {
        "trainer_mode": "pattern-sgd-v0c",
        "trainer_version": "pattern-sgd-v0c",
        "dataset_output_format": "compact-tsv",
        "source_kind": "egaroucid-sequence-local",
    }
    for key, expected in expected_scalars.items():
        if local.get(key) != expected:
            print(f"local report mismatch for {key}: {local.get(key)!r}", file=sys.stderr)
            return False
    cache = local.get("sequence_cache")
    if not isinstance(cache, dict) or cache.get("status") not in {"miss", "hit"}:
        print(f"unexpected sequence cache status: {cache!r}", file=sys.stderr)
        return False
    stages = local.get("stage_timings")
    if not isinstance(stages, dict) or not isinstance(
        stages.get("pattern_dataset_generation", {}).get("wall_time_sec"), (int, float)
    ):
        print(f"missing stage telemetry: {stages!r}", file=sys.stderr)
        return False
    for path in ("suite-summary.md", "analyzer-summary.json", "analyzer-summary.md"):
        if not (output_dir / path).exists():
            print(f"missing suite output: {path}", file=sys.stderr)
            return False
    analyzer = load_json(output_dir / "analyzer-summary.json")
    if analyzer.get("run_count") != 1:
        print(f"analyzer did not include the run: {analyzer!r}", file=sys.stderr)
        return False
    if suite.get("analyzer", {}).get("status") != "success":
        print(f"suite did not record analyzer success: {suite.get('analyzer')!r}", file=sys.stderr)
        return False
    if run.get("sampled_rows") is None or run.get("warning_count") is None:
        print(f"suite run was not enriched from analyzer/local report: {run!r}", file=sys.stderr)
        return False
    return True


def check_resume(args: argparse.Namespace, root: Path) -> bool:
    output_dir = root / "resume"
    cache_dir = root / "resume-cache"
    first = run_suite(args, output_dir, cache_dir, ["--preset", "smoke"])
    if first.returncode != 0:
        return False
    second = run_suite(args, output_dir, cache_dir, ["--preset", "smoke", "--resume"])
    if second.returncode != 0:
        return False
    suite = load_json(output_dir / "suite-report.json")
    run = suite["runs"][0]
    if run.get("status") != "skipped":
        print(f"resume did not skip existing run: {run!r}", file=sys.stderr)
        return False
    analyzer = load_json(output_dir / "analyzer-summary.json")
    if analyzer.get("run_count") != 1:
        print(f"resume analyzer did not include skipped run report: {analyzer!r}", file=sys.stderr)
        return False
    return True


def check_failure(args: argparse.Namespace, root: Path) -> bool:
    output_dir = root / "failure"
    command = base_command(args, output_dir, root / "failure-cache")
    command[command.index("--sequence-input") + 1] = str(root / "does-not-exist.txt")
    command.extend(["--preset", "smoke"])
    result = run_capture(command, env=clean_env())
    if result.returncode == 0:
        print("failure fixture unexpectedly succeeded", file=sys.stderr)
        return False
    report_path = output_dir / "suite-report.json"
    if not report_path.exists():
        print("failure run did not write suite-report.json", file=sys.stderr)
        return False
    suite = load_json(report_path)
    run = suite["runs"][0]
    if run.get("status") != "failed" or run.get("artifact_checksum") is not None:
        print(f"failure suite report made an artifact claim: {run!r}", file=sys.stderr)
        return False
    notes = suite.get("notes")
    if "not strength claim" not in notes or "no Elo" not in notes:
        print(f"failure suite report is missing local-only notes: {notes!r}", file=sys.stderr)
        return False
    return True


def check_docs_privacy_guard(args: argparse.Namespace) -> bool:
    repo_root = args.suite_runner.resolve().parents[3]
    docs = (
        repo_root / "README.md",
        repo_root / "data/corpora/README.md",
        repo_root / "docs/progress/pattern-learning.md",
    )
    blocked = ("/Users/", "/home/", "~/Project/")
    for path in docs:
        text = path.read_text(encoding="utf-8")
        for needle in blocked:
            if needle in text:
                print(f"{path} contains personal local path marker {needle!r}", file=sys.stderr)
                return False
    return True


def check_all_dry_run(args: argparse.Namespace, root: Path) -> bool:
    output_dir = root / "all-dry-run"
    result = run_suite(args, output_dir, root / "all-cache", ["--preset", "all", "--dry-run"])
    if result.returncode != 0:
        return False
    suite = load_json(output_dir / "suite-report.json")
    names = [run.get("preset") for run in suite.get("runs", [])]
    if names != ["10k", "100k", "1m"]:
        print(f"--preset all expanded unexpectedly: {names!r}", file=sys.stderr)
        return False
    max_examples = []
    sampling_modes = []
    file_orders = []
    final_only = []
    trainer_progress = []
    for run in suite.get("runs", []):
        command = run.get("command") or []
        try:
            max_examples.append(command[command.index("--max-examples") + 1])
            sampling_modes.append(command[command.index("--sequence-sampling-mode") + 1])
            file_orders.append(command[command.index("--sequence-file-order") + 1])
            final_only.append("--trainer-no-eval-every-epoch" in command)
            trainer_progress.append(command[command.index("--trainer-progress-every-examples") + 1])
        except (ValueError, IndexError):
            print(f"missing scalable command option: {command!r}", file=sys.stderr)
            return False
    if max_examples != ["10000", "100000", "1000000"]:
        print(f"unexpected all dry-run sizes: {max_examples!r}", file=sys.stderr)
        return False
    if sampling_modes != ["streaming-target", "streaming-target", "streaming-target"]:
        print(f"unexpected all dry-run sampling modes: {sampling_modes!r}", file=sys.stderr)
        return False
    if file_orders != ["hash", "hash", "hash"]:
        print(f"unexpected all dry-run file orders: {file_orders!r}", file=sys.stderr)
        return False
    if final_only != [True, True, True]:
        print(f"unexpected all dry-run trainer diagnostics policy: {final_only!r}", file=sys.stderr)
        return False
    if trainer_progress != ["5000", "50000", "100000"]:
        print(f"unexpected all dry-run trainer progress cadence: {trainer_progress!r}", file=sys.stderr)
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite-runner", required=True, type=Path)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--analyzer", required=True, type=Path)
    parser.add_argument("--dataset-exe", required=True, type=Path)
    parser.add_argument("--eval-smoke-exe", required=True, type=Path)
    parser.add_argument("--search-smoke-exe", required=True, type=Path)
    parser.add_argument("--sequence-fixture", required=True, type=Path)
    parser.add_argument("--sequence-manifest", required=True, type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as temp_dir_name:
        root = Path(temp_dir_name)
        checks = (
            check_dry_run,
            check_measurement_split_policy_dry_run,
            check_teacher_label_dry_run,
            check_local_root_env_defaults,
            check_specific_env_overrides_local_root,
            check_cli_overrides_env,
            check_execute,
            check_resume,
            check_failure,
            check_all_dry_run,
        )
        for check in checks:
            if not check(args, root):
                return 1
        if not check_missing_output_location(args):
            return 1
        if not check_docs_privacy_guard(args):
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
