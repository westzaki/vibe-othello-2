"""Example records and grouping helpers for pattern training."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from dataset_contract import PHASES, SPLITS

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


@dataclass(frozen=True)
class MoveTeacherMove:
    root_board_id: str
    root_record_id: str
    root_split: str
    root_phase: int
    move: str
    teacher_root_score: int
    child_label_score: int
    child_phase: int
    provenance: "MoveTeacherProvenance | None"
    example: Example


@dataclass(frozen=True)
class MoveTeacherRoot:
    root_board_id: str
    root_record_id: str
    split: str
    phase: int
    moves: list[MoveTeacherMove]


@dataclass(frozen=True)
class MoveTeacherProvenance:
    teacher_kind: str
    teacher_source: str
    teacher_artifact_id: str
    teacher_artifact_checksum: str
    teacher_search_config_id: str


@dataclass(frozen=True)
class MoveTeacherInput:
    schema_version: int
    move_rows: int
    provenance: MoveTeacherProvenance | None


@dataclass(frozen=True)
class MoveTeacherLoadResult:
    roots: list[MoveTeacherRoot]
    move_rows: int
    schema_version: int
    provenance: MoveTeacherProvenance | None
    inputs: list[MoveTeacherInput]

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


def split_examples(examples: list[Example]) -> dict[str, list[Example]]:
    return {split: [example for example in examples if example.split == split] for split in SPLITS}


def phase_examples(examples: list[Example]) -> dict[int, list[Example]]:
    return {phase: [example for example in examples if example.phase == phase] for phase in PHASES}
