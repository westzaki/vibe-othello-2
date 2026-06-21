#!/usr/bin/env python3
"""Run a bounded local-only move-teacher decision-leverage campaign."""

from __future__ import annotations

import argparse
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


def load_json(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise RuntimeError(f"JSON root must be an object: {path}")
    return data


def run_or_fail(command: list[str], stdout_path: Path | None = None) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        raise RuntimeError(f"command failed: {' '.join(command)}")
    if stdout_path is not None:
        stdout_path.write_text(result.stdout, encoding="utf-8")
    return result


def should_skip(args: argparse.Namespace, outputs: list[Path]) -> bool:
    return args.resume and all(path.exists() for path in outputs)


def run_stage(
    args: argparse.Namespace,
    name: str,
    command: list[str],
    outputs: list[Path],
    stages: dict[str, dict[str, Any]],
    stdout_path: Path | None = None,
) -> None:
    stages[name] = {
        "command": [Path(part).name if "/" in part else part for part in command],
        "outputs": [report_path(path) for path in outputs],
    }
    if should_skip(args, outputs):
        stages[name]["status"] = "skipped-resume"
        return
    run_or_fail(command, stdout_path=stdout_path)
    stages[name]["status"] = "ok"


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
    run_stage(args, "train_child_value_artifact", command, [weights_json, report], stages)


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
    run_stage(args, "bounded_late_game_arena", command, [arena_report, arena_summary], stages)


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
        "mean_teacher_regret_delta": trained.get("mean_teacher_regret") - previous.get("mean_teacher_regret")
        if isinstance(trained.get("mean_teacher_regret"), (int, float))
        and isinstance(previous.get("mean_teacher_regret"), (int, float))
        else None,
        "interpretation": "positive top1/pairwise deltas and negative regret delta indicate better decision leverage",
    }


def write_summary(path: Path, report: dict[str, Any]) -> None:
    previous = report["ranking"].get("previous")
    trained = report["ranking"].get("move_teacher_child_value")
    comparison = report["ranking"].get("comparison", {})
    arena = report.get("arena")
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
        )
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
                "average_disc_diff": arena_data.get("average_disc_diff"),
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
            },
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
