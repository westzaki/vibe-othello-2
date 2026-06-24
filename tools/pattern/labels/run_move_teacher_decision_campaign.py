#!/usr/bin/env python3
"""Run a bounded local-only move-teacher decision-leverage campaign."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--normalized-tsv", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--max-empty", type=int, default=12)
    parser.add_argument("--max-roots", type=int, default=5000)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--pattern-set", default="pattern-v2-endgame-lite")
    parser.add_argument(
        "--trainer-mode",
        choices=("pattern-sgd-v0c", "pattern-sgd-v0d"),
        default="pattern-sgd-v0c",
    )
    parser.add_argument("--epochs", type=int, default=8)
    parser.add_argument("--learning-rate", type=float, default=0.1)
    parser.add_argument("--lr-schedule", choices=("constant", "inverse-sqrt"), default="inverse-sqrt")
    parser.add_argument("--weight-decay", type=float, default=0.0001)
    parser.add_argument("--dataset-output-format", choices=("compact-tsv", "expanded-tsv"), default="compact-tsv")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--move-teacher-cache-dir", type=Path)
    parser.add_argument("--reuse-move-teacher-cache", action="store_true")
    parser.add_argument("--write-move-teacher-cache", action="store_true")
    parser.add_argument("--allow-cache-miss-solve", action="store_true")

    parser.add_argument("--previous-weights", type=Path)
    parser.add_argument("--previous-manifest", type=Path)
    parser.add_argument("--previous-pattern-set")
    parser.add_argument("--previous-name", default="previous-root-label-artifact")

    parser.add_argument("--arena-baseline-weights", type=Path)
    parser.add_argument("--arena-baseline-manifest", type=Path)
    parser.add_argument("--arena-baseline-name")
    parser.add_argument("--arena-depth", type=int, default=3)
    parser.add_argument("--arena-max-positions", type=int, default=1000)
    parser.add_argument("--arena-side-swap", action="store_true", default=True)
    parser.add_argument("--no-arena-side-swap", dest="arena_side_swap", action="store_false")

    parser.add_argument(
        "--generator",
        type=Path,
        default=root / "build/tools/pattern/labels/vibe-othello-generate-exact-move-teacher-dataset",
    )
    parser.add_argument(
        "--dataset-exe",
        type=Path,
        default=root / "build/tools/pattern/dataset/vibe-othello-pattern-dataset-smoke",
    )
    parser.add_argument("--trainer", type=Path, default=root / "tools/pattern/train/train_pattern.py")
    parser.add_argument("--exporter", type=Path, default=root / "tools/pattern/export/export_v0b.py")
    parser.add_argument(
        "--ranking-evaluator",
        type=Path,
        default=root / "build/tools/pattern/labels/vibe-othello-evaluate-move-teacher-ranking",
    )
    parser.add_argument(
        "--arena-exe",
        type=Path,
        default=root / "build/tools/arena/vibe-othello-pattern-artifact-arena",
    )
    parser.add_argument(
        "--move-teacher-cache-helper",
        type=Path,
        default=root / "tools/pattern/labels/materialize_move_teacher_from_cache.py",
    )
    args = parser.parse_args()

    if args.max_empty < 0 or args.max_empty > 64:
        parser.error("--max-empty must be in [0, 64]")
    if args.max_roots <= 0:
        parser.error("--max-roots must be positive")
    if args.seed < 0:
        parser.error("--seed must be non-negative")
    if args.epochs < 0:
        parser.error("--epochs must be non-negative")
    if args.learning_rate < 0.0:
        parser.error("--learning-rate must be non-negative")
    if args.weight_decay < 0.0:
        parser.error("--weight-decay must be non-negative")
    if (args.previous_weights is None) != (args.previous_manifest is None):
        parser.error("--previous-weights and --previous-manifest must be supplied together")
    if (args.arena_baseline_weights is None) != (args.arena_baseline_manifest is None):
        parser.error("--arena-baseline-weights and --arena-baseline-manifest must be supplied together")
    if (args.reuse_move_teacher_cache or args.write_move_teacher_cache or args.allow_cache_miss_solve) and (
        args.move_teacher_cache_dir is None
    ):
        parser.error("move-teacher cache flags require --move-teacher-cache-dir")
    if args.allow_cache_miss_solve and not args.reuse_move_teacher_cache:
        parser.error("--allow-cache-miss-solve requires --reuse-move-teacher-cache")
    if args.allow_cache_miss_solve and not args.write_move_teacher_cache:
        parser.error("--allow-cache-miss-solve requires --write-move-teacher-cache")
    if args.arena_depth <= 0:
        parser.error("--arena-depth must be positive")
    if args.arena_max_positions <= 0:
        parser.error("--arena-max-positions must be positive")
    return args


def stable_json(data: Any) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def report_path(path: Path | None) -> str | None:
    if path is None:
        return None
    return path.name if path.is_absolute() else str(path)


def command_for_report(command: list[str]) -> list[str]:
    return [Path(part).name if "/" in part else part for part in command]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            digest.update(chunk)
    return f"sha256:{digest.hexdigest()}"


def file_fingerprint(path: Path) -> dict[str, Any]:
    if not path.exists():
        raise RuntimeError(f"missing file for resume fingerprint: {path}")
    return {
        "path": report_path(path),
        "size_bytes": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def resume_expected_metadata(command: list[str], inputs: dict[str, Path]) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "command": command_for_report(command),
        "inputs": {name: file_fingerprint(path) for name, path in sorted(inputs.items())},
    }


def resume_complete_metadata(expected: dict[str, Any], outputs: list[Path]) -> dict[str, Any]:
    data = dict(expected)
    data["outputs"] = {
        f"output_{index}": file_fingerprint(path)
        for index, path in enumerate(outputs)
    }
    return data


def load_json(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise RuntimeError(f"JSON root must be an object: {path}")
    return data


def run_or_fail(command: list[str], stdout_path: Path | None = None) -> subprocess.CompletedProcess[str]:
    # Keep stdout capturable for dataset-producing stages, but stream stderr so
    # long exact-teacher runs expose their progress messages in real time.
    result = subprocess.run(command, check=False, stdout=subprocess.PIPE, text=True)
    if result.returncode != 0:
        if result.stdout:
            sys.stderr.write(result.stdout)
        raise RuntimeError(f"command failed: {' '.join(command)}")
    if stdout_path is not None:
        stdout_path.write_text(result.stdout, encoding="utf-8")
    return result


def first_metadata_mismatch(expected: Any, actual: Any, path: str = "$") -> str | None:
    if expected == actual:
        return None
    if isinstance(expected, dict) and isinstance(actual, dict):
        for key in sorted(set(expected) | set(actual)):
            if key not in expected:
                return f"{path}.{key} unexpected"
            if key not in actual:
                return f"{path}.{key} missing"
            mismatch = first_metadata_mismatch(expected[key], actual[key], f"{path}.{key}")
            if mismatch is not None:
                return mismatch
    if isinstance(expected, list) and isinstance(actual, list):
        if len(expected) != len(actual):
            return f"{path} length {len(actual)} != {len(expected)}"
        for index, (expected_item, actual_item) in enumerate(zip(expected, actual, strict=True)):
            mismatch = first_metadata_mismatch(expected_item, actual_item, f"{path}[{index}]")
            if mismatch is not None:
                return mismatch
    return f"{path} mismatch"


def validate_resume_metadata(
    name: str,
    metadata_path: Path,
    expected: dict[str, Any],
    outputs: list[Path],
) -> None:
    if not metadata_path.exists():
        raise RuntimeError(
            f"resume metadata missing for stage {name}: {metadata_path}; "
            "remove stale outputs or rerun without --resume"
        )
    actual = load_json(metadata_path)
    current = resume_complete_metadata(expected, outputs)
    mismatch = first_metadata_mismatch(current, actual)
    if mismatch is not None:
        raise RuntimeError(
            f"resume metadata mismatch for stage {name} at {mismatch}; "
            "remove stale outputs or rerun without --resume"
        )


def run_stage(
    args: argparse.Namespace,
    name: str,
    command: list[str],
    outputs: list[Path],
    stages: dict[str, dict[str, Any]],
    stdout_path: Path | None = None,
    resume_inputs: dict[str, Path] | None = None,
) -> None:
    metadata_path = args.output_dir / f"{name}.resume.json"
    expected_resume = resume_expected_metadata(command, resume_inputs or {})
    stages[name] = {
        "command": command_for_report(command),
        "outputs": [report_path(path) for path in outputs],
        "resume_metadata": report_path(metadata_path),
    }
    if args.resume and all(path.exists() for path in outputs):
        validate_resume_metadata(name, metadata_path, expected_resume, outputs)
        stages[name]["status"] = "skipped-resume-validated"
        return
    run_or_fail(command, stdout_path=stdout_path)
    metadata_path.write_text(
        stable_json(resume_complete_metadata(expected_resume, outputs)),
        encoding="utf-8",
    )
    stages[name]["status"] = "ok"


def cache_materialize_command(
    args: argparse.Namespace,
    move_teacher: Path,
    child_normalized: Path,
    report: Path,
) -> list[str]:
    return [
        sys.executable,
        str(args.move_teacher_cache_helper),
        "--normalized-tsv",
        str(args.normalized_tsv),
        "--cache-dir",
        str(args.move_teacher_cache_dir),
        "--move-teacher-out",
        str(move_teacher),
        "--child-normalized-out",
        str(child_normalized),
        "--report-out",
        str(report),
        "--max-empty",
        str(args.max_empty),
        "--max-roots",
        str(args.max_roots),
        "--seed",
        str(args.seed),
    ]


def run_cache_materialization_stage(
    args: argparse.Namespace,
    move_teacher: Path,
    child_normalized: Path,
    report: Path,
    stages: dict[str, dict[str, Any]],
    *,
    stage_name: str = "materialize_move_teacher_from_cache",
) -> str:
    command = cache_materialize_command(args, move_teacher, child_normalized, report)
    outputs = [move_teacher, child_normalized, report]
    metadata_path = args.output_dir / f"{stage_name}.resume.json"
    expected_resume = resume_expected_metadata(command, {"normalized_tsv": args.normalized_tsv})
    stages[stage_name] = {
        "command": command_for_report(command),
        "outputs": [report_path(path) for path in outputs],
        "resume_metadata": report_path(metadata_path),
    }
    if args.resume and all(path.exists() for path in outputs):
        validate_resume_metadata(stage_name, metadata_path, expected_resume, outputs)
        stages[stage_name]["status"] = "skipped-resume-validated"
        return "ok"
    result = subprocess.run(command, check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.stdout:
        sys.stdout.write(result.stdout)
    if result.returncode == 3:
        if result.stderr:
            sys.stderr.write(result.stderr)
        stages[stage_name]["status"] = "cache-miss"
        stages[stage_name]["allow_cache_miss_solve"] = args.allow_cache_miss_solve
        return "cache-miss"
    if result.returncode != 0:
        if result.stderr:
            sys.stderr.write(result.stderr)
        raise RuntimeError(f"command failed: {' '.join(command)}")
    metadata_path.write_text(
        stable_json(resume_complete_metadata(expected_resume, outputs)),
        encoding="utf-8",
    )
    stages[stage_name]["status"] = "ok"
    return "ok"


def rewrite_cache_materialization_resume_metadata(
    args: argparse.Namespace,
    move_teacher: Path,
    child_normalized: Path,
    report: Path,
) -> None:
    command = cache_materialize_command(args, move_teacher, child_normalized, report)
    outputs = [move_teacher, child_normalized, report]
    expected_resume = resume_expected_metadata(command, {"normalized_tsv": args.normalized_tsv})
    metadata_path = args.output_dir / "materialize_move_teacher_from_cache.resume.json"
    metadata_path.write_text(
        stable_json(resume_complete_metadata(expected_resume, outputs)),
        encoding="utf-8",
    )


def cache_probe_command(
    args: argparse.Namespace,
    report: Path,
    missing_normalized: Path,
) -> list[str]:
    return [
        sys.executable,
        str(args.move_teacher_cache_helper),
        "--normalized-tsv",
        str(args.normalized_tsv),
        "--cache-dir",
        str(args.move_teacher_cache_dir),
        "--report-out",
        str(report),
        "--max-empty",
        str(args.max_empty),
        "--max-roots",
        str(args.max_roots),
        "--seed",
        str(args.seed),
        "--probe-only",
        "--missing-normalized-out",
        str(missing_normalized),
    ]


def run_cache_probe_stage(
    args: argparse.Namespace,
    report: Path,
    missing_normalized: Path,
    stages: dict[str, dict[str, Any]],
    stage_name: str,
) -> dict[str, Any]:
    command = cache_probe_command(args, report, missing_normalized)
    stages[stage_name] = {
        "command": command_for_report(command),
        "outputs": [report_path(report), report_path(missing_normalized)],
    }
    run_or_fail(command)
    stages[stage_name]["status"] = "ok"
    return load_json(report)


def generate_missing_move_teacher(
    args: argparse.Namespace,
    missing_normalized: Path,
    missing_count: int,
    move_teacher: Path,
    child_normalized: Path,
    report: Path,
    stages: dict[str, dict[str, Any]],
) -> None:
    command = [
        str(args.generator),
        "--normalized-tsv",
        str(missing_normalized),
        "--move-teacher-out",
        str(move_teacher),
        "--child-normalized-out",
        str(child_normalized),
        "--report-out",
        str(report),
        "--max-empty",
        str(args.max_empty),
        "--max-roots",
        str(missing_count),
        "--seed",
        str(args.seed),
        "--progress-every",
        "100",
    ]
    run_stage(
        args,
        "generate_missing_exact_move_teacher_dataset",
        command,
        [move_teacher, child_normalized, report],
        stages,
        resume_inputs={"missing_normalized_tsv": missing_normalized},
    )


def populate_missing_move_teacher_cache(
    args: argparse.Namespace,
    missing_normalized: Path,
    missing_move_teacher: Path,
    report: Path,
    stages: dict[str, dict[str, Any]],
) -> None:
    command = [
        sys.executable,
        str(args.move_teacher_cache_helper),
        "--normalized-tsv",
        str(missing_normalized),
        "--cache-dir",
        str(args.move_teacher_cache_dir),
        "--report-out",
        str(report),
        "--max-empty",
        str(args.max_empty),
        "--max-roots",
        str(args.max_roots),
        "--seed",
        str(args.seed),
        "--source-move-teacher-tsv",
        str(missing_move_teacher),
        "--populate-only",
    ]
    stages["merge_missing_move_teacher_cache"] = {
        "command": command_for_report(command),
        "outputs": [report_path(report)],
    }
    run_or_fail(command)
    stages["merge_missing_move_teacher_cache"]["status"] = "ok"


def cache_section(report: dict[str, Any]) -> dict[str, Any]:
    cache = report.get("cache")
    if not isinstance(cache, dict):
        raise RuntimeError("cache report is missing cache section")
    return cache


def cache_int(cache: dict[str, Any], key: str) -> int:
    value = cache.get(key)
    if not isinstance(value, int):
        raise RuntimeError(f"cache report field {key} is missing or non-integer")
    return value


def augment_partial_cache_report(
    args: argparse.Namespace,
    final_report_path: Path,
    initial_probe: dict[str, Any],
    missing_move_teacher_path: Path,
    missing_report_path: Path,
    merge_report_path: Path,
    final_probe_report_path: Path,
    final_probe: dict[str, Any],
) -> None:
    final_report = load_json(final_report_path)
    final_cache = cache_section(final_report)
    initial_cache = cache_section(initial_probe)
    final_probe_cache = cache_section(final_probe)
    missing_report = load_json(missing_report_path)
    merge_report = load_json(merge_report_path)
    merge_cache = cache_section(merge_report)
    initial_reused_nodes = cache_int(initial_cache, "exact_nodes_reused")
    newly_solved_nodes = int(missing_report.get("teacher_nodes_sum", merge_cache.get("teacher_nodes_cached", 0)))
    missing_roots = cache_int(initial_cache, "root_misses")
    final_cache.update(
        {
            "cache_mode": "partial-miss-solve-then-cache-materialization",
            "partial_miss_solve": True,
            "root_hits_initial": cache_int(initial_cache, "root_hits"),
            "root_misses_initial": missing_roots,
            "cache_hit_ratio_initial": initial_cache.get("cache_hit_ratio"),
            "roots_newly_solved": int(missing_report.get("selected_roots", missing_roots)),
            "root_hits_after_merge": cache_int(final_probe_cache, "root_hits"),
            "root_misses_after_merge": cache_int(final_probe_cache, "root_misses"),
            "exact_nodes_reused": initial_reused_nodes,
            "exact_nodes_saved_estimate": initial_reused_nodes,
            "exact_nodes_newly_solved": newly_solved_nodes,
            "exact_nodes_materialized_from_cache": int(final_report.get("teacher_nodes_sum", 0)),
            "missing_normalized_sha256": initial_cache.get("missing_normalized_sha256"),
            "missing_move_teacher_sha256": sha256_file(missing_move_teacher_path),
            "missing_generator_report_sha256": sha256_file(missing_report_path),
            "cache_merge_report_sha256": sha256_file(merge_report_path),
            "final_probe_report_sha256": sha256_file(final_probe_report_path),
        }
    )
    final_report["partial_cache_miss_solve"] = {
        "enabled": True,
        "initial_root_hits": cache_int(initial_cache, "root_hits"),
        "initial_root_misses": missing_roots,
        "roots_newly_solved": int(missing_report.get("selected_roots", missing_roots)),
        "exact_nodes_reused": initial_reused_nodes,
        "exact_nodes_newly_solved": newly_solved_nodes,
        "missing_generator_report": report_path(missing_report_path),
        "cache_merge_report": report_path(merge_report_path),
        "final_probe_report": "move-teacher-cache-final-probe-report.json",
    }
    final_report_path.write_text(stable_json(final_report), encoding="utf-8")


def partial_cache_miss_resume_command(args: argparse.Namespace) -> str:
    command = [
        "python3",
        "tools/pattern/labels/run_move_teacher_decision_campaign.py",
        "--normalized-tsv",
        str(args.normalized_tsv),
        "--output-dir",
        str(args.output_dir),
        "--max-empty",
        str(args.max_empty),
        "--max-roots",
        str(args.max_roots),
        "--seed",
        str(args.seed),
        "--pattern-set",
        args.pattern_set,
        "--move-teacher-cache-dir",
        str(args.move_teacher_cache_dir),
        "--reuse-move-teacher-cache",
        "--write-move-teacher-cache",
        "--allow-cache-miss-solve",
        "--resume",
    ]
    return " ".join(command)


def populate_move_teacher_cache(
    args: argparse.Namespace,
    move_teacher: Path,
    report: Path,
    stages: dict[str, dict[str, Any]],
) -> None:
    command = [
        sys.executable,
        str(args.move_teacher_cache_helper),
        "--normalized-tsv",
        str(args.normalized_tsv),
        "--cache-dir",
        str(args.move_teacher_cache_dir),
        "--report-out",
        str(report),
        "--max-empty",
        str(args.max_empty),
        "--max-roots",
        str(args.max_roots),
        "--seed",
        str(args.seed),
        "--source-move-teacher-tsv",
        str(move_teacher),
        "--populate-only",
    ]
    stages["populate_move_teacher_cache"] = {
        "command": command_for_report(command),
        "outputs": [report_path(report)],
    }
    run_or_fail(command)
    stages["populate_move_teacher_cache"]["status"] = "ok"


def build_dataset(args: argparse.Namespace, child_normalized: Path, dataset: Path, report: Path, stages: dict[str, dict[str, Any]]) -> None:
    run_stage(
        args,
        "build_child_pattern_dataset",
        [
            str(args.dataset_exe),
            "--normalized-tsv",
            str(child_normalized),
            "--report",
            str(report),
            "--output-format",
            args.dataset_output_format,
            "--pattern-set",
            args.pattern_set,
        ],
        [dataset, report],
        stages,
        stdout_path=dataset,
        resume_inputs={"child_normalized": child_normalized},
    )


def train(args: argparse.Namespace, dataset: Path, weights_json: Path, report: Path, stages: dict[str, dict[str, Any]]) -> None:
    command = [
        sys.executable,
        str(args.trainer),
        "--dataset",
        str(dataset),
        "--mode",
        args.trainer_mode,
        "--epochs",
        str(args.epochs),
        "--learning-rate",
        str(args.learning_rate),
        "--lr-schedule",
        args.lr_schedule,
        "--weight-decay",
        str(args.weight_decay),
        "--weights-out",
        str(weights_json),
        "--report-out",
        str(report),
        "--seed",
        str(args.seed),
    ]
    if args.trainer_mode == "pattern-sgd-v0d":
        command.extend(["--phase-balance", "sqrt-inverse-count"])
    run_stage(
        args,
        "train_child_value_artifact",
        command,
        [weights_json, report],
        stages,
        resume_inputs={"dataset": dataset},
    )


def export_artifact(args: argparse.Namespace, weights_json: Path, weights_bin: Path, manifest: Path, stages: dict[str, dict[str, Any]]) -> None:
    run_stage(
        args,
        "export_child_value_artifact",
        [
            sys.executable,
            str(args.exporter),
            "--weights-json",
            str(weights_json),
            "--weights-out",
            str(weights_bin),
            "--manifest-out",
            str(manifest),
            "--pattern-set",
            args.pattern_set,
        ],
        [weights_bin, manifest],
        stages,
        resume_inputs={"weights_json": weights_json},
    )


def evaluate_artifact(
    args: argparse.Namespace,
    stage_name: str,
    move_teacher: Path,
    weights: Path,
    manifest: Path,
    pattern_set: str,
    report: Path,
    summary: Path,
    stages: dict[str, dict[str, Any]],
) -> None:
    run_stage(
        args,
        stage_name,
        [
            str(args.ranking_evaluator),
            "--move-teacher",
            str(move_teacher),
            "--weights",
            str(weights),
            "--manifest",
            str(manifest),
            "--pattern-set",
            pattern_set,
            "--report-out",
            str(report),
            "--summary-out",
            str(summary),
        ],
        [report, summary],
        stages,
        resume_inputs={
            "move_teacher": move_teacher,
            "weights": weights,
            "manifest": manifest,
        },
    )


def run_arena(
    args: argparse.Namespace,
    candidate_weights: Path,
    candidate_manifest: Path,
    arena_report: Path,
    arena_summary: Path,
    stages: dict[str, dict[str, Any]],
) -> None:
    if args.arena_baseline_weights is None or args.arena_baseline_manifest is None:
        stages["bounded_late_game_arena"] = {"status": "skipped-no-baseline"}
        return
    command = [
        str(args.arena_exe),
        "--positions-tsv",
        str(args.normalized_tsv),
        "--candidate-weights",
        str(candidate_weights),
        "--candidate-manifest",
        str(candidate_manifest),
        "--candidate-name",
        args.pattern_set,
        "--baseline-weights",
        str(args.arena_baseline_weights),
        "--baseline-manifest",
        str(args.arena_baseline_manifest),
        "--baseline-name",
        args.arena_baseline_name or args.pattern_set,
        "--max-empty",
        str(args.max_empty),
        "--max-positions",
        str(args.arena_max_positions),
        "--seed",
        str(args.seed),
        "--depth",
        str(args.arena_depth),
        "--report-out",
        str(arena_report),
        "--summary-out",
        str(arena_summary),
    ]
    if args.arena_side_swap:
        command.append("--side-swap")
    run_stage(
        args,
        "bounded_late_game_arena",
        command,
        [arena_report, arena_summary],
        stages,
        resume_inputs={
            "positions_tsv": args.normalized_tsv,
            "candidate_weights": candidate_weights,
            "candidate_manifest": candidate_manifest,
            "baseline_weights": args.arena_baseline_weights,
            "baseline_manifest": args.arena_baseline_manifest,
        },
    )


def extract_ranking(report_path: Path | None) -> dict[str, Any] | None:
    if report_path is None or not report_path.exists():
        return None
    report = load_json(report_path)
    keys = (
        "root_count",
        "legal_move_count",
        "top1_accuracy",
        "top1_tie_aware_accuracy",
        "best_move_in_top2_rate",
        "pairwise_accuracy",
        "pairwise_count",
        "mean_teacher_regret",
        "median_teacher_regret",
        "exact_best_predicted_score_rank_mean",
        "exact_best_predicted_score_rank_median",
        "predicted_best_exact_margin_mean",
        "roots_with_all_moves_same_predicted_score",
    )
    return {key: report.get(key) for key in keys}


def compare_rankings(previous: dict[str, Any] | None, trained: dict[str, Any] | None) -> dict[str, Any]:
    if previous is None or trained is None:
        return {
            "available": False,
            "reason": "previous/root-label artifact was not supplied",
        }
    return {
        "available": True,
        "top1_accuracy_delta": trained.get("top1_accuracy") - previous.get("top1_accuracy")
        if isinstance(trained.get("top1_accuracy"), (int, float))
        and isinstance(previous.get("top1_accuracy"), (int, float))
        else None,
        "pairwise_accuracy_delta": trained.get("pairwise_accuracy") - previous.get("pairwise_accuracy")
        if isinstance(trained.get("pairwise_accuracy"), (int, float))
        and isinstance(previous.get("pairwise_accuracy"), (int, float))
        else None,
        "top1_tie_aware_accuracy_delta": trained.get("top1_tie_aware_accuracy")
        - previous.get("top1_tie_aware_accuracy")
        if isinstance(trained.get("top1_tie_aware_accuracy"), (int, float))
        and isinstance(previous.get("top1_tie_aware_accuracy"), (int, float))
        else None,
        "best_move_in_top2_rate_delta": trained.get("best_move_in_top2_rate")
        - previous.get("best_move_in_top2_rate")
        if isinstance(trained.get("best_move_in_top2_rate"), (int, float))
        and isinstance(previous.get("best_move_in_top2_rate"), (int, float))
        else None,
        "mean_teacher_regret_delta": trained.get("mean_teacher_regret") - previous.get("mean_teacher_regret")
        if isinstance(trained.get("mean_teacher_regret"), (int, float))
        and isinstance(previous.get("mean_teacher_regret"), (int, float))
        else None,
        "median_teacher_regret_delta": trained.get("median_teacher_regret")
        - previous.get("median_teacher_regret")
        if isinstance(trained.get("median_teacher_regret"), (int, float))
        and isinstance(previous.get("median_teacher_regret"), (int, float))
        else None,
        "exact_best_predicted_score_rank_mean_delta": trained.get("exact_best_predicted_score_rank_mean")
        - previous.get("exact_best_predicted_score_rank_mean")
        if isinstance(trained.get("exact_best_predicted_score_rank_mean"), (int, float))
        and isinstance(previous.get("exact_best_predicted_score_rank_mean"), (int, float))
        else None,
        "roots_with_all_moves_same_predicted_score_delta": trained.get(
            "roots_with_all_moves_same_predicted_score"
        )
        - previous.get("roots_with_all_moves_same_predicted_score")
        if isinstance(trained.get("roots_with_all_moves_same_predicted_score"), (int, float))
        and isinstance(previous.get("roots_with_all_moves_same_predicted_score"), (int, float))
        else None,
        "interpretation": "positive top1/pairwise deltas and negative regret delta indicate better decision leverage",
    }


def write_summary(path: Path, report: dict[str, Any]) -> None:
    previous = report["ranking"].get("previous")
    trained = report["ranking"].get("move_teacher_child_value")
    comparison = report["ranking"].get("comparison", {})
    arena = report.get("arena")
    cache = report.get("move_teacher_cache")
    cache_summary = cache.get("cache") if isinstance(cache, dict) and isinstance(cache.get("cache"), dict) else cache
    lines = [
        "# Move-Teacher Decision Campaign",
        "",
        "Local-only decision-leverage diagnostic. Not Elo, not self-play, not a production strength claim, and not a publication gate.",
        "",
        "## Dataset",
        "",
        f"- move-teacher rows: {report['move_teacher_dataset'].get('move_rows')}",
        f"- selected roots: {report['move_teacher_dataset'].get('selected_roots')}",
        f"- max empty: {report.get('max_empty')}",
        "",
        "## Cache",
        "",
        f"- enabled: {cache_summary is not None}",
        f"- mode: {None if cache_summary is None else cache_summary.get('cache_mode')}",
        f"- root hits: {None if cache_summary is None else cache_summary.get('root_hits')}",
        f"- root misses: {None if cache_summary is None else cache_summary.get('root_misses')}",
        f"- exact nodes newly solved: {None if cache_summary is None else cache_summary.get('exact_nodes_newly_solved')}",
        f"- exact nodes saved estimate: {None if cache_summary is None else cache_summary.get('exact_nodes_saved_estimate')}",
        "",
        "## Ranking",
        "",
        f"- previous top1: {None if previous is None else previous.get('top1_accuracy')}",
        f"- trained top1: {None if trained is None else trained.get('top1_accuracy')}",
        f"- previous pairwise: {None if previous is None else previous.get('pairwise_accuracy')}",
        f"- trained pairwise: {None if trained is None else trained.get('pairwise_accuracy')}",
        f"- previous mean regret: {None if previous is None else previous.get('mean_teacher_regret')}",
        f"- trained mean regret: {None if trained is None else trained.get('mean_teacher_regret')}",
        f"- comparison available: {comparison.get('available')}",
        "",
        "## Arena",
        "",
        f"- status: {None if arena is None else arena.get('status')}",
    ]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    try:
        args.output_dir.mkdir(parents=True, exist_ok=True)
        stages: dict[str, dict[str, Any]] = {}

        move_teacher = args.output_dir / "move-teacher.tsv"
        child_normalized = args.output_dir / "child-normalized.tsv"
        generator_report = args.output_dir / "move-teacher-report.json"
        cache_report = args.output_dir / "move-teacher-cache-report.json"
        cache_probe_report = args.output_dir / "move-teacher-cache-probe-report.json"
        cache_final_probe_report = args.output_dir / "move-teacher-cache-final-probe-report.json"
        missing_normalized = args.output_dir / "missing-move-teacher-roots-normalized.tsv"
        missing_after_merge_normalized = args.output_dir / "missing-move-teacher-roots-after-merge-normalized.tsv"
        missing_move_teacher = args.output_dir / "missing-move-teacher.tsv"
        missing_child_normalized = args.output_dir / "missing-child-normalized.tsv"
        missing_generator_report = args.output_dir / "missing-move-teacher-report.json"
        cache_merge_report = args.output_dir / "move-teacher-cache-merge-report.json"
        dataset = args.output_dir / "child-pattern-dataset.tsv"
        dataset_report = args.output_dir / "child-pattern-dataset-report.json"
        weights_json = args.output_dir / "move-teacher-child-weights.json"
        trainer_report = args.output_dir / "move-teacher-child-trainer-report.json"
        weights_bin = args.output_dir / "move-teacher-child.weights.bin"
        manifest = args.output_dir / "move-teacher-child.manifest.json"
        trained_ranking_report = args.output_dir / "move-teacher-child-ranking-report.json"
        trained_ranking_summary = args.output_dir / "move-teacher-child-ranking-summary.md"
        previous_ranking_report = args.output_dir / "previous-ranking-report.json"
        previous_ranking_summary = args.output_dir / "previous-ranking-summary.md"
        arena_report = args.output_dir / "arena-report.json"
        arena_summary = args.output_dir / "arena-summary.md"
        campaign_report = args.output_dir / "campaign-report.json"
        campaign_summary = args.output_dir / "campaign-summary.md"

        generated_from_cache = False
        partial_cache_miss_solved = False
        if args.reuse_move_teacher_cache:
            cache_status = run_cache_materialization_stage(args, move_teacher, child_normalized, generator_report, stages)
            if cache_status == "ok":
                generated_from_cache = True
            elif args.allow_cache_miss_solve:
                stages["materialize_move_teacher_from_cache_initial"] = stages.pop(
                    "materialize_move_teacher_from_cache"
                )
                initial_probe = run_cache_probe_stage(
                    args,
                    cache_probe_report,
                    missing_normalized,
                    stages,
                    "probe_move_teacher_cache_before_missing_solve",
                )
                initial_cache = cache_section(initial_probe)
                missing_count = cache_int(initial_cache, "root_misses")
                if missing_count <= 0:
                    raise RuntimeError("cache materialization reported a miss but probe found no missing roots")
                generate_missing_move_teacher(
                    args,
                    missing_normalized,
                    missing_count,
                    missing_move_teacher,
                    missing_child_normalized,
                    missing_generator_report,
                    stages,
                )
                populate_missing_move_teacher_cache(
                    args,
                    missing_normalized,
                    missing_move_teacher,
                    cache_merge_report,
                    stages,
                )
                final_probe = run_cache_probe_stage(
                    args,
                    cache_final_probe_report,
                    missing_after_merge_normalized,
                    stages,
                    "probe_move_teacher_cache_after_missing_merge",
                )
                final_probe_cache = cache_section(final_probe)
                if cache_int(final_probe_cache, "root_misses") != 0:
                    raise RuntimeError(
                        "move-teacher cache still has "
                        f"{cache_int(final_probe_cache, 'root_misses')} missing roots after merge; "
                        "final complete materialization was not written"
                    )
                final_status = run_cache_materialization_stage(
                    args,
                    move_teacher,
                    child_normalized,
                    generator_report,
                    stages,
                    stage_name="materialize_move_teacher_from_cache",
                )
                if final_status != "ok":
                    raise RuntimeError("move-teacher cache did not materialize as a full hit after missing merge")
                augment_partial_cache_report(
                    args,
                    generator_report,
                    initial_probe,
                    missing_move_teacher,
                    missing_generator_report,
                    cache_merge_report,
                    cache_final_probe_report,
                    final_probe,
                )
                rewrite_cache_materialization_resume_metadata(args, move_teacher, child_normalized, generator_report)
                generated_from_cache = True
                partial_cache_miss_solved = True
            else:
                probe = run_cache_probe_stage(
                    args,
                    cache_probe_report,
                    missing_normalized,
                    stages,
                    "probe_move_teacher_cache_after_miss",
                )
                cache = cache_section(probe)
                raise RuntimeError(
                    "partial move-teacher cache hit is incomplete: "
                    f"hits={cache_int(cache, 'root_hits')} misses={cache_int(cache, 'root_misses')}. "
                    "Rerun with --allow-cache-miss-solve and --write-move-teacher-cache to solve only missing roots. "
                    f"Resume command: {partial_cache_miss_resume_command(args)}"
                )
        if not generated_from_cache:
            run_stage(
                args,
                "generate_exact_move_teacher_dataset",
                [
                    str(args.generator),
                    "--normalized-tsv",
                    str(args.normalized_tsv),
                    "--move-teacher-out",
                    str(move_teacher),
                    "--child-normalized-out",
                    str(child_normalized),
                    "--report-out",
                    str(generator_report),
                    "--max-empty",
                    str(args.max_empty),
                    "--max-roots",
                    str(args.max_roots),
                    "--seed",
                    str(args.seed),
                    "--progress-every",
                    "100",
                ],
                [move_teacher, child_normalized, generator_report],
                stages,
                resume_inputs={"normalized_tsv": args.normalized_tsv},
            )
        if args.write_move_teacher_cache and not generated_from_cache:
            populate_move_teacher_cache(args, move_teacher, cache_report, stages)
        build_dataset(args, child_normalized, dataset, dataset_report, stages)
        train(args, dataset, weights_json, trainer_report, stages)
        export_artifact(args, weights_json, weights_bin, manifest, stages)

        if args.previous_weights is not None and args.previous_manifest is not None:
            evaluate_artifact(
                args,
                "evaluate_previous_artifact_ranking",
                move_teacher,
                args.previous_weights,
                args.previous_manifest,
                args.previous_pattern_set or args.pattern_set,
                previous_ranking_report,
                previous_ranking_summary,
                stages,
            )
            previous_ranking = extract_ranking(previous_ranking_report)
        else:
            stages["evaluate_previous_artifact_ranking"] = {"status": "skipped-no-previous-artifact"}
            previous_ranking = None

        evaluate_artifact(
            args,
            "evaluate_move_teacher_child_artifact_ranking",
            move_teacher,
            weights_bin,
            manifest,
            args.pattern_set,
            trained_ranking_report,
            trained_ranking_summary,
            stages,
        )
        trained_ranking = extract_ranking(trained_ranking_report)
        run_arena(args, weights_bin, manifest, arena_report, arena_summary, stages)

        arena_payload: dict[str, Any] | None
        if arena_report.exists():
            arena_data = load_json(arena_report)
            arena_payload = {
                "status": "ok",
                "candidate_score_rate": arena_data.get("candidate_score_rate"),
                "games_played": arena_data.get("games_played"),
                "selected_positions": arena_data.get("selected_positions"),
                "average_disc_diff": arena_data.get(
                    "average_disc_diff_candidate_perspective",
                    arena_data.get("average_disc_diff"),
                ),
                "candidate_score_rate_interval_95": arena_data.get("candidate_score_rate_interval_95"),
            }
        else:
            arena_payload = {"status": stages.get("bounded_late_game_arena", {}).get("status", "skipped")}

        report = {
            "schema_version": 1,
            "created_at_utc": datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
            "normalized_input_path": report_path(args.normalized_tsv),
            "output_dir": report_path(args.output_dir),
            "max_empty": args.max_empty,
            "max_roots": args.max_roots,
            "seed": args.seed,
            "pattern_set": args.pattern_set,
            "trainer_mode": args.trainer_mode,
            "epochs": args.epochs,
            "learning_rate": args.learning_rate,
            "lr_schedule": args.lr_schedule,
            "weight_decay": args.weight_decay,
            "move_teacher_dataset": load_json(generator_report),
            "move_teacher_cache": load_json(cache_report) if cache_report.exists() else load_json(generator_report).get("cache"),
            "child_pattern_dataset": load_json(dataset_report),
            "trainer": load_json(trainer_report),
            "ranking": {
                "previous": previous_ranking,
                "move_teacher_child_value": trained_ranking,
                "comparison": compare_rankings(previous_ranking, trained_ranking),
            },
            "arena": arena_payload,
            "stages": stages,
            "outputs": {
                "move_teacher_tsv": report_path(move_teacher),
                "child_normalized_tsv": report_path(child_normalized),
                "child_pattern_dataset_tsv": report_path(dataset),
                "trained_weights_json": report_path(weights_json),
                "trained_weights_bin": report_path(weights_bin),
                "trained_manifest": report_path(manifest),
                "trained_ranking_report": report_path(trained_ranking_report),
                "previous_ranking_report": report_path(previous_ranking_report)
                if previous_ranking_report.exists()
                else None,
                "arena_report": report_path(arena_report) if arena_report.exists() else None,
                "move_teacher_cache_report": report_path(cache_report) if cache_report.exists() else None,
                "move_teacher_cache_probe_report": report_path(cache_probe_report)
                if cache_probe_report.exists()
                else None,
                "move_teacher_cache_merge_report": report_path(cache_merge_report)
                if cache_merge_report.exists()
                else None,
            },
            "partial_cache_miss_solved": partial_cache_miss_solved,
            "warnings": [
                "local-only diagnostic; generated labels, datasets, weights, artifacts, reports, and logs must not be committed",
                "not a local-neighborhood optimizer sweep",
                "not a strength claim",
                "not an Elo result",
                "not self-play",
                "not a production artifact",
                "not a publication gate",
            ],
        }
        campaign_report.write_text(stable_json(report), encoding="utf-8")
        write_summary(campaign_summary, report)
        print(f"campaign_report={campaign_report}")
        print(f"campaign_summary={campaign_summary}")
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
