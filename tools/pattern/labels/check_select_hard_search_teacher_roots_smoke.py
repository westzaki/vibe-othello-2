#!/usr/bin/env python3
"""Smoke test for hard search-teacher root selection."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
import tempfile
from pathlib import Path


NORMALIZED_FIELDS = [
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
TEACHER_FIELDS = [
    "root_board_id",
    "root_record_id",
    "root_split",
    "root_phase",
    "move",
    "root_move_score_side_to_move",
    "child_baseline_score_side_to_move",
    "best_score_margin",
    "teacher_kind",
    "teacher_source",
    "teacher_artifact_id",
    "teacher_artifact_checksum",
    "teacher_search_config_id",
]


def write_tsv(path: Path, fields: list[str], rows: list[dict[str, str]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, delimiter="\t", fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def normalized_row(index: int, phase: int) -> dict[str, str]:
    return {
        "record_id": f"record-{index}",
        "position_id": f"position-{index}",
        "game_group_id": f"game-{index}",
        "board_id": f"board-{index}",
        "source_occurrence_id": f"occ-{index}",
        "source_dataset_id": "fixture",
        "split": ("train", "validation", "test")[index % 3],
        "board_a1_to_h8": "-" * 64,
        "label_kind": "observed_final_disc_diff",
        "label_unit": "final_disc_diff",
        "label_perspective": "side_to_move",
        "label_score_side_to_move": "0",
        "occupied_count": str(4 + phase * 5),
        "phase": str(phase),
        "player_disc_count": "2",
        "opponent_disc_count": "2",
        "empty_count": str(60 - phase * 5),
    }


def teacher_rows(root: dict[str, str], regret: int) -> list[dict[str, str]]:
    common = {
        "root_board_id": root["board_id"],
        "root_record_id": root["record_id"],
        "root_split": root["split"],
        "root_phase": root["phase"],
        "best_score_margin": str(regret),
        "teacher_kind": "artifact_search",
        "teacher_source": "fixture",
        "teacher_artifact_id": "teacher",
        "teacher_artifact_checksum": "0x1234",
        "teacher_search_config_id": "config",
    }
    return [
        {
            **common,
            "move": "a1",
            "root_move_score_side_to_move": str(10 + regret),
            "child_baseline_score_side_to_move": "0",
        },
        {
            **common,
            "move": "b1",
            "root_move_score_side_to_move": "10",
            "child_baseline_score_side_to_move": "-1",
        },
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--selector", required=True, type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        normalized = root / "normalized.tsv"
        teacher = root / "teacher.tsv"
        output = root / "selected.tsv"
        report = root / "report.json"
        roots = [normalized_row(index, index % 2) for index in range(6)]
        write_tsv(normalized, NORMALIZED_FIELDS, roots)
        rows = [
            row
            for index, source_root in enumerate(roots)
            for row in teacher_rows(source_root, index + 1)
        ]
        write_tsv(teacher, TEACHER_FIELDS, rows)
        command = [
            sys.executable,
            str(args.selector),
            "--normalized-tsv",
            str(normalized),
            "--move-teacher-tsv",
            str(teacher),
            "--output-tsv",
            str(output),
            "--report-out",
            str(report),
            "--max-roots",
            "4",
            "--minimum-baseline-regret",
            "1",
            "--seed",
            "7",
        ]
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        if result.returncode != 0:
            print(result.stderr, file=sys.stderr)
            return 1
        payload = json.loads(report.read_text(encoding="utf-8"))
        with output.open(newline="", encoding="utf-8") as handle:
            selected = list(csv.DictReader(handle, delimiter="\t"))
        if (
            payload.get("selected_roots") != 4
            or payload.get("counts_by_phase") != {"0": 2, "1": 2}
            or payload.get("changed_split_assignments_from_normalized_input") != 0
            or payload.get("selected_cross_split_board_collision_count") != 0
            or payload.get("selected_cross_split_game_group_collision_count") != 0
            or len(selected) != 4
            or {row["record_id"] for row in selected} != {
                "record-2",
                "record-3",
                "record-4",
                "record-5",
            }
        ):
            print(f"unexpected hard-root selection: {payload} {selected}", file=sys.stderr)
            return 1

        repeat = subprocess.run(command, capture_output=True, text=True, check=False)
        if repeat.returncode != 0 or json.loads(report.read_text(encoding="utf-8")) != payload:
            print("selection is not deterministic", file=sys.stderr)
            return 1

        collision_normalized = root / "collision-normalized.tsv"
        collision_teacher = root / "collision-teacher.tsv"
        collision_output = root / "collision-selected.tsv"
        collision_report = root / "collision-report.json"
        collision_roots = [normalized_row(10, 0), normalized_row(11, 1)]
        for collision_root in collision_roots:
            collision_root["game_group_id"] = "shared-game"
            collision_root["split"] = "train"
        collision_teacher_rows = teacher_rows(collision_roots[0], 2)
        second_teacher_rows = teacher_rows(collision_roots[1], 3)
        for row in second_teacher_rows:
            row["root_split"] = "test"
        collision_teacher_rows.extend(second_teacher_rows)
        write_tsv(collision_normalized, NORMALIZED_FIELDS, collision_roots)
        write_tsv(collision_teacher, TEACHER_FIELDS, collision_teacher_rows)
        collision_command = [
            sys.executable,
            str(args.selector),
            "--normalized-tsv",
            str(collision_normalized),
            "--move-teacher-tsv",
            str(collision_teacher),
            "--output-tsv",
            str(collision_output),
            "--report-out",
            str(collision_report),
            "--max-roots",
            "2",
            "--minimum-baseline-regret",
            "1",
            "--seed",
            "7",
        ]
        collision_result = subprocess.run(
            collision_command, capture_output=True, text=True, check=False
        )
        if (
            collision_result.returncode == 0
            or "selected roots have cross-split leakage after teacher split assignment"
            not in collision_result.stderr
            or collision_output.exists()
            or collision_report.exists()
        ):
            print(
                "game-group cross-split collision was not rejected before output: "
                f"{collision_result.stderr}",
                file=sys.stderr,
            )
            return 1

    print("hard search-teacher root selector smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
