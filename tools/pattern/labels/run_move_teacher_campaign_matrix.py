#!/usr/bin/env python3
"""Run and aggregate a local-only move-teacher decision campaign matrix."""

from __future__ import annotations

import argparse
import json
import shlex
import statistics
import subprocess
import sys
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


TRAINER_MODES = ("pattern-sgd-v0c", "pattern-sgd-v0d")
RANKING_METRICS = (
    "top1_accuracy",
    "top1_tie_aware_accuracy",
    "best_move_in_top2_rate",
    "pairwise_accuracy",
    "mean_teacher_regret",
    "median_teacher_regret",
    "exact_best_predicted_score_rank_mean",
    "exact_best_predicted_score_rank_median",
    "roots_with_all_moves_same_predicted_score",
)
HELDOUT_SPLITS = ("validation", "test")


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def parse_int_list(text: str, name: str, *, allow_zero: bool) -> list[int]:
    values: list[int] = []
    seen: set[int] = set()
    for raw in text.split(","):
        token = raw.strip()
        if not token:
            raise argparse.ArgumentTypeError(f"{name} contains an empty item")
        try:
            value = int(token)
        except ValueError as error:
            raise argparse.ArgumentTypeError(f"{name} item is not an integer: {token}") from error
        if value < 0 or (value == 0 and not allow_zero):
            expectation = "non-negative" if allow_zero else "positive"
            raise argparse.ArgumentTypeError(f"{name} values must be {expectation}: {value}")
        if value in seen:
            raise argparse.ArgumentTypeError(f"{name} contains duplicate value: {value}")
        seen.add(value)
        values.append(value)
    if not values:
        raise argparse.ArgumentTypeError(f"{name} must not be empty")
    return values


def parse_trainer_modes(text: str) -> list[str]:
    values: list[str] = []
    seen: set[str] = set()
    for raw in text.split(","):
        value = raw.strip()
        if value not in TRAINER_MODES:
            allowed = ", ".join(TRAINER_MODES)
            raise argparse.ArgumentTypeError(f"--trainer-mode must be one of {allowed}: {value}")
        if value in seen:
            raise argparse.ArgumentTypeError(f"--trainer-mode contains duplicate value: {value}")
        seen.add(value)
        values.append(value)
    if not values:
        raise argparse.ArgumentTypeError("--trainer-mode must not be empty")
    return values


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--normalized-tsv", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument(
        "--root-counts",
        required=True,
        type=lambda text: parse_int_list(text, "--root-counts", allow_zero=False),
    )
    parser.add_argument(
        "--seeds",
        required=True,
        type=lambda text: parse_int_list(text, "--seeds", allow_zero=True),
    )
    parser.add_argument("--max-empty", type=int, default=12)
    parser.add_argument("--pattern-set", default="pattern-v2-endgame-lite")
    parser.add_argument("--trainer-mode", type=parse_trainer_modes, default=parse_trainer_modes("pattern-sgd-v0c"))
    parser.add_argument("--epochs", type=int, default=8)
    parser.add_argument("--learning-rate", type=float, default=0.1)
    parser.add_argument("--lr-schedule", choices=("constant", "inverse-sqrt"), default="inverse-sqrt")
    parser.add_argument("--weight-decay", type=float, default=0.0001)
    parser.add_argument("--dataset-output-format", choices=("compact-tsv", "expanded-tsv"), default="compact-tsv")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--keep-going", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
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
        "--campaign-helper",
        type=Path,
        default=root / "tools/pattern/labels/run_move_teacher_decision_campaign.py",
    )
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
    parser.add_argument("--trainer", type=Path, default=root / "tools/pattern/train/train_v0a.py")
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
    if args.arena_depth <= 0:
        parser.error("--arena-depth must be positive")
    if args.arena_max_positions <= 0:
        parser.error("--arena-max-positions must be positive")
    return args


def stable_json(data: Any) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def load_json(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise RuntimeError(f"JSON root must be an object: {path}")
    return data


def report_path(path: Path | None) -> str | None:
    if path is None:
        return None
    return path.name if path.is_absolute() else str(path)


def shell_join(command: list[str]) -> str:
    return " ".join(shlex.quote(part) for part in command)


def command_template(args: argparse.Namespace) -> str:
    command = [
        sys.executable,
        str(args.campaign_helper),
        "--normalized-tsv",
        str(args.normalized_tsv),
        "--output-dir",
        str(args.output_dir / "roots-<count>-seed-<seed>"),
        "--max-empty",
        str(args.max_empty),
        "--max-roots",
        "<count>",
        "--seed",
        "<seed>",
        "--pattern-set",
        args.pattern_set,
        "--trainer-mode",
        args.trainer_mode[0],
        "--epochs",
        str(args.epochs),
        "--learning-rate",
        str(args.learning_rate),
        "--lr-schedule",
        args.lr_schedule,
        "--weight-decay",
        str(args.weight_decay),
    ]
    if args.resume:
        command.append("--resume")
    if args.move_teacher_cache_dir is not None:
        command.extend(["--move-teacher-cache-dir", str(args.move_teacher_cache_dir)])
    if args.reuse_move_teacher_cache:
        command.append("--reuse-move-teacher-cache")
    if args.write_move_teacher_cache:
        command.append("--write-move-teacher-cache")
    if args.allow_cache_miss_solve:
        command.append("--allow-cache-miss-solve")
    return shell_join(command)


def require_file(path: Path, label: str, args: argparse.Namespace) -> None:
    if not path.is_file():
        raise RuntimeError(f"missing {label}: {path}\nresume command shape:\n{command_template(args)}")


def preflight(args: argparse.Namespace) -> None:
    require_file(args.normalized_tsv, "normalized TSV", args)
    require_file(args.campaign_helper, "move-teacher campaign helper", args)
    require_file(args.generator, "move-teacher generator", args)
    require_file(args.dataset_exe, "pattern dataset executable", args)
    require_file(args.trainer, "pattern trainer", args)
    require_file(args.exporter, "pattern exporter", args)
    require_file(args.ranking_evaluator, "move-teacher ranking evaluator", args)
    if args.reuse_move_teacher_cache or args.write_move_teacher_cache:
        require_file(args.move_teacher_cache_helper, "move-teacher cache helper", args)
    if args.previous_weights is not None and args.previous_manifest is not None:
        require_file(args.previous_weights, "previous artifact weights", args)
        require_file(args.previous_manifest, "previous artifact manifest", args)
    if args.arena_baseline_weights is not None and args.arena_baseline_manifest is not None:
        require_file(args.arena_baseline_weights, "arena baseline weights", args)
        require_file(args.arena_baseline_manifest, "arena baseline manifest", args)
        require_file(args.arena_exe, "pattern artifact arena executable", args)


def run_dir_name(root_count: int, seed: int, mode: str, mode_count: int) -> str:
    base = f"roots-{root_count}-seed-{seed}"
    if mode_count == 1:
        return base
    return f"{base}-{mode}"


def campaign_command(args: argparse.Namespace, output_dir: Path, root_count: int, seed: int, mode: str) -> list[str]:
    command = [
        sys.executable,
        str(args.campaign_helper),
        "--normalized-tsv",
        str(args.normalized_tsv),
        "--output-dir",
        str(output_dir),
        "--max-empty",
        str(args.max_empty),
        "--max-roots",
        str(root_count),
        "--seed",
        str(seed),
        "--pattern-set",
        args.pattern_set,
        "--trainer-mode",
        mode,
        "--epochs",
        str(args.epochs),
        "--learning-rate",
        str(args.learning_rate),
        "--lr-schedule",
        args.lr_schedule,
        "--weight-decay",
        str(args.weight_decay),
        "--dataset-output-format",
        args.dataset_output_format,
        "--generator",
        str(args.generator),
        "--dataset-exe",
        str(args.dataset_exe),
        "--trainer",
        str(args.trainer),
        "--exporter",
        str(args.exporter),
        "--ranking-evaluator",
        str(args.ranking_evaluator),
    ]
    if args.move_teacher_cache_dir is not None:
        command.extend(
            [
                "--move-teacher-cache-dir",
                str(args.move_teacher_cache_dir),
                "--move-teacher-cache-helper",
                str(args.move_teacher_cache_helper),
            ]
        )
    if args.reuse_move_teacher_cache:
        command.append("--reuse-move-teacher-cache")
    if args.write_move_teacher_cache:
        command.append("--write-move-teacher-cache")
    if args.allow_cache_miss_solve:
        command.append("--allow-cache-miss-solve")
    if args.previous_weights is not None and args.previous_manifest is not None:
        command.extend(
            [
                "--previous-weights",
                str(args.previous_weights),
                "--previous-manifest",
                str(args.previous_manifest),
                "--previous-pattern-set",
                args.previous_pattern_set or args.pattern_set,
                "--previous-name",
                args.previous_name,
            ]
        )
    if args.arena_baseline_weights is not None and args.arena_baseline_manifest is not None:
        command.extend(
            [
                "--arena-baseline-weights",
                str(args.arena_baseline_weights),
                "--arena-baseline-manifest",
                str(args.arena_baseline_manifest),
                "--arena-baseline-name",
                args.arena_baseline_name or args.pattern_set,
                "--arena-depth",
                str(args.arena_depth),
                "--arena-max-positions",
                str(args.arena_max_positions),
                "--arena-exe",
                str(args.arena_exe),
            ]
        )
        command.append("--arena-side-swap" if args.arena_side_swap else "--no-arena-side-swap")
    if args.resume:
        command.append("--resume")
    return command


def run_campaign(command: list[str]) -> int:
    print(f"running: {shell_join(command)}", flush=True)
    completed = subprocess.run(command, check=False)
    return completed.returncode


def resolve_run_file(run_dir: Path, value: Any, default_name: str) -> Path:
    if isinstance(value, str) and value:
        path = Path(value)
        return path if path.is_absolute() else run_dir / path
    return run_dir / default_name


def numeric(data: dict[str, Any] | None, key: str) -> float | int | None:
    if data is None:
        return None
    value = data.get(key)
    return value if isinstance(value, (int, float)) else None


def metric_subset(report: dict[str, Any] | None) -> dict[str, Any] | None:
    if report is None:
        return None
    keys = (
        "root_count",
        "legal_move_count",
        "pairwise_count",
        *RANKING_METRICS,
    )
    return {key: report.get(key) for key in keys if key in report}


def delta_map(previous: dict[str, Any] | None, trained: dict[str, Any] | None) -> dict[str, Any] | None:
    if previous is None or trained is None:
        return None
    result: dict[str, Any] = {}
    for key in RANKING_METRICS:
        old = numeric(previous, key)
        new = numeric(trained, key)
        result[key] = new - old if old is not None and new is not None else None
    return result


def weighted_average(parts: list[dict[str, Any]], key: str, weight_key: str) -> float | None:
    weighted_sum = 0.0
    weight_sum = 0.0
    for part in parts:
        value = numeric(part, key)
        weight = numeric(part, weight_key)
        if value is None or weight is None:
            continue
        weighted_sum += float(value) * float(weight)
        weight_sum += float(weight)
    if weight_sum == 0.0:
        return None
    return weighted_sum / weight_sum


def split_aggregate(ranking: dict[str, Any] | None) -> dict[str, Any] | None:
    if ranking is None:
        return None
    by_split = ranking.get("results_by_split")
    if not isinstance(by_split, dict):
        return None
    parts = [part for split in HELDOUT_SPLITS if isinstance((part := by_split.get(split)), dict)]
    if not parts:
        return None
    root_count = sum(int(part.get("root_count", 0)) for part in parts)
    legal_move_count = sum(int(part.get("legal_move_count", 0)) for part in parts)
    pairwise_count = sum(int(part.get("pairwise_count", 0)) for part in parts)
    return {
        "root_count": root_count,
        "legal_move_count": legal_move_count,
        "pairwise_count": pairwise_count,
        "top1_accuracy": weighted_average(parts, "top1_accuracy", "root_count"),
        "top1_tie_aware_accuracy": weighted_average(parts, "top1_tie_aware_accuracy", "root_count"),
        "best_move_in_top2_rate": weighted_average(parts, "best_move_in_top2_rate", "root_count"),
        "pairwise_accuracy": weighted_average(parts, "pairwise_accuracy", "pairwise_count"),
        "mean_teacher_regret": weighted_average(parts, "mean_teacher_regret", "root_count"),
        "exact_best_predicted_score_rank_mean": weighted_average(
            parts, "exact_best_predicted_score_rank_mean", "root_count"
        ),
        "roots_with_all_moves_same_predicted_score": sum(
            int(part.get("roots_with_all_moves_same_predicted_score", 0)) for part in parts
        ),
    }


def load_optional_json(path: Path) -> dict[str, Any] | None:
    return load_json(path) if path.exists() else None


def stage_statuses(campaign: dict[str, Any]) -> dict[str, Any]:
    stages = campaign.get("stages")
    if not isinstance(stages, dict):
        return {}
    return {
        name: stage.get("status") if isinstance(stage, dict) else None
        for name, stage in sorted(stages.items())
    }


def run_summary(
    run_dir: Path,
    root_count: int,
    seed: int,
    mode: str,
    command: list[str],
    returncode: int | None,
    *,
    dry_run: bool,
) -> dict[str, Any]:
    if dry_run:
        return {
            "run_id": run_dir.name,
            "root_count": root_count,
            "seed": seed,
            "trainer_mode": mode,
            "status": "planned",
            "command": [Path(part).name if "/" in part else part for part in command],
        }
    campaign_report = run_dir / "campaign-report.json"
    if returncode not in (None, 0):
        return {
            "run_id": run_dir.name,
            "root_count": root_count,
            "seed": seed,
            "trainer_mode": mode,
            "status": "failed",
            "returncode": returncode,
            "campaign_report": report_path(campaign_report),
        }
    if not campaign_report.exists():
        return {
            "run_id": run_dir.name,
            "root_count": root_count,
            "seed": seed,
            "trainer_mode": mode,
            "status": "missing-report",
            "campaign_report": report_path(campaign_report),
        }

    campaign = load_json(campaign_report)
    outputs = campaign.get("outputs") if isinstance(campaign.get("outputs"), dict) else {}
    previous_report_path = resolve_run_file(
        run_dir, outputs.get("previous_ranking_report"), "previous-ranking-report.json"
    )
    trained_report_path = resolve_run_file(
        run_dir, outputs.get("trained_ranking_report"), "move-teacher-child-ranking-report.json"
    )
    arena_report_path = resolve_run_file(run_dir, outputs.get("arena_report"), "arena-report.json")
    previous = load_optional_json(previous_report_path)
    trained = load_optional_json(trained_report_path)
    arena_raw = load_optional_json(arena_report_path)
    previous_heldout = split_aggregate(previous)
    trained_heldout = split_aggregate(trained)
    arena_status = campaign.get("arena") if isinstance(campaign.get("arena"), dict) else {}
    arena = {
        "status": arena_status.get("status", "missing" if arena_raw is None else "ok"),
        "games_played": None if arena_raw is None else arena_raw.get("games_played"),
        "candidate_score_rate": None if arena_raw is None else arena_raw.get("candidate_score_rate"),
        "average_disc_diff_candidate_perspective": None
        if arena_raw is None
        else arena_raw.get("average_disc_diff_candidate_perspective"),
        "candidate_score_rate_interval_95": None
        if arena_raw is None
        else arena_raw.get("candidate_score_rate_interval_95"),
    }
    dataset = campaign.get("move_teacher_dataset") if isinstance(campaign.get("move_teacher_dataset"), dict) else {}
    cache_status = campaign.get("move_teacher_cache")
    if not isinstance(cache_status, dict):
        cache_status = dataset.get("cache") if isinstance(dataset.get("cache"), dict) else None
    return {
        "run_id": run_dir.name,
        "root_count": root_count,
        "seed": seed,
        "trainer_mode": mode,
        "status": "ok",
        "campaign_report": report_path(run_dir / "campaign-report.json"),
        "selected_roots": dataset.get("selected_roots"),
        "move_rows": dataset.get("move_rows"),
        "child_rows": dataset.get("child_normalized_rows"),
        "exact_teacher_nodes": dataset.get("teacher_nodes_sum"),
        "generation_wall_time_sec": dataset.get("wall_time_sec"),
        "move_teacher_cache": cache_status,
        "previous": metric_subset(previous),
        "trained": metric_subset(trained),
        "deltas": delta_map(previous, trained),
        "heldout_validation_test": {
            "previous": previous_heldout,
            "trained": trained_heldout,
            "deltas": delta_map(previous_heldout, trained_heldout),
        },
        "arena": arena,
        "campaign_stage_statuses": stage_statuses(campaign),
    }


def completed_runs(runs: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [run for run in runs if run.get("status") == "ok"]


def comparable_runs(runs: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [run for run in completed_runs(runs) if isinstance(run.get("deltas"), dict)]


def values_for(runs: list[dict[str, Any]], metric: str) -> list[float]:
    values: list[float] = []
    for run in runs:
        deltas = run.get("deltas")
        if not isinstance(deltas, dict):
            continue
        value = deltas.get(metric)
        if isinstance(value, (int, float)):
            values.append(float(value))
    return values


def mean_or_none(values: list[float]) -> float | None:
    return statistics.fmean(values) if values else None


def median_or_none(values: list[float]) -> float | None:
    return statistics.median(values) if values else None


def seed_positive(run: dict[str, Any]) -> bool:
    deltas = run.get("deltas")
    if not isinstance(deltas, dict):
        return False
    return (
        numeric(deltas, "top1_accuracy") is not None
        and numeric(deltas, "top1_accuracy") > 0
        and numeric(deltas, "pairwise_accuracy") is not None
        and numeric(deltas, "pairwise_accuracy") > 0
        and numeric(deltas, "mean_teacher_regret") is not None
        and numeric(deltas, "mean_teacher_regret") < 0
    )


def seed_negative(run: dict[str, Any]) -> bool:
    deltas = run.get("deltas")
    if not isinstance(deltas, dict):
        return False
    pairwise = numeric(deltas, "pairwise_accuracy")
    regret = numeric(deltas, "mean_teacher_regret")
    return (pairwise is not None and pairwise <= 0) or (regret is not None and regret >= 0)


def heldout_supports(run: dict[str, Any]) -> bool:
    heldout = run.get("heldout_validation_test")
    if not isinstance(heldout, dict):
        return False
    deltas = heldout.get("deltas")
    if not isinstance(deltas, dict):
        return False
    return (
        numeric(deltas, "best_move_in_top2_rate") is not None
        and numeric(deltas, "best_move_in_top2_rate") > 0
        and numeric(deltas, "pairwise_accuracy") is not None
        and numeric(deltas, "pairwise_accuracy") > 0
        and numeric(deltas, "mean_teacher_regret") is not None
        and numeric(deltas, "mean_teacher_regret") < 0
    )


def arena_non_negative(run: dict[str, Any]) -> bool:
    arena = run.get("arena")
    if not isinstance(arena, dict) or arena.get("status") != "ok":
        return False
    rate = arena.get("candidate_score_rate")
    return isinstance(rate, (int, float)) and rate >= 0.5


def best_run(runs: list[dict[str, Any]]) -> dict[str, Any] | None:
    candidates = [run for run in runs if isinstance(run.get("deltas"), dict)]
    if not candidates:
        return None
    return max(
        candidates,
        key=lambda run: (
            numeric(run["deltas"], "pairwise_accuracy")
            if numeric(run["deltas"], "pairwise_accuracy") is not None
            else float("-inf"),
            -numeric(run["deltas"], "mean_teacher_regret")
            if numeric(run["deltas"], "mean_teacher_regret") is not None
            else float("-inf"),
            numeric(run["deltas"], "top1_accuracy")
            if numeric(run["deltas"], "top1_accuracy") is not None
            else float("-inf"),
        ),
    )


def worst_run(runs: list[dict[str, Any]]) -> dict[str, Any] | None:
    candidates = [run for run in runs if isinstance(run.get("deltas"), dict)]
    if not candidates:
        return None
    return min(
        candidates,
        key=lambda run: (
            numeric(run["deltas"], "pairwise_accuracy")
            if numeric(run["deltas"], "pairwise_accuracy") is not None
            else float("inf"),
            -numeric(run["deltas"], "mean_teacher_regret")
            if numeric(run["deltas"], "mean_teacher_regret") is not None
            else float("inf"),
            numeric(run["deltas"], "top1_accuracy")
            if numeric(run["deltas"], "top1_accuracy") is not None
            else float("inf"),
        ),
    )


def summarize_group(runs: list[dict[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {
        "run_count": len(runs),
        "completed_run_count": len(completed_runs(runs)),
        "comparable_run_count": len(comparable_runs(runs)),
        "positive_seed_count": sum(1 for run in runs if seed_positive(run)),
        "negative_seed_count": sum(1 for run in runs if seed_negative(run)),
        "heldout_support_count": sum(1 for run in runs if heldout_supports(run)),
        "arena_completed_count": sum(
            1
            for run in runs
            if isinstance(run.get("arena"), dict) and run["arena"].get("status") == "ok"
        ),
        "arena_non_negative_count": sum(1 for run in runs if arena_non_negative(run)),
    }
    for metric in RANKING_METRICS:
        values = values_for(runs, metric)
        result[f"{metric}_delta_mean"] = mean_or_none(values)
        result[f"{metric}_delta_median"] = median_or_none(values)
        result[f"{metric}_delta_min"] = min(values) if values else None
        result[f"{metric}_delta_max"] = max(values) if values else None
        result[f"{metric}_delta_spread"] = max(values) - min(values) if values else None
    best = best_run(runs)
    worst = worst_run(runs)
    result["best_seed"] = None if best is None else {"run_id": best["run_id"], "seed": best["seed"]}
    result["worst_seed"] = None if worst is None else {"run_id": worst["run_id"], "seed": worst["seed"]}
    return result


def recommendation(overall: dict[str, Any]) -> str:
    completed = overall.get("comparable_run_count", 0)
    if completed == 0:
        return "inconclusive: no completed comparable runs"
    half = completed / 2
    top1_count = overall.get("top1_positive_count", 0)
    pairwise_count = overall.get("pairwise_positive_count", 0)
    regret_count = overall.get("regret_decrease_count", 0)
    heldout_count = overall.get("heldout_support_count", 0)
    arena_completed = overall.get("arena_completed_count", 0)
    arena_non_negative_count = overall.get("arena_non_negative_count", 0)
    arena_ok = arena_completed == 0 or arena_non_negative_count > arena_completed / 2

    if pairwise_count <= half or regret_count <= half:
        return "negative local signal: pairwise/regret did not improve on most completed runs"
    if top1_count > half and heldout_count > half and arena_ok:
        return (
            "robust local decision-leverage signal: scale roots, repeat arenas, "
            "then compare against v1 and exact-root v2 before production validation planning"
        )
    if pairwise_count == completed and regret_count == completed and heldout_count > half:
        return (
            "positive decision-leverage signal with mixed top1/tie-aware: keep scaling roots "
            "and arenas before considering a ranking objective"
        )
    return (
        "inconclusive: full-set pairwise/regret are positive on most runs, but held-out, "
        "top1, or arena repeatability is not yet consistent"
    )


def aggregate(runs: list[dict[str, Any]]) -> dict[str, Any]:
    completed = comparable_runs(runs)
    by_root_count = {
        str(root_count): summarize_group([run for run in runs if run.get("root_count") == root_count])
        for root_count in sorted({run.get("root_count") for run in runs if isinstance(run.get("root_count"), int)})
    }
    top1_positive_count = sum(
        1
        for run in completed
        if isinstance(run.get("deltas"), dict)
        and numeric(run["deltas"], "top1_accuracy") is not None
        and numeric(run["deltas"], "top1_accuracy") > 0
    )
    pairwise_positive_count = sum(
        1
        for run in completed
        if isinstance(run.get("deltas"), dict)
        and numeric(run["deltas"], "pairwise_accuracy") is not None
        and numeric(run["deltas"], "pairwise_accuracy") > 0
    )
    regret_decrease_count = sum(
        1
        for run in completed
        if isinstance(run.get("deltas"), dict)
        and numeric(run["deltas"], "mean_teacher_regret") is not None
        and numeric(run["deltas"], "mean_teacher_regret") < 0
    )
    overall = summarize_group(runs)
    overall.update(
        {
            "top1_positive_count": top1_positive_count,
            "pairwise_positive_count": pairwise_positive_count,
            "regret_decrease_count": regret_decrease_count,
        }
    )
    overall["recommendation"] = recommendation(overall)
    return {
        "overall": overall,
        "by_root_count": by_root_count,
        "heldout_consistently_supports_full_set": overall.get("heldout_support_count", 0)
        > max(0, overall.get("completed_run_count", 0)) / 2,
        "arena_direction_consistently_non_negative": overall.get("arena_completed_count", 0) == 0
        or overall.get("arena_non_negative_count", 0) > overall.get("arena_completed_count", 0) / 2,
    }


def format_number(value: Any, digits: int = 6) -> str:
    if value is None:
        return "n/a"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return f"{value:.{digits}f}"
    return str(value)


def run_delta(run: dict[str, Any], metric: str) -> Any:
    deltas = run.get("deltas")
    return None if not isinstance(deltas, dict) else deltas.get(metric)


def run_heldout_delta(run: dict[str, Any], metric: str) -> Any:
    heldout = run.get("heldout_validation_test")
    if not isinstance(heldout, dict):
        return None
    deltas = heldout.get("deltas")
    return None if not isinstance(deltas, dict) else deltas.get(metric)


def write_summary(path: Path, report: dict[str, Any]) -> None:
    aggregate_data = report["aggregate"]
    lines = [
        "# Move-Teacher Campaign Matrix",
        "",
        "Local-only decision-leverage diagnostic. Not Elo, not self-play, not a production strength claim, and not a publication gate.",
        "",
        "## Matrix",
        "",
        f"- root counts: {', '.join(str(value) for value in report['root_counts'])}",
        f"- seeds: {', '.join(str(value) for value in report['seeds'])}",
        f"- trainer modes: {', '.join(report['trainer_modes'])}",
        f"- pattern set: {report['pattern_set']}",
        "",
        "## Per Run",
        "",
        "| Run | Roots | Seed | Top1 d | Tie-aware d | Top2 d | Pairwise d | Mean regret d | Held-out top2 d | Held-out pairwise d | Held-out regret d | Arena score | Status |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for run in report["runs"]:
        arena = run.get("arena") if isinstance(run.get("arena"), dict) else {}
        lines.append(
            "| "
            + " | ".join(
                [
                    str(run.get("run_id")),
                    format_number(run.get("selected_roots")),
                    format_number(run.get("seed")),
                    format_number(run_delta(run, "top1_accuracy")),
                    format_number(run_delta(run, "top1_tie_aware_accuracy")),
                    format_number(run_delta(run, "best_move_in_top2_rate")),
                    format_number(run_delta(run, "pairwise_accuracy")),
                    format_number(run_delta(run, "mean_teacher_regret")),
                    format_number(run_heldout_delta(run, "best_move_in_top2_rate")),
                    format_number(run_heldout_delta(run, "pairwise_accuracy")),
                    format_number(run_heldout_delta(run, "mean_teacher_regret")),
                    format_number(arena.get("candidate_score_rate") if isinstance(arena, dict) else None),
                    str(run.get("status")),
                ]
            )
            + " |"
        )
    lines.extend(
        [
            "",
            "## Aggregate By Root Count",
            "",
            "| Roots | Runs | Positive seeds | Negative seeds | Mean top1 d | Median top1 d | Mean pairwise d | Median pairwise d | Mean regret d | Median regret d | Held-out support | Arena non-negative |",
            "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
    )
    for root_count, data in aggregate_data["by_root_count"].items():
        lines.append(
            "| "
            + " | ".join(
                [
                    root_count,
                    format_number(data.get("completed_run_count")),
                    format_number(data.get("positive_seed_count")),
                    format_number(data.get("negative_seed_count")),
                    format_number(data.get("top1_accuracy_delta_mean")),
                    format_number(data.get("top1_accuracy_delta_median")),
                    format_number(data.get("pairwise_accuracy_delta_mean")),
                    format_number(data.get("pairwise_accuracy_delta_median")),
                    format_number(data.get("mean_teacher_regret_delta_mean")),
                    format_number(data.get("mean_teacher_regret_delta_median")),
                    format_number(data.get("heldout_support_count")),
                    f"{format_number(data.get('arena_non_negative_count'))}/{format_number(data.get('arena_completed_count'))}",
                ]
            )
            + " |"
        )
    overall = aggregate_data["overall"]
    lines.extend(
        [
            "",
            "## Interpretation",
            "",
            f"- completed runs: {overall.get('completed_run_count')}/{overall.get('run_count')}",
            f"- top1 positive runs: {overall.get('top1_positive_count')}",
            f"- pairwise positive runs: {overall.get('pairwise_positive_count')}",
            f"- mean regret decrease runs: {overall.get('regret_decrease_count')}",
            f"- held-out support runs: {overall.get('heldout_support_count')}",
            f"- arena non-negative runs: {overall.get('arena_non_negative_count')}/{overall.get('arena_completed_count')}",
            f"- recommendation: {overall.get('recommendation')}",
            "",
            "## Non-Claims",
            "",
            "This matrix does not claim engine strength, Elo, self-play improvement, production readiness, publication readiness, or license clearance for generated artifacts.",
        ]
    )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    try:
        if not args.dry_run:
            preflight(args)
        args.output_dir.mkdir(parents=True, exist_ok=True)
        runs: list[dict[str, Any]] = []
        for root_count in args.root_counts:
            for seed in args.seeds:
                for mode in args.trainer_mode:
                    run_dir = args.output_dir / run_dir_name(root_count, seed, mode, len(args.trainer_mode))
                    command = campaign_command(args, run_dir, root_count, seed, mode)
                    if args.dry_run:
                        print(f"planned: {shell_join(command)}", flush=True)
                        returncode: int | None = None
                    else:
                        run_dir.mkdir(parents=True, exist_ok=True)
                        returncode = run_campaign(command)
                    summary = run_summary(
                        run_dir,
                        root_count,
                        seed,
                        mode,
                        command,
                        returncode,
                        dry_run=args.dry_run,
                    )
                    runs.append(summary)
                    if returncode not in (None, 0) and not args.keep_going:
                        break
                if runs and runs[-1].get("status") == "failed" and not args.keep_going:
                    break
            if runs and runs[-1].get("status") == "failed" and not args.keep_going:
                break

        report = {
            "schema_version": 1,
            "created_at_utc": datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
            "normalized_input_path": report_path(args.normalized_tsv),
            "output_dir": report_path(args.output_dir),
            "root_counts": args.root_counts,
            "seeds": args.seeds,
            "max_empty": args.max_empty,
            "pattern_set": args.pattern_set,
            "trainer_modes": args.trainer_mode,
            "epochs": args.epochs,
            "learning_rate": args.learning_rate,
            "lr_schedule": args.lr_schedule,
            "weight_decay": args.weight_decay,
            "arena": {
                "enabled": args.arena_baseline_weights is not None,
                "max_positions": args.arena_max_positions,
                "depth": args.arena_depth,
                "side_swap": args.arena_side_swap,
            },
            "move_teacher_cache": {
                "enabled": args.move_teacher_cache_dir is not None,
                "cache_dir": report_path(args.move_teacher_cache_dir),
                "reuse": args.reuse_move_teacher_cache,
                "write": args.write_move_teacher_cache,
                "allow_cache_miss_solve": args.allow_cache_miss_solve,
                "partial_miss_solve": "not_implemented",
            },
            "runs": runs,
            "aggregate": aggregate(runs),
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
        report_path_json = args.output_dir / "matrix-report.json"
        summary_path = args.output_dir / "matrix-summary.md"
        report_path_json.write_text(stable_json(report), encoding="utf-8")
        write_summary(summary_path, report)
        print(f"matrix_report={report_path_json}")
        print(f"matrix_summary={summary_path}")
        if any(run.get("status") == "failed" for run in runs):
            return 1
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
