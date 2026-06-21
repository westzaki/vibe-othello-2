#!/usr/bin/env python3
"""Run a small local-only late-phase observed-vs-exact-teacher fitting diagnostic."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import subprocess
import sys
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


NORMALIZED_HEADER_V2 = [
    "record_id",
    "position_id",
    "game_group_id",
    "board_id",
    "source_occurrence_id",
    "source_dataset_id",
    "split",
    "board_a1_to_h8",
    "label_kind",
    "label_unit",
    "label_perspective",
    "label_score_side_to_move",
    "occupied_count",
    "phase",
    "player_disc_count",
    "opponent_disc_count",
    "empty_count",
]


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--normalized-tsv", type=Path)
    source.add_argument("--local-training-run-report", type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--max-empty", type=int, default=12)
    parser.add_argument("--max-positions", type=int)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--trainer-mode", choices=("pattern-sgd-v0c", "pattern-sgd-v0d"), default="pattern-sgd-v0c")
    parser.add_argument("--epochs", type=int, default=4)
    parser.add_argument("--learning-rate", type=float, default=0.05)
    parser.add_argument("--weight-decay", type=float, default=0.0)
    parser.add_argument("--dataset-output-format", choices=("compact-tsv", "expanded-tsv"), default="compact-tsv")
    parser.add_argument("--pattern-set", default="buro-lite")
    parser.add_argument(
        "--generator",
        type=Path,
        default=root / "build/tools/pattern/labels/vibe-othello-generate-exact-endgame-teacher-labels",
    )
    parser.add_argument(
        "--overlay",
        type=Path,
        default=root / "tools/pattern/labels/apply_teacher_labels.py",
    )
    parser.add_argument(
        "--dataset-exe",
        type=Path,
        default=root / "build/tools/pattern/dataset/vibe-othello-pattern-dataset-smoke",
    )
    parser.add_argument("--trainer", type=Path, default=root / "tools/pattern/train/train_v0a.py")
    args = parser.parse_args()
    if args.max_empty < 0 or args.max_empty > 64:
        parser.error("--max-empty must be in [0, 64]")
    if args.max_positions is not None and args.max_positions <= 0:
        parser.error("--max-positions must be positive")
    if args.seed < 0:
        parser.error("--seed must be non-negative")
    if args.epochs < 0:
        parser.error("--epochs must be non-negative")
    if args.learning_rate < 0.0:
        parser.error("--learning-rate must be non-negative")
    if args.weight_decay < 0.0:
        parser.error("--weight-decay must be non-negative")
    return args


def stable_json(data: Any) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def report_path(path: Path) -> str:
    return path.name if path.is_absolute() else str(path)


def load_json(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise RuntimeError(f"JSON root must be an object: {path}")
    return data


def resolve_normalized_from_run_report(report_path: Path) -> Path:
    report = load_json(report_path)
    output_files = report.get("output_files")
    if not isinstance(output_files, dict):
        raise RuntimeError("--local-training-run-report is missing output_files")
    candidates = (
        output_files.get("resplit_normalized_tsv"),
        output_files.get("sampled_normalized_tsv"),
        output_files.get("normalized_tsv"),
    )
    for candidate in candidates:
        if isinstance(candidate, str) and candidate:
            path = Path(candidate)
            if not path.is_absolute():
                path = report_path.parent / path
            if path.exists():
                return path
    raise RuntimeError("could not resolve normalized TSV from local training run report")


def fnv1a64(text: str) -> int:
    value = 14695981039346656037
    for byte in text.encode("utf-8"):
        value ^= byte
        value = (value * 1099511628211) & ((1 << 64) - 1)
    return value


def selection_key(board_id: str, seed: int) -> int:
    return fnv1a64(f"{seed}\t{board_id}")


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if reader.fieldnames != NORMALIZED_HEADER_V2:
            raise RuntimeError("normalized TSV must use schema v2 header")
        return list(reader)


def write_rows(path: Path, rows: list[dict[str, str]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, delimiter="\t", lineterminator="\n", fieldnames=NORMALIZED_HEADER_V2)
        writer.writeheader()
        writer.writerows(rows)


def selected_low_empty_rows(rows: list[dict[str, str]], max_empty: int, max_positions: int | None, seed: int) -> tuple[list[dict[str, str]], dict[str, Any]]:
    eligible: list[dict[str, str]] = []
    board_ids: set[str] = set()
    for row in rows:
        try:
            empty = int(row["empty_count"])
        except ValueError as error:
            raise RuntimeError("empty_count must be an integer") from error
        if empty <= max_empty:
            eligible.append(row)
            board_ids.add(row["board_id"])
    selected_ids = sorted(board_ids)
    if max_positions is not None and len(selected_ids) > max_positions:
        selected_ids = sorted(
            sorted(selected_ids, key=lambda board_id: (selection_key(board_id, seed), board_id))[:max_positions]
        )
    selected_set = set(selected_ids)
    selected_rows = [row for row in eligible if row["board_id"] in selected_set]
    return selected_rows, {
        "input_rows": len(rows),
        "eligible_rows": len(eligible),
        "eligible_unique_boards": len(board_ids),
        "selected_unique_boards": len(selected_ids),
        "selected_rows": len(selected_rows),
    }


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return f"sha256:{digest.hexdigest()}"


def run_or_fail(command: list[str], stdout_path: Path | None = None) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        raise RuntimeError(f"command failed: {' '.join(command)}")
    if stdout_path is not None:
        stdout_path.write_text(result.stdout, encoding="utf-8")
    return result


def build_dataset(args: argparse.Namespace, normalized: Path, dataset: Path, report: Path) -> None:
    command = [
        str(args.dataset_exe),
        "--normalized-tsv",
        str(normalized),
        "--report",
        str(report),
        "--output-format",
        args.dataset_output_format,
        "--pattern-set",
        args.pattern_set,
    ]
    run_or_fail(command, stdout_path=dataset)


def train(args: argparse.Namespace, dataset: Path, weights: Path, report: Path, seed: int) -> None:
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
        "--weight-decay",
        str(args.weight_decay),
        "--weights-out",
        str(weights),
        "--report-out",
        str(report),
        "--seed",
        str(seed),
    ]
    if args.trainer_mode == "pattern-sgd-v0d":
        command.extend(["--phase-balance", "sqrt-inverse-count"])
    run_or_fail(command)


def split_metrics(trainer_report: dict[str, Any], split: str) -> dict[str, Any]:
    final = trainer_report.get("final_pattern_sgd_metrics")
    if not isinstance(final, dict):
        return {}
    by_split = final.get("metrics_by_split")
    if not isinstance(by_split, dict):
        return {}
    metrics = by_split.get(split)
    return metrics if isinstance(metrics, dict) else {}


def phase_metrics(trainer_report: dict[str, Any], split: str) -> dict[str, Any]:
    final = trainer_report.get("final_pattern_sgd_metrics")
    if not isinstance(final, dict):
        return {}
    by_split_phase = final.get("metrics_by_split_phase")
    if not isinstance(by_split_phase, dict):
        return {}
    metrics = by_split_phase.get(split)
    return metrics if isinstance(metrics, dict) else {}


def summarize_run(name: str, trainer_report: dict[str, Any], dataset_report: dict[str, Any], dataset: Path) -> dict[str, Any]:
    return {
        "name": name,
        "dataset_sha256": sha256_file(dataset),
        "accepted_examples": trainer_report.get("accepted_examples"),
        "dataset_example_rows": dataset_report.get("example_rows"),
        "dataset_feature_occurrence_count": dataset_report.get("feature_occurrence_count"),
        "best_validation_MAE": trainer_report.get("best_validation_MAE"),
        "final_validation_MAE": trainer_report.get("final_validation_MAE"),
        "train_MAE": split_metrics(trainer_report, "train").get("MAE"),
        "validation_MAE": split_metrics(trainer_report, "validation").get("MAE"),
        "test_MAE": split_metrics(trainer_report, "test").get("MAE"),
        "train_sign_accuracy": split_metrics(trainer_report, "train").get("sign_accuracy"),
        "validation_sign_accuracy": split_metrics(trainer_report, "validation").get("sign_accuracy"),
        "test_sign_accuracy": split_metrics(trainer_report, "test").get("sign_accuracy"),
        "phase_level_MAE": {
            "train": phase_metrics(trainer_report, "train"),
            "validation": phase_metrics(trainer_report, "validation"),
            "test": phase_metrics(trainer_report, "test"),
        },
    }


def improvement(observed: float | None, exact: float | None) -> dict[str, Any]:
    if observed is None or exact is None:
        return {"absolute_MAE_improvement": None, "relative_MAE_improvement": None, "meaningful": False}
    absolute = observed - exact
    relative = absolute / observed if observed != 0 else None
    return {
        "absolute_MAE_improvement": absolute,
        "relative_MAE_improvement": relative,
        "meaningful": absolute >= 0.2 or (relative is not None and relative >= 0.01),
    }


def write_summary(path: Path, report: dict[str, Any]) -> None:
    decision = report["decision"]
    observed = report["runs"]["observed"]
    exact = report["runs"]["exact_teacher"]
    lines = [
        "# Exact Teacher Late-Phase Campaign",
        "",
        "Local-only fitting diagnostic. Not a strength claim, Elo result, match bench, self-play result, or production artifact.",
        "",
        "## Validation Primary",
        "",
        f"- observed best validation MAE: {observed.get('best_validation_MAE')}",
        f"- exact-teacher best validation MAE: {exact.get('best_validation_MAE')}",
        f"- absolute MAE improvement: {decision.get('absolute_MAE_improvement')}",
        f"- relative MAE improvement: {decision.get('relative_MAE_improvement')}",
        f"- meaningful improvement: {decision.get('meaningful')}",
        "",
        "## Test Reporting",
        "",
        f"- observed test MAE: {observed.get('test_MAE')}",
        f"- exact-teacher test MAE: {exact.get('test_MAE')}",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    args = parse_args()
    try:
        normalized = args.normalized_tsv or resolve_normalized_from_run_report(args.local_training_run_report)
        rows = read_rows(normalized)
        selected_rows, selection_report = selected_low_empty_rows(rows, args.max_empty, args.max_positions, args.seed)
        if not selected_rows:
            raise RuntimeError("no low-empty rows selected")
        args.output_dir.mkdir(parents=True, exist_ok=True)
        selected_normalized = args.output_dir / "selected-low-empty-normalized.tsv"
        teacher_labels = args.output_dir / "exact-teacher-labels.tsv"
        generator_report = args.output_dir / "exact-teacher-report.json"
        teacher_normalized = args.output_dir / "teacher-normalized.tsv"
        overlay_report = args.output_dir / "teacher-overlay-report.json"
        observed_dataset = args.output_dir / "observed-pattern-dataset.tsv"
        observed_dataset_report = args.output_dir / "observed-pattern-dataset-report.json"
        exact_dataset = args.output_dir / "exact-teacher-pattern-dataset.tsv"
        exact_dataset_report = args.output_dir / "exact-teacher-pattern-dataset-report.json"
        observed_weights = args.output_dir / "observed-weights.json"
        observed_trainer_report = args.output_dir / "observed-trainer-report.json"
        exact_weights = args.output_dir / "exact-teacher-weights.json"
        exact_trainer_report = args.output_dir / "exact-teacher-trainer-report.json"
        campaign_report = args.output_dir / "campaign-report.json"
        campaign_summary = args.output_dir / "campaign-summary.md"

        write_rows(selected_normalized, selected_rows)
        run_or_fail(
            [
                str(args.generator),
                "--normalized-tsv",
                str(selected_normalized),
                "--teacher-labels-out",
                str(teacher_labels),
                "--report-out",
                str(generator_report),
                "--max-empty",
                str(args.max_empty),
                "--seed",
                str(args.seed),
            ]
        )
        run_or_fail(
            [
                sys.executable,
                str(args.overlay),
                "--normalized-tsv",
                str(selected_normalized),
                "--teacher-labels",
                str(teacher_labels),
                "--output",
                str(teacher_normalized),
                "--report",
                str(overlay_report),
                "--missing-policy",
                "drop",
            ]
        )
        build_dataset(args, selected_normalized, observed_dataset, observed_dataset_report)
        build_dataset(args, teacher_normalized, exact_dataset, exact_dataset_report)
        train(args, observed_dataset, observed_weights, observed_trainer_report, args.seed)
        train(args, exact_dataset, exact_weights, exact_trainer_report, args.seed)

        observed_report = load_json(observed_trainer_report)
        exact_report = load_json(exact_trainer_report)
        observed_summary = summarize_run(
            "observed", observed_report, load_json(observed_dataset_report), observed_dataset
        )
        exact_summary = summarize_run(
            "exact_teacher", exact_report, load_json(exact_dataset_report), exact_dataset
        )
        decision = improvement(
            observed_summary.get("best_validation_MAE"), exact_summary.get("best_validation_MAE")
        )
        report = {
            "schema_version": 1,
            "created_at_utc": datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
            "normalized_input_path": report_path(normalized),
            "max_empty": args.max_empty,
            "max_positions": args.max_positions,
            "seed": args.seed,
            "trainer_mode": args.trainer_mode,
            "epochs": args.epochs,
            "learning_rate": args.learning_rate,
            "weight_decay": args.weight_decay,
            "pattern_set": args.pattern_set,
            "selection": selection_report,
            "teacher_coverage": load_json(overlay_report),
            "exact_solver": load_json(generator_report),
            "runs": {"observed": observed_summary, "exact_teacher": exact_summary},
            "decision": decision
            | {
                "primary_metric": "best_validation_MAE",
                "test_metric_policy": "reporting/tie-break only",
            },
            "warnings": [
                "local-only diagnostic; generated labels, datasets, weights, reports, and logs must not be committed",
                "not a strength claim",
                "not an Elo result",
                "not match bench",
                "not self-play",
                "not a production artifact",
            ],
        }
        campaign_report.write_text(stable_json(report), encoding="utf-8")
        write_summary(campaign_summary, report)
        print(f"campaign_report={campaign_report}")
        print(f"campaign_summary={campaign_summary}")
        print(f"meaningful={report['decision']['meaningful']}")
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
