#!/usr/bin/env python3
"""Smoke test for WTHOR binary import and theoretical WLD isolation."""

from __future__ import annotations

import argparse
import csv
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


VALID_GAME = (
    "d3 c3 b3 e3 f3 c5 f6 g2 b5 c6 f4 a5 h1 f5 d6 e7 d7 e6 d8 c4 "
    "c7 b7 a8 b6 a4 f8 g4 b4 e8 a3 a7 g5 g8 c2 h4 g3 a2 h3 c1 d1 "
    "d2 e1 f1 f7 a6 h6 e2 b8 g7 c8 h5 g6 h2 h7 h8 g1 b2 f2 a1 b1"
)
NORMALIZED_HEADER = [
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
POLICY_HEADER = [
    "root_board_id",
    "board_a1_to_h8",
    "root_split",
    "root_phase",
    "root_empty_count",
    "move",
    "occurrence_count",
    "win_count",
    "draw_count",
    "loss_count",
    "source_dataset_id",
]


def wthor_move(token: str) -> int:
    return (int(token[1]) * 10) + (ord(token[0]) - ord("a") + 1)


def record(moves: str, actual: int, theoretical: int) -> bytes:
    values = [wthor_move(token) for token in moves.split() if token != "pass"]
    values.extend([0] * (60 - len(values)))
    return struct.pack("<HHHBB60B", 1, 2, 3, actual, theoretical, *values)


def write_fixture(path: Path) -> None:
    valid = record(VALID_GAME, 40, 40)
    duplicate = record(VALID_GAME, 40, 40)
    incomplete = record(" ".join(VALID_GAME.split()[:40]), 40, 40)
    invalid = record("d3 d3", 32, 32)
    header = struct.pack("<4BIHH4B", 20, 26, 2, 24, 4, 0, 2025, 8, 0, 24, 0)
    path.write_bytes(header + valid + duplicate + incomplete + invalid)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--importer", required=True, type=Path)
    parser.add_argument("--replay-helper", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="vibe-othello-wthor-") as temp:
        root = Path(temp)
        fixture = root / "WTH_2025.wtb"
        normalized = root / "normalized.tsv"
        theoretical = root / "theoretical-wld.tsv"
        policy = root / "played-move-policy.tsv"
        report = root / "report.json"
        write_fixture(fixture)
        result = subprocess.run(
            [
                sys.executable,
                str(args.importer),
                "--input",
                str(fixture),
                "--manifest",
                str(args.manifest),
                "--replay-helper",
                str(args.replay_helper),
                "--output",
                str(normalized),
                "--theoretical-wld-out",
                str(theoretical),
                "--policy-out",
                str(policy),
                "--report",
                str(report),
                "--min-ply",
                "4",
                "--max-ply",
                "12",
                "--ply-stride",
                "4",
                "--strict-board-disjoint-splits",
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            sys.stderr.write(result.stdout)
            sys.stderr.write(result.stderr)
            return 1

        with normalized.open(encoding="utf-8", newline="") as handle:
            reader = csv.DictReader(handle, delimiter="\t")
            if reader.fieldnames != NORMALIZED_HEADER:
                print(f"unexpected normalized header: {reader.fieldnames}", file=sys.stderr)
                return 1
            rows = list(reader)
        if len(rows) != 6:
            print(f"expected 6 normalized rows, got {len(rows)}", file=sys.stderr)
            return 1
        if {row["label_kind"] for row in rows} != {"observed_final_disc_diff"}:
            print("WTHOR theoretical score leaked into normalized labels", file=sys.stderr)
            return 1
        if len({row["game_group_id"] for row in rows}) != 1:
            print("duplicate semantic game did not share game_group_id", file=sys.stderr)
            return 1
        if len({row["split"] for row in rows}) != 1:
            print("duplicate semantic game crossed splits", file=sys.stderr)
            return 1

        with theoretical.open(encoding="utf-8", newline="") as handle:
            theoretical_rows = list(csv.DictReader(handle, delimiter="\t"))
        if len(theoretical_rows) != 1:
            print(
                f"expected one de-duplicated theoretical WLD row, got {len(theoretical_rows)}",
                file=sys.stderr,
            )
            return 1
        theoretical_row = theoretical_rows[0]
        if (
            theoretical_row["label_kind"] != "wld"
            or theoretical_row["label_unit"] != "wld"
            or theoretical_row["label_score_side_to_move"] not in {"-1", "0", "1"}
            or theoretical_row["occurrence_count"] != "3"
        ):
            print(f"unexpected theoretical WLD row: {theoretical_row}", file=sys.stderr)
            return 1

        with policy.open(encoding="utf-8", newline="") as handle:
            reader = csv.DictReader(handle, delimiter="\t")
            if reader.fieldnames != POLICY_HEADER:
                print(f"unexpected policy header: {reader.fieldnames}", file=sys.stderr)
                return 1
            policy_rows = list(reader)
        if len(policy_rows) != 3:
            print(f"expected 3 aggregate policy rows, got {len(policy_rows)}", file=sys.stderr)
            return 1
        if {row["move"] for row in policy_rows} != {"f3", "b5", "h1"}:
            print(f"unexpected played moves: {policy_rows}", file=sys.stderr)
            return 1
        if any(
            row["occurrence_count"] != "2"
            or row["win_count"] != "0"
            or row["draw_count"] != "0"
            or row["loss_count"] != "2"
            for row in policy_rows
        ):
            print(f"unexpected aggregate policy outcomes: {policy_rows}", file=sys.stderr)
            return 1

        payload = json.loads(report.read_text(encoding="utf-8"))
        expected = {
            "input_games": 4,
            "accepted_games": 3,
            "rejected_games": 1,
            "terminal_count": 2,
            "incomplete_games": 1,
            "normalized_label_eligible_games": 2,
            "emitted_positions": 6,
            "theoretical_wld_rows": 1,
            "theoretical_cutoff_games": 3,
            "source_file_count": 1,
            "played_move_policy_rows": 3,
            "played_move_policy_occurrences": 6,
        }
        for key, value in expected.items():
            if payload.get(key) != value:
                print(
                    f"report mismatch for {key}: {payload.get(key)!r} != {value!r}",
                    file=sys.stderr,
                )
                return 1
        if payload.get("cross_split_board_collision_count") != 0:
            print("connected split left a board collision", file=sys.stderr)
            return 1

        selected_normalized = root / "selected-normalized.tsv"
        selected_policy = root / "selected-policy.tsv"
        selected_report = root / "selected-report.json"
        selected_result = subprocess.run(
            [
                sys.executable,
                str(args.importer),
                "--input",
                str(fixture),
                "--manifest",
                str(args.manifest),
                "--replay-helper",
                str(args.replay_helper),
                "--output",
                str(selected_normalized),
                "--report",
                str(selected_report),
                "--policy-out",
                str(selected_policy),
                "--selected-ply",
                "4",
                "--selected-ply",
                "12",
                "--strict-board-disjoint-splits",
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        if selected_result.returncode != 0:
            sys.stderr.write(selected_result.stdout)
            sys.stderr.write(selected_result.stderr)
            return 1
        with selected_normalized.open(encoding="utf-8", newline="") as handle:
            selected_rows = list(csv.DictReader(handle, delimiter="\t"))
        if len(selected_rows) != 4:
            print(
                f"expected 4 exact-cutoff rows, got {len(selected_rows)}",
                file=sys.stderr,
            )
            return 1
        with selected_policy.open(encoding="utf-8", newline="") as handle:
            selected_policy_rows = list(csv.DictReader(handle, delimiter="\t"))
        if len(selected_policy_rows) != 2 or {
            row["move"] for row in selected_policy_rows
        } != {"f3", "h1"}:
            print(
                f"unexpected exact-cutoff policy rows: {selected_policy_rows}",
                file=sys.stderr,
            )
            return 1
        selected_payload = json.loads(selected_report.read_text(encoding="utf-8"))
        if selected_payload.get("emit_policy", {}).get("selected_plies") != [4, 12]:
            print("exact cutoff selection missing from report", file=sys.stderr)
            return 1

    print("WTHOR import smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
