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
SPLITS = ("train", "validation", "test")
PHASES = tuple(range(13))
SCHEMA_VERSION = 1
TRAINER_VERSION_V0A = "phase-bias-v0a"
TRAINER_VERSION_V0B = "pattern-sgd-v0b"


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
        choices=(TRAINER_VERSION_V0A, TRAINER_VERSION_V0B),
        default=TRAINER_VERSION_V0A,
    )
    parser.add_argument("--epochs", type=int, default=8)
    parser.add_argument("--learning-rate", type=float, default=0.05)
    parser.add_argument("--l2", type=float, default=0.0)
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
        input_feature_rows=input_feature_rows,
        accepted_examples=accepted_examples,
        rejected_examples=rejected_examples,
        rejected_feature_rows=rejected_feature_rows,
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
        "input_feature_rows": load_result.input_feature_rows,
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


def validate_training_examples(examples: list[Example]) -> None:
    if not examples:
        raise RuntimeError("dataset has no accepted examples")
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
        "input_feature_rows": load_result.input_feature_rows,
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


def main() -> int:
    args = parse_args()
    try:
        load_result = load_examples(args.dataset)
        validate_training_examples(load_result.accepted_examples)
        if args.mode == TRAINER_VERSION_V0A:
            run_phase_bias_v0a(load_result, args)
        elif args.mode == TRAINER_VERSION_V0B:
            run_pattern_sgd_v0b(load_result, args)
        else:
            raise RuntimeError(f"unsupported mode: {args.mode}")
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1

    print(
        "summary "
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
