#!/usr/bin/env python3
"""CTest wrapper for teacher label overlay TSV validation."""

from __future__ import annotations

import argparse
import csv
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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--overlay", required=True, type=Path)
    return parser.parse_args()


def write_tsv(path: Path, header: list[str], rows: list[dict[str, str]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, delimiter="\t", lineterminator="\n", fieldnames=header)
        writer.writeheader()
        writer.writerows(rows)


def load_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def normalized_rows() -> list[dict[str, str]]:
    boards = [
        "XXOO" + "-" * 60,
        "XXXOO" + "-" * 59,
        "XXXXOO" + "-" * 58,
    ]
    return [
        {
            "record_id": f"record-{index}",
            "position_id": f"position-{index}",
            "game_group_id": f"game-{index % 2}",
            "board_id": f"board-{index}",
            "source_occurrence_id": f"source-{index}",
            "source_dataset_id": "synthetic-overlay",
            "split": split,
            "board_a1_to_h8": boards[index - 1],
            "label_kind": "observed_final_disc_diff",
            "label_unit": "final_disc_diff",
            "label_perspective": "side_to_move",
            "label_score_side_to_move": str(index),
            "occupied_count": str(3 + index),
            "phase": "0",
            "player_disc_count": str(1 + index),
            "opponent_disc_count": "2",
            "empty_count": str(61 - index),
        }
        for index, split in enumerate(("train", "validation", "test"), start=1)
    ]


def teacher_rows(board_count: int = 3) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for index in range(1, board_count + 1):
        rows.append(
            {
                "board_id": f"board-{index}",
                "label_kind": "teacher_exact_final_disc_diff"
                if index != 2
                else "teacher_search_final_disc_diff",
                "label_unit": "disc",
                "label_perspective": "side_to_move",
                "label_score_side_to_move": str(10 - index),
                "teacher_source": "synthetic-fixture" if index != 2 else "search-depth-2",
                "teacher_depth": str(index),
                "teacher_nodes": str(index * 100),
            }
        )
    return rows


def run_overlay(
    overlay: Path,
    normalized: Path,
    teacher: Path,
    output: Path,
    report: Path,
    *extra: str,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(overlay),
            "--normalized-tsv",
            str(normalized),
            "--teacher-labels",
            str(teacher),
            "--output",
            str(output),
            "--report",
            str(report),
            *extra,
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def require_success(result: subprocess.CompletedProcess[str]) -> bool:
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return False
    return True


def require_failure(result: subprocess.CompletedProcess[str], expected: str) -> bool:
    if result.returncode == 0:
        print("command unexpectedly succeeded", file=sys.stderr)
        return False
    if expected not in result.stderr:
        print(f"missing expected error {expected!r}: {result.stderr!r}", file=sys.stderr)
        return False
    return True


def check_basic(overlay: Path, root: Path) -> bool:
    normalized = root / "normalized.tsv"
    teacher = root / "teacher.tsv"
    output = root / "teacher-normalized.tsv"
    report = root / "teacher-report.json"
    original_rows = normalized_rows()
    write_tsv(normalized, NORMALIZED_HEADER_V2, original_rows)
    teacher_data = teacher_rows()
    teacher_data.append(dict(teacher_data[0]))
    write_tsv(teacher, TEACHER_HEADER, teacher_data)
    result = run_overlay(overlay, normalized, teacher, output, report)
    if not require_success(result):
        return False
    rows = load_tsv(output)
    if [row["record_id"] for row in rows] != ["record-1", "record-2", "record-3"]:
        print(f"row order changed: {rows!r}", file=sys.stderr)
        return False
    for before, after in zip(original_rows, rows, strict=True):
        for key in NORMALIZED_HEADER_V2:
            if key in {"label_kind", "label_unit", "label_perspective", "label_score_side_to_move"}:
                continue
            if after[key] != before[key]:
                print(f"non-label field changed: {key}", file=sys.stderr)
                return False
    if rows[0]["label_unit"] != "disc" or rows[1]["label_kind"] != "teacher_search_final_disc_diff":
        print(f"label fields were not overlaid: {rows!r}", file=sys.stderr)
        return False
    data = load_json(report)
    expected = {
        "input_rows": 3,
        "output_rows": 3,
        "matched_rows": 3,
        "missing_rows": 0,
        "dropped_rows": 0,
        "unique_teacher_boards": 3,
        "label_kind_counts_before": {"observed_final_disc_diff": 3},
        "teacher_depth_min": 1,
        "teacher_depth_max": 3,
        "teacher_nodes_sum": 600,
    }
    for key, value in expected.items():
        if data.get(key) != value:
            print(f"report mismatch for {key}: {data.get(key)!r}", file=sys.stderr)
            return False
    if data.get("teacher_source_counts") != {"search-depth-2": 1, "synthetic-fixture": 2}:
        print(f"bad source counts: {data.get('teacher_source_counts')!r}", file=sys.stderr)
        return False
    if not str(data.get("checksum", "")).startswith("sha256:"):
        print(f"bad checksum: {data.get('checksum')!r}", file=sys.stderr)
        return False
    return True


def check_missing_policies(overlay: Path, root: Path) -> bool:
    normalized = root / "missing-normalized.tsv"
    teacher = root / "missing-teacher.tsv"
    write_tsv(normalized, NORMALIZED_HEADER_V2, normalized_rows())
    write_tsv(teacher, TEACHER_HEADER, teacher_rows(2))
    if not require_failure(
        run_overlay(overlay, normalized, teacher, root / "fail.tsv", root / "fail.json"),
        "missing teacher label",
    ):
        return False

    keep_output = root / "keep.tsv"
    keep_report = root / "keep.json"
    result = run_overlay(
        overlay,
        normalized,
        teacher,
        keep_output,
        keep_report,
        "--missing-policy",
        "keep-observed",
    )
    if not require_success(result):
        return False
    keep_rows = load_tsv(keep_output)
    if keep_rows[2]["label_kind"] != "observed_final_disc_diff":
        print(f"keep-observed did not preserve missing row: {keep_rows[2]!r}", file=sys.stderr)
        return False
    if load_json(keep_report).get("missing_rows") != 1:
        print("keep-observed report missing_rows mismatch", file=sys.stderr)
        return False

    drop_output = root / "drop.tsv"
    drop_report = root / "drop.json"
    result = run_overlay(
        overlay,
        normalized,
        teacher,
        drop_output,
        drop_report,
        "--missing-policy",
        "drop",
    )
    if not require_success(result):
        return False
    if [row["board_id"] for row in load_tsv(drop_output)] != ["board-1", "board-2"]:
        print("drop policy did not drop only missing row", file=sys.stderr)
        return False
    if load_json(drop_report).get("dropped_rows") != 1:
        print("drop report mismatch", file=sys.stderr)
        return False

    write_tsv(teacher, TEACHER_HEADER, [])
    if not require_failure(
        run_overlay(
            overlay,
            normalized,
            teacher,
            root / "zero.tsv",
            root / "zero.json",
            "--missing-policy",
            "drop",
        ),
        "teacher label TSV has no rows",
    ):
        return False
    return True


def check_conflict_and_validation(overlay: Path, root: Path) -> bool:
    normalized = root / "validation-normalized.tsv"
    teacher = root / "validation-teacher.tsv"
    write_tsv(normalized, NORMALIZED_HEADER_V2, normalized_rows())

    conflicting = teacher_rows(1)
    changed = dict(conflicting[0])
    changed["label_score_side_to_move"] = "11"
    write_tsv(teacher, TEACHER_HEADER, conflicting + [changed])
    if not require_failure(
        run_overlay(overlay, normalized, teacher, root / "conflict.tsv", root / "conflict.json"),
        "conflicting teacher label",
    ):
        return False

    v1 = root / "schema-v1.tsv"
    v1_rows = [
        {key: normalized_rows()[0][key] for key in NORMALIZED_HEADER_V1 if key in normalized_rows()[0]}
    ]
    write_tsv(v1, NORMALIZED_HEADER_V1, v1_rows)
    write_tsv(teacher, TEACHER_HEADER, teacher_rows(1))
    if not require_failure(
        run_overlay(overlay, v1, teacher, root / "v1.tsv", root / "v1.json"),
        "schema v2",
    ):
        return False

    cases = [
        ("label_score_side_to_move", "65", "label_score_side_to_move must be in [-64, 64]"),
        ("label_unit", "final_disc_diff", "label_unit must be disc"),
        ("label_perspective", "player", "label_perspective must be side_to_move"),
        ("teacher_depth", "-1", "teacher_depth must be >= 0 or empty"),
        ("teacher_nodes", "bad", "teacher_nodes must be an integer"),
    ]
    for field, value, expected in cases:
        rows = teacher_rows(1)
        rows[0][field] = value
        write_tsv(teacher, TEACHER_HEADER, rows)
        if not require_failure(
            run_overlay(overlay, normalized, teacher, root / f"bad-{field}.tsv", root / f"bad-{field}.json"),
            expected,
        ):
            return False
    return True


def main() -> int:
    args = parse_args()
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        checks = (
            check_basic(args.overlay, root),
            check_missing_policies(args.overlay, root),
            check_conflict_and_validation(args.overlay, root),
        )
        if not all(checks):
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
