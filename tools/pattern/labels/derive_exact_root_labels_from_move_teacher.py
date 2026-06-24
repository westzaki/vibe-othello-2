#!/usr/bin/env python3
"""Derive exact root teacher labels from complete exact move-teacher rows."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import sys
import time
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
MOVE_TEACHER_HEADER = [
    "root_board_id",
    "root_record_id",
    "root_split",
    "root_phase",
    "root_empty_count",
    "move",
    "child_board_id",
    "child_board_a1_to_h8",
    "child_empty_count",
    "child_phase",
    "root_move_score_side_to_move",
    "child_label_score_side_to_move",
    "is_best_move",
    "best_move_tie_count",
    "move_rank",
    "best_score_margin",
    "teacher_source",
    "teacher_depth",
    "teacher_nodes",
]
TEACHER_LABEL_HEADER = [
    "board_id",
    "label_kind",
    "label_unit",
    "label_perspective",
    "label_score_side_to_move",
    "teacher_source",
    "teacher_depth",
    "teacher_nodes",
]

LABEL_KIND = "teacher_exact_final_disc_diff"
LABEL_UNIT = "disc"
LABEL_PERSPECTIVE = "side_to_move"
TEACHER_SOURCE = "exact-move-teacher-derived-root-v1"
MOVE_TEACHER_SOURCES = ("exact-move-teacher-v1", "exact-move-teacher-v2")
MOVE_TEACHER_SOURCE_SET = set(MOVE_TEACHER_SOURCES)


class DeriveError(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--normalized-tsv", required=True, type=Path)
    parser.add_argument("--move-teacher-tsv", required=True, type=Path)
    parser.add_argument("--teacher-labels-out", required=True, type=Path)
    parser.add_argument("--report-out", required=True, type=Path)
    parser.add_argument("--missing-policy", choices=("fail", "drop"), default="fail")
    return parser.parse_args()


def stable_json(data: Any) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            digest.update(chunk)
    return f"sha256:{digest.hexdigest()}"


def sha256_text(text: str) -> str:
    return f"sha256:{hashlib.sha256(text.encode('utf-8')).hexdigest()}"


def report_path(path: Path | None) -> str | None:
    if path is None:
        return None
    return path.name if path.is_absolute() else str(path)


def digest_for_parts(*parts: object) -> str:
    return hashlib.sha256("\t".join(str(part) for part in parts).encode("ascii")).hexdigest()


def prefixed_id(prefix: str, digest: str) -> str:
    return f"{prefix}-{digest[:16]}"


def expected_board_id(board: str) -> str:
    return prefixed_id("board", digest_for_parts("board-v1", board))


def parse_int(text: str, field: str) -> int:
    try:
        return int(text)
    except ValueError as error:
        raise DeriveError(f"{field} must be an integer: {text}") from error


def phase_for_occupied(occupied_count: int) -> int:
    return min(12, ((occupied_count - 4) * 13) // 60)


def board_counts(board: str) -> dict[str, int]:
    if len(board) != 64:
        raise DeriveError("board_a1_to_h8 must be exactly 64 characters")
    player = board.count("X")
    opponent = board.count("O")
    empty = board.count("-")
    if player + opponent + empty != 64:
        raise DeriveError("board_a1_to_h8 contains characters outside X/O/-")
    return {
        "occupied_count": player + opponent,
        "player_disc_count": player,
        "opponent_disc_count": opponent,
        "empty_count": empty,
    }


def load_tsv(path: Path, expected_header: list[str]) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if reader.fieldnames != expected_header:
            raise DeriveError(f"{path} has unexpected TSV header: {reader.fieldnames}")
        return list(reader)


def validate_normalized_row(row: dict[str, str]) -> None:
    board_id = row["board_id"]
    if not board_id:
        raise DeriveError("board_id must be non-empty")
    if row["label_perspective"] != LABEL_PERSPECTIVE:
        raise DeriveError(f"{board_id}: label_perspective must be {LABEL_PERSPECTIVE}")
    board = row["board_a1_to_h8"]
    derived_board_id = expected_board_id(board)
    if board_id != derived_board_id:
        raise DeriveError(f"{board_id}: board_id does not match board_a1_to_h8; expected {derived_board_id}")
    counts = board_counts(board)
    for key, expected in counts.items():
        if parse_int(row[key], key) != expected:
            raise DeriveError(f"{board_id}: {key} does not match board_a1_to_h8")
    occupied = parse_int(row["occupied_count"], "occupied_count")
    phase = parse_int(row["phase"], "phase")
    if phase != phase_for_occupied(occupied):
        raise DeriveError(f"{board_id}: phase does not match occupied_count")


def load_roots(path: Path) -> tuple[list[dict[str, str]], dict[str, Any]]:
    rows = load_tsv(path, NORMALIZED_HEADER_V2)
    roots: list[dict[str, str]] = []
    seen: dict[str, str] = {}
    duplicate_board_rows = 0
    for row in rows:
        validate_normalized_row(row)
        board_id = row["board_id"]
        previous = seen.get(board_id)
        if previous is not None:
            duplicate_board_rows += 1
            if previous != row["board_a1_to_h8"]:
                raise DeriveError(f"{board_id}: same board_id has different board contents")
            continue
        seen[board_id] = row["board_a1_to_h8"]
        roots.append(row)
    identities = [f"{row['board_id']}\t{row['board_a1_to_h8']}\t{row['empty_count']}" for row in roots]
    return roots, {
        "input_rows": len(rows),
        "selected_roots": len(roots),
        "duplicate_board_rows": duplicate_board_rows,
        "ordered_root_digest": sha256_text("\n".join(identities) + "\n"),
        "board_contents_digest": sha256_text(
            "\n".join(f"{row['board_id']}\t{row['board_a1_to_h8']}" for row in sorted(roots, key=lambda item: item["board_id"]))
            + "\n"
        ),
    }


def load_move_rows(
    path: Path, roots: dict[str, dict[str, str]]
) -> tuple[dict[str, list[dict[str, str]]], str | None]:
    rows = load_tsv(path, MOVE_TEACHER_HEADER)
    grouped: dict[str, list[dict[str, str]]] = {}
    seen_moves: set[tuple[str, str]] = set()
    move_teacher_source: str | None = None
    for row in rows:
        board_id = row["root_board_id"]
        if board_id not in roots:
            raise DeriveError(f"{board_id}: move-teacher root is not in normalized TSV")
        root = roots[board_id]
        if row["root_record_id"] != root["record_id"]:
            raise DeriveError(f"{board_id}: root_record_id does not match normalized TSV")
        if row["root_split"] != root["split"]:
            raise DeriveError(f"{board_id}: root_split does not match normalized TSV")
        if row["root_phase"] != root["phase"]:
            raise DeriveError(f"{board_id}: root_phase does not match normalized TSV")
        if row["root_empty_count"] != root["empty_count"]:
            raise DeriveError(f"{board_id}: root_empty_count does not match normalized TSV")
        teacher_source = row["teacher_source"]
        if teacher_source not in MOVE_TEACHER_SOURCE_SET:
            raise DeriveError(f"{board_id}: teacher_source must be one of {', '.join(MOVE_TEACHER_SOURCES)}")
        if move_teacher_source is None:
            move_teacher_source = teacher_source
        elif teacher_source != move_teacher_source:
            raise DeriveError(
                f"{board_id}: mixed move-teacher teacher_source values are not supported: "
                f"{move_teacher_source} and {teacher_source}"
            )
        move_key = (board_id, row["move"])
        if move_key in seen_moves:
            raise DeriveError(f"{board_id} {row['move']}: duplicate root/move row")
        seen_moves.add(move_key)
        score = parse_int(row["root_move_score_side_to_move"], "root_move_score_side_to_move")
        child_score = parse_int(row["child_label_score_side_to_move"], "child_label_score_side_to_move")
        if score != -child_score:
            raise DeriveError(f"{board_id} {row['move']}: root score must be negative child score")
        if score < -64 or score > 64:
            raise DeriveError(f"{board_id} {row['move']}: root score is outside [-64, 64]")
        if parse_int(row["teacher_nodes"], "teacher_nodes") < 0:
            raise DeriveError(f"{board_id} {row['move']}: teacher_nodes must be non-negative")
        if parse_int(row["teacher_depth"], "teacher_depth") < 0:
            raise DeriveError(f"{board_id} {row['move']}: teacher_depth must be non-negative")
        grouped.setdefault(board_id, []).append(row)
    return grouped, move_teacher_source


def derive_label(root: dict[str, str], rows: list[dict[str, str]]) -> dict[str, str]:
    if not rows:
        raise DeriveError(f"{root['board_id']}: missing move-teacher rows")
    scores = [parse_int(row["root_move_score_side_to_move"], "root_move_score_side_to_move") for row in rows]
    nodes = sum(parse_int(row["teacher_nodes"], "teacher_nodes") for row in rows)
    depth = max(parse_int(row["teacher_depth"], "teacher_depth") for row in rows)
    return {
        "board_id": root["board_id"],
        "label_kind": LABEL_KIND,
        "label_unit": LABEL_UNIT,
        "label_perspective": LABEL_PERSPECTIVE,
        "label_score_side_to_move": str(max(scores)),
        "teacher_source": TEACHER_SOURCE,
        "teacher_depth": str(depth),
        "teacher_nodes": str(nodes),
    }


def write_teacher_labels(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=TEACHER_LABEL_HEADER, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row[key] for key in TEACHER_LABEL_HEADER})


def build_report(
    args: argparse.Namespace,
    root_stats: dict[str, Any],
    label_rows: list[dict[str, str]],
    move_row_count: int,
    move_teacher_source: str | None,
    missing_roots: list[str],
    started: float,
) -> dict[str, Any]:
    scores = [parse_int(row["label_score_side_to_move"], "label_score_side_to_move") for row in label_rows]
    nodes = [parse_int(row["teacher_nodes"], "teacher_nodes") for row in label_rows]
    return {
        "schema_version": 1,
        "normalized_input_path": report_path(args.normalized_tsv),
        "move_teacher_tsv": report_path(args.move_teacher_tsv),
        "teacher_labels_out": report_path(args.teacher_labels_out),
        "missing_policy": args.missing_policy,
        "input_rows": root_stats["input_rows"],
        "selected_roots": root_stats["selected_roots"],
        "derived_root_count": len(label_rows),
        "missing_root_count": len(missing_roots),
        "missing_board_ids_sample": missing_roots[:10],
        "move_teacher_rows": move_row_count,
        "move_teacher_source": move_teacher_source,
        "accepted_move_teacher_sources": list(MOVE_TEACHER_SOURCES),
        "duplicate_board_rows": root_stats["duplicate_board_rows"],
        "label_score_min": min(scores) if scores else None,
        "label_score_max": max(scores) if scores else None,
        "label_score_sum": sum(scores),
        "teacher_nodes_sum": sum(nodes),
        "teacher_nodes_aggregate": "sum child teacher_nodes for the root",
        "teacher_depth_aggregate": "max child teacher_depth for the root",
        "normalized_input_sha256": sha256_file(args.normalized_tsv),
        "move_teacher_sha256": sha256_file(args.move_teacher_tsv),
        "teacher_labels_sha256": sha256_file(args.teacher_labels_out),
        "ordered_root_digest": root_stats["ordered_root_digest"],
        "board_contents_digest": root_stats["board_contents_digest"],
        "wall_time_sec": time.monotonic() - started,
        "notes": [
            "root exact score is max(root_move_score_side_to_move)",
            "teacher_nodes is the sum of child move-teacher nodes for each root",
            "local-only derived teacher labels",
            "not a strength claim",
            "not an Elo result",
            "not self-play",
            "not a production artifact",
            "generated labels and artifacts must not be committed",
        ],
    }


def main() -> int:
    args = parse_args()
    started = time.monotonic()
    try:
        roots, root_stats = load_roots(args.normalized_tsv)
        if not roots:
            raise DeriveError("normalized TSV contains no roots")
        root_by_id = {row["board_id"]: row for row in roots}
        grouped, move_teacher_source = load_move_rows(args.move_teacher_tsv, root_by_id)
        missing_roots = [row["board_id"] for row in roots if row["board_id"] not in grouped]
        if missing_roots and args.missing_policy == "fail":
            raise DeriveError(
                f"move-teacher TSV is missing {len(missing_roots)} roots; first missing board_id: {missing_roots[0]}"
            )
        label_rows = [
            derive_label(root, grouped[root["board_id"]])
            for root in roots
            if root["board_id"] in grouped
        ]
        args.teacher_labels_out.parent.mkdir(parents=True, exist_ok=True)
        args.report_out.parent.mkdir(parents=True, exist_ok=True)
        write_teacher_labels(args.teacher_labels_out, label_rows)
        report = build_report(
            args,
            root_stats,
            label_rows,
            sum(len(rows) for rows in grouped.values()),
            move_teacher_source,
            missing_roots,
            started,
        )
        args.report_out.write_text(stable_json(report), encoding="utf-8")
        print(f"teacher_labels={args.teacher_labels_out}")
        print(f"report={args.report_out}")
        print(f"derived_roots={len(label_rows)}")
        return 0
    except (DeriveError, OSError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
