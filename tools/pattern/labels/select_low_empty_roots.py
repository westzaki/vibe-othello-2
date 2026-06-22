#!/usr/bin/env python3
"""Select deterministic low-empty roots from a normalized schema v2 TSV."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import sys
from collections import Counter
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


SCHEMA_VERSION = 1
NORMALIZED_HEADER_V1 = [
    "record_id",
    "position_id",
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
LOCAL_ONLY_NOTES = [
    "local-only low-empty root selection",
    "preserves input split assignments",
    "preserves original selected rows and header",
    "generated selected TSVs and reports must not be committed",
    "not an Elo result",
    "not self-play",
    "not a production strength claim",
    "not a publication gate",
]


@dataclass(frozen=True)
class EligibleRoot:
    row: dict[str, str]
    input_index: int
    board_id: str
    empty_count: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--normalized-tsv", required=True, type=Path)
    parser.add_argument("--output-tsv", required=True, type=Path)
    parser.add_argument("--report-out", required=True, type=Path)
    parser.add_argument("--max-empty", type=int, default=12)
    parser.add_argument("--max-roots", required=True, type=int)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--dedupe-key", choices=("board_id",), default="board_id")
    parser.add_argument("--preserve-split", action="store_true")
    parser.add_argument("--require-schema-v2", action="store_true")
    parser.add_argument("--allow-less-than-requested", action="store_true")
    args = parser.parse_args()
    if args.max_empty < 0 or args.max_empty > 64:
        parser.error("--max-empty must be in [0, 64]")
    if args.max_roots <= 0:
        parser.error("--max-roots must be positive")
    if args.seed < 0:
        parser.error("--seed must be non-negative")
    return args


def stable_json(data: Any) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def report_path(path: Path) -> str:
    return path.name if path.is_absolute() else str(path)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            digest.update(chunk)
    return f"sha256:{digest.hexdigest()}"


def selection_key(board_id: str, seed: int) -> tuple[str, str]:
    digest = hashlib.sha256(f"{seed}\t{board_id}".encode("utf-8")).hexdigest()
    return digest, board_id


def counter_dict(counter: Counter[str]) -> dict[str, int]:
    return {key: counter[key] for key in sorted(counter)}


def require_schema_v2(fieldnames: list[str] | None) -> None:
    if fieldnames == NORMALIZED_HEADER_V2:
        return
    if fieldnames == NORMALIZED_HEADER_V1:
        raise RuntimeError("normalized TSV must use schema v2; schema v1 is not supported")
    raise RuntimeError("normalized TSV must use normalized schema v2 header")


def parse_empty_count(row: dict[str, str], line_number: int) -> int:
    try:
        value = int(row["empty_count"])
    except ValueError as error:
        raise RuntimeError(f"line {line_number}: empty_count must be an integer") from error
    if value < 0 or value > 64:
        raise RuntimeError(f"line {line_number}: empty_count must be in [0, 64]")
    return value


def read_unique_eligible_roots(path: Path, max_empty: int) -> tuple[list[EligibleRoot], dict[str, Any]]:
    input_rows = 0
    eligible_rows = 0
    duplicate_board_rows = 0
    input_duplicate_board_rows = 0
    seen_input_boards: set[str] = set()
    unique: dict[str, EligibleRoot] = {}
    eligible_split_counts: Counter[str] = Counter()
    eligible_empty_counts: Counter[str] = Counter()
    eligible_phase_counts: Counter[str] = Counter()

    try:
        with path.open(newline="", encoding="utf-8") as handle:
            reader = csv.DictReader(handle, delimiter="\t")
            require_schema_v2(reader.fieldnames)
            for line_number, row in enumerate(reader, start=2):
                input_rows += 1
                board_id = row.get("board_id", "")
                if not board_id:
                    raise RuntimeError(f"line {line_number}: board_id must be non-empty")
                if board_id in seen_input_boards:
                    input_duplicate_board_rows += 1
                else:
                    seen_input_boards.add(board_id)
                empty_count = parse_empty_count(row, line_number)
                if empty_count > max_empty:
                    continue
                eligible_rows += 1
                eligible_split_counts[row.get("split", "")] += 1
                eligible_empty_counts[str(empty_count)] += 1
                eligible_phase_counts[row.get("phase", "")] += 1
                if board_id in unique:
                    duplicate_board_rows += 1
                    continue
                unique[board_id] = EligibleRoot(
                    row=dict(row),
                    input_index=input_rows,
                    board_id=board_id,
                    empty_count=empty_count,
                )
    except OSError as error:
        raise RuntimeError(f"{path}: could not read normalized TSV: {error}") from error

    return list(unique.values()), {
        "input_rows": input_rows,
        "eligible_rows": eligible_rows,
        "unique_eligible_roots": len(unique),
        "duplicate_board_rows": duplicate_board_rows,
        "input_duplicate_board_rows": input_duplicate_board_rows,
        "eligible_split_counts": counter_dict(eligible_split_counts),
        "eligible_empty_count_counts": counter_dict(eligible_empty_counts),
        "eligible_phase_counts": counter_dict(eligible_phase_counts),
    }


def select_roots(
    unique_eligible_roots: list[EligibleRoot],
    max_roots: int,
    seed: int,
    allow_less_than_requested: bool,
) -> list[EligibleRoot]:
    if len(unique_eligible_roots) < max_roots and not allow_less_than_requested:
        raise RuntimeError(
            "available unique low-empty roots "
            f"({len(unique_eligible_roots)}) are fewer than requested max roots ({max_roots}); "
            "rerun with --allow-less-than-requested only for explicitly partial diagnostics"
        )
    selected = sorted(
        sorted(unique_eligible_roots, key=lambda item: selection_key(item.board_id, seed))[
            : min(max_roots, len(unique_eligible_roots))
        ],
        key=lambda item: item.input_index,
    )
    return selected


def write_selected_rows(path: Path, rows: list[EligibleRoot]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle,
            delimiter="\t",
            lineterminator="\n",
            fieldnames=NORMALIZED_HEADER_V2,
        )
        writer.writeheader()
        for item in rows:
            writer.writerow(item.row)


def selection_counts(rows: list[EligibleRoot]) -> dict[str, Any]:
    split_counts: Counter[str] = Counter()
    empty_counts: Counter[str] = Counter()
    phase_counts: Counter[str] = Counter()
    for item in rows:
        split_counts[item.row.get("split", "")] += 1
        empty_counts[str(item.empty_count)] += 1
        phase_counts[item.row.get("phase", "")] += 1
    return {
        "split_counts": counter_dict(split_counts),
        "empty_count_counts": counter_dict(empty_counts),
        "phase_counts": counter_dict(phase_counts),
    }


def command_args(args: argparse.Namespace) -> dict[str, Any]:
    return {
        "normalized_tsv": report_path(args.normalized_tsv),
        "output_tsv": report_path(args.output_tsv),
        "report_out": report_path(args.report_out),
        "max_empty": args.max_empty,
        "max_roots": args.max_roots,
        "seed": args.seed,
        "dedupe_key": args.dedupe_key,
        "preserve_split": bool(args.preserve_split),
        "require_schema_v2": bool(args.require_schema_v2),
        "allow_less_than_requested": bool(args.allow_less_than_requested),
    }


def build_report(args: argparse.Namespace, read_report: dict[str, Any], selected: list[EligibleRoot]) -> dict[str, Any]:
    counts = selection_counts(selected)
    partial = len(selected) < args.max_roots
    return {
        "schema_version": SCHEMA_VERSION,
        "created_at_utc": datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "normalized_schema_version": 2,
        "selection_policy": "low-empty-deterministic-board-id-hash-v1",
        "input_path": report_path(args.normalized_tsv),
        "output_path": report_path(args.output_tsv),
        "input_checksum": sha256_file(args.normalized_tsv),
        "checksum": sha256_file(args.output_tsv),
        "output_checksum": sha256_file(args.output_tsv),
        "input_rows": read_report["input_rows"],
        "eligible_rows": read_report["eligible_rows"],
        "unique_eligible_roots": read_report["unique_eligible_roots"],
        "selected_roots": len(selected),
        "selected_rows": len(selected),
        "duplicate_board_rows": read_report["duplicate_board_rows"],
        "input_duplicate_board_rows": read_report["input_duplicate_board_rows"],
        "split_counts": counts["split_counts"],
        "empty_count_counts": counts["empty_count_counts"],
        "phase_counts": counts["phase_counts"],
        "eligible_split_counts": read_report["eligible_split_counts"],
        "eligible_empty_count_counts": read_report["eligible_empty_count_counts"],
        "eligible_phase_counts": read_report["eligible_phase_counts"],
        "requested_max_roots": args.max_roots,
        "partial_selection": partial,
        "command_args": command_args(args),
        "notes": LOCAL_ONLY_NOTES
        + (
            [
                "selected fewer roots than requested because --allow-less-than-requested was set",
            ]
            if partial
            else []
        ),
    }


def main() -> int:
    args = parse_args()
    try:
        unique_roots, read_report = read_unique_eligible_roots(args.normalized_tsv, args.max_empty)
        selected = select_roots(
            unique_roots,
            args.max_roots,
            args.seed,
            args.allow_less_than_requested,
        )
        write_selected_rows(args.output_tsv, selected)
        report = build_report(args, read_report, selected)
        args.report_out.parent.mkdir(parents=True, exist_ok=True)
        args.report_out.write_text(stable_json(report), encoding="utf-8")
        print(f"selected_roots={len(selected)}")
        print(f"unique_eligible_roots={read_report['unique_eligible_roots']}")
        print(f"report={args.report_out}")
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
