#!/usr/bin/env python3
"""Train tiny pattern-learning baselines from pattern rows TSV."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import random
import sys
from dataclasses import dataclass, field
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
COMPACT_HEADER = [
    "record_id",
    "ply",
    "split",
    "label_final_disc_diff",
    "phase",
    "pattern_features",
]
SPLITS = ("train", "validation", "test")
PHASES = tuple(range(13))
SCHEMA_VERSION = 1
TRAINER_VERSION_V0A = "phase-bias-v0a"
TRAINER_VERSION_V0B = "pattern-sgd-v0b"
TRAINER_VERSION_V0C = "pattern-sgd-v0c"


@dataclass(frozen=True)
class Feature:
    pattern_id: str
    instance: int
    ternary_index: int


@dataclass(frozen=True)
class ParsedFeatureRow:
    record_id: str
    ply: int
    split: str
    label: int
    phase: int
    feature: Feature


@dataclass(frozen=True)
class Example:
    record_id: str
    ply: int
    split: str
    label_final_disc_diff: int
    phase: int
    features: list[Feature]


@dataclass
class ExampleBuilder:
    record_id: str
    ply: int
    split: str
    label: int
    phase: int
    features: list[Feature] = field(default_factory=list)
    row_count: int = 0
    reject_reasons: list[str] = field(default_factory=list)

    def add(self, row: ParsedFeatureRow, line_number: int) -> None:
        self.row_count += 1
        if (
            row.ply != self.ply
            or row.split != self.split
            or row.label != self.label
            or row.phase != self.phase
        ):
            self.reject_reasons.append(
                f"line {line_number}: record_id {self.record_id} has inconsistent metadata"
            )
        self.features.append(row.feature)

    def to_example(self) -> Example:
        return Example(
            record_id=self.record_id,
            ply=self.ply,
            split=self.split,
            label_final_disc_diff=self.label,
            phase=self.phase,
            features=self.features,
        )


@dataclass(frozen=True)
class DuplicateFeatureRows:
    total: int
    by_record_id: list[dict[str, Any]]


@dataclass(frozen=True)
class LoadResult:
    input_format: str
    input_rows: int
    input_feature_rows: int
    accepted_examples: list[Example]
    rejected_examples: int
    rejected_feature_rows: int
    duplicate_feature_rows: DuplicateFeatureRows
    notes: list[str]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", required=True, type=Path)
    parser.add_argument(
        "--mode",
        choices=(TRAINER_VERSION_V0A, TRAINER_VERSION_V0B, TRAINER_VERSION_V0C),
        default=TRAINER_VERSION_V0A,
    )
    parser.add_argument("--epochs", type=int, default=8)
    parser.add_argument("--learning-rate", type=float, default=0.05)
    parser.add_argument("--l2", type=float, default=0.0)
    parser.add_argument(
        "--weight-decay",
        type=float,
        help="v0c-only alias for --l2; applies to pattern weights, not phase bias.",
    )
    parser.add_argument(
        "--lr-schedule",
        choices=("constant", "inverse-sqrt"),
        default="constant",
        help="v0c-only learning-rate schedule.",
    )
    parser.add_argument(
        "--gradient-clip",
        type=float,
        help="v0c-only optional absolute clip applied to per-feature error gradients.",
    )
    parser.add_argument(
        "--early-stop-patience",
        type=int,
        help="v0c-only validation MAE patience in epochs.",
    )
    parser.add_argument(
        "--eval-every-epoch",
        dest="eval_every_epoch",
        action="store_true",
        default=True,
        help="v0c-only; evaluate train/validation diagnostics after every epoch.",
    )
    parser.add_argument(
        "--no-eval-every-epoch",
        dest="eval_every_epoch",
        action="store_false",
        help="v0c-only; keep only final epoch diagnostics.",
    )
    parser.add_argument(
        "--shuffle-policy",
        choices=("deterministic",),
        default="deterministic",
        help="v0c-only; deterministic shuffle is the only supported policy.",
    )
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--weights-out", required=True, type=Path)
    parser.add_argument("--report-out", required=True, type=Path)
    args = parser.parse_args()
    if args.epochs < 0:
        parser.error("--epochs must be non-negative")
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
    return args


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


def validate_row(
    row: dict[str, str], line_number: int
) -> tuple[ParsedFeatureRow | None, str | None]:
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
    return (
        ParsedFeatureRow(
            record_id=record_id,
            ply=ply,
            split=split,
            label=label,
            phase=phase,
            feature=Feature(pattern_id=pattern_id, instance=instance, ternary_index=ternary_index),
        ),
        None,
    )


def validate_common_example_fields(
    row: dict[str, str], line_number: int
) -> tuple[str | None, int | None, str | None, int | None, int | None, str | None]:
    record_id = row["record_id"]
    if not record_id:
        return None, None, None, None, None, f"line {line_number}: record_id is empty"
    split = row["split"]
    if split not in SPLITS:
        return None, None, None, None, None, (
            f"line {line_number}: split must be train, validation, or test"
        )
    ply = parse_int(row["ply"])
    label = parse_int(row["label_final_disc_diff"])
    phase = parse_int(row["phase"])
    if ply is None or ply < 0:
        return None, None, None, None, None, (
            f"line {line_number}: ply must be a non-negative integer"
        )
    if label is None or label < -64 or label > 64:
        return None, None, None, None, None, (
            f"line {line_number}: label_final_disc_diff must be in [-64, 64]"
        )
    if phase is None or phase < 0 or phase > 12:
        return None, None, None, None, None, f"line {line_number}: phase must be in [0, 12]"
    return record_id, ply, split, label, phase, None


def parse_compact_feature_token(token: str, line_number: int) -> tuple[Feature | None, str | None]:
    fields = token.split(":")
    if len(fields) != 3:
        return None, (
            f"line {line_number}: pattern_features token must be "
            "pattern_id:instance:ternary_index"
        )
    pattern_id, instance_text, ternary_text = fields
    if not pattern_id:
        return None, f"line {line_number}: pattern_id is empty"
    instance = parse_int(instance_text)
    ternary_index = parse_int(ternary_text)
    if instance is None or instance < 0:
        return None, f"line {line_number}: instance must be a non-negative integer"
    if ternary_index is None or ternary_index < 0:
        return None, f"line {line_number}: ternary_index must be a non-negative integer"
    return Feature(pattern_id=pattern_id, instance=instance, ternary_index=ternary_index), None


def validate_compact_row(row: dict[str, str], line_number: int) -> tuple[Example | None, str | None]:
    record_id, ply, split, label, phase, error = validate_common_example_fields(row, line_number)
    if error is not None:
        return None, error
    assert record_id is not None
    assert ply is not None
    assert split is not None
    assert label is not None
    assert phase is not None
    encoded = row["pattern_features"]
    if not encoded:
        return None, f"line {line_number}: pattern_features must be non-empty"
    features: list[Feature] = []
    for token in encoded.split(","):
        if not token:
            return None, f"line {line_number}: pattern_features contains an empty token"
        feature, feature_error = parse_compact_feature_token(token, line_number)
        if feature_error is not None:
            return None, feature_error
        assert feature is not None
        features.append(feature)
    if not features:
        return None, f"line {line_number}: pattern_features must be non-empty"
    return (
        Example(
            record_id=record_id,
            ply=ply,
            split=split,
            label_final_disc_diff=label,
            phase=phase,
            features=features,
        ),
        None,
    )


def row_record_id(row: dict[str, Any] | None) -> str | None:
    if row is None:
        return None
    value = row.get("record_id")
    return value if isinstance(value, str) and value else None


def duplicate_feature_rows_for(examples: list[Example]) -> DuplicateFeatureRows:
    total = 0
    by_record_id: list[dict[str, Any]] = []
    for example in examples:
        counts: dict[tuple[str, int, int], int] = {}
        for feature in example.features:
            key = (feature.pattern_id, feature.instance, feature.ternary_index)
            counts[key] = counts.get(key, 0) + 1
        duplicates = [
            {
                "pattern_id": pattern_id,
                "instance": instance,
                "ternary_index": ternary_index,
                "count": count,
            }
            for (pattern_id, instance, ternary_index), count in counts.items()
            if count > 1
        ]
        if duplicates:
            duplicate_count = sum(duplicate["count"] - 1 for duplicate in duplicates)
            total += duplicate_count
            by_record_id.append(
                {
                    "record_id": example.record_id,
                    "duplicate_feature_rows": duplicate_count,
                    "features": duplicates,
                }
            )
    return DuplicateFeatureRows(total=total, by_record_id=by_record_id)


def load_examples(path: Path) -> LoadResult:
    try:
        with path.open(newline="", encoding="utf-8") as header_file:
            header_reader = csv.reader(header_file, delimiter="\t")
            header = next(header_reader, None)
    except OSError as error:
        raise RuntimeError(f"cannot read dataset: {path}: {error}") from error
    if header == EXPECTED_HEADER:
        return load_expanded_examples(path)
    if header == COMPACT_HEADER:
        return load_compact_examples(path)
    raise RuntimeError("unexpected TSV header")


def load_expanded_examples(path: Path) -> LoadResult:
    builders: dict[str, ExampleBuilder] = {}
    rejected_record_ids: set[str] = set()
    invalid_rows_by_record_id: dict[str, int] = {}
    rejected_feature_rows_without_record_id = 0
    notes: list[str] = []
    input_feature_rows = 0
    try:
        input_file = path.open(newline="", encoding="utf-8")
    except OSError as error:
        raise RuntimeError(f"cannot read dataset: {path}: {error}") from error

    with input_file:
        reader = csv.DictReader(input_file, delimiter="\t")
        if reader.fieldnames != EXPECTED_HEADER:
            raise RuntimeError("unexpected TSV header")
        for line_number, row in enumerate(reader, start=2):
            input_feature_rows += 1
            record_id = row_record_id(row)
            if row is None or set(row) != set(EXPECTED_HEADER) or None in row:
                if record_id is None:
                    rejected_feature_rows_without_record_id += 1
                else:
                    invalid_rows_by_record_id[record_id] = (
                        invalid_rows_by_record_id.get(record_id, 0) + 1
                    )
                    rejected_record_ids.add(record_id)
                notes.append(f"line {line_number}: expected 8 TSV fields")
                continue

            parsed_row, error = validate_row(row, line_number)
            if parsed_row is None:
                if record_id is None:
                    rejected_feature_rows_without_record_id += 1
                else:
                    invalid_rows_by_record_id[record_id] = (
                        invalid_rows_by_record_id.get(record_id, 0) + 1
                    )
                    rejected_record_ids.add(record_id)
                if error is not None:
                    notes.append(error)
                continue

            builder = builders.get(parsed_row.record_id)
            if builder is None:
                builder = ExampleBuilder(
                    record_id=parsed_row.record_id,
                    ply=parsed_row.ply,
                    split=parsed_row.split,
                    label=parsed_row.label,
                    phase=parsed_row.phase,
                )
                builders[parsed_row.record_id] = builder
            builder.add(parsed_row, line_number)
            if builder.reject_reasons:
                rejected_record_ids.add(parsed_row.record_id)

    if input_feature_rows == 0:
        raise RuntimeError("dataset has no rows")

    accepted_examples: list[Example] = []
    rejected_feature_rows = rejected_feature_rows_without_record_id
    rejected_examples = 0
    for record_id, builder in builders.items():
        if record_id in rejected_record_ids:
            rejected_examples += 1
            rejected_feature_rows += builder.row_count + invalid_rows_by_record_id.get(record_id, 0)
            notes.extend(builder.reject_reasons)
            continue
        accepted_examples.append(builder.to_example())

    for record_id, invalid_rows in invalid_rows_by_record_id.items():
        if record_id not in builders:
            rejected_examples += 1
            rejected_feature_rows += invalid_rows

    duplicate_feature_rows = duplicate_feature_rows_for(accepted_examples)
    return LoadResult(
        input_format="expanded-tsv",
        input_rows=input_feature_rows,
        input_feature_rows=input_feature_rows,
        accepted_examples=accepted_examples,
        rejected_examples=rejected_examples,
        rejected_feature_rows=rejected_feature_rows,
        duplicate_feature_rows=duplicate_feature_rows,
        notes=notes,
    )


def load_compact_examples(path: Path) -> LoadResult:
    accepted_examples: list[Example] = []
    rejected_examples = 0
    notes: list[str] = []
    input_rows = 0
    try:
        input_file = path.open(newline="", encoding="utf-8")
    except OSError as error:
        raise RuntimeError(f"cannot read dataset: {path}: {error}") from error

    with input_file:
        reader = csv.DictReader(input_file, delimiter="\t")
        if reader.fieldnames != COMPACT_HEADER:
            raise RuntimeError("unexpected TSV header")
        for line_number, row in enumerate(reader, start=2):
            input_rows += 1
            if row is None or set(row) != set(COMPACT_HEADER) or None in row:
                rejected_examples += 1
                notes.append(f"line {line_number}: expected 6 TSV fields")
                continue
            example, error = validate_compact_row(row, line_number)
            if example is None:
                rejected_examples += 1
                if error is not None:
                    notes.append(error)
                continue
            accepted_examples.append(example)

    if input_rows == 0:
        raise RuntimeError("dataset has no rows")
    feature_occurrences = sum(len(example.features) for example in accepted_examples)
    duplicate_feature_rows = duplicate_feature_rows_for(accepted_examples)
    return LoadResult(
        input_format="compact-tsv",
        input_rows=input_rows,
        input_feature_rows=feature_occurrences,
        accepted_examples=accepted_examples,
        rejected_examples=rejected_examples,
        rejected_feature_rows=0,
        duplicate_feature_rows=duplicate_feature_rows,
        notes=notes,
    )


def mean(values: list[int]) -> float:
    return sum(values) / len(values) if values else 0.0


def train_phase_bias(examples: list[Example]) -> dict[str, float]:
    labels_by_phase: dict[int, list[int]] = {phase: [] for phase in PHASES}
    for example in examples:
        if example.split == "train":
            labels_by_phase[example.phase].append(example.label_final_disc_diff)
    return {str(phase): mean(labels_by_phase[phase]) for phase in PHASES}


PatternWeightKey = tuple[int, str, int]
PatternWeights = dict[PatternWeightKey, float]


def sign(value: float) -> int:
    if value > 0:
        return 1
    if value < 0:
        return -1
    return 0


def phase_bias_score(example: Example, phase_bias: dict[str, float]) -> float:
    return phase_bias[str(example.phase)]


def pattern_sgd_score(
    example: Example, phase_bias: dict[str, float], pattern_weights: PatternWeights
) -> float:
    score = phase_bias_score(example, phase_bias)
    for feature in example.features:
        score += pattern_weights.get(
            (example.phase, feature.pattern_id, feature.ternary_index), 0.0
        )
    return score


def metrics_for_examples(
    examples: list[Example],
    phase_bias: dict[str, float],
    pattern_weights: PatternWeights | None = None,
    include_sign_accuracy: bool = True,
    include_examples: bool = False,
    rows_are_feature_rows: bool = False,
) -> dict[str, Any]:
    feature_rows = sum(len(example.features) for example in examples)
    if not examples:
        metrics: dict[str, Any] = {
            "rows": 0,
            "MAE": None,
            "RMSE": None,
        }
        if include_examples:
            metrics["examples"] = 0
        if include_sign_accuracy:
            metrics["sign_accuracy"] = None
        return metrics

    absolute_error = 0.0
    squared_error = 0.0
    sign_matches = 0
    for example in examples:
        if pattern_weights is None:
            prediction = phase_bias_score(example, phase_bias)
        else:
            prediction = pattern_sgd_score(example, phase_bias, pattern_weights)
        error = prediction - example.label_final_disc_diff
        absolute_error += abs(error)
        squared_error += error * error
        if sign(prediction) == sign(example.label_final_disc_diff):
            sign_matches += 1
    example_count = len(examples)
    metrics = {
        "rows": feature_rows if rows_are_feature_rows else example_count,
        "MAE": absolute_error / example_count,
        "RMSE": math.sqrt(squared_error / example_count),
    }
    if include_examples:
        metrics["examples"] = example_count
    if include_sign_accuracy:
        metrics["sign_accuracy"] = sign_matches / example_count
    return metrics


def mean_float(values: list[float]) -> float | None:
    return sum(values) / len(values) if values else None


def stddev_float(values: list[float]) -> float | None:
    if not values:
        return None
    value_mean = sum(values) / len(values)
    return math.sqrt(sum((value - value_mean) ** 2 for value in values) / len(values))


def weight_l2_norm(pattern_weights: PatternWeights) -> float:
    return math.sqrt(sum(weight * weight for weight in pattern_weights.values()))


def max_abs_weight(pattern_weights: PatternWeights) -> float:
    return max((abs(weight) for weight in pattern_weights.values()), default=0.0)


def v0c_metrics_for_examples(
    examples: list[Example], phase_bias: dict[str, float], pattern_weights: PatternWeights
) -> dict[str, Any]:
    feature_occurrences = sum(len(example.features) for example in examples)
    if not examples:
        return {
            "examples": 0,
            "feature_occurrences": feature_occurrences,
            "MAE": None,
            "RMSE": None,
            "sign_accuracy": None,
            "residual_MAE": None,
            "residual_RMSE": None,
            "label_mean": None,
            "label_stddev": None,
            "residual_mean": None,
            "residual_stddev": None,
        }

    labels: list[float] = []
    residuals: list[float] = []
    absolute_error = 0.0
    squared_error = 0.0
    residual_absolute_error = 0.0
    residual_squared_error = 0.0
    sign_matches = 0
    for example in examples:
        label = float(example.label_final_disc_diff)
        phase_prediction = phase_bias_score(example, phase_bias)
        residual = label - phase_prediction
        prediction = pattern_sgd_score(example, phase_bias, pattern_weights)
        error = prediction - label
        residual_prediction = prediction - phase_prediction
        residual_error = residual_prediction - residual
        labels.append(label)
        residuals.append(residual)
        absolute_error += abs(error)
        squared_error += error * error
        residual_absolute_error += abs(residual_error)
        residual_squared_error += residual_error * residual_error
        if sign(prediction) == sign(label):
            sign_matches += 1
    example_count = len(examples)
    return {
        "examples": example_count,
        "feature_occurrences": feature_occurrences,
        "MAE": absolute_error / example_count,
        "RMSE": math.sqrt(squared_error / example_count),
        "sign_accuracy": sign_matches / example_count,
        "residual_MAE": residual_absolute_error / example_count,
        "residual_RMSE": math.sqrt(residual_squared_error / example_count),
        "label_mean": mean_float(labels),
        "label_stddev": stddev_float(labels),
        "residual_mean": mean_float(residuals),
        "residual_stddev": stddev_float(residuals),
    }


def v0c_metrics_by_split(
    examples: list[Example], phase_bias: dict[str, float], pattern_weights: PatternWeights
) -> dict[str, Any]:
    examples_by_split = split_examples(examples)
    return {
        split: v0c_metrics_for_examples(examples_by_split[split], phase_bias, pattern_weights)
        for split in SPLITS
    }


def v0c_metrics_by_split_phase(
    examples: list[Example], phase_bias: dict[str, float], pattern_weights: PatternWeights
) -> dict[str, Any]:
    result: dict[str, Any] = {}
    examples_by_split = split_examples(examples)
    for split in SPLITS:
        result[split] = {}
        for phase in PHASES:
            phase_bucket = [
                example for example in examples_by_split[split] if example.phase == phase
            ]
            result[split][str(phase)] = v0c_metrics_for_examples(
                phase_bucket, phase_bias, pattern_weights
            )
    return result


def stable_float(value: Any) -> Any:
    if isinstance(value, float):
        return float(f"{value:.12g}")
    if isinstance(value, dict):
        return {key: stable_float(nested) for key, nested in value.items()}
    if isinstance(value, list):
        return [stable_float(nested) for nested in value]
    return value


def report_without_checksum(
    load_result: LoadResult, phase_bias: dict[str, float]
) -> dict[str, Any]:
    accepted = load_result.accepted_examples
    examples_by_split = {
        split: [example for example in accepted if example.split == split] for split in SPLITS
    }
    examples_by_phase = {
        phase: [example for example in accepted if example.phase == phase] for phase in PHASES
    }
    feature_counts = [len(example.features) for example in accepted]

    report = {
        "schema_version": SCHEMA_VERSION,
        "trainer_version": TRAINER_VERSION_V0A,
        "input_format": load_result.input_format,
        "input_rows": load_result.input_rows,
        "input_feature_rows": load_result.input_feature_rows,
        "example_count": len(accepted),
        "feature_occurrence_count": sum(feature_counts),
        "average_features_per_example": (
            sum(feature_counts) / len(feature_counts) if feature_counts else 0.0
        ),
        "accepted_examples": len(accepted),
        "rejected_examples": load_result.rejected_examples,
        "rejected_feature_rows": load_result.rejected_feature_rows,
        "counts_by_split_examples": {
            split: len(examples_by_split[split]) for split in SPLITS
        },
        "counts_by_phase_examples": {
            str(phase): len(examples_by_phase[phase]) for phase in PHASES
        },
        "feature_rows_by_example_min": min(feature_counts) if feature_counts else 0,
        "feature_rows_by_example_max": max(feature_counts) if feature_counts else 0,
        "duplicate_feature_rows": load_result.duplicate_feature_rows.total,
        "duplicate_feature_rows_by_record_id": load_result.duplicate_feature_rows.by_record_id,
        "metrics_by_split": {
            split: metrics_for_examples(examples_by_split[split], phase_bias) for split in SPLITS
        },
        "metrics_by_phase": {
            str(phase): metrics_for_examples(examples_by_phase[phase], phase_bias)
            for phase in PHASES
        },
        "phase_bias": phase_bias,
        "notes": [
            "example-level phase-bias baseline; pattern weights, SGD, ridge regression, "
            "artifact export, and self-play are out of scope",
            "phase bias is learned from train split examples only",
            "validation and test examples are used only for metrics",
            "metrics are example-weighted over record_id groups",
            *load_result.notes,
        ],
    }
    return stable_float(report)


def validate_training_examples(load_result: LoadResult) -> None:
    examples = load_result.accepted_examples
    if not examples:
        detail = f"; first error: {load_result.notes[0]}" if load_result.notes else ""
        raise RuntimeError(f"dataset has no accepted examples{detail}")
    if not any(example.split == "train" for example in examples):
        raise RuntimeError("dataset has no accepted train examples")


def split_examples(examples: list[Example]) -> dict[str, list[Example]]:
    return {split: [example for example in examples if example.split == split] for split in SPLITS}


def phase_examples(examples: list[Example]) -> dict[int, list[Example]]:
    return {phase: [example for example in examples if example.phase == phase] for phase in PHASES}


def train_pattern_sgd(
    examples: list[Example],
    phase_bias: dict[str, float],
    epochs: int,
    learning_rate: float,
    l2: float,
    seed: int,
) -> tuple[PatternWeights, list[dict[str, Any]]]:
    train_examples = [example for example in examples if example.split == "train"]
    validation_examples = [example for example in examples if example.split == "validation"]
    pattern_weights: PatternWeights = {}
    metrics_by_epoch: list[dict[str, Any]] = []

    for epoch in range(epochs):
        epoch_examples = list(train_examples)
        random.Random(seed + epoch).shuffle(epoch_examples)
        for example in epoch_examples:
            feature_count = len(example.features)
            if feature_count == 0:
                continue
            error = (
                pattern_sgd_score(example, phase_bias, pattern_weights)
                - example.label_final_disc_diff
            )
            for feature in example.features:
                key = (example.phase, feature.pattern_id, feature.ternary_index)
                weight = pattern_weights.get(key, 0.0)
                weight -= learning_rate * ((error / feature_count) + (l2 * weight))
                if weight == 0.0:
                    pattern_weights.pop(key, None)
                else:
                    pattern_weights[key] = weight
        train_metrics = metrics_for_examples(train_examples, phase_bias, pattern_weights)
        validation_metrics = metrics_for_examples(validation_examples, phase_bias, pattern_weights)
        metrics_by_epoch.append(
            {
                "epoch": epoch + 1,
                "train": {
                    "MAE": train_metrics["MAE"],
                    "RMSE": train_metrics["RMSE"],
                },
                "validation": {
                    "MAE": validation_metrics["MAE"],
                    "RMSE": validation_metrics["RMSE"],
                },
            }
        )
    return pattern_weights, stable_float(metrics_by_epoch)


def v0c_weight_decay(args: argparse.Namespace) -> float:
    return args.weight_decay if args.weight_decay is not None else args.l2


def v0c_learning_rate(base_learning_rate: float, schedule: str, epoch: int) -> float:
    if schedule == "constant":
        return base_learning_rate
    if schedule == "inverse-sqrt":
        return base_learning_rate / math.sqrt(epoch)
    raise RuntimeError(f"unsupported lr schedule: {schedule}")


def train_pattern_sgd_v0c(
    examples: list[Example],
    phase_bias: dict[str, float],
    args: argparse.Namespace,
) -> tuple[PatternWeights, list[dict[str, Any]], dict[str, Any]]:
    train_examples = [example for example in examples if example.split == "train"]
    validation_examples = [example for example in examples if example.split == "validation"]
    pattern_weights: PatternWeights = {}
    best_weights: PatternWeights = {}
    metrics_by_epoch: list[dict[str, Any]] = []
    best_validation_mae: float | None = None
    best_epoch: int | None = None
    epochs_without_improvement = 0
    early_stop_triggered = False
    weight_decay = v0c_weight_decay(args)

    for epoch in range(1, args.epochs + 1):
        epoch_examples = list(train_examples)
        random.Random(args.seed + epoch - 1).shuffle(epoch_examples)
        learning_rate = v0c_learning_rate(args.learning_rate, args.lr_schedule, epoch)
        gradient_clip_count = 0
        updated_feature_occurrence_count = 0
        for example in epoch_examples:
            feature_count = len(example.features)
            if feature_count == 0:
                continue
            error = (
                pattern_sgd_score(example, phase_bias, pattern_weights)
                - example.label_final_disc_diff
            )
            for feature in example.features:
                key = (example.phase, feature.pattern_id, feature.ternary_index)
                weight = pattern_weights.get(key, 0.0)
                gradient = error / feature_count
                if args.gradient_clip is not None:
                    clipped = max(-args.gradient_clip, min(args.gradient_clip, gradient))
                    if clipped != gradient:
                        gradient_clip_count += 1
                    gradient = clipped
                weight -= learning_rate * (gradient + weight_decay * weight)
                updated_feature_occurrence_count += 1
                if weight == 0.0:
                    pattern_weights.pop(key, None)
                else:
                    pattern_weights[key] = weight

        train_metrics = v0c_metrics_for_examples(train_examples, phase_bias, pattern_weights)
        validation_metrics = v0c_metrics_for_examples(
            validation_examples, phase_bias, pattern_weights
        )
        validation_mae = validation_metrics["MAE"]
        if validation_mae is not None and (
            best_validation_mae is None or validation_mae < best_validation_mae
        ):
            best_validation_mae = validation_mae
            best_epoch = epoch
            best_weights = dict(pattern_weights)
            epochs_without_improvement = 0
        elif validation_mae is not None:
            epochs_without_improvement += 1

        if args.eval_every_epoch or epoch == args.epochs:
            metrics_by_epoch.append(
                {
                    "epoch": epoch,
                    "learning_rate": learning_rate,
                    "train": {
                        "MAE": train_metrics["MAE"],
                        "RMSE": train_metrics["RMSE"],
                        "sign_accuracy": train_metrics["sign_accuracy"],
                        "residual_MAE": train_metrics["residual_MAE"],
                        "residual_RMSE": train_metrics["residual_RMSE"],
                    },
                    "validation": {
                        "MAE": validation_metrics["MAE"],
                        "RMSE": validation_metrics["RMSE"],
                        "sign_accuracy": validation_metrics["sign_accuracy"],
                        "residual_MAE": validation_metrics["residual_MAE"],
                        "residual_RMSE": validation_metrics["residual_RMSE"],
                    },
                    "nonzero_weight_count": len(nonzero_pattern_weight_items(pattern_weights)),
                    "weight_l2_norm": weight_l2_norm(pattern_weights),
                    "max_abs_weight": max_abs_weight(pattern_weights),
                    "gradient_clip_count": gradient_clip_count
                    if args.gradient_clip is not None
                    else None,
                    "updated_feature_occurrence_count": updated_feature_occurrence_count,
                }
            )

        if (
            args.early_stop_patience is not None
            and validation_mae is not None
            and epochs_without_improvement >= args.early_stop_patience
        ):
            early_stop_triggered = True
            break

    if early_stop_triggered and best_epoch is not None:
        pattern_weights = best_weights
    epochs_completed = metrics_by_epoch[-1]["epoch"] if metrics_by_epoch else 0
    if args.epochs > 0 and not metrics_by_epoch:
        epochs_completed = epoch
    final_validation_metrics = v0c_metrics_for_examples(
        validation_examples, phase_bias, pattern_weights
    )
    state = {
        "epochs_requested": args.epochs,
        "epochs_completed": epochs_completed,
        "early_stop_triggered": early_stop_triggered,
        "best_epoch": best_epoch,
        "best_validation_MAE": best_validation_mae,
        "final_validation_MAE": final_validation_metrics["MAE"],
    }
    return pattern_weights, stable_float(metrics_by_epoch), stable_float(state)


def nonzero_pattern_weight_items(pattern_weights: PatternWeights) -> list[dict[str, Any]]:
    return [
        {
            "phase": phase,
            "pattern_id": pattern_id,
            "ternary_index": ternary_index,
            "weight": weight,
        }
        for (phase, pattern_id, ternary_index), weight in sorted(pattern_weights.items())
        if weight != 0.0
    ]


def weight_count_by_phase(pattern_weights: PatternWeights) -> dict[str, int]:
    counts = {str(phase): 0 for phase in PHASES}
    for phase, _pattern_id, _ternary_index in pattern_weights:
        counts[str(phase)] += 1
    return counts


def weight_count_by_pattern(pattern_weights: PatternWeights) -> dict[str, int]:
    counts: dict[str, int] = {}
    for _phase, pattern_id, _ternary_index in pattern_weights:
        counts[pattern_id] = counts.get(pattern_id, 0) + 1
    return {pattern_id: counts[pattern_id] for pattern_id in sorted(counts)}


def top_abs_weights(pattern_weights: PatternWeights, limit: int = 20) -> list[dict[str, Any]]:
    rows = nonzero_pattern_weight_items(pattern_weights)
    rows.sort(
        key=lambda row: (
            -abs(float(row["weight"])),
            int(row["phase"]),
            str(row["pattern_id"]),
            int(row["ternary_index"]),
        )
    )
    return rows[:limit]


def pattern_weights_json(phase_bias: dict[str, float], pattern_weights: PatternWeights) -> str:
    payload = {
        "schema_version": SCHEMA_VERSION,
        "trainer_version": TRAINER_VERSION_V0B,
        "phase_bias": {str(phase): phase_bias[str(phase)] for phase in PHASES},
        "pattern_weights": nonzero_pattern_weight_items(pattern_weights),
    }
    return json.dumps(stable_float(payload), indent=2, sort_keys=True) + "\n"


def weights_checksum_for(weights_text: str) -> str:
    return f"sha256:{hashlib.sha256(weights_text.encode('utf-8')).hexdigest()}"


def metrics_by_split(
    examples: list[Example], phase_bias: dict[str, float], pattern_weights: PatternWeights | None
) -> dict[str, Any]:
    examples_by_split = split_examples(examples)
    return {
        split: metrics_for_examples(
            examples_by_split[split],
            phase_bias,
            pattern_weights,
            include_examples=True,
            rows_are_feature_rows=True,
        )
        for split in SPLITS
    }


def metrics_by_phase(
    examples: list[Example], phase_bias: dict[str, float], pattern_weights: PatternWeights | None
) -> dict[str, Any]:
    examples_by_phase = phase_examples(examples)
    return {
        str(phase): metrics_for_examples(
            examples_by_phase[phase],
            phase_bias,
            pattern_weights,
            include_sign_accuracy=False,
            include_examples=True,
            rows_are_feature_rows=True,
        )
        for phase in PHASES
    }


def pattern_sgd_report_without_checksum(
    load_result: LoadResult,
    phase_bias: dict[str, float],
    pattern_weights: PatternWeights,
    metrics_by_epoch: list[dict[str, Any]],
    args: argparse.Namespace,
    weights_checksum: str,
) -> dict[str, Any]:
    accepted = load_result.accepted_examples
    examples_by_split = split_examples(accepted)
    examples_by_phase = phase_examples(accepted)
    feature_counts = [len(example.features) for example in accepted]
    nonzero_weights = nonzero_pattern_weight_items(pattern_weights)
    weight_l2_norm = math.sqrt(sum(weight * weight for weight in pattern_weights.values()))

    report = {
        "schema_version": SCHEMA_VERSION,
        "trainer_version": TRAINER_VERSION_V0B,
        "input_format": load_result.input_format,
        "input_rows": load_result.input_rows,
        "input_feature_rows": load_result.input_feature_rows,
        "example_count": len(accepted),
        "feature_occurrence_count": sum(feature_counts),
        "average_features_per_example": (
            sum(feature_counts) / len(feature_counts) if feature_counts else 0.0
        ),
        "accepted_examples": len(accepted),
        "rejected_examples": load_result.rejected_examples,
        "rejected_feature_rows": load_result.rejected_feature_rows,
        "counts_by_split_examples": {
            split: len(examples_by_split[split]) for split in SPLITS
        },
        "counts_by_phase_examples": {
            str(phase): len(examples_by_phase[phase]) for phase in PHASES
        },
        "feature_rows_by_example_min": min(feature_counts) if feature_counts else 0,
        "feature_rows_by_example_max": max(feature_counts) if feature_counts else 0,
        "duplicate_feature_rows": load_result.duplicate_feature_rows.total,
        "duplicate_feature_rows_by_record_id": load_result.duplicate_feature_rows.by_record_id,
        "epochs": args.epochs,
        "learning_rate": args.learning_rate,
        "l2": args.l2,
        "seed": args.seed,
        "shuffle_policy": "deterministic python random.Random(seed + epoch)",
        "baseline_phase_bias_metrics": {
            "phase_bias": phase_bias,
            "metrics_by_split": metrics_by_split(accepted, phase_bias, None),
            "metrics_by_phase": metrics_by_phase(accepted, phase_bias, None),
        },
        "final_pattern_sgd_metrics": {
            "metrics_by_split": metrics_by_split(accepted, phase_bias, pattern_weights),
            "metrics_by_phase": metrics_by_phase(accepted, phase_bias, pattern_weights),
        },
        "metrics_by_epoch": metrics_by_epoch,
        "nonzero_weight_count": len(nonzero_weights),
        "weight_l2_norm": weight_l2_norm,
        "weights_checksum": weights_checksum,
        "notes": [
            "pattern-sgd-v0b is an example-level pattern weight learning smoke trainer, "
            "not a production trainer",
            "phase bias is initialized from train split examples and held fixed during v0b SGD",
            "pattern weights are learned from train split examples only",
            "validation and test examples are used only for metrics",
            "feature occurrences are added once per feature row, matching runtime evaluator "
            "occurrence semantics",
            "duplicate feature rows are reported and intentionally kept as repeated contributions "
            "in v0b",
            "weights use a local intermediate JSON format; artifact export and runtime load are out of scope",
            *load_result.notes,
        ],
    }
    return stable_float(report)


def pattern_sgd_v0c_report_without_checksum(
    load_result: LoadResult,
    phase_bias: dict[str, float],
    pattern_weights: PatternWeights,
    metrics_by_epoch: list[dict[str, Any]],
    training_state: dict[str, Any],
    args: argparse.Namespace,
    weights_checksum: str,
) -> dict[str, Any]:
    accepted = load_result.accepted_examples
    examples_by_split = split_examples(accepted)
    examples_by_phase = phase_examples(accepted)
    feature_counts = [len(example.features) for example in accepted]
    nonzero_weights = nonzero_pattern_weight_items(pattern_weights)
    final_metrics_by_split = v0c_metrics_by_split(accepted, phase_bias, pattern_weights)
    baseline_metrics_by_split = v0c_metrics_by_split(accepted, phase_bias, {})

    report = {
        "schema_version": SCHEMA_VERSION,
        "trainer_version": TRAINER_VERSION_V0C,
        "input_format": load_result.input_format,
        "input_rows": load_result.input_rows,
        "input_feature_rows": load_result.input_feature_rows,
        "example_count": len(accepted),
        "feature_occurrence_count": sum(feature_counts),
        "average_features_per_example": (
            sum(feature_counts) / len(feature_counts) if feature_counts else 0.0
        ),
        "accepted_examples": len(accepted),
        "rejected_examples": load_result.rejected_examples,
        "rejected_feature_rows": load_result.rejected_feature_rows,
        "counts_by_split_examples": {
            split: len(examples_by_split[split]) for split in SPLITS
        },
        "counts_by_phase_examples": {
            str(phase): len(examples_by_phase[phase]) for phase in PHASES
        },
        "feature_rows_by_example_min": min(feature_counts) if feature_counts else 0,
        "feature_rows_by_example_max": max(feature_counts) if feature_counts else 0,
        "duplicate_feature_rows": load_result.duplicate_feature_rows.total,
        "duplicate_feature_rows_by_record_id": load_result.duplicate_feature_rows.by_record_id,
        "epochs_requested": training_state["epochs_requested"],
        "epochs_completed": training_state["epochs_completed"],
        "early_stop_triggered": training_state["early_stop_triggered"],
        "best_epoch": training_state["best_epoch"],
        "best_validation_MAE": training_state["best_validation_MAE"],
        "final_validation_MAE": training_state["final_validation_MAE"],
        "deterministic_seed": args.seed,
        "learning_rate": args.learning_rate,
        "lr_schedule": args.lr_schedule,
        "l2": args.l2,
        "weight_decay": v0c_weight_decay(args),
        "gradient_clip": args.gradient_clip,
        "early_stop_patience": args.early_stop_patience,
        "eval_every_epoch": args.eval_every_epoch,
        "shuffle_policy": "deterministic",
        "update_rule": {
            "loss": "squared-error SGD",
            "prediction": "phase_bias[phase] + sum(pattern_weight[phase, pattern_id, ternary_index])",
            "error": "prediction - label",
            "per_occurrence_gradient": "error / feature_count",
            "gradient_clip": "optional absolute clip applied to per-feature error gradient before weight decay",
            "weight_decay": "applied only to pattern weights",
            "phase_bias": "learned from train split and fixed during pattern SGD",
            "weight_key": "phase + pattern_id + ternary_index; instance is excluded",
        },
        "residual_definition": {
            "label": "label_final_disc_diff",
            "phase_bias_prediction": "phase_bias[phase]",
            "residual": "label - phase_bias_prediction",
        },
        "baseline_phase_bias_metrics": {
            "phase_bias": phase_bias,
            "metrics_by_split": baseline_metrics_by_split,
            "metrics_by_split_phase": v0c_metrics_by_split_phase(accepted, phase_bias, {}),
        },
        "final_pattern_sgd_metrics": {
            "metrics_by_split": final_metrics_by_split,
            "metrics_by_split_phase": v0c_metrics_by_split_phase(
                accepted, phase_bias, pattern_weights
            ),
        },
        "metrics_by_epoch": metrics_by_epoch,
        "nonzero_weight_count": len(nonzero_weights),
        "weight_l2_norm": weight_l2_norm(pattern_weights),
        "max_abs_weight": max_abs_weight(pattern_weights),
        "weights_by_phase": weight_count_by_phase(pattern_weights),
        "weights_by_pattern": weight_count_by_pattern(pattern_weights),
        "top_abs_weights": top_abs_weights(pattern_weights),
        "weights_checksum": weights_checksum,
        "notes": [
            "pattern-sgd-v0c is a local research trainer, not a production trainer",
            "v0c report metrics are fitting diagnostics, not strength, Elo, match bench, or self-play claims",
            "phase bias is learned from train split examples and held fixed during v0c SGD",
            "pattern weights are learned from train split examples only",
            "validation and test examples are used only for metrics and early stopping",
            "feature occurrences are added once per feature row, matching runtime evaluator occurrence semantics",
            "duplicate feature rows are reported and intentionally kept as repeated contributions in v0c",
            "weights JSON remains compatible with the v0b exporter schema",
            *load_result.notes,
        ],
    }
    return stable_float(report)


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


def run_phase_bias_v0a(load_result: LoadResult, args: argparse.Namespace) -> None:
    phase_bias = train_phase_bias(load_result.accepted_examples)
    weights_text = weights_tsv(phase_bias)
    report = report_without_checksum(load_result, phase_bias)
    report["checksum"] = checksum_for(report, weights_text)
    report_text = json.dumps(report, indent=2, sort_keys=False) + "\n"
    write_text(args.weights_out, weights_text)
    write_text(args.report_out, report_text)


def run_pattern_sgd_v0b(load_result: LoadResult, args: argparse.Namespace) -> None:
    phase_bias = train_phase_bias(load_result.accepted_examples)
    pattern_weights, metrics_by_epoch = train_pattern_sgd(
        load_result.accepted_examples,
        phase_bias,
        args.epochs,
        args.learning_rate,
        args.l2,
        args.seed,
    )
    weights_text = pattern_weights_json(phase_bias, pattern_weights)
    weights_checksum = weights_checksum_for(weights_text)
    report = pattern_sgd_report_without_checksum(
        load_result,
        phase_bias,
        pattern_weights,
        metrics_by_epoch,
        args,
        weights_checksum,
    )
    report["checksum"] = checksum_for(report, weights_text)
    report_text = json.dumps(report, indent=2, sort_keys=False) + "\n"
    write_text(args.weights_out, weights_text)
    write_text(args.report_out, report_text)


def run_pattern_sgd_v0c(load_result: LoadResult, args: argparse.Namespace) -> None:
    phase_bias = train_phase_bias(load_result.accepted_examples)
    pattern_weights, metrics_by_epoch, training_state = train_pattern_sgd_v0c(
        load_result.accepted_examples,
        phase_bias,
        args,
    )
    weights_text = pattern_weights_json(phase_bias, pattern_weights)
    weights_checksum = weights_checksum_for(weights_text)
    report = pattern_sgd_v0c_report_without_checksum(
        load_result,
        phase_bias,
        pattern_weights,
        metrics_by_epoch,
        training_state,
        args,
        weights_checksum,
    )
    report["checksum"] = checksum_for(report, weights_text)
    report_text = json.dumps(report, indent=2, sort_keys=False) + "\n"
    write_text(args.weights_out, weights_text)
    write_text(args.report_out, report_text)


def main() -> int:
    args = parse_args()
    try:
        load_result = load_examples(args.dataset)
        validate_training_examples(load_result)
        if args.mode == TRAINER_VERSION_V0A:
            run_phase_bias_v0a(load_result, args)
        elif args.mode == TRAINER_VERSION_V0B:
            run_pattern_sgd_v0b(load_result, args)
        elif args.mode == TRAINER_VERSION_V0C:
            run_pattern_sgd_v0c(load_result, args)
        else:
            raise RuntimeError(f"unsupported mode: {args.mode}")
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1

    print(
        "summary "
        f"input_format={load_result.input_format} "
        f"input_feature_rows={load_result.input_feature_rows} "
        f"accepted_examples={len(load_result.accepted_examples)} "
        f"rejected_examples={load_result.rejected_examples} "
        f"rejected_feature_rows={load_result.rejected_feature_rows} "
        f"trainer_version={args.mode}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
