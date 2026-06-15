#!/usr/bin/env python3
"""Train the phase-bias-v0a pattern baseline from pattern rows TSV."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


EXPECTED_HEADER = [
    "record_id",
    "ply",
    "split",
    "label_final_disc_diff",
    "phase",
    "pattern_id",
    "instance",
    "ternary_index",
]
SPLITS = ("train", "validation", "test")
PHASES = tuple(range(13))
SCHEMA_VERSION = 1
TRAINER_VERSION = "phase-bias-v0a"


@dataclass(frozen=True)
class AcceptedRow:
    split: str
    label: int
    phase: int


@dataclass(frozen=True)
class LoadResult:
    input_rows: int
    accepted_rows: list[AcceptedRow]
    rejected_rows: int
    notes: list[str]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", required=True, type=Path)
    parser.add_argument("--weights-out", required=True, type=Path)
    parser.add_argument("--report-out", required=True, type=Path)
    return parser.parse_args()


def parse_int(text: str | None) -> int | None:
    if text is None:
        return None
    try:
        value = int(text)
    except ValueError:
        return None
    if str(value) != text:
        return None
    return value


def validate_row(row: dict[str, str], line_number: int) -> tuple[AcceptedRow | None, str | None]:
    record_id = row["record_id"]
    pattern_id = row["pattern_id"]
    if not record_id:
        return None, f"line {line_number}: record_id is empty"
    if not pattern_id:
        return None, f"line {line_number}: pattern_id is empty"

    split = row["split"]
    if split not in SPLITS:
        return None, f"line {line_number}: split must be train, validation, or test"

    ply = parse_int(row["ply"])
    label = parse_int(row["label_final_disc_diff"])
    phase = parse_int(row["phase"])
    instance = parse_int(row["instance"])
    ternary_index = parse_int(row["ternary_index"])
    if ply is None or ply < 0:
        return None, f"line {line_number}: ply must be a non-negative integer"
    if label is None or label < -64 or label > 64:
        return None, f"line {line_number}: label_final_disc_diff must be in [-64, 64]"
    if phase is None or phase < 0 or phase > 12:
        return None, f"line {line_number}: phase must be in [0, 12]"
    if instance is None or instance < 0:
        return None, f"line {line_number}: instance must be a non-negative integer"
    if ternary_index is None or ternary_index < 0:
        return None, f"line {line_number}: ternary_index must be a non-negative integer"
    return AcceptedRow(split=split, label=label, phase=phase), None


def load_rows(path: Path) -> LoadResult:
    accepted: list[AcceptedRow] = []
    notes: list[str] = []
    input_rows = 0
    rejected_rows = 0
    try:
        input_file = path.open(newline="", encoding="utf-8")
    except OSError as error:
        raise RuntimeError(f"cannot read dataset: {path}: {error}") from error

    with input_file:
        reader = csv.DictReader(input_file, delimiter="\t")
        if reader.fieldnames != EXPECTED_HEADER:
            raise RuntimeError("unexpected TSV header")
        for line_number, row in enumerate(reader, start=2):
            input_rows += 1
            if row is None or set(row) != set(EXPECTED_HEADER) or None in row:
                rejected_rows += 1
                notes.append(f"line {line_number}: expected 8 TSV fields")
                continue
            accepted_row, error = validate_row(row, line_number)
            if accepted_row is None:
                rejected_rows += 1
                if error is not None:
                    notes.append(error)
                continue
            accepted.append(accepted_row)

    if input_rows == 0:
        raise RuntimeError("dataset has no rows")
    return LoadResult(
        input_rows=input_rows,
        accepted_rows=accepted,
        rejected_rows=rejected_rows,
        notes=notes,
    )


def mean(values: list[int]) -> float:
    return sum(values) / len(values) if values else 0.0


def train_phase_bias(rows: list[AcceptedRow]) -> dict[str, float]:
    labels_by_phase: dict[int, list[int]] = {phase: [] for phase in PHASES}
    for row in rows:
        if row.split == "train":
            labels_by_phase[row.phase].append(row.label)
    return {str(phase): mean(labels_by_phase[phase]) for phase in PHASES}


def sign(value: float) -> int:
    if value > 0:
        return 1
    if value < 0:
        return -1
    return 0


def metrics_for_rows(rows: list[AcceptedRow], phase_bias: dict[str, float]) -> dict[str, Any]:
    if not rows:
        return {"rows": 0, "MAE": None, "RMSE": None, "sign_accuracy": None}

    absolute_error = 0.0
    squared_error = 0.0
    sign_matches = 0
    for row in rows:
        prediction = phase_bias[str(row.phase)]
        error = prediction - row.label
        absolute_error += abs(error)
        squared_error += error * error
        if sign(prediction) == sign(row.label):
            sign_matches += 1
    row_count = len(rows)
    return {
        "rows": row_count,
        "MAE": absolute_error / row_count,
        "RMSE": math.sqrt(squared_error / row_count),
        "sign_accuracy": sign_matches / row_count,
    }


def stable_float(value: Any) -> Any:
    if isinstance(value, float):
        return float(f"{value:.12g}")
    if isinstance(value, dict):
        return {key: stable_float(nested) for key, nested in value.items()}
    if isinstance(value, list):
        return [stable_float(nested) for nested in value]
    return value


def report_without_checksum(load_result: LoadResult, phase_bias: dict[str, float]) -> dict[str, Any]:
    accepted = load_result.accepted_rows
    rows_by_split = {split: [row for row in accepted if row.split == split] for split in SPLITS}
    rows_by_phase = {phase: [row for row in accepted if row.phase == phase] for phase in PHASES}

    report = {
        "schema_version": SCHEMA_VERSION,
        "trainer_version": TRAINER_VERSION,
        "input_rows": load_result.input_rows,
        "accepted_rows": len(accepted),
        "rejected_rows": load_result.rejected_rows,
        "counts_by_split": {split: len(rows_by_split[split]) for split in SPLITS},
        "counts_by_phase": {str(phase): len(rows_by_phase[phase]) for phase in PHASES},
        "metrics_by_split": {
            split: metrics_for_rows(rows_by_split[split], phase_bias) for split in SPLITS
        },
        "metrics_by_phase": {
            str(phase): metrics_for_rows(rows_by_phase[phase], phase_bias) for phase in PHASES
        },
        "phase_bias": phase_bias,
        "notes": [
            "phase-bias baseline only; pattern weights, SGD, artifact export, and self-play are out of scope",
            "validation and test rows are used only for metrics",
            "metrics are row-weighted over emitted pattern rows",
            *load_result.notes,
        ],
    }
    return stable_float(report)


def validate_training_rows(rows: list[AcceptedRow]) -> None:
    if not rows:
        raise RuntimeError("dataset has no accepted rows")
    if not any(row.split == "train" for row in rows):
        raise RuntimeError("dataset has no accepted train rows")


def checksum_for(report: dict[str, Any], weights_text: str) -> str:
    payload = json.dumps(report, sort_keys=True, separators=(",", ":")).encode("utf-8")
    digest = hashlib.sha256(payload + b"\n" + weights_text.encode("utf-8")).hexdigest()
    return f"sha256:{digest}"


def weights_tsv(phase_bias: dict[str, float]) -> str:
    lines = ["phase\tbias"]
    for phase in PHASES:
        lines.append(f"{phase}\t{phase_bias[str(phase)]:.12g}")
    return "\n".join(lines) + "\n"


def write_text(path: Path, text: str) -> None:
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    except OSError as error:
        raise RuntimeError(f"cannot write {path}: {error}") from error


def main() -> int:
    args = parse_args()
    try:
        load_result = load_rows(args.dataset)
        validate_training_rows(load_result.accepted_rows)
        phase_bias = train_phase_bias(load_result.accepted_rows)
        weights_text = weights_tsv(phase_bias)
        report = report_without_checksum(load_result, phase_bias)
        report["checksum"] = checksum_for(report, weights_text)
        report_text = json.dumps(report, indent=2, sort_keys=False) + "\n"
        write_text(args.weights_out, weights_text)
        write_text(args.report_out, report_text)
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1

    print(
        "summary "
        f"input_rows={load_result.input_rows} "
        f"accepted_rows={len(load_result.accepted_rows)} "
        f"rejected_rows={load_result.rejected_rows} "
        f"trainer_version={TRAINER_VERSION}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
