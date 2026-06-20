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
import csv


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


def run_runner(
    args: argparse.Namespace,
    output_dir: Path,
    *,
    cache_dir: Path | None = None,
    sequence_fixture: Path | None = None,
    extra_args: list[str] | None = None,
) -> tuple[dict[str, str], dict[str, Any], str] | None:
    command = [
        sys.executable,
        str(args.runner),
        "--sequence-input",
        str(sequence_fixture or args.sequence_fixture),
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
        "--sequence-sampling-mode",
        "bounded-dev",
        "--sequence-file-order",
        "hash",
        "--sequence-max-files",
        "1",
        "--sequence-max-games",
        "4",
        "--sequence-game-sample-rate",
        "1.0",
        "--sequence-progress-every-games",
        "2",
        "--sequence-progress-every-files",
        "1",
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
    if cache_dir is not None:
        command.extend(["--sequence-cache-dir", str(cache_dir)])
    command.extend(extra_args or [])
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
    return summary, load_json(output_dir / "local-training-run-report.json"), result.stderr


def check_report(
    report: dict[str, Any],
    output_dir: Path,
    runner_stderr: str,
    expected_cache_status: str,
) -> bool:
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
    sequence_policy = report.get("sequence_import_policy")
    expected_sequence_policy = {
        "sampling_mode": "bounded-dev",
        "file_order": "hash",
        "max_files": 1,
        "max_games": 4,
        "file_sample_rate": None,
        "game_sample_rate": 1.0,
        "source_files_seen": 1,
        "source_files_processed": 1,
        "games_seen": 4,
        "games_replayed": 4,
        "replay_skip_count": 0,
        "sampling_frame_notes": [
            "bounded-dev mode is for local measurement iteration",
            "bounded-dev is not a full-corpus exact top-k sample",
            "bounded-dev is not a production strength claim",
        ],
    }
    if sequence_policy != expected_sequence_policy:
        print(f"unexpected sequence import policy: {sequence_policy!r}", file=sys.stderr)
        return False
    cache = report.get("sequence_cache")
    if not isinstance(cache, dict) or cache.get("status") != expected_cache_status:
        print(f"unexpected sequence cache summary: {cache!r}", file=sys.stderr)
        return False
    if expected_cache_status in {"miss", "hit"} and not cache.get("cache_key"):
        print(f"missing cache key: {cache!r}", file=sys.stderr)
        return False
    stages = report.get("stage_timings")
    if not isinstance(stages, dict):
        print("missing stage timings", file=sys.stderr)
        return False
    for stage_name in (
        "sequence_import_or_cache_restore",
        "normalized_sampling",
        "pattern_dataset_generation",
        "trainer_v0b",
        "v0b_export",
        "evaluation_smoke",
        "search_smoke",
    ):
        stage = stages.get(stage_name)
        if not isinstance(stage, dict) or not isinstance(stage.get("wall_time_sec"), (int, float)):
            print(f"missing stage telemetry for {stage_name}: {stage!r}", file=sys.stderr)
            return False
    fingerprints = report.get("source_fingerprints")
    if not isinstance(fingerprints, dict) or not fingerprints.get("aggregate_input_sha256"):
        print(f"missing source fingerprints: {fingerprints!r}", file=sys.stderr)
        return False
    sample_counts = report.get("sample_counts_by_split")
    if not isinstance(sample_counts, dict) or sum(sample_counts.values()) < 80:
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
    if policy.get("search", {}).get("method") != "deterministic record_id sha256 top-k":
        print(f"unexpected search smoke policy: {policy!r}", file=sys.stderr)
        return False
    output_files = report.get("output_files")
    required = {
        "sequence_import_report_json",
        "sequence_import_stderr_log",
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
    dataset_report = load_json(output_dir / output_files["dataset_report_json"])
    if dataset_report.get("split_policy") != "importer-preserved: dataset_id + game_group_id sha256":
        print(f"unexpected dataset split policy: {dataset_report.get('split_policy')!r}", file=sys.stderr)
        return False
    stderr_log = (output_dir / output_files["sequence_import_stderr_log"]).read_text(
        encoding="utf-8"
    )
    if expected_cache_status == "hit":
        if "sequence cache hit" not in runner_stderr and "sequence cache hit" not in stderr_log:
            print("cache hit was not recorded in stderr log", file=sys.stderr)
            return False
    elif "progress elapsed_sec=" not in runner_stderr:
        print(f"runner did not stream sequence importer progress: {runner_stderr!r}", file=sys.stderr)
        return False
    if expected_cache_status != "hit" and "progress elapsed_sec=" not in stderr_log:
        print(f"sequence importer stderr log is missing progress: {stderr_log!r}", file=sys.stderr)
        return False
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
        "sample_policy": "deterministic position_id group sha256 top-k when max_positions is set; duplicate records for selected position_id are preserved",
        "seed": 7,
    }
    if emit_policy != expected_emit_policy:
        print(f"unexpected sequence emit policy: {emit_policy!r}", file=sys.stderr)
        return False
    if sequence_report.get("emitted_positions") < 80:
        print(f"sequence import cap did not bound emitted positions: {sequence_report!r}", file=sys.stderr)
        return False
    normalized = output_dir / output_files["normalized_tsv"]
    with normalized.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    if len({row["position_id"] for row in rows}) > 80:
        print("sequence import cap did not bound selected position_id groups", file=sys.stderr)
        return False
    sampling_policy = sequence_report.get("sampling_policy")
    if not isinstance(sampling_policy, dict) or sampling_policy.get("mode") != "bounded-dev":
        print(f"unexpected sequence sampling policy: {sampling_policy!r}", file=sys.stderr)
        return False
    for key, value in expected_sequence_policy.items():
        if sequence_report.get(key) != value:
            print(f"sequence report sampling mismatch for {key}: {sequence_report.get(key)!r}", file=sys.stderr)
            return False
    if sequence_report.get("rejected_games") != 1 or sequence_report.get("pass_count", 0) < 3:
        print(f"sequence importer did not report expected reject/pass counts: {sequence_report!r}", file=sys.stderr)
        return False
    return True


def stable_report_projection(report: dict[str, Any]) -> dict[str, Any]:
    return {
        "artifact_checksum": report.get("artifact_checksum"),
        "dataset_report_checksum": report.get("dataset_report_checksum"),
        "sample_report_checksum": report.get("sample_report_checksum"),
        "sample_counts_by_split": report.get("sample_counts_by_split"),
        "sample_counts_by_phase": report.get("sample_counts_by_phase"),
        "trainer_report_checksum": report.get("trainer_report_checksum"),
        "weights_checksum": report.get("weights_checksum"),
    }


def check_cache_path_independence(args: argparse.Namespace, temp_dir: Path, cache_dir: Path) -> bool:
    first_output = temp_dir / "path-first"
    second_output = temp_dir / "path-second"
    fresh_output = temp_dir / "path-fresh"
    copied_dir = temp_dir / "copied-input"
    copied_dir.mkdir()
    copied_fixture = copied_dir / args.sequence_fixture.name
    copied_fixture.write_text(args.sequence_fixture.read_text(encoding="utf-8"), encoding="utf-8")
    first = run_runner(
        args,
        first_output,
        cache_dir=cache_dir,
        sequence_fixture=args.sequence_fixture,
        extra_args=["--skip-eval-smoke", "--skip-search-smoke", "--skip-v0a-baseline"],
    )
    second = run_runner(
        args,
        second_output,
        cache_dir=cache_dir,
        sequence_fixture=copied_fixture,
        extra_args=["--skip-eval-smoke", "--skip-search-smoke", "--skip-v0a-baseline"],
    )
    fresh = run_runner(
        args,
        fresh_output,
        sequence_fixture=copied_fixture,
        extra_args=["--skip-eval-smoke", "--skip-search-smoke", "--skip-v0a-baseline"],
    )
    if first is None or second is None or fresh is None:
        return False
    first_key = first[1].get("sequence_cache", {}).get("cache_key")
    second_key = second[1].get("sequence_cache", {}).get("cache_key")
    if first_key != second_key:
        print(f"cache key changed across local paths: {first_key!r} != {second_key!r}", file=sys.stderr)
        return False
    second_files = second[1].get("output_files")
    fresh_files = fresh[1].get("output_files")
    if not isinstance(second_files, dict) or not isinstance(fresh_files, dict):
        print("missing output files for path independence check", file=sys.stderr)
        return False
    cached_normalized = (second_output / second_files["normalized_tsv"]).read_text(encoding="utf-8")
    fresh_normalized = (fresh_output / fresh_files["normalized_tsv"]).read_text(encoding="utf-8")
    if cached_normalized != fresh_normalized:
        print("cache hit normalized TSV differs from fresh import under another path", file=sys.stderr)
        return False
    cached_report = (second_output / second_files["sequence_import_report_json"]).read_text(encoding="utf-8")
    fresh_report = (fresh_output / fresh_files["sequence_import_report_json"]).read_text(encoding="utf-8")
    if cached_report != fresh_report:
        print("cache hit import report differs from fresh import under another path", file=sys.stderr)
        return False
    return True


def check_corrupt_cache_rebuild(args: argparse.Namespace, temp_dir: Path) -> bool:
    cache_dir = temp_dir / "corrupt-cache"
    first = run_runner(
        args,
        temp_dir / "corrupt-first",
        cache_dir=cache_dir,
        extra_args=["--skip-eval-smoke", "--skip-search-smoke", "--skip-v0a-baseline"],
    )
    if first is None:
        return False
    cache = first[1].get("sequence_cache")
    if not isinstance(cache, dict) or cache.get("status") != "miss":
        print(f"corrupt setup did not populate cache: {cache!r}", file=sys.stderr)
        return False
    key = cache.get("cache_key")
    if not isinstance(key, str):
        print(f"missing cache key for corrupt setup: {cache!r}", file=sys.stderr)
        return False
    cached_normalized = cache_dir / key / "sequence-normalized.tsv"
    cached_normalized.write_text("corrupt\n", encoding="utf-8")
    second = run_runner(
        args,
        temp_dir / "corrupt-second",
        cache_dir=cache_dir,
        extra_args=["--skip-eval-smoke", "--skip-search-smoke", "--skip-v0a-baseline"],
    )
    if second is None:
        return False
    second_cache = second[1].get("sequence_cache")
    if not isinstance(second_cache, dict) or second_cache.get("status") != "invalidated":
        print(f"corrupt cache was not invalidated: {second_cache!r}", file=sys.stderr)
        return False
    return True


def check_metadata_mismatch_rebuild(args: argparse.Namespace, temp_dir: Path) -> bool:
    cache_dir = temp_dir / "metadata-cache"
    first = run_runner(
        args,
        temp_dir / "metadata-first",
        cache_dir=cache_dir,
        extra_args=["--skip-eval-smoke", "--skip-search-smoke", "--skip-v0a-baseline"],
    )
    if first is None:
        return False
    cache = first[1].get("sequence_cache")
    if not isinstance(cache, dict) or cache.get("status") != "miss":
        print(f"metadata setup did not populate cache: {cache!r}", file=sys.stderr)
        return False
    key = cache.get("cache_key")
    if not isinstance(key, str):
        print(f"missing cache key for metadata setup: {cache!r}", file=sys.stderr)
        return False
    metadata_path = cache_dir / key / "metadata.json"
    metadata = load_json(metadata_path)
    metadata["cache_key"] = "wrong-key"
    metadata_path.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    second = run_runner(
        args,
        temp_dir / "metadata-second",
        cache_dir=cache_dir,
        extra_args=["--skip-eval-smoke", "--skip-search-smoke", "--skip-v0a-baseline"],
    )
    if second is None:
        return False
    second_cache = second[1].get("sequence_cache")
    if not isinstance(second_cache, dict) or second_cache.get("status") != "invalidated":
        print(f"metadata mismatch was not invalidated: {second_cache!r}", file=sys.stderr)
        return False
    return True


def check_option_invalidation(args: argparse.Namespace, temp_dir: Path) -> bool:
    cache_dir = temp_dir / "option-cache"
    base = run_runner(
        args,
        temp_dir / "option-base",
        cache_dir=cache_dir,
        extra_args=["--skip-eval-smoke", "--skip-search-smoke", "--skip-v0a-baseline"],
    )
    changed = run_runner(
        args,
        temp_dir / "option-changed",
        cache_dir=cache_dir,
        extra_args=[
            "--skip-eval-smoke",
            "--skip-search-smoke",
            "--skip-v0a-baseline",
            "--sequence-max-ply",
            "56",
        ],
    )
    if base is None or changed is None:
        return False
    base_key = base[1].get("sequence_cache", {}).get("cache_key")
    changed_key = changed[1].get("sequence_cache", {}).get("cache_key")
    if base_key == changed_key:
        print("cache key did not change when sequence max ply changed", file=sys.stderr)
        return False
    if changed[1].get("sequence_cache", {}).get("status") != "miss":
        print(f"changed semantic option did not miss cache: {changed[1].get('sequence_cache')!r}", file=sys.stderr)
        return False
    return True


def check_cache_atomic_failure(args: argparse.Namespace, temp_dir: Path) -> bool:
    invalid_input = temp_dir / "invalid-sequence.txt"
    invalid_input.write_text("a1\n", encoding="utf-8")
    cache_dir = temp_dir / "failure-cache"
    result = subprocess.run(
        [
            sys.executable,
            str(args.runner),
            "--sequence-input",
            str(invalid_input),
            "--sequence-manifest",
            str(args.sequence_manifest),
            "--output-dir",
            str(temp_dir / "failure-output"),
            "--run-id",
            "failure-sequence-runner-smoke",
            "--created-at-utc",
            "2026-01-02T03:04:05Z",
            "--max-examples",
            "10",
            "--max-per-phase",
            "10",
            "--epochs",
            "1",
            "--seed",
            "7",
            "--sequence-cache-dir",
            str(cache_dir),
            "--sequence-min-ply",
            "4",
            "--sequence-sampling-mode",
            "bounded-dev",
            "--sequence-max-games",
            "1",
            "--sequence-file-order",
            "hash",
            "--sequence-no-emit-terminal",
            "--skip-eval-smoke",
            "--skip-search-smoke",
            "--skip-v0a-baseline",
            "--dataset-exe",
            str(args.dataset_exe),
            "--eval-smoke-exe",
            str(args.eval_smoke_exe),
            "--search-smoke-exe",
            str(args.search_smoke_exe),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode == 0:
        print("invalid sequence cache run unexpectedly succeeded", file=sys.stderr)
        return False
    if cache_dir.exists() and list(cache_dir.rglob("metadata.json")):
        print("failed import left a valid cache metadata marker", file=sys.stderr)
        return False
    return True


def check_strict_board_leakage_failure(args: argparse.Namespace, output_dir: Path) -> bool:
    report = load_json(output_dir / "local-training-run-report.json")
    output_files = report.get("output_files")
    if not isinstance(output_files, dict) or "sampled_normalized_tsv" not in output_files:
        print("missing sampled normalized TSV for strict leakage test", file=sys.stderr)
        return False
    sampled = output_dir / output_files["sampled_normalized_tsv"]
    with sampled.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        rows = list(reader)
        fieldnames = reader.fieldnames
    if fieldnames is None or "board_id" not in fieldnames or len(rows) < 2:
        print("strict leakage test needs v2 sampled rows", file=sys.stderr)
        return False
    rows[1]["board_id"] = rows[0]["board_id"]
    rows[1]["split"] = "test" if rows[0]["split"] != "test" else "train"
    rows[1]["position_id"] = rows[1]["position_id"] + "-collision"

    collision_tsv = output_dir / "synthetic-board-leakage.tsv"
    with collision_tsv.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, delimiter="\t", fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows[:2])

    strict_output = output_dir / "strict-failure"
    result = subprocess.run(
        [
            sys.executable,
            str(args.runner),
            "--normalized-tsv",
            str(collision_tsv),
            "--output-dir",
            str(strict_output),
            "--run-id",
            "strict-board-leakage-smoke",
            "--created-at-utc",
            "2026-01-02T03:04:05Z",
            "--strict-board-disjoint-splits",
            "--dataset-exe",
            str(args.dataset_exe),
            "--eval-smoke-exe",
            str(args.eval_smoke_exe),
            "--search-smoke-exe",
            str(args.search_smoke_exe),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode == 0:
        print("strict board leakage run unexpectedly succeeded", file=sys.stderr)
        return False
    if "exact board leakage detected across sampled splits" not in result.stderr:
        print("strict board leakage failure did not explain collision", file=sys.stderr)
        sys.stderr.write(result.stderr)
        return False
    return True


def check_bounded_sequence_pass_through(args: argparse.Namespace, output_dir: Path) -> bool:
    bounded_output = output_dir / "bounded-sequence"
    command = [
        sys.executable,
        str(args.runner),
        "--sequence-input",
        str(args.sequence_fixture),
        "--sequence-manifest",
        str(args.sequence_manifest),
        "--output-dir",
        str(bounded_output),
        "--run-id",
        "bounded-sequence-runner-smoke",
        "--created-at-utc",
        "2026-01-02T03:04:05Z",
        "--max-examples",
        "100",
        "--max-per-phase",
        "100",
        "--epochs",
        "1",
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
        "--sequence-sampling-mode",
        "bounded-dev",
        "--sequence-max-games",
        "4",
        "--sequence-max-files",
        "1",
        "--sequence-game-sample-rate",
        "1.0",
        "--sequence-file-order",
        "hash",
        "--sequence-progress-every-games",
        "1",
        "--sequence-progress-every-files",
        "1",
        "--sequence-no-emit-terminal",
        "--skip-eval-smoke",
        "--skip-search-smoke",
        "--skip-v0a-baseline",
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
        return False
    report = load_json(bounded_output / "local-training-run-report.json")
    output_files = report.get("output_files")
    if not isinstance(output_files, dict) or "sequence_import_report_json" not in output_files:
        print("bounded runner report is missing sequence import report", file=sys.stderr)
        return False
    sequence_report = load_json(bounded_output / output_files["sequence_import_report_json"])
    expected = {
        "sampling_mode": "bounded-dev",
        "file_order": "hash",
        "max_files": 1,
        "max_games": 4,
        "game_sample_rate": 1.0,
        "games_replayed": 4,
    }
    for key, value in expected.items():
        if sequence_report.get(key) != value:
            print(f"bounded sequence report mismatch for {key}: {sequence_report.get(key)!r}", file=sys.stderr)
            return False
    stderr_log = bounded_output / "sequence-import-stderr.log"
    if "progress elapsed_sec=" not in stderr_log.read_text(encoding="utf-8"):
        print("bounded sequence progress was not streamed to stderr log", file=sys.stderr)
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
        cache_dir = temp_dir / "sequence-cache"
        first = run_runner(args, temp_dir / "first", cache_dir=cache_dir)
        second = run_runner(args, temp_dir / "second", cache_dir=cache_dir)
        if first is None or second is None:
            return 1
        first_summary, first_report, first_stderr = first
        second_summary, second_report, _second_stderr = second
        if first_summary.get("trainer_report_checksum") != second_summary.get(
            "trainer_report_checksum"
        ):
            print("trainer checksum is not deterministic", file=sys.stderr)
            return 1
        if first_summary.get("artifact_checksum") != second_summary.get("artifact_checksum"):
            print("artifact checksum is not deterministic", file=sys.stderr)
            return 1
        if stable_report_projection(first_report) != stable_report_projection(second_report):
            print("local sequence run stable report fields are not deterministic", file=sys.stderr)
            return 1
        if not check_report(first_report, temp_dir / "first", first_stderr, "miss"):
            return 1
        if not check_report(second_report, temp_dir / "second", _second_stderr, "hit"):
            return 1
        if not check_strict_board_leakage_failure(args, temp_dir / "first"):
            return 1
        if not check_bounded_sequence_pass_through(args, temp_dir):
            return 1
        if not check_cache_path_independence(args, temp_dir, temp_dir / "path-cache"):
            return 1
        if not check_corrupt_cache_rebuild(args, temp_dir):
            return 1
        if not check_metadata_mismatch_rebuild(args, temp_dir):
            return 1
        if not check_option_invalidation(args, temp_dir):
            return 1
        if not check_cache_atomic_failure(args, temp_dir):
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
