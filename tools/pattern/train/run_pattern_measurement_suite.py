#!/usr/bin/env python3
"""Run local-only Egaroucid sequence pattern measurement presets."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


SUITE_SCHEMA_VERSION = 1
LOCAL_ONLY_NOTES = [
    "local-only measurement suite",
    "not strength claim",
    "no Elo",
    "no match bench",
    "no self-play",
    "generated artifacts not committed",
]


@dataclass(frozen=True)
class Preset:
    name: str
    max_examples: int
    sequence_sampling_mode: str
    sequence_max_positions: int
    eval_smoke_max_positions: int
    search_smoke_max_positions: int
    default_epochs: int
    sequence_max_files: int | None = None
    sequence_max_games: int | None = None
    sequence_file_order: str | None = None
    sequence_progress_every_games: int | None = None
    sequence_progress_every_files: int | None = None
    trainer_eval_every_epoch: bool = True
    trainer_progress_every_examples: int | None = None


PRESETS: dict[str, Preset] = {
    "smoke": Preset(
        name="smoke",
        max_examples=100,
        sequence_sampling_mode="bounded-dev",
        sequence_max_positions=100,
        sequence_max_files=1,
        sequence_max_games=4,
        sequence_file_order="hash",
        sequence_progress_every_games=1,
        sequence_progress_every_files=1,
        trainer_eval_every_epoch=True,
        trainer_progress_every_examples=10,
        eval_smoke_max_positions=10,
        search_smoke_max_positions=3,
        default_epochs=4,
    ),
    "10k": Preset(
        name="10k",
        max_examples=10_000,
        sequence_sampling_mode="streaming-target",
        sequence_max_positions=10_000,
        sequence_file_order="hash",
        sequence_progress_every_games=10_000,
        sequence_progress_every_files=10,
        trainer_eval_every_epoch=False,
        trainer_progress_every_examples=5_000,
        eval_smoke_max_positions=1_000,
        search_smoke_max_positions=200,
        default_epochs=8,
    ),
    "100k": Preset(
        name="100k",
        max_examples=100_000,
        sequence_sampling_mode="streaming-target",
        sequence_max_positions=100_000,
        sequence_file_order="hash",
        sequence_progress_every_games=100_000,
        sequence_progress_every_files=25,
        trainer_eval_every_epoch=False,
        trainer_progress_every_examples=50_000,
        eval_smoke_max_positions=5_000,
        search_smoke_max_positions=500,
        default_epochs=8,
    ),
    "1m": Preset(
        name="1m",
        max_examples=1_000_000,
        sequence_sampling_mode="streaming-target",
        sequence_max_positions=1_000_000,
        sequence_file_order="hash",
        sequence_progress_every_games=100_000,
        sequence_progress_every_files=25,
        trainer_eval_every_epoch=False,
        trainer_progress_every_examples=100_000,
        eval_smoke_max_positions=10_000,
        search_smoke_max_positions=1_000,
        default_epochs=8,
    ),
}
ALL_PRESET_NAMES = ("10k", "100k", "1m")


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sequence-input", required=True, type=Path)
    parser.add_argument("--sequence-manifest", required=True, type=Path)
    parser.add_argument(
        "--suite-output-dir",
        type=Path,
        help=(
            "Suite output directory. If omitted, use VIBE_OTHELLO_MEASUREMENTS/"
            "<suite-prefix> or VIBE_OTHELLO_LOCAL/measurements/<suite-prefix>."
        ),
    )
    parser.add_argument("--sequence-cache-dir", type=Path)
    parser.add_argument(
        "--preset",
        choices=("smoke", "10k", "100k", "1m", "all"),
        default="smoke",
    )
    parser.add_argument("--run-prefix")
    parser.add_argument("--created-at-utc")
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--pattern-set", default="pattern-v1-buro-lite")
    parser.add_argument("--dataset-output-format", default="compact-tsv", choices=("expanded-tsv", "compact-tsv"))
    parser.add_argument("--trainer-mode", default="pattern-sgd-v0c", choices=("pattern-sgd-v0b", "pattern-sgd-v0c", "pattern-sgd-v0d"))
    parser.add_argument("--epochs", type=int)
    parser.add_argument("--learning-rate", type=float, default=0.1)
    parser.add_argument("--l2", type=float, default=0.0)
    parser.add_argument("--weight-decay", type=float)
    parser.add_argument("--lr-schedule", choices=("constant", "inverse-sqrt"), default="constant")
    parser.add_argument("--gradient-clip", type=float)
    parser.add_argument("--early-stop-patience", type=int)
    parser.add_argument("--phase-balance", choices=("none", "inverse-count", "sqrt-inverse-count"))
    parser.add_argument("--max-phase-weight", type=float)
    parser.add_argument("--min-phase-weight", type=float)
    parser.add_argument(
        "--trainer-eval-every-epoch",
        dest="trainer_eval_every_epoch",
        action="store_true",
        default=None,
        help="Override preset v0c diagnostics to evaluate every epoch.",
    )
    parser.add_argument(
        "--trainer-no-eval-every-epoch",
        dest="trainer_eval_every_epoch",
        action="store_false",
        help="Override preset v0c diagnostics to evaluate only final epoch.",
    )
    parser.add_argument("--trainer-progress-every-examples", type=int)
    parser.add_argument(
        "--measurement-split-policy",
        choices=("preserve", "connected-board-game"),
        default="preserve",
    )
    parser.add_argument("--teacher-labels", type=Path)
    parser.add_argument(
        "--teacher-label-missing-policy",
        choices=("fail", "keep-observed", "drop"),
        default="fail",
    )
    parser.add_argument(
        "--teacher-label-conflict-policy",
        choices=("fail",),
        default="fail",
    )
    parser.add_argument("--strict-board-disjoint-splits", action="store_true")
    parser.add_argument("--eval-smoke-max-positions", type=int)
    parser.add_argument("--search-smoke-max-positions", type=int)
    parser.add_argument("--max-examples", type=int, help="Override preset max examples for local iteration.")
    parser.add_argument(
        "--sequence-max-positions",
        type=int,
        help="Override preset sequence position cap for local iteration.",
    )
    parser.add_argument(
        "--sequence-sampling-mode",
        choices=("full-scan-topk", "streaming-target", "bounded-dev"),
        help="Override preset sequence sampling mode for local iteration.",
    )
    parser.add_argument("--sequence-max-files", type=int)
    parser.add_argument("--sequence-max-games", type=int)
    parser.add_argument("--sequence-file-order", choices=("path", "hash"))
    parser.add_argument("--sequence-progress-every-games", type=int)
    parser.add_argument("--sequence-progress-every-files", type=int)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--keep-going", action="store_true")
    parser.add_argument("--no-overwrite", action="store_true")
    parser.add_argument(
        "--runner",
        type=Path,
        default=root / "tools/pattern/train/run_egaroucid_local_training.py",
    )
    parser.add_argument(
        "--analyzer",
        type=Path,
        default=root / "tools/pattern/train/analyze_local_training_runs.py",
    )
    parser.add_argument("--sequence-importer", type=Path)
    parser.add_argument("--trainer", type=Path)
    parser.add_argument("--v0a-exporter", type=Path)
    parser.add_argument("--v0b-exporter", type=Path)
    parser.add_argument("--dataset-exe", type=Path)
    parser.add_argument("--eval-smoke-exe", type=Path)
    parser.add_argument("--search-smoke-exe", type=Path)
    args = parser.parse_args()
    if args.seed < 0:
        parser.error("--seed must be non-negative")
    for name in (
        "epochs",
        "max_examples",
        "sequence_max_positions",
        "sequence_max_files",
        "sequence_max_games",
        "sequence_progress_every_games",
        "sequence_progress_every_files",
        "trainer_progress_every_examples",
        "eval_smoke_max_positions",
        "search_smoke_max_positions",
    ):
        value = getattr(args, name)
        if value is not None and value <= 0:
            parser.error(f"--{name.replace('_', '-')} must be positive")
    if args.learning_rate < 0.0:
        parser.error("--learning-rate must be non-negative")
    if args.l2 < 0.0:
        parser.error("--l2 must be non-negative")
    if args.weight_decay is not None and args.weight_decay < 0.0:
        parser.error("--weight-decay must be non-negative")
    if args.gradient_clip is not None and args.gradient_clip <= 0.0:
        parser.error("--gradient-clip must be positive")
    if args.early_stop_patience is not None and args.early_stop_patience < 0:
        parser.error("--early-stop-patience must be non-negative")
    v0d_only_args = (
        args.phase_balance is not None
        or args.max_phase_weight is not None
        or args.min_phase_weight is not None
    )
    if args.trainer_mode != "pattern-sgd-v0d" and v0d_only_args:
        parser.error("--phase-balance, --max-phase-weight, and --min-phase-weight require --trainer-mode pattern-sgd-v0d")
    if args.max_phase_weight is not None and args.max_phase_weight <= 0.0:
        parser.error("--max-phase-weight must be positive")
    if args.min_phase_weight is not None and args.min_phase_weight <= 0.0:
        parser.error("--min-phase-weight must be positive")
    if (
        args.max_phase_weight is not None
        and args.min_phase_weight is not None
        and args.min_phase_weight > args.max_phase_weight
    ):
        parser.error("--min-phase-weight must be <= --max-phase-weight")
    return args


def stable_json(data: Any) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def utc_now() -> str:
    return datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def compact_timestamp(timestamp: str) -> str:
    return timestamp.replace("-", "").replace(":", "").replace("+00:00", "Z")


def sanitize_id(text: str) -> str:
    sanitized = re.sub(r"[^A-Za-z0-9_.-]+", "-", text.strip())
    sanitized = sanitized.strip(".-")
    return sanitized or "suite"


def selected_presets(preset: str) -> list[Preset]:
    if preset == "all":
        return [PRESETS[name] for name in ALL_PRESET_NAMES]
    return [PRESETS[preset]]


def env_path(name: str) -> Path | None:
    value = os.environ.get(name)
    if value is None or value == "":
        return None
    return Path(value).expanduser()


def suite_prefix_for(args: argparse.Namespace, created_at: str) -> str:
    return sanitize_id(args.run_prefix or f"pattern-suite-{compact_timestamp(created_at)}")


def resolve_suite_output_dir(args: argparse.Namespace, created_at: str) -> Path:
    if args.suite_output_dir is not None:
        return args.suite_output_dir
    suite_prefix = suite_prefix_for(args, created_at)
    measurements_root = env_path("VIBE_OTHELLO_MEASUREMENTS")
    if measurements_root is not None:
        return measurements_root / suite_prefix
    local_root = env_path("VIBE_OTHELLO_LOCAL")
    if local_root is not None:
        return local_root / "measurements" / suite_prefix
    raise RuntimeError(
        "either --suite-output-dir, VIBE_OTHELLO_MEASUREMENTS, or "
        "VIBE_OTHELLO_LOCAL is required for measurement suite output"
    )


def resolve_sequence_cache_dir(args: argparse.Namespace, suite_output_dir: Path) -> Path:
    if args.sequence_cache_dir is not None:
        return args.sequence_cache_dir
    sequence_cache_root = env_path("VIBE_OTHELLO_SEQUENCE_CACHE")
    if sequence_cache_root is not None:
        return sequence_cache_root
    local_root = env_path("VIBE_OTHELLO_LOCAL")
    if local_root is not None:
        return local_root / "sequence-cache"
    return suite_output_dir / "sequence-cache"


def git_commit() -> str | None:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        return None
    return result.stdout.strip() or None


def load_json_object(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"could not read JSON object {path}: {error}") from error
    if not isinstance(data, dict):
        raise RuntimeError(f"JSON root must be an object: {path}")
    return data


def int_or_none(value: Any) -> int | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value)
        except ValueError:
            return None
    return None


def sampled_rows_from_report(report: dict[str, Any]) -> int | None:
    counts = report.get("sample_counts_by_split")
    if not isinstance(counts, dict):
        return None
    values = [int_or_none(value) for value in counts.values()]
    if any(value is None for value in values):
        return None
    return sum(value for value in values if value is not None)


def cache_status_from_report(report: dict[str, Any]) -> str | None:
    cache = report.get("sequence_cache")
    if not isinstance(cache, dict):
        return None
    status = cache.get("status")
    return status if isinstance(status, str) and status else None


def count_from_smoke_summary(summary: Any, key: str) -> int | None:
    if not isinstance(summary, dict):
        return None
    nested = summary.get("summary")
    if isinstance(nested, dict) and key in nested:
        return int_or_none(nested.get(key))
    return int_or_none(summary.get(key))


def local_report_path(run_dir: Path) -> Path:
    return run_dir / "local-training-run-report.json"


def validate_resume_report(report_path: Path, run_id: str) -> dict[str, Any]:
    report = load_json_object(report_path)
    actual_run_id = report.get("run_id")
    if actual_run_id is not None and actual_run_id != run_id:
        raise RuntimeError(f"resume report run_id mismatch: expected {run_id}, got {actual_run_id}")
    if report.get("schema_version") != 1:
        raise RuntimeError(f"resume report has unsupported schema_version: {report.get('schema_version')!r}")
    return report


def command_for_preset(
    args: argparse.Namespace,
    preset: Preset,
    run_id: str,
    run_dir: Path,
    created_at: str,
    sequence_cache_dir: Path,
) -> list[str]:
    max_examples = args.max_examples or preset.max_examples
    sequence_max_positions = args.sequence_max_positions or preset.sequence_max_positions
    sampling_mode = args.sequence_sampling_mode or preset.sequence_sampling_mode
    epochs = args.epochs or preset.default_epochs
    eval_positions = args.eval_smoke_max_positions or preset.eval_smoke_max_positions
    search_positions = args.search_smoke_max_positions or preset.search_smoke_max_positions
    sequence_file_order = args.sequence_file_order or preset.sequence_file_order
    sequence_progress_every_games = (
        args.sequence_progress_every_games or preset.sequence_progress_every_games
    )
    sequence_progress_every_files = (
        args.sequence_progress_every_files or preset.sequence_progress_every_files
    )
    trainer_eval_every_epoch = (
        preset.trainer_eval_every_epoch
        if args.trainer_eval_every_epoch is None
        else args.trainer_eval_every_epoch
    )
    trainer_progress_every_examples = (
        args.trainer_progress_every_examples or preset.trainer_progress_every_examples
    )
    sequence_max_files = args.sequence_max_files
    sequence_max_games = args.sequence_max_games
    if sampling_mode in {"bounded-dev", "streaming-target"}:
        sequence_max_files = sequence_max_files or preset.sequence_max_files
        sequence_max_games = sequence_max_games or preset.sequence_max_games

    command = [
        sys.executable,
        str(args.runner),
        "--sequence-input",
        str(args.sequence_input),
        "--sequence-manifest",
        str(args.sequence_manifest),
        "--output-dir",
        str(run_dir),
        "--run-id",
        run_id,
        "--created-at-utc",
        created_at,
        "--max-examples",
        str(max_examples),
        "--seed",
        str(args.seed),
        "--pattern-set",
        args.pattern_set,
        "--dataset-output-format",
        args.dataset_output_format,
        "--trainer-mode",
        args.trainer_mode,
        "--epochs",
        str(epochs),
        "--learning-rate",
        str(args.learning_rate),
        "--l2",
        str(args.l2),
        "--lr-schedule",
        args.lr_schedule,
        "--sequence-cache-dir",
        str(sequence_cache_dir),
        "--sequence-sampling-mode",
        sampling_mode,
        "--sequence-max-positions",
        str(sequence_max_positions),
        "--eval-smoke-max-positions",
        str(eval_positions),
        "--search-smoke-max-positions",
        str(search_positions),
    ]
    if sequence_file_order is not None:
        command.extend(["--sequence-file-order", sequence_file_order])
    if sequence_max_files is not None:
        command.extend(["--sequence-max-files", str(sequence_max_files)])
    if sequence_max_games is not None:
        command.extend(["--sequence-max-games", str(sequence_max_games)])
    if sequence_progress_every_games is not None:
        command.extend(["--sequence-progress-every-games", str(sequence_progress_every_games)])
    if sequence_progress_every_files is not None:
        command.extend(["--sequence-progress-every-files", str(sequence_progress_every_files)])
    if args.weight_decay is not None:
        command.extend(["--weight-decay", str(args.weight_decay)])
    if args.gradient_clip is not None:
        command.extend(["--gradient-clip", str(args.gradient_clip)])
    if args.early_stop_patience is not None:
        command.extend(["--early-stop-patience", str(args.early_stop_patience)])
    if args.trainer_mode == "pattern-sgd-v0d":
        if args.phase_balance is not None:
            command.extend(["--phase-balance", args.phase_balance])
        if args.max_phase_weight is not None:
            command.extend(["--max-phase-weight", str(args.max_phase_weight)])
        if args.min_phase_weight is not None:
            command.extend(["--min-phase-weight", str(args.min_phase_weight)])
    command.append(
        "--trainer-eval-every-epoch"
        if trainer_eval_every_epoch
        else "--trainer-no-eval-every-epoch"
    )
    if trainer_progress_every_examples is not None:
        command.extend(["--trainer-progress-every-examples", str(trainer_progress_every_examples)])
    if args.measurement_split_policy != "preserve":
        command.extend(["--measurement-split-policy", args.measurement_split_policy])
    if args.teacher_labels is not None:
        command.extend(["--teacher-labels", str(args.teacher_labels)])
        command.extend(["--teacher-label-missing-policy", args.teacher_label_missing_policy])
        command.extend(["--teacher-label-conflict-policy", args.teacher_label_conflict_policy])
    if args.strict_board_disjoint_splits:
        command.append("--strict-board-disjoint-splits")
    for name, flag in (
        ("sequence_importer", "--sequence-importer"),
        ("trainer", "--trainer"),
        ("v0a_exporter", "--v0a-exporter"),
        ("v0b_exporter", "--v0b-exporter"),
        ("dataset_exe", "--dataset-exe"),
        ("eval_smoke_exe", "--eval-smoke-exe"),
        ("search_smoke_exe", "--search-smoke-exe"),
    ):
        value = getattr(args, name)
        if value is not None:
            command.extend([flag, str(value)])
    return command


def command_arg(command: list[str], flag: str) -> str | None:
    try:
        return command[command.index(flag) + 1]
    except (ValueError, IndexError):
        return None


def base_run_entry(
    preset: Preset,
    run_id: str,
    run_dir: Path,
    command: list[str],
    log_dir: Path,
) -> dict[str, Any]:
    stdout_log = log_dir / f"{run_id}.stdout.log"
    stderr_log = log_dir / f"{run_id}.stderr.log"
    return {
        "artifact_checksum": None,
        "cache_status": None,
        "command": command,
        "dataset_output_format": None,
        "eval_smoke_positions": None,
        "final_validation_MAE": None,
        "finished_at_utc": None,
        "local_training_report": str(local_report_path(run_dir)),
        "measurement_split_policy": command_arg(command, "--measurement-split-policy") or "preserve",
        "teacher_labels_enabled": "--teacher-labels" in command,
        "teacher_label_missing_policy": command_arg(command, "--teacher-label-missing-policy") or "fail",
        "teacher_label_conflict_policy": command_arg(command, "--teacher-label-conflict-policy") or "fail",
        "nonzero_weight_count": None,
        "output_dir": str(run_dir),
        "preset": preset.name,
        "return_code": None,
        "run_id": run_id,
        "sampled_rows": None,
        "search_smoke_positions": None,
        "sequence_file_order": command_arg(command, "--sequence-file-order"),
        "sequence_max_positions": int_or_none(command_arg(command, "--sequence-max-positions")),
        "sequence_sampling_mode": command_arg(command, "--sequence-sampling-mode"),
        "started_at_utc": None,
        "status": "planned",
        "stderr_log": str(stderr_log),
        "stdout_log": str(stdout_log),
        "test_MAE": None,
        "total_wall_time_sec": None,
        "trainer_eval_every_epoch": "--trainer-no-eval-every-epoch" not in command,
        "trainer_progress_every_examples": int_or_none(
            command_arg(command, "--trainer-progress-every-examples")
        ),
        "trainer_mode": None,
        "trainer_report_checksum": None,
        "trainer_version": None,
        "wall_time_sec": None,
        "warning_count": None,
        "weight_l2_norm": None,
    }


def populate_entry_from_local_report(entry: dict[str, Any], report: dict[str, Any]) -> None:
    entry["artifact_checksum"] = report.get("artifact_checksum")
    entry["cache_status"] = cache_status_from_report(report)
    entry["dataset_output_format"] = report.get("dataset_output_format")
    entry["sampled_rows"] = sampled_rows_from_report(report)
    entry["measurement_split_policy"] = report.get("measurement_split_policy")
    entry["teacher_labels_enabled"] = report.get("teacher_labels_enabled")
    entry["teacher_label_missing_policy"] = report.get("teacher_label_missing_policy")
    entry["teacher_label_conflict_policy"] = report.get("teacher_label_conflict_policy")
    entry["teacher_label_report_checksum"] = report.get("teacher_label_report_checksum")
    entry["trainer_mode"] = report.get("trainer_mode")
    entry["trainer_report_checksum"] = report.get("trainer_report_checksum")
    entry["trainer_version"] = report.get("trainer_version")
    entry["eval_smoke_positions"] = report.get("eval_smoke_used_positions") or count_from_smoke_summary(
        report.get("evaluation_smoke_summary"), "positions_count"
    )
    entry["search_smoke_positions"] = report.get("search_smoke_used_positions") or count_from_smoke_summary(
        report.get("search_smoke_summary"), "positions_count"
    )


def write_suite_summary(path: Path, report: dict[str, Any]) -> None:
    lines = [
        "# Pattern Measurement Suite Summary",
        "",
        "| preset | sequence_sampling_mode | sequence_file_order | measurement_split_policy | sampled_rows | cache_status | dataset_format | trainer_mode | final_validation_MAE | test_MAE | nonzero_weight_count | weight_l2_norm | eval_smoke_positions | search_smoke_positions | artifact_checksum | total_wall_time_sec | warnings |",
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for run in report.get("runs", []):
        lines.append(
            "| "
            + " | ".join(
                markdown_cell(value)
                for value in (
                    run.get("preset"),
                    run.get("sequence_sampling_mode"),
                    run.get("sequence_file_order"),
                    run.get("measurement_split_policy"),
                    run.get("sampled_rows"),
                    run.get("cache_status"),
                    run.get("dataset_output_format"),
                    run.get("trainer_mode"),
                    run.get("final_validation_MAE"),
                    run.get("test_MAE"),
                    run.get("nonzero_weight_count"),
                    run.get("weight_l2_norm"),
                    run.get("eval_smoke_positions"),
                    run.get("search_smoke_positions"),
                    run.get("artifact_checksum"),
                    run.get("total_wall_time_sec") or run.get("wall_time_sec"),
                    run.get("warning_count"),
                )
            )
            + " |"
        )
    lines.extend(
        [
            "",
            "Notes:",
            "",
        ]
    )
    for note in report.get("notes", []):
        lines.append(f"- {note}")
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def markdown_cell(value: Any) -> str:
    if value is None:
        return ""
    return str(value).replace("|", "\\|").replace("\n", " ")


def suite_report_document(
    args: argparse.Namespace,
    created_at: str,
    preset_names: list[str],
    sequence_cache_dir: Path,
    runs: list[dict[str, Any]],
    analyzer: dict[str, Any],
) -> dict[str, Any]:
    return {
        "suite_schema_version": SUITE_SCHEMA_VERSION,
        "created_at_utc": created_at,
        "suite_output_dir": str(args.suite_output_dir),
        "preset_names": preset_names,
        "git_commit": git_commit(),
        "sequence_input": str(args.sequence_input),
        "sequence_manifest": str(args.sequence_manifest),
        "sequence_cache_dir": str(sequence_cache_dir),
        "pattern_set": args.pattern_set,
        "dataset_output_format": args.dataset_output_format,
        "trainer_mode": args.trainer_mode,
        "trainer_optimizer_args": {
            "epochs": args.epochs,
            "learning_rate": args.learning_rate,
            "l2": args.l2,
            "weight_decay": args.weight_decay,
            "lr_schedule": args.lr_schedule,
            "gradient_clip": args.gradient_clip,
            "early_stop_patience": args.early_stop_patience,
            "phase_balance": args.phase_balance if args.trainer_mode == "pattern-sgd-v0d" else None,
            "max_phase_weight": args.max_phase_weight if args.trainer_mode == "pattern-sgd-v0d" else None,
            "min_phase_weight": args.min_phase_weight if args.trainer_mode == "pattern-sgd-v0d" else None,
            "seed": args.seed,
        },
        "strict_board_disjoint_splits": args.strict_board_disjoint_splits,
        "measurement_split_policy": args.measurement_split_policy,
        "teacher_labels_enabled": args.teacher_labels is not None,
        "teacher_labels_path": str(args.teacher_labels) if args.teacher_labels is not None else None,
        "teacher_label_missing_policy": args.teacher_label_missing_policy,
        "teacher_label_conflict_policy": args.teacher_label_conflict_policy,
        "dry_run": args.dry_run,
        "resume": args.resume,
        "runs": runs,
        "analyzer": analyzer,
        "notes": LOCAL_ONLY_NOTES,
    }


def write_suite_files(
    args: argparse.Namespace,
    created_at: str,
    preset_names: list[str],
    sequence_cache_dir: Path,
    runs: list[dict[str, Any]],
    analyzer: dict[str, Any],
) -> dict[str, Any]:
    report = suite_report_document(args, created_at, preset_names, sequence_cache_dir, runs, analyzer)
    args.suite_output_dir.mkdir(parents=True, exist_ok=True)
    report_path = args.suite_output_dir / "suite-report.json"
    summary_path = args.suite_output_dir / "suite-summary.md"
    report_path.write_text(stable_json(report), encoding="utf-8")
    write_suite_summary(summary_path, report)
    return report


def tee_stream(stream: Any, log_file: Any, mirror: Any | None) -> None:
    try:
        for line in stream:
            log_file.write(line)
            log_file.flush()
            if mirror is not None:
                mirror.write(line)
                mirror.flush()
    finally:
        stream.close()


def run_single_preset(entry: dict[str, Any], command: list[str]) -> int:
    stdout_log = Path(entry["stdout_log"])
    stderr_log = Path(entry["stderr_log"])
    stdout_log.parent.mkdir(parents=True, exist_ok=True)
    stderr_log.parent.mkdir(parents=True, exist_ok=True)
    entry["started_at_utc"] = utc_now()
    start = time.monotonic()
    with stdout_log.open("w", encoding="utf-8") as stdout_file:
        with stderr_log.open("w", encoding="utf-8") as stderr_file:
            process = subprocess.Popen(
                command,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1,
            )
            assert process.stdout is not None
            assert process.stderr is not None
            stdout_thread = threading.Thread(
                target=tee_stream,
                args=(process.stdout, stdout_file, None),
                daemon=True,
            )
            stderr_thread = threading.Thread(
                target=tee_stream,
                args=(process.stderr, stderr_file, sys.stderr),
                daemon=True,
            )
            stdout_thread.start()
            stderr_thread.start()
            try:
                return_code = process.wait()
            except KeyboardInterrupt:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                raise
            finally:
                stdout_thread.join()
                stderr_thread.join()
    entry["wall_time_sec"] = float(f"{time.monotonic() - start:.6g}")
    entry["finished_at_utc"] = utc_now()
    entry["return_code"] = return_code
    entry["status"] = "success" if return_code == 0 else "failed"
    if return_code == 0:
        report = load_json_object(Path(entry["local_training_report"]))
        populate_entry_from_local_report(entry, report)
    return return_code


def run_analyzer(
    args: argparse.Namespace,
    reports: list[Path],
    log_dir: Path,
) -> tuple[dict[str, Any], dict[str, Any] | None]:
    if not reports:
        return {
            "status": "skipped",
            "reason": "no completed or skipped local training reports",
            "json_out": None,
            "markdown_out": None,
            "return_code": None,
        }, None
    json_out = args.suite_output_dir / "analyzer-summary.json"
    markdown_out = args.suite_output_dir / "analyzer-summary.md"
    stdout_log = log_dir / "analyzer.stdout.log"
    stderr_log = log_dir / "analyzer.stderr.log"
    stdout_log.parent.mkdir(parents=True, exist_ok=True)
    command = [
        sys.executable,
        str(args.analyzer),
        "--json-out",
        str(json_out),
        "--markdown-out",
        str(markdown_out),
    ]
    for report in reports:
        command.extend(["--report", str(report)])
    result = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    stdout_log.write_text(result.stdout, encoding="utf-8")
    stderr_log.write_text(result.stderr, encoding="utf-8")
    analyzer = {
        "command": command,
        "json_out": str(json_out),
        "markdown_out": str(markdown_out),
        "return_code": result.returncode,
        "status": "success" if result.returncode == 0 else "failed",
        "stderr_log": str(stderr_log),
        "stdout_log": str(stdout_log),
    }
    if result.returncode != 0:
        return analyzer, None
    return analyzer, load_json_object(json_out)


def enrich_entries_from_analyzer(runs: list[dict[str, Any]], analyzer_summary: dict[str, Any] | None) -> None:
    if not isinstance(analyzer_summary, dict):
        return
    by_path: dict[str, dict[str, Any]] = {}
    for run in analyzer_summary.get("runs", []):
        if isinstance(run, dict) and isinstance(run.get("path"), str):
            by_path[str(Path(run["path"]).resolve())] = run
    for entry in runs:
        summary = by_path.get(str(Path(entry["local_training_report"]).resolve()))
        if not isinstance(summary, dict):
            continue
        entry["final_validation_MAE"] = summary.get("final_validation_MAE")
        entry["test_MAE"] = summary.get("test_MAE")
        weights = summary.get("weight_diagnostics")
        if isinstance(weights, dict):
            entry["nonzero_weight_count"] = weights.get("nonzero_weight_count")
            entry["weight_l2_norm"] = weights.get("weight_l2_norm")
        stage_times = summary.get("stage_wall_times")
        if isinstance(stage_times, dict):
            entry["total_wall_time_sec"] = stage_times.get("total_recorded")
        warnings = summary.get("warnings")
        entry["warning_count"] = len(warnings) if isinstance(warnings, list) else None


def main() -> int:
    args = parse_args()
    created_at = args.created_at_utc or utc_now()
    suite_prefix = suite_prefix_for(args, created_at)
    try:
        args.suite_output_dir = resolve_suite_output_dir(args, created_at)
        sequence_cache_dir = resolve_sequence_cache_dir(args, args.suite_output_dir)
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    presets = selected_presets(args.preset)
    preset_names = [preset.name for preset in presets]
    runs_dir = args.suite_output_dir / "runs"
    log_dir = args.suite_output_dir / "logs"
    args.suite_output_dir.mkdir(parents=True, exist_ok=True)
    runs: list[dict[str, Any]] = []
    analyzer: dict[str, Any] = {
        "status": "not-run",
        "json_out": None,
        "markdown_out": None,
        "return_code": None,
    }
    exit_code = 0

    for preset in presets:
        run_id = sanitize_id(f"{suite_prefix}-{preset.name}")
        run_dir = runs_dir / run_id
        command = command_for_preset(args, preset, run_id, run_dir, created_at, sequence_cache_dir)
        entry = base_run_entry(preset, run_id, run_dir, command, log_dir)
        runs.append(entry)
        report_path = local_report_path(run_dir)

        try:
            if args.resume and report_path.exists():
                try:
                    report = validate_resume_report(report_path, run_id)
                except RuntimeError as error:
                    if args.no_overwrite:
                        raise
                    entry["resume_validation_error"] = str(error)
                else:
                    entry["status"] = "skipped"
                    entry["return_code"] = 0
                    entry["wall_time_sec"] = 0.0
                    populate_entry_from_local_report(entry, report)
                    continue
            if args.no_overwrite and run_dir.exists():
                raise RuntimeError(f"--no-overwrite refuses existing output dir: {run_dir}")
            if args.dry_run:
                continue
            return_code = run_single_preset(entry, command)
            if return_code != 0:
                exit_code = 1
                if not args.keep_going:
                    break
        except RuntimeError as error:
            entry["status"] = "failed"
            entry["return_code"] = 1
            entry["error"] = str(error)
            exit_code = 1
            if not args.keep_going:
                break

    report_paths = [
        Path(entry["local_training_report"])
        for entry in runs
        if entry.get("status") in {"success", "skipped"} and Path(entry["local_training_report"]).exists()
    ]
    if not args.dry_run:
        analyzer, analyzer_summary = run_analyzer(args, report_paths, log_dir)
        enrich_entries_from_analyzer(runs, analyzer_summary)
        if analyzer.get("status") == "failed":
            exit_code = 1
    write_suite_files(args, created_at, preset_names, sequence_cache_dir, runs, analyzer)

    print(f"suite_report={args.suite_output_dir / 'suite-report.json'}")
    print(f"suite_summary={args.suite_output_dir / 'suite-summary.md'}")
    print(f"status={'failed' if exit_code else 'ok'}")
    for entry in runs:
        if args.dry_run:
            print(f"planned_command[{entry['preset']}]={' '.join(entry['command'])}")
        else:
            print(f"run[{entry['preset']}].status={entry['status']}")
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
