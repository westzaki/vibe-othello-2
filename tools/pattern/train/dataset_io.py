"""Pattern trainer dataset loading for expanded and compact TSV inputs."""

from __future__ import annotations

import csv
from pathlib import Path

from dataset_contract import COMPACT_HEADER, EXPECTED_HEADER, SPLITS, parse_int
from examples import (
    Example,
    ExampleBuilder,
    Feature,
    LoadResult,
    ParsedFeatureRow,
    duplicate_feature_rows_for,
    row_record_id,
)

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


def validate_training_examples(load_result: LoadResult) -> None:
    examples = load_result.accepted_examples
    if not examples:
        detail = f"; first error: {load_result.notes[0]}" if load_result.notes else ""
        raise RuntimeError(f"dataset has no accepted examples{detail}")
    if not any(example.split == "train" for example in examples):
        raise RuntimeError("dataset has no accepted train examples")
