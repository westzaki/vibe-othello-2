#!/usr/bin/env python3
"""Run deterministic local-only v0c trainer optimizer sweeps."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import re
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


SCHEMA_VERSION = 1
EXPANDED_HEADER = [
    "record_id",
    "ply",
    "split",
    "label_final_disc_diff",
    "phase",
    "pattern_id",
    "instance",
    "ternary_index",
]
COMPACT_HEADER = [
    "record_id",
    "ply",
    "split",
    "label_final_disc_diff",
    "phase",
    "pattern_features",
]
LOCAL_ONLY_NOTES = [
    "local diagnostic trainer sweep",
    "fixed pattern dataset comparison",
    "best config selected by validation MAE",
    "test metrics are reporting and tie-break only",
    "not strength claim",
    "no Elo",
    "no match bench",
    "no self-play",
    "not a production artifact",
    "not a publication gate",
    "generated sweep outputs must not be committed",
]
BEST_SELECTION_POLICY = {
    "primary": "best_validation_MAE if present and numeric, else final_validation_MAE",
    "tie_breakers": [
        "lower final_validation_MAE",
        "lower test_MAE",
        "lower weight_l2_norm",
        "fewer nonzero_weight_count",
        "config_id lexicographic order",
    ],
    "test_metric_use": "reported and used only as a tie-breaker",
}


@dataclass(frozen=True)
class SweepConfig:
    config_id: str
    epochs: int
    learning_rate: float
    lr_schedule: str
    mode: str = "pattern-sgd-v0c"
    l2: float = 0.0
    weight_decay: float | None = None
    gradient_clip: float | None = None
    early_stop_patience: int | None = None
    phase_balance: str | None = None
    max_phase_weight: float | None = None
    min_phase_weight: float | None = None


PRESETS: dict[str, list[SweepConfig]] = {
    "v0c-100k-core": [
        SweepConfig("baseline_const_lr0.1", 8, 0.1, "constant"),
        SweepConfig("const_lr0.05", 8, 0.05, "constant"),
        SweepConfig("const_lr0.03", 8, 0.03, "constant"),
        SweepConfig("const_lr0.2", 8, 0.2, "constant"),
        SweepConfig("isqrt_lr0.1", 12, 0.1, "inverse-sqrt"),
        SweepConfig("isqrt_lr0.05", 12, 0.05, "inverse-sqrt"),
        SweepConfig("isqrt_lr0.03", 12, 0.03, "inverse-sqrt"),
        SweepConfig("isqrt_lr0.1_wd1e-4", 12, 0.1, "inverse-sqrt", weight_decay=0.0001),
        SweepConfig("isqrt_lr0.05_wd1e-4", 12, 0.05, "inverse-sqrt", weight_decay=0.0001),
        SweepConfig("isqrt_lr0.05_clip2", 12, 0.05, "inverse-sqrt", gradient_clip=2.0),
    ],
    "v0d-100k-phase-core": [
        SweepConfig("v0d_sqrt_bal_lr0.05", 12, 0.05, "inverse-sqrt", mode="pattern-sgd-v0d", phase_balance="sqrt-inverse-count"),
        SweepConfig("v0d_sqrt_bal_lr0.1", 12, 0.1, "inverse-sqrt", mode="pattern-sgd-v0d", phase_balance="sqrt-inverse-count"),
        SweepConfig("v0d_inv_bal_lr0.05", 12, 0.05, "inverse-sqrt", mode="pattern-sgd-v0d", phase_balance="inverse-count"),
        SweepConfig("v0d_none_lr0.05", 12, 0.05, "inverse-sqrt", mode="pattern-sgd-v0d", phase_balance="none"),
        SweepConfig("v0d_sqrt_bal_lr0.05_wd1e-4", 12, 0.05, "inverse-sqrt", mode="pattern-sgd-v0d", weight_decay=0.0001, phase_balance="sqrt-inverse-count"),
        SweepConfig("v0d_sqrt_bal_lr0.05_clip2", 12, 0.05, "inverse-sqrt", mode="pattern-sgd-v0d", gradient_clip=2.0, phase_balance="sqrt-inverse-count"),
    ],
}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--sweep-preset", choices=tuple(sorted(PRESETS)), default="v0c-100k-core")
    parser.add_argument("--run-prefix", default="pattern-trainer-sweep")
    parser.add_argument("--created-at-utc")
    parser.add_argument(
        "--trainer",
        type=Path,
        default=root / "tools/pattern/train/train_v0a.py",
    )
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--keep-going", action="store_true")
    parser.add_argument("--max-configs", type=int)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--source-local-run-report", type=Path)
    args = parser.parse_args()
    if args.max_configs is not None and args.max_configs <= 0:
        parser.error("--max-configs must be positive")
    if args.seed < 0:
        parser.error("--seed must be non-negative")
    if args.dataset is None and args.source_local_run_report is None:
        parser.error("--dataset is required unless --source-local-run-report is provided")
    return args


def stable_json(data: Any) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def utc_now() -> str:
    return datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def sanitize_id(text: str) -> str:
    sanitized = re.sub(r"[^A-Za-z0-9_.-]+", "-", text.strip())
    sanitized = sanitized.strip(".-")
    return sanitized or "sweep"


def load_json_object(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"could not read JSON object {path}: {error}") from error
    if not isinstance(data, dict):
        raise RuntimeError(f"JSON root must be an object: {path}")
    return data


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        raise RuntimeError(f"could not checksum {path}: {error}") from error
    return f"sha256:{digest.hexdigest()}"


def dataset_metadata(dataset: Path) -> dict[str, Any]:
    if not dataset.exists():
        raise RuntimeError(f"dataset does not exist: {dataset}")
    if not dataset.is_file():
        raise RuntimeError(f"dataset is not a file: {dataset}")
    try:
        with dataset.open("r", encoding="utf-8", newline="") as handle:
            reader = csv.reader(handle, delimiter="\t")
            header = next(reader, None)
            row_count = sum(1 for _row in reader)
    except OSError as error:
        raise RuntimeError(f"could not read dataset {dataset}: {error}") from error
    if header == EXPANDED_HEADER:
        dataset_format = "expanded-tsv"
    elif header == COMPACT_HEADER:
        dataset_format = "compact-tsv"
    else:
        raise RuntimeError(f"dataset header is not a supported pattern dataset TSV: {header!r}")
    return {
        "dataset_path": str(dataset),
        "dataset_checksum": sha256_file(dataset),
        "dataset_row_count": row_count,
        "dataset_format": dataset_format,
    }


def resolve_output_file(report_path: Path, value: Any) -> Path | None:
    if not isinstance(value, str) or not value:
        return None
    path = Path(value)
    if path.is_absolute():
        return path
    return report_path.parent / path


def source_summary(source_report_path: Path) -> tuple[dict[str, Any], Path | None]:
    report = load_json_object(source_report_path)
    output_files = report.get("output_files")
    dataset_path = None
    if isinstance(output_files, dict):
        dataset_path = resolve_output_file(source_report_path, output_files.get("pattern_dataset_tsv"))
    summary = {
        "source_run_id": report.get("run_id"),
        "measurement_split_policy": report.get("measurement_split_policy"),
        "dataset_output_format": report.get("dataset_output_format"),
        "sample_counts_by_split": report.get("sample_counts_by_split"),
        "sample_counts_by_phase": report.get("sample_counts_by_phase"),
        "dataset_example_rows": report.get("dataset_example_rows"),
        "dataset_feature_occurrence_count": report.get("dataset_feature_occurrence_count"),
        "pattern_set_id": report.get("pattern_set_id"),
        "source_kind": report.get("source_kind"),
        "trainer_mode": report.get("trainer_mode"),
    }
    return summary, dataset_path


def source_report_identity(source_report_path: Path | None) -> dict[str, Any] | None:
    if source_report_path is None:
        return None
    return {
        "path": str(source_report_path),
        "checksum": sha256_file(source_report_path) if source_report_path.exists() else None,
    }


def selected_configs(args: argparse.Namespace) -> list[SweepConfig]:
    configs = PRESETS[args.sweep_preset]
    if args.max_configs is not None:
        return configs[: args.max_configs]
    return configs


def config_dict(config: SweepConfig) -> dict[str, Any]:
    return {
        "config_id": config.config_id,
        "mode": config.mode,
        "epochs": config.epochs,
        "learning_rate": config.learning_rate,
        "lr_schedule": config.lr_schedule,
        "l2": config.l2,
        "weight_decay": config.weight_decay,
        "gradient_clip": config.gradient_clip,
        "early_stop_patience": config.early_stop_patience,
        "phase_balance": config.phase_balance,
        "max_phase_weight": config.max_phase_weight,
        "min_phase_weight": config.min_phase_weight,
        "eval_every_epoch": False,
    }


def run_metadata(
    args: argparse.Namespace,
    dataset_meta: dict[str, Any],
    config: SweepConfig,
    command: list[str],
    source_report_path: Path | None,
    source_run_summary: dict[str, Any] | None,
) -> dict[str, Any]:
    return {
        "schema_version": SCHEMA_VERSION,
        "dataset_checksum": dataset_meta["dataset_checksum"],
        "dataset_format": dataset_meta["dataset_format"],
        "dataset_row_count": dataset_meta["dataset_row_count"],
        "sweep_preset": args.sweep_preset,
        "config": config_dict(config),
        "seed": args.seed,
        "trainer_path": str(args.trainer),
        "command": command,
        "source_local_run_report": source_report_identity(source_report_path),
        "source_run_summary": source_run_summary,
    }


def command_for_config(
    trainer: Path,
    dataset: Path,
    config: SweepConfig,
    run_dir: Path,
    seed: int,
) -> tuple[list[str], Path, Path]:
    weights = run_dir / "weights.json"
    report = run_dir / "trainer-report.json"
    command = [
        sys.executable,
        str(trainer),
        "--dataset",
        str(dataset),
        "--mode",
        config.mode,
        "--epochs",
        str(config.epochs),
        "--learning-rate",
        str(config.learning_rate),
        "--l2",
        str(config.l2),
        "--lr-schedule",
        config.lr_schedule,
        "--no-eval-every-epoch",
        "--seed",
        str(seed),
        "--weights-out",
        str(weights),
        "--report-out",
        str(report),
    ]
    if config.weight_decay is not None:
        command.extend(["--weight-decay", str(config.weight_decay)])
    if config.gradient_clip is not None:
        command.extend(["--gradient-clip", str(config.gradient_clip)])
    if config.early_stop_patience is not None:
        command.extend(["--early-stop-patience", str(config.early_stop_patience)])
    if config.mode == "pattern-sgd-v0d":
        if config.phase_balance is not None:
            command.extend(["--phase-balance", config.phase_balance])
        if config.max_phase_weight is not None:
            command.extend(["--max-phase-weight", str(config.max_phase_weight)])
        if config.min_phase_weight is not None:
            command.extend(["--min-phase-weight", str(config.min_phase_weight)])
    return command, weights, report


def numeric(value: Any) -> float | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)) and math.isfinite(float(value)):
        return float(value)
    return None


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


def split_metric(report: dict[str, Any], split: str, key: str) -> float | None:
    root = report.get("final_pattern_sgd_metrics")
    if not isinstance(root, dict):
        return None
    by_split = root.get("metrics_by_split")
    if not isinstance(by_split, dict):
        return None
    metrics = by_split.get(split)
    if not isinstance(metrics, dict):
        return None
    return numeric(metrics.get(key))


def first_present(*values: float | None) -> float | None:
    for value in values:
        if value is not None:
            return value
    return None


def extract_run_metrics(trainer_report: dict[str, Any]) -> dict[str, Any]:
    return {
        "best_validation_MAE": numeric(trainer_report.get("best_validation_MAE")),
        "final_validation_MAE": first_present(
            numeric(trainer_report.get("final_validation_MAE")),
            split_metric(trainer_report, "validation", "MAE"),
        ),
        "test_MAE": split_metric(trainer_report, "test", "MAE"),
        "train_MAE": split_metric(trainer_report, "train", "MAE"),
        "validation_sign_accuracy": split_metric(trainer_report, "validation", "sign_accuracy"),
        "test_sign_accuracy": split_metric(trainer_report, "test", "sign_accuracy"),
        "nonzero_weight_count": int_or_none(trainer_report.get("nonzero_weight_count")),
        "weight_l2_norm": numeric(trainer_report.get("weight_l2_norm")),
        "max_abs_weight": numeric(trainer_report.get("max_abs_weight")),
        "epoch_count": int_or_none(trainer_report.get("epochs_completed")),
    }


def warnings_for_run(
    status: str,
    metrics: dict[str, Any],
    source_run_summary: dict[str, Any] | None,
) -> list[str]:
    warnings: list[str] = []
    if status == "failed":
        warnings.append("failed_run")
    if metrics.get("final_validation_MAE") is None and metrics.get("best_validation_MAE") is None:
        warnings.append("validation_missing")
    if metrics.get("test_MAE") is None:
        warnings.append("test_missing")
    train_mae = metrics.get("train_MAE")
    validation_mae = metrics.get("final_validation_MAE") or metrics.get("best_validation_MAE")
    if train_mae is not None and validation_mae is not None:
        if validation_mae - train_mae > max(5.0, train_mae * 0.5):
            warnings.append("train_validation_gap_large")
    weight_l2 = metrics.get("weight_l2_norm")
    if weight_l2 is not None and weight_l2 > 5000.0:
        warnings.append("weight_l2_norm_large")
    max_abs = metrics.get("max_abs_weight")
    if max_abs is not None and max_abs > 64.0:
        warnings.append("max_abs_weight_large")
    if metrics.get("nonzero_weight_count") == 0:
        warnings.append("nonzero_weight_count_zero")
    if (
        source_run_summary is not None
        and source_run_summary.get("measurement_split_policy") is not None
        and source_run_summary.get("measurement_split_policy") != "connected-board-game"
    ):
        warnings.append("source_report_not_connected_split")
    return warnings


def source_warnings(source_run_summary: dict[str, Any] | None) -> list[str]:
    if (
        source_run_summary is not None
        and source_run_summary.get("measurement_split_policy") is not None
        and source_run_summary.get("measurement_split_policy") != "connected-board-game"
    ):
        return ["source_report_not_connected_split"]
    return []


def load_resume_report(path: Path, expected_trainer_version: str) -> dict[str, Any]:
    try:
        report = load_json_object(path)
    except RuntimeError as error:
        raise RuntimeError(f"resume trainer report is invalid: {error}") from error
    if report.get("trainer_version") != expected_trainer_version:
        raise RuntimeError(
            f"resume trainer report has unexpected trainer_version: {report.get('trainer_version')!r}"
        )
    if report.get("checksum") is None:
        raise RuntimeError("resume trainer report is missing checksum")
    return report


def validate_resume_metadata(path: Path, expected: dict[str, Any]) -> None:
    existing = load_json_object(path)
    if existing != expected:
        raise RuntimeError(f"resume metadata mismatch: {path}")


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


def run_command(command: list[str], stdout_log: Path, stderr_log: Path) -> int:
    stdout_log.parent.mkdir(parents=True, exist_ok=True)
    stderr_log.parent.mkdir(parents=True, exist_ok=True)
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
    return return_code


def base_run_entry(
    args: argparse.Namespace,
    config: SweepConfig,
    run_id: str,
    dataset: Path,
    run_dir: Path,
    log_dir: Path,
) -> dict[str, Any]:
    command, weights, report = command_for_config(args.trainer, dataset, config, run_dir, args.seed)
    metadata = run_dir / "sweep-run-metadata.json"
    stdout_log = log_dir / f"{config.config_id}.stdout.log"
    stderr_log = log_dir / f"{config.config_id}.stderr.log"
    return {
        "config_id": config.config_id,
        "run_id": run_id,
        "command": command,
        "status": "planned",
        "return_code": None,
        "started_at_utc": None,
        "finished_at_utc": None,
        "wall_time_sec": None,
        "weights_path": str(weights),
        "trainer_report_path": str(report),
        "sweep_metadata_path": str(metadata),
        "trainer_report_checksum": None,
        "weights_checksum": None,
        "stdout_log": str(stdout_log),
        "stderr_log": str(stderr_log),
        "best_validation_MAE": None,
        "final_validation_MAE": None,
        "test_MAE": None,
        "train_MAE": None,
        "validation_sign_accuracy": None,
        "test_sign_accuracy": None,
        "nonzero_weight_count": None,
        "weight_l2_norm": None,
        "max_abs_weight": None,
        "epoch_count": None,
        "warnings": [],
    }


def populate_run_from_report(
    entry: dict[str, Any],
    trainer_report: dict[str, Any],
    weights_path: Path,
    source_run_summary: dict[str, Any] | None,
) -> None:
    metrics = extract_run_metrics(trainer_report)
    entry.update(metrics)
    entry["trainer_report_checksum"] = trainer_report.get("checksum")
    entry["weights_checksum"] = trainer_report.get("weights_checksum")
    if weights_path.exists():
        entry["weights_checksum"] = entry["weights_checksum"] or sha256_file(weights_path)
    entry["warnings"] = warnings_for_run(str(entry.get("status")), metrics, source_run_summary)


def ranking_key(run: dict[str, Any]) -> tuple[float, float, float, float, int, str]:
    primary = run.get("best_validation_MAE")
    if primary is None:
        primary = run.get("final_validation_MAE")
    values = [
        primary,
        run.get("final_validation_MAE"),
        run.get("test_MAE"),
        run.get("weight_l2_norm"),
    ]
    numeric_values = [float(value) if isinstance(value, (int, float)) else math.inf for value in values]
    nonzero = run.get("nonzero_weight_count")
    return (
        numeric_values[0],
        numeric_values[1],
        numeric_values[2],
        numeric_values[3],
        int(nonzero) if isinstance(nonzero, int) else 2**63 - 1,
        str(run.get("config_id", "")),
    )


def best_config_id(runs: list[dict[str, Any]]) -> str | None:
    candidates = [
        run
        for run in runs
        if run.get("status") in {"success", "skipped"}
        and ranking_key(run)[0] != math.inf
    ]
    if not candidates:
        return None
    return str(min(candidates, key=ranking_key)["config_id"])


def markdown_cell(value: Any) -> str:
    if value is None:
        return ""
    return str(value).replace("|", "\\|").replace("\n", " ")


def write_sweep_summary(path: Path, report: dict[str, Any]) -> None:
    lines = [
        "# Pattern Trainer Sweep Summary",
        "",
        f"- sweep_preset: `{report.get('sweep_preset')}`",
        f"- dataset_format: `{report.get('dataset_format')}`",
        f"- dataset_row_count: `{report.get('dataset_row_count')}`",
        f"- best_config_id: `{report.get('best_config_id')}`",
        "",
        "| config | status | primary validation MAE | final validation MAE | test MAE | train MAE | validation sign accuracy | test sign accuracy | nonzero weights | weight L2 norm | max abs weight | epochs | warnings |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    best = report.get("best_config_id")
    for run in report.get("runs", []):
        primary = run.get("best_validation_MAE")
        if primary is None:
            primary = run.get("final_validation_MAE")
        label = str(run.get("config_id"))
        if label == best:
            label = f"**{label}**"
        lines.append(
            "| "
            + " | ".join(
                markdown_cell(value)
                for value in (
                    label,
                    run.get("status"),
                    primary,
                    run.get("final_validation_MAE"),
                    run.get("test_MAE"),
                    run.get("train_MAE"),
                    run.get("validation_sign_accuracy"),
                    run.get("test_sign_accuracy"),
                    run.get("nonzero_weight_count"),
                    run.get("weight_l2_norm"),
                    run.get("max_abs_weight"),
                    run.get("epoch_count"),
                    ", ".join(run.get("warnings") or []),
                )
            )
            + " |"
        )
    lines.extend(["", "Notes:", ""])
    for note in report.get("notes", []):
        lines.append(f"- {note}")
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def sweep_report(
    args: argparse.Namespace,
    created_at: str,
    dataset_meta: dict[str, Any],
    configs: list[SweepConfig],
    runs: list[dict[str, Any]],
    source_report_path: Path | None,
    source_run_summary: dict[str, Any] | None,
) -> dict[str, Any]:
    report = {
        "schema_version": SCHEMA_VERSION,
        "created_at_utc": created_at,
        "dataset_path": dataset_meta["dataset_path"],
        "dataset_checksum": dataset_meta["dataset_checksum"],
        "dataset_row_count": dataset_meta["dataset_row_count"],
        "dataset_format": dataset_meta["dataset_format"],
        "sweep_preset": args.sweep_preset,
        "run_prefix": args.run_prefix,
        "trainer_path": str(args.trainer),
        "source_local_run_report": str(source_report_path) if source_report_path is not None else None,
        "source_run_summary": source_run_summary,
        "dry_run": args.dry_run,
        "resume": args.resume,
        "keep_going": args.keep_going,
        "seed": args.seed,
        "configs": [config_dict(config) for config in configs],
        "runs": runs,
        "best_config_id": best_config_id(runs),
        "best_selection_policy": BEST_SELECTION_POLICY,
        "notes": LOCAL_ONLY_NOTES,
    }
    return report


def write_outputs(
    args: argparse.Namespace,
    created_at: str,
    dataset_meta: dict[str, Any],
    configs: list[SweepConfig],
    runs: list[dict[str, Any]],
    source_report_path: Path | None,
    source_run_summary: dict[str, Any] | None,
) -> dict[str, Any]:
    args.output_dir.mkdir(parents=True, exist_ok=True)
    report = sweep_report(
        args,
        created_at,
        dataset_meta,
        configs,
        runs,
        source_report_path,
        source_run_summary,
    )
    report_path = args.output_dir / "sweep-report.json"
    summary_path = args.output_dir / "sweep-summary.md"
    report_path.write_text(stable_json(report), encoding="utf-8")
    write_sweep_summary(summary_path, report)
    return report


def main() -> int:
    args = parse_args()
    created_at = args.created_at_utc or utc_now()
    configs = selected_configs(args)
    source_run_summary = None
    source_report_path = args.source_local_run_report
    try:
        if source_report_path is not None:
            source_run_summary, source_dataset = source_summary(source_report_path)
            if args.dataset is None:
                args.dataset = source_dataset
        if args.dataset is None:
            raise RuntimeError("--dataset could not be resolved from source local run report")
        dataset = args.dataset
        dataset_meta = dataset_metadata(dataset)
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    runs_dir = args.output_dir / "runs"
    log_dir = args.output_dir / "logs"
    runs: list[dict[str, Any]] = []
    exit_code = 0

    for config in configs:
        run_id = sanitize_id(f"{args.run_prefix}-{config.config_id}")
        run_dir = runs_dir / config.config_id
        entry = base_run_entry(args, config, run_id, dataset, run_dir, log_dir)
        entry["warnings"] = source_warnings(source_run_summary)
        runs.append(entry)
        weights_path = Path(entry["weights_path"])
        trainer_report_path = Path(entry["trainer_report_path"])
        metadata_path = Path(entry["sweep_metadata_path"])
        expected_metadata = run_metadata(
            args,
            dataset_meta,
            config,
            entry["command"],
            source_report_path,
            source_run_summary,
        )

        try:
            if args.resume:
                resume_files_exist = (
                    trainer_report_path.exists() and weights_path.exists() and metadata_path.exists()
                )
                if resume_files_exist:
                    try:
                        validate_resume_metadata(metadata_path, expected_metadata)
                        existing_report = load_resume_report(trainer_report_path, config.mode)
                    except RuntimeError as error:
                        entry["resume_validation_error"] = str(error)
                    else:
                        entry["status"] = "skipped"
                        entry["return_code"] = 0
                        entry["wall_time_sec"] = 0.0
                        populate_run_from_report(
                            entry, existing_report, weights_path, source_run_summary
                        )
                        continue
            if args.dry_run:
                continue
            run_dir.mkdir(parents=True, exist_ok=True)
            entry["started_at_utc"] = utc_now()
            start = time.monotonic()
            return_code = run_command(
                entry["command"],
                Path(entry["stdout_log"]),
                Path(entry["stderr_log"]),
            )
            entry["wall_time_sec"] = float(f"{time.monotonic() - start:.6g}")
            entry["finished_at_utc"] = utc_now()
            entry["return_code"] = return_code
            entry["status"] = "success" if return_code == 0 else "failed"
            if return_code == 0:
                trainer_report = load_json_object(trainer_report_path)
                metadata_path.write_text(stable_json(expected_metadata), encoding="utf-8")
                populate_run_from_report(entry, trainer_report, weights_path, source_run_summary)
            else:
                entry["warnings"] = warnings_for_run("failed", {}, source_run_summary)
                exit_code = 1
                if not args.keep_going:
                    break
        except RuntimeError as error:
            entry["status"] = "failed"
            entry["return_code"] = 1
            entry["error"] = str(error)
            entry["warnings"] = warnings_for_run("failed", {}, source_run_summary)
            exit_code = 1
            if not args.keep_going:
                break

    write_outputs(
        args,
        created_at,
        dataset_meta,
        configs,
        runs,
        source_report_path,
        source_run_summary,
    )
    print(f"sweep_report={args.output_dir / 'sweep-report.json'}")
    print(f"sweep_summary={args.output_dir / 'sweep-summary.md'}")
    print(f"best_config_id={best_config_id(runs)}")
    print(f"status={'failed' if exit_code else 'ok'}")
    for run in runs:
        if args.dry_run:
            print(f"planned_command[{run['config_id']}]={' '.join(run['command'])}")
        else:
            print(f"run[{run['config_id']}].status={run['status']}")
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
