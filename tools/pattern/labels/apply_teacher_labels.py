#!/usr/bin/env python3
"""Overlay local-only teacher labels onto normalized schema v2 TSV rows."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any


SCHEMA_VERSION = 1
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
TEACHER_HEADER = [
    "board_id",
    "label_kind",
    "label_unit",
    "label_perspective",
    "label_score_side_to_move",
    "teacher_source",
    "teacher_depth",
    "teacher_nodes",
]
TEACHER_LABEL_KINDS = {
    "teacher_exact_final_disc_diff",
    "teacher_search_final_disc_diff",
    "teacher_static_eval_disc_diff",
}
LOCAL_ONLY_NOTES = [
    "local-only teacher label overlay",
    "not a strength claim",
    "not an Elo result",
    "not match bench",
    "not self-play",
    "not production artifact",
    "generated teacher labels and overlaid TSVs must not be committed",
]


@dataclass(frozen=True)
class TeacherPayload:
    label_kind: str
    label_unit: str
    label_perspective: str
    label_score_side_to_move: str
    teacher_source: str
    teacher_depth: int | None
    teacher_nodes: int | None

    def label_tuple(self) -> tuple[str, str, str, str]:
        return (
            self.label_kind,
            self.label_unit,
            self.label_perspective,
            self.label_score_side_to_move,
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--normalized-tsv", required=True, type=Path)
    parser.add_argument("--teacher-labels", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument(
        "--missing-policy",
        choices=("fail", "keep-observed", "drop"),
        default="fail",
    )
    parser.add_argument("--conflict-policy", choices=("fail",), default="fail")
    return parser.parse_args()


def stable_json(data: Any) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def parse_int_field(value: str, field: str, line_number: int) -> int:
    try:
        parsed = int(value)
    except ValueError as error:
        raise RuntimeError(f"line {line_number}: {field} must be an integer") from error
    return parsed


def parse_optional_nonnegative_int(value: str, field: str, line_number: int) -> int | None:
    if value == "":
        return None
    parsed = parse_int_field(value, field, line_number)
    if parsed < 0:
        raise RuntimeError(f"line {line_number}: {field} must be >= 0 or empty")
    return parsed


def validate_teacher_row(row: dict[str, str], line_number: int) -> TeacherPayload:
    board_id = row.get("board_id", "")
    if not board_id:
        raise RuntimeError(f"line {line_number}: board_id must be non-empty")
    label_kind = row.get("label_kind", "")
    if not label_kind:
        raise RuntimeError(f"line {line_number}: label_kind must be non-empty")
    if label_kind not in TEACHER_LABEL_KINDS:
        raise RuntimeError(
            f"line {line_number}: label_kind must be one of {sorted(TEACHER_LABEL_KINDS)}"
        )
    label_unit = row.get("label_unit", "")
    if label_unit != "disc":
        raise RuntimeError(f"line {line_number}: label_unit must be disc")
    label_perspective = row.get("label_perspective", "")
    if label_perspective != "side_to_move":
        raise RuntimeError(f"line {line_number}: label_perspective must be side_to_move")
    label_score = row.get("label_score_side_to_move", "")
    parsed_score = parse_int_field(label_score, "label_score_side_to_move", line_number)
    if parsed_score < -64 or parsed_score > 64:
        raise RuntimeError(f"line {line_number}: label_score_side_to_move must be in [-64, 64]")
    teacher_source = row.get("teacher_source", "")
    if not teacher_source:
        raise RuntimeError(f"line {line_number}: teacher_source must be non-empty")
    depth = parse_optional_nonnegative_int(row.get("teacher_depth", ""), "teacher_depth", line_number)
    nodes = parse_optional_nonnegative_int(row.get("teacher_nodes", ""), "teacher_nodes", line_number)
    return TeacherPayload(
        label_kind=label_kind,
        label_unit=label_unit,
        label_perspective=label_perspective,
        label_score_side_to_move=str(parsed_score),
        teacher_source=teacher_source,
        teacher_depth=depth,
        teacher_nodes=nodes,
    )


def load_teacher_payloads(path: Path) -> dict[str, TeacherPayload]:
    payloads: dict[str, TeacherPayload] = {}
    try:
        with path.open(newline="", encoding="utf-8") as handle:
            reader = csv.DictReader(handle, delimiter="\t")
            if reader.fieldnames != TEACHER_HEADER:
                raise RuntimeError(f"{path}: unexpected teacher label TSV header")
            for line_number, row in enumerate(reader, start=2):
                payload = validate_teacher_row(row, line_number)
                board_id = row["board_id"]
                existing = payloads.get(board_id)
                if existing is None:
                    payloads[board_id] = payload
                elif existing != payload:
                    raise RuntimeError(f"line {line_number}: conflicting teacher label for board_id {board_id}")
    except OSError as error:
        raise RuntimeError(f"{path}: could not read teacher label TSV: {error}") from error
    if not payloads:
        raise RuntimeError(f"{path}: teacher label TSV has no rows")
    return payloads


def ensure_normalized_header(reader: csv.DictReader[str]) -> None:
    if reader.fieldnames != NORMALIZED_HEADER_V2:
        raise RuntimeError("normalized TSV must use schema v2 header")


def count(counter: Counter[str], key: str) -> None:
    counter[key] += 1


def update_nested_count(target: dict[str, Counter[str]], outer: str, label_kind: str) -> None:
    target.setdefault(outer, Counter())[label_kind] += 1


def checksum_line(fields: list[str]) -> bytes:
    return ("\t".join(fields) + "\n").encode("utf-8")


def counter_dict(counter: Counter[str]) -> dict[str, int]:
    return {key: counter[key] for key in sorted(counter)}


def nested_counter_dict(counters: dict[str, Counter[str]]) -> dict[str, dict[str, int]]:
    return {key: counter_dict(counters[key]) for key in sorted(counters)}


def apply_overlay(args: argparse.Namespace) -> dict[str, Any]:
    teacher_payloads = load_teacher_payloads(args.teacher_labels)
    input_rows = 0
    output_rows = 0
    matched_rows = 0
    missing_rows = 0
    dropped_rows = 0
    label_kind_before: Counter[str] = Counter()
    label_kind_after: Counter[str] = Counter()
    by_split_after: dict[str, Counter[str]] = {}
    by_phase_after: dict[str, Counter[str]] = {}
    teacher_source_counts: Counter[str] = Counter()
    depths: list[int] = []
    nodes_sum = 0
    digest = hashlib.sha256()

    try:
        with args.normalized_tsv.open(newline="", encoding="utf-8") as input_handle:
            reader = csv.DictReader(input_handle, delimiter="\t")
            ensure_normalized_header(reader)
            with args.output.open("w", newline="", encoding="utf-8") as output_handle:
                writer = csv.DictWriter(
                    output_handle,
                    delimiter="\t",
                    lineterminator="\n",
                    fieldnames=NORMALIZED_HEADER_V2,
                )
                writer.writeheader()
                for line_number, row in enumerate(reader, start=2):
                    input_rows += 1
                    board_id = row.get("board_id", "")
                    if not board_id:
                        raise RuntimeError(f"line {line_number}: board_id must be non-empty")
                    count(label_kind_before, row.get("label_kind", ""))
                    payload = teacher_payloads.get(board_id)
                    if payload is None:
                        missing_rows += 1
                        if args.missing_policy == "fail":
                            raise RuntimeError(f"line {line_number}: missing teacher label for board_id {board_id}")
                        if args.missing_policy == "drop":
                            dropped_rows += 1
                            continue
                    else:
                        matched_rows += 1
                        (
                            row["label_kind"],
                            row["label_unit"],
                            row["label_perspective"],
                            row["label_score_side_to_move"],
                        ) = payload.label_tuple()
                        teacher_source_counts[payload.teacher_source] += 1
                        if payload.teacher_depth is not None:
                            depths.append(payload.teacher_depth)
                        if payload.teacher_nodes is not None:
                            nodes_sum += payload.teacher_nodes

                    writer.writerow(row)
                    fields = [row[field] for field in NORMALIZED_HEADER_V2]
                    digest.update(checksum_line(fields))
                    output_rows += 1
                    final_kind = row["label_kind"]
                    count(label_kind_after, final_kind)
                    update_nested_count(by_split_after, row["split"], final_kind)
                    update_nested_count(by_phase_after, row["phase"], final_kind)
    except OSError as error:
        raise RuntimeError(f"could not process normalized TSV: {error}") from error

    if input_rows == 0:
        raise RuntimeError("normalized TSV has no rows")
    if output_rows == 0:
        raise RuntimeError("teacher overlay produced zero rows")

    return {
        "schema_version": SCHEMA_VERSION,
        "normalized_input_path": str(args.normalized_tsv),
        "teacher_labels_path": str(args.teacher_labels),
        "output_path": str(args.output),
        "missing_policy": args.missing_policy,
        "conflict_policy": args.conflict_policy,
        "input_rows": input_rows,
        "output_rows": output_rows,
        "matched_rows": matched_rows,
        "missing_rows": missing_rows,
        "dropped_rows": dropped_rows,
        "unique_teacher_boards": len(teacher_payloads),
        "label_kind_counts_before": counter_dict(label_kind_before),
        "label_kind_counts_after": counter_dict(label_kind_after),
        "label_kind_counts_by_split_after": nested_counter_dict(by_split_after),
        "label_kind_counts_by_phase_after": nested_counter_dict(by_phase_after),
        "teacher_source_counts": counter_dict(teacher_source_counts),
        "teacher_depth_min": min(depths) if depths else None,
        "teacher_depth_max": max(depths) if depths else None,
        "teacher_nodes_sum": nodes_sum,
        "checksum": f"sha256:{digest.hexdigest()}",
        "notes": LOCAL_ONLY_NOTES,
    }


def main() -> int:
    args = parse_args()
    try:
        report = apply_overlay(args)
        args.report.write_text(stable_json(report), encoding="utf-8")
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1
    print(f"output={args.output}")
    print(f"report={args.report}")
    print(f"checksum={report['checksum']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
