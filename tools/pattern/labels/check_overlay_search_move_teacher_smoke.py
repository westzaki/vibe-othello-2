#!/usr/bin/env python3
"""Smoke test for complete-root search move-teacher overlay."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
import tempfile
from pathlib import Path


MOVE_FIELDS = [
    "root_board_id",
    "root_record_id",
    "root_split",
    "root_phase",
    "move",
    "child_board_id",
    "child_label_score_side_to_move",
    "teacher_kind",
    "teacher_source",
    "teacher_artifact_id",
    "teacher_artifact_checksum",
    "teacher_search_config_id",
    "teacher_depth",
]
CHILD_FIELDS = [
    "record_id",
    "board_id",
    "board_a1_to_h8",
    "split",
    "phase",
    "label_score_side_to_move",
]


def write_tsv(path: Path, fields: list[str], rows: list[dict[str, str]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, delimiter="\t", fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def move(root: str, child: str, score: int, depth: int, split: str = "train") -> dict[str, str]:
    return {
        "root_board_id": root,
        "root_record_id": f"record-{root}",
        "root_split": split,
        "root_phase": "5",
        "move": "a1",
        "child_board_id": child,
        "child_label_score_side_to_move": str(score),
        "teacher_kind": "artifact_search",
        "teacher_source": "fixture",
        "teacher_artifact_id": "teacher",
        "teacher_artifact_checksum": "0x1234",
        "teacher_search_config_id": f"depth-{depth}",
        "teacher_depth": str(depth),
    }


def child(board_id: str, score: int, split: str = "train") -> dict[str, str]:
    return {
        "record_id": board_id,
        "board_id": board_id,
        "board_a1_to_h8": "-" * 64,
        "split": split,
        "phase": "5",
        "label_score_side_to_move": str(score),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--overlay", required=True, type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        base_moves = root / "base-moves.tsv"
        base_children = root / "base-children.tsv"
        deep_moves = root / "deep-moves.tsv"
        deep_children = root / "deep-children.tsv"
        merged_moves = root / "merged-moves.tsv"
        retained_moves = root / "retained-moves.tsv"
        merged_children = root / "merged-children.tsv"
        report = root / "report.json"

        # root-a is replaced; root-b shares the relabeled child and must be
        # excluded as a complete root; root-c remains shallow supervision.
        write_tsv(
            base_moves,
            MOVE_FIELDS,
            [move("root-a", "child-shared", 1, 4), move("root-b", "child-shared", 1, 4),
             move("root-c", "child-c", 3, 4, "validation")],
        )
        write_tsv(
            base_children,
            CHILD_FIELDS,
            [child("child-shared", 1), child("child-c", 3, "validation")],
        )
        write_tsv(deep_moves, MOVE_FIELDS, [move("root-a", "child-shared", 2, 5)])
        write_tsv(deep_children, CHILD_FIELDS, [child("child-shared", 2)])

        command = [
            sys.executable,
            str(args.overlay),
            "--base-move-teacher",
            str(base_moves),
            "--base-children",
            str(base_children),
            "--overlay-move-teacher",
            str(deep_moves),
            "--overlay-children",
            str(deep_children),
            "--move-teacher-out",
            str(merged_moves),
            "--retained-base-move-teacher-out",
            str(retained_moves),
            "--children-out",
            str(merged_children),
            "--report-out",
            str(report),
        ]
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        if result.returncode != 0:
            print(result.stderr, file=sys.stderr)
            return 1

        payload = json.loads(report.read_text(encoding="utf-8"))
        with merged_moves.open(newline="", encoding="utf-8") as handle:
            merged = list(csv.DictReader(handle, delimiter="\t"))
        with retained_moves.open(newline="", encoding="utf-8") as handle:
            retained = list(csv.DictReader(handle, delimiter="\t"))
        with merged_children.open(newline="", encoding="utf-8") as handle:
            children = list(csv.DictReader(handle, delimiter="\t"))
        counts = payload.get("counts", {})
        if (
            counts.get("merged_roots") != 2
            or counts.get("retained_base_roots") != 1
            or counts.get("base_roots_excluded_for_cross_depth_child_label_conflict") != 1
            or {(row["root_board_id"], row["teacher_depth"]) for row in merged}
            != {("root-a", "5"), ("root-c", "4")}
            or [row["root_board_id"] for row in retained] != ["root-c"]
            or {row["board_id"]: row["label_score_side_to_move"] for row in children}
            != {"child-shared": "2", "child-c": "3"}
        ):
            print(f"unexpected overlay result: {payload} {merged} {retained} {children}",
                  file=sys.stderr)
            return 1

        repeat = subprocess.run(command, capture_output=True, text=True, check=False)
        if repeat.returncode != 0 or json.loads(report.read_text(encoding="utf-8")) != payload:
            print("overlay is not deterministic", file=sys.stderr)
            return 1

        incomplete_moves = root / "incomplete-moves.tsv"
        write_tsv(
            base_moves,
            MOVE_FIELDS,
            [
                move("root-a", "child-shared", 1, 4),
                {**move("root-a", "child-other", 0, 4), "move": "b1"},
            ],
        )
        write_tsv(incomplete_moves, MOVE_FIELDS, [move("root-a", "child-shared", 2, 5)])
        incomplete_command = list(command)
        incomplete_command[incomplete_command.index(str(deep_moves))] = str(
            incomplete_moves
        )
        rejected = subprocess.run(
            incomplete_command,
            capture_output=True,
            text=True,
            check=False,
        )
        if (
            rejected.returncode == 0
            or "overlay does not contain the complete base move set"
            not in rejected.stderr
        ):
            print(f"incomplete overlay was accepted: {rejected.stderr}", file=sys.stderr)
            return 1

    print("search move-teacher overlay smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
