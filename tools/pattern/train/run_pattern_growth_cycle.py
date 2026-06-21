#!/usr/bin/env python3
"""Run a local-only pattern-learning growth cycle."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import shlex
import statistics
import subprocess
import sys
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


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
DECISION_CATEGORIES = (
    "promote_to_larger_local_validation",
    "hold_for_more_data",
    "inconclusive",
    "negative",
    "needs_rank_objective",
    "needs_feature_capacity",
    "needs_search_depth_validation",
)
LOCAL_ONLY_WARNINGS = (
    "local-only growth-cycle diagnostic",
    "generated labels, datasets, weights, artifacts, raw reports, and logs must not be committed",
    "not an Elo result",
    "not self-play",
    "not a production strength claim",
    "not a publication gate",
)


class GrowthCycleError(RuntimeError):
    """Raised for expected local input and tool failures."""


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def stable_json(data: Any) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def load_json(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise GrowthCycleError(f"JSON root must be an object: {path}")
    return data


def write_json(path: Path, data: Any) -> None:
    path.write_text(stable_json(data), encoding="utf-8")


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


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--normalized-tsv", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--pattern-set", default="pattern-v2-endgame-lite")
    parser.add_argument("--max-empty", type=int, default=12)
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
    parser.add_argument(
        "--arena-depths",
        default="3,5",
        type=lambda text: parse_int_list(text, "--arena-depths", allow_zero=False),
    )
    parser.add_argument(
        "--arena-seeds",
        default="0,10,20",
        type=lambda text: parse_int_list(text, "--arena-seeds", allow_zero=True),
    )
    parser.add_argument("--arena-max-positions", type=int, default=1000)
    parser.add_argument("--trainer-mode", choices=("pattern-sgd-v0c", "pattern-sgd-v0d"), default="pattern-sgd-v0c")
    parser.add_argument("--epochs", type=int, default=8)
    parser.add_argument("--learning-rate", type=float, default=0.1)
    parser.add_argument("--lr-schedule", choices=("constant", "inverse-sqrt"), default="inverse-sqrt")
    parser.add_argument("--weight-decay", type=float, default=0.0001)
    parser.add_argument("--dataset-output-format", choices=("compact-tsv", "expanded-tsv"), default="compact-tsv")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--keep-going", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--skip-arenas", action="store_true")
    parser.add_argument("--created-at-utc")

    parser.add_argument("--baseline-root-label-weights", type=Path)
    parser.add_argument("--baseline-root-label-manifest", type=Path)
    parser.add_argument("--baseline-root-label-name", default="exact-root-v2")
    parser.add_argument("--baseline-root-label-pattern-set")
    parser.add_argument("--baseline-v1-weights", type=Path)
    parser.add_argument("--baseline-v1-manifest", type=Path)
    parser.add_argument("--baseline-v1-name", default="pattern-v1-buro-lite")

    parser.add_argument("--matrix-report", type=Path)
    parser.add_argument(
        "--matrix-helper",
        type=Path,
        default=root / "tools/pattern/labels/run_move_teacher_campaign_matrix.py",
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
    args = parser.parse_args()

    if args.max_empty < 0 or args.max_empty > 64:
        parser.error("--max-empty must be in [0, 64]")
    if args.arena_max_positions <= 0:
        parser.error("--arena-max-positions must be positive")
    if args.epochs < 0:
        parser.error("--epochs must be non-negative")
    if args.learning_rate < 0.0:
        parser.error("--learning-rate must be non-negative")
    if args.weight_decay < 0.0:
        parser.error("--weight-decay must be non-negative")
    return args


def sanitize_path(path: Path | None, role: str = "local-path") -> str | None:
    if path is None:
        return None
    if path.is_absolute():
        return f"<{role}>/{path.name}"
    return str(path)


def command_path(path: Path) -> str:
    return path.name if path.is_absolute() else str(path)


def command_for_report(command: list[str]) -> list[str]:
    return [command_path(Path(part)) if "/" in part else part for part in command]


def shell_join(command: list[str]) -> str:
    return " ".join(shlex.quote(part) for part in command)


def sha256_file(path: Path, cache: dict[Path, str] | None = None) -> str:
    resolved = path.resolve()
    if cache is not None and resolved in cache:
        return cache[resolved]
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            digest.update(chunk)
    value = f"sha256:{digest.hexdigest()}"
    if cache is not None:
        cache[resolved] = value
    return value


def artifact_metadata(path: Path | None, manifest: Path | None, name: str, cache: dict[Path, str]) -> dict[str, Any]:
    if path is None or manifest is None:
        return {
            "available": False,
            "name": name,
            "weights": None,
            "manifest": None,
            "reason": "artifact path was not supplied",
        }
    if not path.is_file() or not manifest.is_file():
        return {
            "available": False,
            "name": name,
            "weights": sanitize_path(path, "artifact"),
            "manifest": sanitize_path(manifest, "artifact"),
            "reason": "artifact file is missing",
        }
    return {
        "available": True,
        "name": name,
        "weights": {
            "path": sanitize_path(path, "artifact"),
            "size_bytes": path.stat().st_size,
            "sha256": sha256_file(path, cache),
        },
        "manifest": {
            "path": sanitize_path(manifest, "artifact"),
            "size_bytes": manifest.stat().st_size,
            "sha256": sha256_file(manifest, cache),
        },
    }


def repo_commit_sha() -> str | None:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            check=False,
            capture_output=True,
            text=True,
            cwd=repo_root(),
        )
    except OSError:
        return None
    if result.returncode != 0:
        return None
    return result.stdout.strip() or None


def growth_cycle_command_template() -> str:
    return "\n".join(
        [
            "python3 tools/pattern/train/run_pattern_growth_cycle.py \\",
            "  --normalized-tsv \"$VIBE_OTHELLO_MEASUREMENTS/<connected-or-low-empty-normalized.tsv>\" \\",
            "  --output-dir \"$VIBE_OTHELLO_MEASUREMENTS/<pattern-growth-cycle-run>\" \\",
            "  --pattern-set pattern-v2-endgame-lite \\",
            "  --baseline-root-label-weights \"$VIBE_OTHELLO_MEASUREMENTS/<exact-root-v2.weights.bin>\" \\",
            "  --baseline-root-label-manifest \"$VIBE_OTHELLO_MEASUREMENTS/<exact-root-v2.manifest.json>\" \\",
            "  --baseline-v1-weights \"$VIBE_OTHELLO_MEASUREMENTS/<v1.weights.bin>\" \\",
            "  --baseline-v1-manifest \"$VIBE_OTHELLO_MEASUREMENTS/<v1.manifest.json>\" \\",
            "  --root-counts 5000,10000,20000 \\",
            "  --seeds 0,1,2 \\",
            "  --arena-depths 3,5 \\",
            "  --arena-seeds 0,10,20 \\",
            "  --arena-max-positions 1000 \\",
            "  --resume",
        ]
    )


def exact_root_baseline_template() -> str:
    return "\n".join(
        [
            "python3 tools/pattern/labels/run_exact_teacher_late_phase_campaign.py \\",
            "  --normalized-tsv \"$VIBE_OTHELLO_MEASUREMENTS/<connected-or-low-empty-normalized.tsv>\" \\",
            "  --output-dir \"$VIBE_OTHELLO_MEASUREMENTS/<exact-root-v2-run>\" \\",
            "  --pattern-set pattern-v2-endgame-lite \\",
            "  --trainer-mode pattern-sgd-v0c \\",
            "  --resume",
        ]
    )


def v1_baseline_template() -> str:
    return "\n".join(
        [
            "python3 tools/pattern/labels/run_exact_teacher_late_phase_campaign.py \\",
            "  --normalized-tsv \"$VIBE_OTHELLO_MEASUREMENTS/<connected-or-low-empty-normalized.tsv>\" \\",
            "  --output-dir \"$VIBE_OTHELLO_MEASUREMENTS/<v1-baseline-run>\" \\",
            "  --pattern-set pattern-v1-buro-lite \\",
            "  --trainer-mode pattern-sgd-v0c \\",
            "  --resume",
        ]
    )


def build_tool_template() -> str:
    return "cmake --build build"


def count_low_empty_roots(path: Path, max_empty: int) -> dict[str, Any]:
    unique_board_ids: set[str] = set()
    input_rows = 0
    eligible_rows = 0
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if reader.fieldnames is None:
            raise GrowthCycleError(f"normalized TSV is missing a header: {path}")
        required = {"board_id", "empty_count"}
        missing = sorted(required - set(reader.fieldnames))
        if missing:
            raise GrowthCycleError(f"normalized TSV is missing required columns {missing}: {path}")
        for row in reader:
            input_rows += 1
            try:
                empty_count = int(row["empty_count"])
            except (TypeError, ValueError) as error:
                raise GrowthCycleError(f"invalid empty_count at data row {input_rows}: {row.get('empty_count')!r}") from error
            if empty_count <= max_empty:
                eligible_rows += 1
                board_id = row["board_id"]
                if board_id:
                    unique_board_ids.add(board_id)
    return {
        "input_rows": input_rows,
        "eligible_low_empty_rows": eligible_rows,
        "available_unique_low_empty_roots": len(unique_board_ids),
    }


def select_root_counts(requested: list[int], available: int) -> tuple[list[int], list[dict[str, Any]]]:
    selected: list[int] = []
    seen: set[int] = set()
    decisions: list[dict[str, Any]] = []
    for value in requested:
        if value <= available:
            chosen = value
            status = "selected"
        else:
            chosen = available
            status = "downgraded-to-available"
        if chosen <= 0:
            decisions.append({"requested": value, "selected": None, "status": "unavailable"})
            continue
        if chosen in seen:
            decisions.append({"requested": value, "selected": chosen, "status": f"{status}-deduplicated"})
            continue
        seen.add(chosen)
        selected.append(chosen)
        decisions.append({"requested": value, "selected": chosen, "status": status})
    return selected, decisions


def missing_file(path: Path | None) -> bool:
    return path is None or not path.is_file()


def preflight(args: argparse.Namespace, cache: dict[Path, str]) -> tuple[dict[str, Any], list[dict[str, Any]], list[int]]:
    missing: list[dict[str, Any]] = []
    if missing_file(args.normalized_tsv):
        missing.append(
            {
                "input": "normalized-tsv",
                "path": sanitize_path(args.normalized_tsv, "normalized-tsv"),
                "template": growth_cycle_command_template(),
            }
        )
    if missing_file(args.matrix_helper):
        missing.append(
            {
                "input": "matrix-helper",
                "path": sanitize_path(args.matrix_helper, "repo-tool"),
                "template": "use the checked-in tools/pattern/labels/run_move_teacher_campaign_matrix.py",
            }
        )
    if args.matrix_report is None:
        for label, path in (
            ("move-teacher generator", args.generator),
            ("pattern dataset executable", args.dataset_exe),
            ("pattern trainer", args.trainer),
            ("pattern exporter", args.exporter),
            ("move-teacher ranking evaluator", args.ranking_evaluator),
        ):
            if missing_file(path):
                missing.append(
                    {
                        "input": label,
                        "path": sanitize_path(path, "repo-tool"),
                        "template": build_tool_template(),
                    }
                )
        if missing_file(args.baseline_root_label_weights) or missing_file(args.baseline_root_label_manifest):
            missing.append(
                {
                    "input": "baseline-root-label-artifact",
                    "weights": sanitize_path(args.baseline_root_label_weights, "artifact"),
                    "manifest": sanitize_path(args.baseline_root_label_manifest, "artifact"),
                    "template": exact_root_baseline_template(),
                }
            )
    if not args.skip_arenas:
        if missing_file(args.baseline_v1_weights) or missing_file(args.baseline_v1_manifest):
            missing.append(
                {
                    "input": "baseline-v1-artifact",
                    "weights": sanitize_path(args.baseline_v1_weights, "artifact"),
                    "manifest": sanitize_path(args.baseline_v1_manifest, "artifact"),
                    "template": v1_baseline_template(),
                }
            )
        if missing_file(args.arena_exe):
            missing.append(
                {
                    "input": "pattern-artifact-arena",
                    "path": sanitize_path(args.arena_exe, "repo-tool"),
                    "template": build_tool_template(),
                }
            )

    root_count_data: dict[str, Any] = {
        "input_rows": None,
        "eligible_low_empty_rows": None,
        "available_unique_low_empty_roots": None,
    }
    selected_counts: list[int] = []
    root_count_decisions: list[dict[str, Any]] = []
    if args.normalized_tsv.is_file():
        root_count_data = count_low_empty_roots(args.normalized_tsv, args.max_empty)
        selected_counts, root_count_decisions = select_root_counts(
            args.root_counts, int(root_count_data["available_unique_low_empty_roots"])
        )
        if not selected_counts:
            missing.append(
                {
                    "input": "low-empty-roots",
                    "available_unique_low_empty_roots": root_count_data["available_unique_low_empty_roots"],
                    "template": "generate or select a normalized schema v2 TSV with low-empty roots before rerunning",
                }
            )

    data = {
        "status": "failed" if missing else "ok",
        "normalized_tsv": {
            "path": sanitize_path(args.normalized_tsv, "normalized-tsv"),
            **root_count_data,
            "sha256": sha256_file(args.normalized_tsv, cache) if args.normalized_tsv.is_file() else None,
        },
        "requested_root_counts": args.root_counts,
        "selected_root_counts": selected_counts,
        "root_count_decisions": root_count_decisions,
        "required_inputs_missing": missing,
        "command_template": growth_cycle_command_template() if missing else None,
    }
    return data, missing, selected_counts


def baseline_inventory(args: argparse.Namespace, cache: dict[Path, str], preflight_data: dict[str, Any]) -> dict[str, Any]:
    return {
        "status": "ok",
        "normalized_tsv": preflight_data["normalized_tsv"],
        "requested_root_counts": args.root_counts,
        "selected_root_counts": preflight_data.get("selected_root_counts", []),
        "baseline_root_label": artifact_metadata(
            args.baseline_root_label_weights,
            args.baseline_root_label_manifest,
            args.baseline_root_label_name,
            cache,
        ),
        "baseline_v1": artifact_metadata(args.baseline_v1_weights, args.baseline_v1_manifest, args.baseline_v1_name, cache),
        "pattern_set": args.pattern_set,
        "trainer_mode": args.trainer_mode,
        "arena": {
            "enabled": not args.skip_arenas,
            "depths": args.arena_depths,
            "seeds": args.arena_seeds,
            "max_positions": args.arena_max_positions,
            "side_swap": True,
        },
        "commit_sha": repo_commit_sha(),
    }


def matrix_command(args: argparse.Namespace, matrix_dir: Path, root_counts: list[int]) -> list[str]:
    command = [
        sys.executable,
        str(args.matrix_helper),
        "--normalized-tsv",
        str(args.normalized_tsv),
        "--output-dir",
        str(matrix_dir),
        "--root-counts",
        ",".join(str(value) for value in root_counts),
        "--seeds",
        ",".join(str(value) for value in args.seeds),
        "--max-empty",
        str(args.max_empty),
        "--pattern-set",
        args.pattern_set,
        "--trainer-mode",
        args.trainer_mode,
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
        "--previous-weights",
        str(args.baseline_root_label_weights),
        "--previous-manifest",
        str(args.baseline_root_label_manifest),
        "--previous-pattern-set",
        args.baseline_root_label_pattern_set or args.pattern_set,
        "--previous-name",
        args.baseline_root_label_name,
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
    if args.resume:
        command.append("--resume")
    if args.keep_going:
        command.append("--keep-going")
    return command


def run_or_reuse_matrix(args: argparse.Namespace, root_counts: list[int]) -> tuple[dict[str, Any] | None, dict[str, Any]]:
    matrix_dir = args.output_dir / "decision-leverage-matrix"
    report_path = args.matrix_report or (matrix_dir / "matrix-report.json")
    if args.dry_run:
        command = matrix_command(args, matrix_dir, root_counts)
        return None, {
            "status": "planned",
            "report": sanitize_path(report_path, "growth-output"),
            "summary": sanitize_path(matrix_dir / "matrix-summary.md", "growth-output"),
            "command": command_for_report(command),
        }
    if args.matrix_report is not None:
        if not args.matrix_report.is_file():
            raise GrowthCycleError(f"missing explicit matrix report: {args.matrix_report}")
        return load_json(args.matrix_report), {
            "status": "reused-explicit-matrix-report",
            "report": sanitize_path(args.matrix_report, "matrix-report"),
            "summary": None,
            "command": None,
        }
    had_existing_report = report_path.is_file()
    matrix_dir.mkdir(parents=True, exist_ok=True)
    command = matrix_command(args, matrix_dir, root_counts)
    print(f"running decision-leverage matrix: {shell_join(command)}", flush=True)
    completed = subprocess.run(command, check=False)
    if completed.returncode != 0:
        raise GrowthCycleError(f"decision-leverage matrix failed with exit code {completed.returncode}")
    if not report_path.is_file():
        raise GrowthCycleError(f"decision-leverage matrix did not write report: {report_path}")
    return load_json(report_path), {
        "status": "resume-validated-by-matrix-helper" if args.resume and had_existing_report else "ok",
        "report": sanitize_path(report_path, "growth-output"),
        "summary": sanitize_path(matrix_dir / "matrix-summary.md", "growth-output"),
        "command": command_for_report(command),
    }


def numeric(mapping: dict[str, Any] | None, key: str) -> float | int | None:
    if not isinstance(mapping, dict):
        return None
    value = mapping.get(key)
    return value if isinstance(value, (int, float)) else None


def get_delta(run: dict[str, Any], key: str) -> float | None:
    deltas = run.get("deltas")
    value = numeric(deltas if isinstance(deltas, dict) else None, key)
    return float(value) if value is not None else None


def get_heldout_delta(run: dict[str, Any], key: str) -> float | None:
    heldout = run.get("heldout_validation_test")
    if not isinstance(heldout, dict):
        return None
    deltas = heldout.get("deltas")
    value = numeric(deltas if isinstance(deltas, dict) else None, key)
    return float(value) if value is not None else None


def completed_comparable_runs(matrix_report: dict[str, Any] | None) -> list[dict[str, Any]]:
    if not isinstance(matrix_report, dict):
        return []
    runs = matrix_report.get("runs")
    if not isinstance(runs, list):
        return []
    return [
        run
        for run in runs
        if isinstance(run, dict) and run.get("status") == "ok" and isinstance(run.get("deltas"), dict)
    ]


def run_supports_decision_leverage(run: dict[str, Any]) -> bool:
    top1 = get_delta(run, "top1_accuracy")
    pairwise = get_delta(run, "pairwise_accuracy")
    regret = get_delta(run, "mean_teacher_regret")
    return top1 is not None and top1 > 0 and pairwise is not None and pairwise > 0 and regret is not None and regret < 0


def run_is_negative(run: dict[str, Any]) -> bool:
    pairwise = get_delta(run, "pairwise_accuracy")
    regret = get_delta(run, "mean_teacher_regret")
    return (pairwise is not None and pairwise <= 0) or (regret is not None and regret >= 0)


def heldout_supports(run: dict[str, Any]) -> bool:
    top2 = get_heldout_delta(run, "best_move_in_top2_rate")
    pairwise = get_heldout_delta(run, "pairwise_accuracy")
    regret = get_heldout_delta(run, "mean_teacher_regret")
    return top2 is not None and top2 > 0 and pairwise is not None and pairwise > 0 and regret is not None and regret < 0


def all_same_ratio(run: dict[str, Any]) -> float | None:
    trained = run.get("trained")
    root_count = numeric(trained if isinstance(trained, dict) else None, "root_count")
    all_same = numeric(trained if isinstance(trained, dict) else None, "roots_with_all_moves_same_predicted_score")
    if not root_count or all_same is None:
        return None
    return float(all_same) / float(root_count)


def static_score_range_width(run: dict[str, Any]) -> float | None:
    trained = run.get("trained")
    if not isinstance(trained, dict):
        return None
    score_range = trained.get("static_score_range")
    if not isinstance(score_range, dict):
        return None
    low = numeric(score_range, "min")
    high = numeric(score_range, "max")
    if low is None or high is None:
        return None
    return float(high) - float(low)


def mean_or_none(values: list[float]) -> float | None:
    return statistics.fmean(values) if values else None


def median_or_none(values: list[float]) -> float | None:
    return statistics.median(values) if values else None


def summarize_metric(runs: list[dict[str, Any]], metric: str) -> dict[str, Any]:
    values = [value for run in runs if (value := get_delta(run, metric)) is not None]
    return {
        "count": len(values),
        "positive_count": sum(1 for value in values if value > 0),
        "negative_count": sum(1 for value in values if value < 0),
        "zero_count": sum(1 for value in values if value == 0),
        "mean": mean_or_none(values),
        "median": median_or_none(values),
        "min": min(values) if values else None,
        "max": max(values) if values else None,
    }


def summarize_decision_leverage(matrix_report: dict[str, Any] | None) -> dict[str, Any]:
    runs = completed_comparable_runs(matrix_report)
    run_count = len(runs)
    root_counts = sorted({run.get("root_count") for run in runs if isinstance(run.get("root_count"), int)})
    by_root_count = {
        str(root_count): {
            "run_count": len(group),
            "positive_count": sum(1 for run in group if run_supports_decision_leverage(run)),
            "heldout_support_count": sum(1 for run in group if heldout_supports(run)),
            "pairwise_delta": summarize_metric(group, "pairwise_accuracy"),
            "mean_regret_delta": summarize_metric(group, "mean_teacher_regret"),
        }
        for root_count in root_counts
        for group in [[run for run in runs if run.get("root_count") == root_count]]
    }
    top1_positive = sum(1 for run in runs if (value := get_delta(run, "top1_accuracy")) is not None and value > 0)
    pairwise_positive = sum(
        1 for run in runs if (value := get_delta(run, "pairwise_accuracy")) is not None and value > 0
    )
    regret_decrease = sum(
        1 for run in runs if (value := get_delta(run, "mean_teacher_regret")) is not None and value < 0
    )
    top2_positive = sum(
        1 for run in runs if (value := get_delta(run, "best_move_in_top2_rate")) is not None and value > 0
    )
    heldout_support = sum(1 for run in runs if heldout_supports(run))
    negative = sum(1 for run in runs if run_is_negative(run))
    high_all_same = sum(1 for run in runs if (ratio := all_same_ratio(run)) is not None and ratio >= 0.15)
    compressed = sum(1 for run in runs if (width := static_score_range_width(run)) is not None and width <= 2.0)
    most_threshold = run_count / 2.0
    gate_passed = (
        run_count > 0
        and top1_positive > most_threshold
        and pairwise_positive > most_threshold
        and regret_decrease > most_threshold
        and heldout_support > most_threshold
    )
    stable = run_count > 0 and negative == 0 and all(
        data["positive_count"] > data["run_count"] / 2.0 and data["heldout_support_count"] > data["run_count"] / 2.0
        for data in by_root_count.values()
    )
    return {
        "status": "ok" if run_count else "missing",
        "completed_comparable_runs": run_count,
        "root_counts": root_counts,
        "top1_positive_count": top1_positive,
        "top2_positive_count": top2_positive,
        "pairwise_positive_count": pairwise_positive,
        "regret_decrease_count": regret_decrease,
        "heldout_support_count": heldout_support,
        "negative_pairwise_or_regret_count": negative,
        "high_all_same_count": high_all_same,
        "compressed_static_score_range_count": compressed,
        "gate_passed": gate_passed,
        "stable_across_root_counts_and_seeds": stable,
        "metrics": {metric: summarize_metric(runs, metric) for metric in RANKING_METRICS},
        "by_root_count": by_root_count,
    }


def resolve_run_file(base: Path, value: Any, default_name: str) -> Path:
    if isinstance(value, str) and value:
        path = Path(value)
        return path if path.is_absolute() else base / path
    return base / default_name


def matrix_base_dir(args: argparse.Namespace) -> Path:
    if args.matrix_report is not None:
        return args.matrix_report.parent
    return args.output_dir / "decision-leverage-matrix"


def candidate_quality(run: dict[str, Any]) -> tuple[float, float, float]:
    pairwise = get_delta(run, "pairwise_accuracy")
    regret = get_delta(run, "mean_teacher_regret")
    top1 = get_delta(run, "top1_accuracy")
    return (
        pairwise if pairwise is not None else float("-inf"),
        -regret if regret is not None else float("-inf"),
        top1 if top1 is not None else float("-inf"),
    )


def extract_candidates(args: argparse.Namespace, matrix_report: dict[str, Any] | None) -> list[dict[str, Any]]:
    if matrix_report is None:
        return []
    base_dir = matrix_base_dir(args)
    result: list[dict[str, Any]] = []
    for run in completed_comparable_runs(matrix_report):
        run_id = run.get("run_id")
        if not isinstance(run_id, str):
            continue
        run_dir = base_dir / run_id
        campaign_report = run_dir / "campaign-report.json"
        if not campaign_report.exists():
            continue
        campaign = load_json(campaign_report)
        outputs = campaign.get("outputs") if isinstance(campaign.get("outputs"), dict) else {}
        weights = resolve_run_file(run_dir, outputs.get("trained_weights_bin"), "move-teacher-child.weights.bin")
        manifest = resolve_run_file(run_dir, outputs.get("trained_manifest"), "move-teacher-child.manifest.json")
        if not weights.exists() or not manifest.exists():
            continue
        result.append(
            {
                "run_id": run_id,
                "root_count": run.get("root_count"),
                "seed": run.get("seed"),
                "trainer_mode": run.get("trainer_mode"),
                "weights": weights,
                "manifest": manifest,
                "ranking_deltas": run.get("deltas"),
                "heldout_validation_test": run.get("heldout_validation_test"),
                "quality": candidate_quality(run),
            }
        )
    return sorted(
        result,
        key=lambda item: (
            item["root_count"] if isinstance(item.get("root_count"), int) else -1,
            item["quality"],
            -(item["seed"] if isinstance(item.get("seed"), int) else 0),
        ),
        reverse=True,
    )


def best_candidate(candidates: list[dict[str, Any]]) -> dict[str, Any] | None:
    if not candidates:
        return None
    return max(
        candidates,
        key=lambda item: (
            item["quality"],
            item["root_count"] if isinstance(item.get("root_count"), int) else -1,
        ),
    )


def selected_arena_candidates(candidates: list[dict[str, Any]]) -> list[dict[str, Any]]:
    if not candidates:
        return []
    root_counts = [item["root_count"] for item in candidates if isinstance(item.get("root_count"), int)]
    if not root_counts:
        return []
    largest_root = max(root_counts)
    selected: list[dict[str, Any]] = [item for item in candidates if item.get("root_count") == largest_root]
    for root_count in sorted(
        {
            item["root_count"]
            for item in candidates
            if isinstance(item.get("root_count"), int) and item.get("root_count") != largest_root
        }
    ):
        group = [item for item in candidates if item.get("root_count") == root_count]
        if group:
            selected.append(best_candidate(group) or group[0])
    return selected


def arena_depth_seed_pairs(args: argparse.Namespace, *, full: bool) -> list[tuple[int, int, str | None]]:
    shallow = min(args.arena_depths)
    first_seed = args.arena_seeds[0]
    pairs: list[tuple[int, int, str | None]] = []
    if full:
        for seed in args.arena_seeds:
            pairs.append((shallow, seed, None))
        for depth in args.arena_depths:
            if depth == shallow:
                continue
            pairs.append((depth, first_seed, "depth>shallow only runs first arena seed by bounded policy"))
    else:
        pairs.append((shallow, first_seed, "non-largest root-count candidate gets shallow first-seed arena only"))
    return pairs


def variant_key(*parts: Any) -> str:
    text = "-".join(str(part) for part in parts if part is not None)
    return "".join(char if char.isalnum() or char in ("-", "_") else "-" for char in text)


def arena_variant_plan(args: argparse.Namespace, candidates: list[dict[str, Any]], decision: dict[str, Any]) -> list[dict[str, Any]]:
    if args.skip_arenas:
        return []
    if not decision.get("gate_passed"):
        return []
    selected = selected_arena_candidates(candidates)
    if not selected:
        return []
    root_counts = [item["root_count"] for item in selected if isinstance(item.get("root_count"), int)]
    if not root_counts:
        return []
    largest_root = max(root_counts)
    best = best_candidate(selected)
    variants: list[dict[str, Any]] = []
    shallow = min(args.arena_depths)
    first_seed = args.arena_seeds[0]
    if best is not None:
        variants.append(
            {
                "comparison": "same_artifact_sanity",
                "candidate": best,
                "baseline_weights": best["weights"],
                "baseline_manifest": best["manifest"],
                "baseline_name": args.pattern_set,
                "depth": shallow,
                "arena_seed": first_seed,
                "required_sanity": True,
            }
        )
    if args.baseline_root_label_weights is not None and args.baseline_root_label_manifest is not None:
        for depth, arena_seed, skip_note in arena_depth_seed_pairs(args, full=True):
            variants.append(
                {
                    "comparison": "exact_root_v2_vs_v1",
                    "candidate": None,
                "candidate_weights": args.baseline_root_label_weights,
                "candidate_manifest": args.baseline_root_label_manifest,
                    "candidate_name": args.baseline_root_label_pattern_set or args.pattern_set,
                    "baseline_weights": args.baseline_v1_weights,
                    "baseline_manifest": args.baseline_v1_manifest,
                    "baseline_name": args.baseline_v1_name,
                    "depth": depth,
                    "arena_seed": arena_seed,
                    "skip_note": skip_note,
                }
            )
    for candidate in selected:
        full = candidate.get("root_count") == largest_root
        for depth, arena_seed, skip_note in arena_depth_seed_pairs(args, full=full):
            variants.append(
                {
                    "comparison": "move_teacher_v2_vs_v1",
                    "candidate": candidate,
                    "baseline_weights": args.baseline_v1_weights,
                    "baseline_manifest": args.baseline_v1_manifest,
                    "baseline_name": args.baseline_v1_name,
                    "depth": depth,
                    "arena_seed": arena_seed,
                    "skip_note": skip_note,
                }
            )
            if args.baseline_root_label_weights is not None and args.baseline_root_label_manifest is not None:
                variants.append(
                    {
                        "comparison": "move_teacher_v2_vs_exact_root_v2",
                        "candidate": candidate,
                        "baseline_weights": args.baseline_root_label_weights,
                        "baseline_manifest": args.baseline_root_label_manifest,
                        "baseline_name": args.baseline_root_label_pattern_set or args.pattern_set,
                        "depth": depth,
                        "arena_seed": arena_seed,
                        "skip_note": skip_note,
                    }
                )
    if best is not None:
        variants.append(
            {
                "comparison": "candidate_baseline_swap_sanity",
                "candidate": None,
                "candidate_weights": args.baseline_v1_weights,
                "candidate_manifest": args.baseline_v1_manifest,
                "candidate_name": args.baseline_v1_name,
                "baseline_weights": best["weights"],
                "baseline_manifest": best["manifest"],
                "baseline_name": args.pattern_set,
                "depth": shallow,
                "arena_seed": first_seed,
                "swap_of": best["run_id"],
                "required_sanity": True,
            }
        )
    return variants


def arena_command(args: argparse.Namespace, variant: dict[str, Any], report: Path, summary: Path) -> list[str]:
    candidate = variant.get("candidate")
    if isinstance(candidate, dict):
        candidate_weights = candidate["weights"]
        candidate_manifest = candidate["manifest"]
        candidate_name = args.pattern_set
    else:
        candidate_weights = variant["candidate_weights"]
        candidate_manifest = variant["candidate_manifest"]
        candidate_name = variant["candidate_name"]
    return [
        str(args.arena_exe),
        "--positions-tsv",
        str(args.normalized_tsv),
        "--candidate-weights",
        str(candidate_weights),
        "--candidate-manifest",
        str(candidate_manifest),
        "--candidate-name",
        str(candidate_name),
        "--baseline-weights",
        str(variant["baseline_weights"]),
        "--baseline-manifest",
        str(variant["baseline_manifest"]),
        "--baseline-name",
        str(variant["baseline_name"]),
        "--max-empty",
        str(args.max_empty),
        "--max-positions",
        str(args.arena_max_positions),
        "--seed",
        str(variant["arena_seed"]),
        "--side-swap",
        "--depth",
        str(variant["depth"]),
        "--report-out",
        str(report),
        "--summary-out",
        str(summary),
        "--progress-every",
        "100",
    ]


def resume_expected(command: list[str], inputs: list[Path], cache: dict[Path, str]) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "command": command_for_report(command),
        "inputs": [
            {
                "path": sanitize_path(path, "arena-input"),
                "size_bytes": path.stat().st_size,
                "sha256": sha256_file(path, cache),
            }
            for path in inputs
        ],
    }


def resume_complete(expected: dict[str, Any], outputs: list[Path], cache: dict[Path, str]) -> dict[str, Any]:
    data = dict(expected)
    data["outputs"] = [
        {
            "path": sanitize_path(path, "growth-output"),
            "size_bytes": path.stat().st_size,
            "sha256": sha256_file(path, cache),
        }
        for path in outputs
    ]
    return data


def first_mismatch(expected: Any, actual: Any, path: str = "$") -> str | None:
    if expected == actual:
        return None
    if isinstance(expected, dict) and isinstance(actual, dict):
        for key in sorted(set(expected) | set(actual)):
            if key not in expected:
                return f"{path}.{key} unexpected"
            if key not in actual:
                return f"{path}.{key} missing"
            mismatch = first_mismatch(expected[key], actual[key], f"{path}.{key}")
            if mismatch is not None:
                return mismatch
    if isinstance(expected, list) and isinstance(actual, list):
        if len(expected) != len(actual):
            return f"{path} length {len(actual)} != {len(expected)}"
        for index, (expected_item, actual_item) in enumerate(zip(expected, actual, strict=True)):
            mismatch = first_mismatch(expected_item, actual_item, f"{path}[{index}]")
            if mismatch is not None:
                return mismatch
    return f"{path} mismatch"


def arena_result_from_report(path: Path, variant: dict[str, Any], status: str) -> dict[str, Any]:
    if not path.exists():
        return {"status": status}
    report = load_json(path)
    candidate = variant.get("candidate")
    return {
        "status": status,
        "comparison": variant.get("comparison"),
        "run_id": candidate.get("run_id") if isinstance(candidate, dict) else None,
        "root_count": candidate.get("root_count") if isinstance(candidate, dict) else None,
        "training_seed": candidate.get("seed") if isinstance(candidate, dict) else None,
        "depth": variant.get("depth"),
        "arena_seed": variant.get("arena_seed"),
        "games_played": report.get("games_played"),
        "candidate_wins": report.get("candidate_wins"),
        "baseline_wins": report.get("baseline_wins"),
        "draws": report.get("draws"),
        "candidate_score_rate": report.get("candidate_score_rate"),
        "candidate_score_rate_interval_95": report.get("candidate_score_rate_interval_95"),
        "average_disc_diff_candidate_perspective": report.get("average_disc_diff_candidate_perspective"),
        "illegal_or_failed_games": report.get("illegal_or_failed_games"),
        "selected_positions": report.get("selected_positions"),
        "report": sanitize_path(path, "growth-output"),
        "interpretation": arena_interpretation(report),
    }


def arena_interpretation(report: dict[str, Any]) -> str:
    rate = numeric(report, "candidate_score_rate")
    failed = numeric(report, "illegal_or_failed_games") or 0
    if failed:
        return "failed games present; treat as sanity failure unless explained"
    if rate is None:
        return "missing score rate"
    if rate > 0.52:
        return "supportive bounded arena direction"
    if rate >= 0.5:
        return "non-negative bounded arena direction"
    if rate >= 0.49:
        return "close to neutral bounded arena direction"
    return "negative bounded arena direction"


def same_artifact_sanity_passes(result: dict[str, Any]) -> bool:
    rate = numeric(result, "candidate_score_rate")
    diff = numeric(result, "average_disc_diff_candidate_perspective")
    failed = numeric(result, "illegal_or_failed_games") or 0
    return failed == 0 and rate is not None and abs(float(rate) - 0.5) <= 0.02 and diff is not None and abs(float(diff)) <= 1.0


def run_arena_matrix(
    args: argparse.Namespace,
    variants: list[dict[str, Any]],
    cache: dict[Path, str],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]], dict[str, Any] | None]:
    arena_dir = args.output_dir / "arena-validation"
    results: list[dict[str, Any]] = []
    skipped: list[dict[str, Any]] = []
    early_stop: dict[str, Any] | None = None
    if args.dry_run:
        for variant in variants:
            candidate = variant.get("candidate")
            key = variant_key(
                variant.get("comparison"),
                candidate.get("run_id") if isinstance(candidate, dict) else variant.get("candidate_name"),
                "depth",
                variant.get("depth"),
                "arena-seed",
                variant.get("arena_seed"),
            )
            report_path = arena_dir / key / "arena-report.json"
            command = arena_command(args, variant, report_path, arena_dir / key / "arena-summary.md")
            results.append(
                {
                    "status": "planned",
                    "comparison": variant.get("comparison"),
                    "run_id": candidate.get("run_id") if isinstance(candidate, dict) else None,
                    "root_count": candidate.get("root_count") if isinstance(candidate, dict) else None,
                    "depth": variant.get("depth"),
                    "arena_seed": variant.get("arena_seed"),
                    "command": command_for_report(command),
                }
            )
        return results, skipped, None

    for variant in variants:
        candidate = variant.get("candidate")
        key = variant_key(
            variant.get("comparison"),
            candidate.get("run_id") if isinstance(candidate, dict) else variant.get("candidate_name"),
            "depth",
            variant.get("depth"),
            "arena-seed",
            variant.get("arena_seed"),
        )
        run_dir = arena_dir / key
        report_path = run_dir / "arena-report.json"
        summary_path = run_dir / "arena-summary.md"
        resume_path = run_dir / "arena.resume.json"
        command = arena_command(args, variant, report_path, summary_path)
        inputs = [
            args.normalized_tsv,
            Path(command[command.index("--candidate-weights") + 1]),
            Path(command[command.index("--candidate-manifest") + 1]),
            Path(command[command.index("--baseline-weights") + 1]),
            Path(command[command.index("--baseline-manifest") + 1]),
        ]
        expected = resume_expected(command, inputs, cache)
        if args.resume and report_path.exists() and summary_path.exists() and resume_path.exists():
            actual = load_json(resume_path)
            mismatch = first_mismatch(resume_complete(expected, [report_path, summary_path], cache), actual)
            if mismatch is not None:
                raise GrowthCycleError(f"arena resume metadata mismatch for {key} at {mismatch}")
            result = arena_result_from_report(report_path, variant, "skipped-resume-validated")
        else:
            run_dir.mkdir(parents=True, exist_ok=True)
            print(f"running arena: {shell_join(command)}", flush=True)
            completed = subprocess.run(command, check=False)
            if completed.returncode != 0:
                result = {
                    "status": "failed",
                    "comparison": variant.get("comparison"),
                    "returncode": completed.returncode,
                    "command": command_for_report(command),
                    "report": sanitize_path(report_path, "growth-output"),
                }
                results.append(result)
                if variant.get("required_sanity") or not args.keep_going:
                    early_stop = {
                        "stage": "arena_validation_matrix",
                        "reason": "arena command failed",
                        "variant": key,
                    }
                    break
                continue
            write_json(resume_path, resume_complete(expected, [report_path, summary_path], cache))
            result = arena_result_from_report(report_path, variant, "ok")
        results.append(result)
        if variant.get("comparison") == "same_artifact_sanity" and not same_artifact_sanity_passes(result):
            early_stop = {
                "stage": "arena_validation_matrix",
                "reason": "same-artifact sanity failed",
                "variant": key,
            }
            break
    if early_stop is not None:
        completed_keys = {
            (
                result.get("comparison"),
                result.get("run_id"),
                result.get("depth"),
                result.get("arena_seed"),
            )
            for result in results
        }
        for variant in variants:
            candidate = variant.get("candidate")
            key_tuple = (
                variant.get("comparison"),
                candidate.get("run_id") if isinstance(candidate, dict) else None,
                variant.get("depth"),
                variant.get("arena_seed"),
            )
            if key_tuple not in completed_keys:
                skipped.append(
                    {
                        "status": "skipped-early-stop",
                        "comparison": variant.get("comparison"),
                        "run_id": candidate.get("run_id") if isinstance(candidate, dict) else None,
                        "depth": variant.get("depth"),
                        "arena_seed": variant.get("arena_seed"),
                        "reason": early_stop["reason"],
                    }
                )
    return results, skipped, early_stop


def arena_supportive(result: dict[str, Any], *, close_ok: bool) -> bool:
    if result.get("status") not in ("ok", "skipped-resume-validated"):
        return False
    failed = numeric(result, "illegal_or_failed_games") or 0
    if failed != 0:
        return False
    rate = numeric(result, "candidate_score_rate")
    if rate is None:
        return False
    return float(rate) >= (0.49 if close_ok else 0.5)


def summarize_arena_validation(results: list[dict[str, Any]], skipped: list[dict[str, Any]]) -> dict[str, Any]:
    completed = [result for result in results if result.get("status") in ("ok", "skipped-resume-validated")]
    by_comparison: dict[str, dict[str, Any]] = {}
    for comparison in sorted({str(result.get("comparison")) for result in completed}):
        group = [result for result in completed if result.get("comparison") == comparison]
        close_ok = comparison == "move_teacher_v2_vs_exact_root_v2"
        by_comparison[comparison] = {
            "completed_count": len(group),
            "supportive_count": sum(1 for result in group if arena_supportive(result, close_ok=close_ok)),
            "non_negative_count": sum(1 for result in group if arena_supportive(result, close_ok=False)),
            "positive_margin_count": sum(
                1
                for result in group
                if (value := numeric(result, "candidate_score_rate")) is not None and float(value) > 0.5
            ),
            "failed_game_count": sum(int(numeric(result, "illegal_or_failed_games") or 0) for result in group),
            "score_rate_mean": mean_or_none(
                [float(value) for result in group if (value := numeric(result, "candidate_score_rate")) is not None]
            ),
        }
    same = next((result for result in completed if result.get("comparison") == "same_artifact_sanity"), None)
    move_vs_v1 = by_comparison.get("move_teacher_v2_vs_v1", {})
    move_vs_exact = by_comparison.get("move_teacher_v2_vs_exact_root_v2", {})
    same_ok = same is None or same_artifact_sanity_passes(same)
    v1_ok = move_vs_v1.get("completed_count", 0) == 0 or move_vs_v1.get("supportive_count", 0) > move_vs_v1.get(
        "completed_count", 0
    ) / 2.0
    exact_ok = move_vs_exact.get("completed_count", 0) == 0 or move_vs_exact.get("supportive_count", 0) > move_vs_exact.get(
        "completed_count", 0
    ) / 2.0
    failed_ok = all(data.get("failed_game_count", 0) == 0 for data in by_comparison.values())
    positive_margin = move_vs_v1.get("positive_margin_count", 0) > 0
    gate_passed = bool(completed) and same_ok and v1_ok and exact_ok and failed_ok
    return {
        "status": "ok" if completed else "missing",
        "completed_count": len(completed),
        "skipped_count": len(skipped),
        "gate_passed": gate_passed,
        "positive_arena_margin_observed": positive_margin,
        "same_artifact_sanity_passed": same_ok,
        "move_teacher_vs_v1_supportive": v1_ok,
        "move_teacher_vs_exact_root_close_or_non_negative": exact_ok,
        "failed_games_zero_or_explained": failed_ok,
        "by_comparison": by_comparison,
    }


def promotion_decision(decision: dict[str, Any], arena: dict[str, Any]) -> dict[str, Any]:
    completed = int(decision.get("completed_comparable_runs", 0) or 0)
    if completed == 0:
        category = "inconclusive"
        reasons = ["no completed comparable decision-leverage runs"]
    elif (
        not decision.get("gate_passed")
        and (
            decision.get("high_all_same_count", 0) > completed / 2
            or decision.get("compressed_static_score_range_count", 0) > completed / 2
        )
    ):
        category = "needs_rank_objective"
        reasons = ["predicted move scores are often all-same or compressed"]
    elif decision.get("pairwise_positive_count", 0) <= completed / 2 or decision.get("regret_decrease_count", 0) <= completed / 2:
        if decision.get("top1_positive_count", 0) <= completed / 2:
            category = "negative"
            reasons = ["pairwise/regret and top1 do not improve on most completed runs"]
        else:
            category = "needs_rank_objective"
            reasons = ["value fitting does not move pairwise/regret on most completed runs"]
    elif decision.get("gate_passed") and arena.get("status") == "missing":
        category = "hold_for_more_data"
        reasons = ["decision metrics pass, but bounded arena validation has not run"]
    elif decision.get("gate_passed") and not arena.get("gate_passed"):
        category = "hold_for_more_data"
        reasons = ["decision metrics pass, but arena support is missing, neutral, or noisy"]
    elif (
        decision.get("gate_passed")
        and arena.get("gate_passed")
        and arena.get("positive_arena_margin_observed")
        and decision.get("stable_across_root_counts_and_seeds")
    ):
        category = "promote_to_larger_local_validation"
        reasons = ["decision-leverage and arena gates pass with stable root-count/seed support"]
    elif decision.get("gate_passed") and arena.get("gate_passed"):
        category = "hold_for_more_data"
        reasons = ["gates pass, but arena margin, root-count stability, or seed stability is not yet strong"]
    else:
        category = "inconclusive"
        reasons = ["mixed decision-leverage, held-out, or arena evidence"]

    if category == "hold_for_more_data" and arena.get("gate_passed") and not decision.get("stable_across_root_counts_and_seeds"):
        alternate = "needs_search_depth_validation"
    elif category == "needs_rank_objective":
        alternate = "needs_feature_capacity" if decision.get("compressed_static_score_range_count", 0) > completed / 2 else None
    else:
        alternate = None
    return {
        "category": category,
        "alternate_category": alternate,
        "allowed_categories": list(DECISION_CATEGORIES),
        "decision_leverage_gate": decision.get("gate_passed", False),
        "arena_support_gate": arena.get("gate_passed", False),
        "stable_across_root_counts_and_seeds": decision.get("stable_across_root_counts_and_seeds", False),
        "reasons": reasons,
    }


def next_action(scorecard: dict[str, Any], decision: dict[str, Any], arena: dict[str, Any]) -> dict[str, Any]:
    category = scorecard["category"]
    if category == "promote_to_larger_local_validation":
        action = "run_larger_low_empty_input_generation_to_support_50000_roots"
        why = "the current local cycle passes decision-leverage and bounded arena gates"
        evidence = [
            f"top1 positive {decision.get('top1_positive_count')}/{decision.get('completed_comparable_runs')}",
            f"pairwise positive {decision.get('pairwise_positive_count')}/{decision.get('completed_comparable_runs')}",
            f"regret decreased {decision.get('regret_decrease_count')}/{decision.get('completed_comparable_runs')}",
            f"arena completed {arena.get('completed_count')}",
        ]
        not_next = "do not add pattern-v3 or a pairwise rank trainer before the larger local validation fails"
    elif category == "hold_for_more_data":
        action = "run_more_bounded_arenas_for_the_largest_available_artifacts"
        why = "ranking evidence is positive, but arena or seed/root-count support is not decisive enough"
        evidence = [
            f"decision gate passed={decision.get('gate_passed')}",
            f"arena gate passed={arena.get('gate_passed')}",
            f"arena completed {arena.get('completed_count')}",
        ]
        not_next = "do not claim strength or publish artifacts from this bounded evidence"
    elif category == "needs_rank_objective":
        action = "add_pairwise_rank_trainer_v0e_or_rank_loss_diagnostic"
        why = "value-style child-label training is not reliably improving move ranking"
        evidence = [
            f"high all-same runs {decision.get('high_all_same_count')}",
            f"compressed score-range runs {decision.get('compressed_static_score_range_count')}",
            f"pairwise positive {decision.get('pairwise_positive_count')}/{decision.get('completed_comparable_runs')}",
        ]
        not_next = "do not spend the next PR only tuning value-trainer learning rate or weight decay"
    elif category == "negative":
        action = "stop_scaling_this_value_training_route_and_debug_label_or_ranking_failures"
        why = "pairwise/regret and top1 evidence are negative on most completed runs"
        evidence = [
            f"negative pairwise/regret runs {decision.get('negative_pairwise_or_regret_count')}",
            f"top1 positive {decision.get('top1_positive_count')}/{decision.get('completed_comparable_runs')}",
        ]
        not_next = "do not run larger arenas until the ranking regression is explained"
    else:
        action = "reuse_or_complete_the_missing_matrix_and_arena_reports"
        why = "the cycle lacks enough comparable evidence for a promotion decision"
        evidence = [
            f"decision completed {decision.get('completed_comparable_runs')}",
            f"arena completed {arena.get('completed_count')}",
        ]
        not_next = "do not introduce pattern-v3 or production validation before the local scorecard is complete"
    return {"action": action, "why": why, "evidence": evidence, "what_not_to_do_next": not_next}


def write_summary(path: Path, report: dict[str, Any]) -> None:
    scorecard = report["promotion_scorecard"]
    decision = report["decision_leverage"]["summary"]
    arena = report["arena_validation"]["summary"]
    next_step = report["next_action"]
    lines = [
        "# Pattern-Learning Growth Cycle",
        "",
        "Local-only autonomous growth-cycle diagnostic. Not Elo, not self-play, not production strength, and not a publication gate.",
        "",
        "## Inputs",
        "",
        f"- normalized TSV: {report['baseline_inventory']['normalized_tsv']['path']}",
        f"- available low-empty roots: {report['baseline_inventory']['normalized_tsv'].get('available_unique_low_empty_roots')}",
        f"- selected root counts: {', '.join(str(value) for value in report['baseline_inventory'].get('selected_root_counts', []))}",
        f"- pattern set: {report['baseline_inventory'].get('pattern_set')}",
        f"- trainer mode: {report['baseline_inventory'].get('trainer_mode')}",
        "",
        "## Decision Leverage",
        "",
        f"- completed comparable runs: {decision.get('completed_comparable_runs')}",
        f"- top1 positive: {decision.get('top1_positive_count')}",
        f"- pairwise positive: {decision.get('pairwise_positive_count')}",
        f"- mean regret decreased: {decision.get('regret_decrease_count')}",
        f"- held-out support: {decision.get('heldout_support_count')}",
        f"- gate passed: {decision.get('gate_passed')}",
        "",
        "## Arena",
        "",
        f"- completed variants: {arena.get('completed_count')}",
        f"- gate passed: {arena.get('gate_passed')}",
        f"- same-artifact sanity: {arena.get('same_artifact_sanity_passed')}",
        f"- move-teacher v2 vs v1 supportive: {arena.get('move_teacher_vs_v1_supportive')}",
        f"- move-teacher v2 vs exact-root close/non-negative: {arena.get('move_teacher_vs_exact_root_close_or_non_negative')}",
        "",
        "## Promotion Decision",
        "",
        f"- category: `{scorecard.get('category')}`",
        f"- alternate category: `{scorecard.get('alternate_category')}`",
        f"- reasons: {'; '.join(scorecard.get('reasons', []))}",
        "",
        "## Next Action",
        "",
        f"- action: `{next_step.get('action')}`",
        f"- why: {next_step.get('why')}",
        f"- evidence: {'; '.join(next_step.get('evidence', []))}",
        f"- what not to do next: {next_step.get('what_not_to_do_next')}",
        "",
        "## Non-Claims",
        "",
        "This report does not claim engine strength, Elo, self-play improvement, production readiness, publication readiness, or license clearance for generated artifacts.",
    ]
    if report.get("early_stop"):
        lines[3:3] = ["", f"Early stop: {report['early_stop'].get('reason')}"]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def failed_report(args: argparse.Namespace, preflight_data: dict[str, Any], missing: list[dict[str, Any]]) -> dict[str, Any]:
    created_at = args.created_at_utc or datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    scorecard = {
        "category": "inconclusive",
        "allowed_categories": list(DECISION_CATEGORIES),
        "decision_leverage_gate": False,
        "arena_support_gate": False,
        "stable_across_root_counts_and_seeds": False,
        "reasons": ["critical local inputs are missing"],
    }
    next_step = {
        "action": "provide_missing_local_inputs_and_resume",
        "why": "preflight cannot safely run the local growth cycle without these inputs",
        "evidence": [item["input"] for item in missing],
        "what_not_to_do_next": "do not commit generated labels, datasets, weights, artifacts, or raw local reports",
    }
    return {
        "schema_version": 1,
        "created_at_utc": created_at,
        "status": "failed-preflight",
        "stages": {"preflight": preflight_data},
        "baseline_inventory": {
            "status": "failed-preflight",
            "normalized_tsv": preflight_data.get("normalized_tsv", {}),
            "requested_root_counts": args.root_counts,
            "selected_root_counts": preflight_data.get("selected_root_counts", []),
            "pattern_set": args.pattern_set,
            "trainer_mode": args.trainer_mode,
        },
        "decision_leverage": {"summary": summarize_decision_leverage(None), "matrix_report": None},
        "arena_validation": {"summary": summarize_arena_validation([], []), "results": [], "skipped": []},
        "promotion_scorecard": scorecard,
        "next_action": next_step,
        "early_stop": {"stage": "preflight", "reason": "critical inputs missing"},
        "warnings": list(LOCAL_ONLY_WARNINGS),
    }


def main() -> int:
    args = parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    report_path = args.output_dir / "growth-cycle-report.json"
    summary_path = args.output_dir / "growth-cycle-summary.md"
    checksum_cache: dict[Path, str] = {}
    try:
        preflight_data, missing, selected_counts = preflight(args, checksum_cache)
        if missing and not args.dry_run:
            report = failed_report(args, preflight_data, missing)
            write_json(report_path, report)
            write_summary(summary_path, report)
            print(f"growth_cycle_report={report_path}")
            print(f"growth_cycle_summary={summary_path}")
            print("preflight failed; see required_inputs_missing in growth-cycle-report.json", file=sys.stderr)
            return 1

        inventory = baseline_inventory(args, checksum_cache, preflight_data)
        matrix_report, matrix_stage = run_or_reuse_matrix(args, selected_counts or args.root_counts)
        decision_summary = summarize_decision_leverage(matrix_report)

        candidates = extract_candidates(args, matrix_report)
        variants = arena_variant_plan(args, candidates, decision_summary)
        early_stop: dict[str, Any] | None = None
        if args.skip_arenas:
            arena_results: list[dict[str, Any]] = []
            arena_skipped = [{"status": "skipped", "reason": "--skip-arenas was supplied"}]
            early_stop = {"stage": "arena_validation_matrix", "reason": "arenas skipped by CLI"}
        elif not variants:
            reason = "decision-leverage gate did not pass" if not decision_summary.get("gate_passed") else "no arena candidates"
            arena_results = []
            arena_skipped = [{"status": "skipped", "reason": reason}]
            early_stop = {"stage": "arena_validation_matrix", "reason": reason}
        else:
            arena_results, arena_skipped, early_stop = run_arena_matrix(args, variants, checksum_cache)

        arena_summary = summarize_arena_validation(arena_results, arena_skipped)
        scorecard = promotion_decision(decision_summary, arena_summary)
        next_step = next_action(scorecard, decision_summary, arena_summary)
        created_at = args.created_at_utc or datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")
        report = {
            "schema_version": 1,
            "created_at_utc": created_at,
            "status": "ok" if not args.dry_run else "planned",
            "stages": {
                "preflight": preflight_data,
                "baseline_inventory": {"status": "ok"},
                "decision_leverage_matrix": matrix_stage,
                "arena_validation_matrix": {
                    "status": "planned" if args.dry_run else ("ok" if arena_results else "skipped"),
                    "planned_variant_count": len(variants),
                    "completed_variant_count": len(
                        [
                            result
                            for result in arena_results
                            if result.get("status") in ("ok", "skipped-resume-validated")
                        ]
                    ),
                    "skipped_variant_count": len(arena_skipped),
                },
            },
            "baseline_inventory": inventory,
            "decision_leverage": {
                "summary": decision_summary,
                "matrix_report": matrix_report,
            },
            "arena_validation": {
                "summary": arena_summary,
                "results": arena_results,
                "skipped": arena_skipped,
            },
            "promotion_scorecard": scorecard,
            "next_action": next_step,
            "early_stop": early_stop,
            "warnings": list(LOCAL_ONLY_WARNINGS),
        }
        write_json(report_path, report)
        write_summary(summary_path, report)
        print(f"growth_cycle_report={report_path}")
        print(f"growth_cycle_summary={summary_path}")
    except (OSError, GrowthCycleError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
