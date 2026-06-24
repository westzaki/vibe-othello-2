#!/usr/bin/env python3
"""CTest smoke coverage for deriving root labels from move-teacher rows."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import subprocess
import sys
import tempfile
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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--deriver", required=True, type=Path)
    return parser.parse_args()


def phase_for_occupied(occupied: int) -> int:
    return min(12, ((occupied - 4) * 13) // 60)


def board(player: int, opponent: int, empty: int) -> str:
    if player + opponent + empty != 64:
        raise AssertionError("bad board counts")
    return ("X" * player) + ("O" * opponent) + ("-" * empty)


def digest_for_parts(*parts: object) -> str:
    return hashlib.sha256("\t".join(str(part) for part in parts).encode("ascii")).hexdigest()


def board_id_for(board_text: str) -> str:
    return f"board-{digest_for_parts('board-v1', board_text)[:16]}"


def normalized_row(record: str, split: str, board_text: str) -> dict[str, str]:
    occupied = board_text.count("X") + board_text.count("O")
    empty = board_text.count("-")
    board_id = board_id_for(board_text)
    return {
        "record_id": record,
        "position_id": f"{record}:position",
        "game_group_id": f"{record}:game",
        "board_id": board_id,
        "source_occurrence_id": f"{record}:source",
        "source_dataset_id": "synthetic-derived-root-smoke",
        "split": split,
        "board_a1_to_h8": board_text,
        "label_kind": "observed_final_disc_diff",
        "label_unit": "disc",
        "label_perspective": "side_to_move",
        "label_score_side_to_move": "0",
        "occupied_count": str(occupied),
        "phase": str(phase_for_occupied(occupied)),
        "player_disc_count": str(board_text.count("X")),
        "opponent_disc_count": str(board_text.count("O")),
        "empty_count": str(empty),
    }


def move_row(root: dict[str, str], move: str, score: int, nodes: int) -> dict[str, str]:
    child_board = root["board_a1_to_h8"].replace("-", "X", 1)
    child_occupied = child_board.count("X") + child_board.count("O")
    child_empty = child_board.count("-")
    child_id = f"child:{root['board_id']}:{move}"
    return {
        "root_board_id": root["board_id"],
        "root_record_id": root["record_id"],
        "root_split": root["split"],
        "root_phase": root["phase"],
        "root_empty_count": root["empty_count"],
        "move": move,
        "child_board_id": child_id,
        "child_board_a1_to_h8": child_board,
        "child_empty_count": str(child_empty),
        "child_phase": str(phase_for_occupied(child_occupied)),
        "root_move_score_side_to_move": str(score),
        "child_label_score_side_to_move": str(-score),
        "is_best_move": "1" if score >= 5 else "0",
        "best_move_tie_count": "1",
        "move_rank": "1" if score >= 5 else "2",
        "best_score_margin": "0" if score >= 5 else "4",
        "teacher_source": "exact-move-teacher-v2",
        "teacher_depth": str(child_empty),
        "teacher_nodes": str(nodes),
    }


def write_tsv(path: Path, header: list[str], rows: list[dict[str, str]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=header, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def load_tsv(path: Path) -> tuple[list[str], list[dict[str, str]]]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        return list(reader.fieldnames or []), list(reader)


def load_json(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise AssertionError(path)
    return data


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, text=True, capture_output=True)


def require_success(result: subprocess.CompletedProcess[str]) -> bool:
    if result.returncode != 0:
        print(result.stdout, file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        return False
    return True


def require_failure(result: subprocess.CompletedProcess[str], needle: str) -> bool:
    if result.returncode == 0:
        print("command unexpectedly succeeded", file=sys.stderr)
        return False
    combined = result.stdout + result.stderr
    if needle not in combined:
        print(f"failure did not contain {needle!r}: {combined}", file=sys.stderr)
        return False
    return True


def derive_command(args: argparse.Namespace, normalized: Path, move_teacher: Path, out: Path, report: Path) -> list[str]:
    return [
        sys.executable,
        str(args.deriver),
        "--normalized-tsv",
        str(normalized),
        "--move-teacher-tsv",
        str(move_teacher),
        "--teacher-labels-out",
        str(out),
        "--report-out",
        str(report),
        "--missing-policy",
        "fail",
    ]


def fixture(root: Path) -> tuple[Path, Path, list[dict[str, str]], list[dict[str, str]]]:
    root_a = normalized_row("record-a", "train", board(61, 2, 1))
    root_b = normalized_row("record-b", "validation", board(58, 5, 1))
    normalized = root / "normalized.tsv"
    move_teacher = root / "move-teacher.tsv"
    move_rows = [
        move_row(root_a, "a1", 1, 11),
        move_row(root_a, "b1", 5, 13),
        move_row(root_b, "pass", -2, 17),
    ]
    write_tsv(normalized, NORMALIZED_HEADER_V2, [root_a, root_b])
    write_tsv(move_teacher, MOVE_TEACHER_HEADER, move_rows)
    return normalized, move_teacher, [root_a, root_b], move_rows


def check_derives_max_score(args: argparse.Namespace, root: Path) -> bool:
    normalized, move_teacher, roots, _ = fixture(root)
    labels = root / "teacher-labels.tsv"
    report = root / "derive-report.json"
    if not require_success(run(derive_command(args, normalized, move_teacher, labels, report))):
        return False
    header, rows = load_tsv(labels)
    if header != TEACHER_LABEL_HEADER:
        print(f"teacher label header is not apply_teacher_labels-compatible: {header}", file=sys.stderr)
        return False
    by_id = {row["board_id"]: row for row in rows}
    if by_id[roots[0]["board_id"]]["label_score_side_to_move"] != "5":
        print(f"root score was not max move score: {rows}", file=sys.stderr)
        return False
    if by_id[roots[0]["board_id"]]["teacher_nodes"] != "24":
        print(f"teacher_nodes was not summed: {rows}", file=sys.stderr)
        return False
    data = load_json(report)
    if data.get("derived_root_count") != 2 or data.get("teacher_nodes_sum") != 41:
        print(f"derive report missing counts: {data}", file=sys.stderr)
        return False
    return True


def check_missing_and_duplicate_fail(args: argparse.Namespace, root: Path) -> bool:
    normalized, move_teacher, _, move_rows = fixture(root)
    missing_move_teacher = root / "missing-move-teacher.tsv"
    write_tsv(missing_move_teacher, MOVE_TEACHER_HEADER, move_rows[:2])
    if not require_failure(
        run(derive_command(args, normalized, missing_move_teacher, root / "missing-labels.tsv", root / "missing-report.json")),
        "missing 1 roots",
    ):
        return False
    duplicate_move_teacher = root / "duplicate-move-teacher.tsv"
    write_tsv(duplicate_move_teacher, MOVE_TEACHER_HEADER, [*move_rows, move_rows[0]])
    return require_failure(
        run(derive_command(args, normalized, duplicate_move_teacher, root / "duplicate-labels.tsv", root / "duplicate-report.json")),
        "duplicate root/move row",
    )


def main() -> int:
    args = parse_args()
    with tempfile.TemporaryDirectory() as temp_text:
        root = Path(temp_text)
        if not check_derives_max_score(args, root):
            return 1
        if not check_missing_and_duplicate_fail(args, root):
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
