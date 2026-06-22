#!/usr/bin/env python3
"""Smoke checks for low-empty root selector."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


HEADER_V1 = [
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
HEADER_V2 = [
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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selector", required=True, type=Path)
    return parser.parse_args()


def row(board_id: str, split: str, empty_count: int, phase: int, record: int) -> dict[str, str]:
    occupied = 64 - empty_count
    return {
        "record_id": f"record-{record}",
        "position_id": f"position-{record}",
        "game_group_id": f"game-{record // 2}",
        "board_id": board_id,
        "source_occurrence_id": f"occurrence-{record}",
        "source_dataset_id": "synthetic-selector-smoke",
        "split": split,
        "board_a1_to_h8": "X" * occupied + "." * empty_count,
        "label_kind": "observed_final_disc_diff",
        "label_unit": "disc",
        "label_perspective": "side_to_move",
        "label_score_side_to_move": str((record % 9) - 4),
        "occupied_count": str(occupied),
        "phase": str(phase),
        "player_disc_count": str(occupied // 2),
        "opponent_disc_count": str(occupied - occupied // 2),
        "empty_count": str(empty_count),
    }


def write_tsv(path: Path, header: list[str], rows: list[dict[str, str]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, delimiter="\t", lineterminator="\n", fieldnames=header)
        writer.writeheader()
        for item in rows:
            writer.writerow({field: item[field] for field in header})


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def run_selector(selector: Path, input_tsv: Path, out_tsv: Path, report: Path, *extra: str) -> subprocess.CompletedProcess[str]:
    command = [
        sys.executable,
        str(selector),
        "--normalized-tsv",
        str(input_tsv),
        "--output-tsv",
        str(out_tsv),
        "--report-out",
        str(report),
        "--max-empty",
        "12",
        "--max-roots",
        "3",
        "--seed",
        "0",
        "--dedupe-key",
        "board_id",
        "--preserve-split",
        "--require-schema-v2",
        *extra,
    ]
    return subprocess.run(command, check=False, capture_output=True, text=True)


def selected_board_ids(rows: list[dict[str, str]]) -> list[str]:
    return [row["board_id"] for row in rows]


def assert_report_fields(report: dict[str, Any]) -> None:
    required = {
        "input_rows",
        "eligible_rows",
        "unique_eligible_roots",
        "selected_roots",
        "duplicate_board_rows",
        "split_counts",
        "empty_count_counts",
        "phase_counts",
        "checksum",
        "command_args",
        "notes",
    }
    missing = sorted(required - set(report))
    if missing:
        raise AssertionError(f"selector report missing fields: {missing}")
    if not str(report["checksum"]).startswith("sha256:"):
        raise AssertionError("selector report checksum must be sha256-prefixed")
    if not any("not an Elo result" in note for note in report["notes"]):
        raise AssertionError("selector report must include non-claim notes")


def check_selector(selector: Path) -> bool:
    with tempfile.TemporaryDirectory() as raw_tmp:
        tmp = Path(raw_tmp)
        input_tsv = tmp / "input.tsv"
        output_tsv = tmp / "selected.tsv"
        report_json = tmp / "report.json"
        rows = [
            row("board-a", "train", 12, 10, 1),
            row("board-b", "validation", 8, 11, 2),
            row("board-b", "test", 8, 11, 3),
            row("board-c", "test", 13, 9, 4),
            row("board-d", "test", 2, 12, 5),
        ]
        write_tsv(input_tsv, HEADER_V2, rows)

        accepted = run_selector(selector, input_tsv, output_tsv, report_json)
        if accepted.returncode != 0:
            print(accepted.stderr, file=sys.stderr)
            return False
        selected = read_rows(output_tsv)
        report = json.loads(report_json.read_text(encoding="utf-8"))
        assert_report_fields(report)
        if selected_board_ids(selected) != ["board-a", "board-b", "board-d"]:
            print(f"unexpected selected boards: {selected_board_ids(selected)}", file=sys.stderr)
            return False
        if report["eligible_rows"] != 4 or report["unique_eligible_roots"] != 3:
            print(f"unexpected eligibility counts: {report}", file=sys.stderr)
            return False
        if report["duplicate_board_rows"] != 1:
            print(f"expected one duplicate eligible board row: {report}", file=sys.stderr)
            return False
        if report["split_counts"] != {"test": 1, "train": 1, "validation": 1}:
            print(f"split preservation mismatch: {report['split_counts']}", file=sys.stderr)
            return False
        if report["empty_count_counts"] != {"12": 1, "2": 1, "8": 1}:
            print(f"empty-count filter mismatch: {report['empty_count_counts']}", file=sys.stderr)
            return False

        v1_tsv = tmp / "schema-v1.tsv"
        write_tsv(v1_tsv, HEADER_V1, [rows[0]])
        rejected = run_selector(selector, v1_tsv, tmp / "v1-out.tsv", tmp / "v1-report.json")
        if rejected.returncode == 0 or "schema v1 is not supported" not in rejected.stderr:
            print(f"schema v1 should be rejected clearly: {rejected.stderr}", file=sys.stderr)
            return False

        too_many = run_selector(selector, input_tsv, tmp / "too-many.tsv", tmp / "too-many.json", "--max-roots", "4")
        if too_many.returncode == 0 or "fewer than requested" not in too_many.stderr:
            print(f"insufficient roots should fail: {too_many.stderr}", file=sys.stderr)
            return False
        partial = run_selector(
            selector,
            input_tsv,
            tmp / "partial.tsv",
            tmp / "partial.json",
            "--max-roots",
            "4",
            "--allow-less-than-requested",
        )
        if partial.returncode != 0:
            print(f"allow-less-than-requested should pass: {partial.stderr}", file=sys.stderr)
            return False
        partial_report = json.loads((tmp / "partial.json").read_text(encoding="utf-8"))
        if not partial_report["partial_selection"] or partial_report["selected_roots"] != 3:
            print(f"partial report mismatch: {partial_report}", file=sys.stderr)
            return False

        many_tsv = tmp / "many.tsv"
        many_rows = [row(f"sample-board-{index:02d}", "train", 4 + index % 4, 8 + index % 5, index) for index in range(30)]
        write_tsv(many_tsv, HEADER_V2, many_rows)
        deterministic_a = run_selector(selector, many_tsv, tmp / "seed0-a.tsv", tmp / "seed0-a.json", "--max-roots", "5")
        deterministic_b = run_selector(selector, many_tsv, tmp / "seed0-b.tsv", tmp / "seed0-b.json", "--max-roots", "5")
        different_seed = subprocess.run(
            [
                sys.executable,
                str(selector),
                "--normalized-tsv",
                str(many_tsv),
                "--output-tsv",
                str(tmp / "seed1.tsv"),
                "--report-out",
                str(tmp / "seed1.json"),
                "--max-empty",
                "12",
                "--max-roots",
                "5",
                "--seed",
                "1",
                "--dedupe-key",
                "board_id",
                "--preserve-split",
                "--require-schema-v2",
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        if deterministic_a.returncode != 0 or deterministic_b.returncode != 0 or different_seed.returncode != 0:
            print("deterministic sampling command failed", file=sys.stderr)
            return False
        seed0_a = selected_board_ids(read_rows(tmp / "seed0-a.tsv"))
        seed0_b = selected_board_ids(read_rows(tmp / "seed0-b.tsv"))
        seed1 = selected_board_ids(read_rows(tmp / "seed1.tsv"))
        if seed0_a != seed0_b:
            print(f"same seed was not deterministic: {seed0_a} vs {seed0_b}", file=sys.stderr)
            return False
        if seed0_a == seed1:
            print(f"different seed did not change capped sample: {seed0_a}", file=sys.stderr)
            return False
    return True


def main() -> int:
    args = parse_args()
    return 0 if check_selector(args.selector) else 1


if __name__ == "__main__":
    sys.exit(main())
