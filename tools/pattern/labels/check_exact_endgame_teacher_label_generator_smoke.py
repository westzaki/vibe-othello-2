#!/usr/bin/env python3
"""CTest wrapper for exact endgame teacher label generation."""

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


POSITIONS = [
    (
        "zero-empty",
        "train",
        "BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/WWWWWWWW/WWWWWWWW/WWWWWWWW b",
    ),
    (
        "one-empty-move",
        "train",
        "BBBBBBW./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b",
    ),
    (
        "one-empty-pass",
        "validation",
        "BBBBBWB./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b",
    ),
    (
        "two-empty",
        "test",
        "WWWWWWW./BBBBBBB./BBWBWBWW/BBBBBWWB/BWBBWWWB/BBBWWWWW/BBBBWWWW/BBBWWWWW b",
    ),
    (
        "fourteen-empty",
        "train",
        "WWWWWWW./.WWWBW../BBWBBB../BWWBB.B./BWBBBBBB/BWBBB.B./.WWWWB../BW.WWWWW b",
    ),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generator", required=True, type=Path)
    parser.add_argument("--overlay", required=True, type=Path)
    parser.add_argument("--dataset-exe", required=True, type=Path)
    parser.add_argument("--trainer", required=True, type=Path)
    return parser.parse_args()


def phase_for_occupied(occupied: int) -> int:
    return min(12, ((occupied - 4) * 13) // 60)


def relative_board(serialized: str) -> str:
    board_text, side = serialized.split()
    player_disc = "B" if side == "b" else "W"
    opponent_disc = "W" if side == "b" else "B"
    board = ["-"] * 64
    ranks = board_text.split("/")
    if len(ranks) != 8:
        raise AssertionError(serialized)
    for rank_from_top, row in enumerate(ranks):
        rank = 7 - rank_from_top
        if len(row) != 8:
            raise AssertionError(serialized)
        for file, value in enumerate(row):
            index = rank * 8 + file
            if value == player_disc:
                board[index] = "X"
            elif value == opponent_disc:
                board[index] = "O"
            elif value == ".":
                board[index] = "-"
            else:
                raise AssertionError(serialized)
    return "".join(board)


def normalized_row(index: int, board_id: str, split: str, serialized: str) -> dict[str, str]:
    board = relative_board(serialized)
    player = board.count("X")
    opponent = board.count("O")
    empty = board.count("-")
    occupied = player + opponent
    return {
        "record_id": f"record-{index}",
        "position_id": f"position-{index}",
        "game_group_id": f"game-{index % 3}",
        "board_id": board_id,
        "source_occurrence_id": f"source-{index}",
        "source_dataset_id": "synthetic-exact-teacher",
        "split": split,
        "board_a1_to_h8": board,
        "label_kind": "observed_final_disc_diff",
        "label_unit": "final_disc_diff",
        "label_perspective": "side_to_move",
        "label_score_side_to_move": "0",
        "occupied_count": str(occupied),
        "phase": str(phase_for_occupied(occupied)),
        "player_disc_count": str(player),
        "opponent_disc_count": str(opponent),
        "empty_count": str(empty),
    }


def fixture_rows(include_duplicate: bool = True) -> list[dict[str, str]]:
    rows = [
        normalized_row(index, f"board-{name}", split, serialized)
        for index, (name, split, serialized) in enumerate(POSITIONS, start=1)
    ]
    if include_duplicate:
        duplicate = dict(rows[1])
        duplicate["record_id"] = "record-duplicate"
        duplicate["position_id"] = "position-duplicate"
        duplicate["source_occurrence_id"] = "source-duplicate"
        rows.append(duplicate)
    return rows


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


def run(command: list[str], **kwargs: Any) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, capture_output=True, text=True, **kwargs)


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


def run_generator(
    generator: Path,
    normalized: Path,
    labels: Path,
    report: Path,
    *extra: str,
) -> subprocess.CompletedProcess[str]:
    return run(
        [
            str(generator),
            "--normalized-tsv",
            str(normalized),
            "--teacher-labels-out",
            str(labels),
            "--report-out",
            str(report),
            *extra,
        ]
    )


def check_basic(generator: Path, root: Path) -> bool:
    normalized = root / "normalized.tsv"
    labels = root / "teacher-labels.tsv"
    report_path = root / "report.json"
    write_tsv(normalized, NORMALIZED_HEADER_V2, fixture_rows())
    result = run_generator(
        generator,
        normalized,
        labels,
        report_path,
        "--max-empty",
        "2",
        "--seed",
        "7",
        "--progress-every",
        "2",
    )
    if not require_success(result):
        return False
    rows = load_tsv(labels)
    if list(rows[0].keys()) != TEACHER_HEADER:
        print(f"bad teacher header: {rows[0].keys()}", file=sys.stderr)
        return False
    expected_ids = [
        "board-one-empty-move",
        "board-one-empty-pass",
        "board-two-empty",
        "board-zero-empty",
    ]
    if [row["board_id"] for row in rows] != sorted(expected_ids):
        print(f"teacher rows not sorted by board_id: {rows!r}", file=sys.stderr)
        return False
    for row in rows:
        if row["label_kind"] != "teacher_exact_final_disc_diff":
            print(f"bad label kind: {row!r}", file=sys.stderr)
            return False
        if row["label_unit"] != "disc" or row["label_perspective"] != "side_to_move":
            print(f"bad label unit/perspective: {row!r}", file=sys.stderr)
            return False
        score = int(row["label_score_side_to_move"])
        depth = int(row["teacher_depth"])
        nodes = int(row["teacher_nodes"])
        if score < -64 or score > 64 or depth < 0 or depth > 2 or nodes <= 0:
            print(f"bad teacher numeric fields: {row!r}", file=sys.stderr)
            return False
        if row["teacher_source"] != "exact-endgame-v1":
            print(f"bad teacher source: {row!r}", file=sys.stderr)
            return False
    report = load_json(report_path)
    expected = {
        "schema_version": 1,
        "input_rows": 6,
        "eligible_rows": 5,
        "selected_rows": 4,
        "unique_boards_seen": 5,
        "unique_boards_solved": 4,
        "skipped_too_many_empty": 1,
        "duplicate_board_rows": 1,
        "solve_failures": 0,
        "teacher_depth_min": 0,
        "teacher_depth_max": 2,
    }
    for key, value in expected.items():
        if report.get(key) != value:
            print(f"report mismatch for {key}: {report.get(key)!r}", file=sys.stderr)
            return False
    if not str(report.get("checksum", "")).startswith("0x"):
        print(f"bad checksum: {report.get('checksum')!r}", file=sys.stderr)
        return False
    notes = report.get("notes", [])
    for phrase in ("local-only exact teacher label generation", "not an Elo result", "generated labels must not be committed"):
        if phrase not in notes:
            print(f"missing report note {phrase!r}: {notes!r}", file=sys.stderr)
            return False
    if "/" in report.get("normalized_input_path", ""):
        print(f"report wrote local path: {report.get('normalized_input_path')!r}", file=sys.stderr)
        return False
    return True


def check_max_positions(generator: Path, root: Path) -> bool:
    normalized = root / "cap-normalized.tsv"
    write_tsv(normalized, NORMALIZED_HEADER_V2, fixture_rows(include_duplicate=False))
    outputs: list[str] = []
    for index in range(2):
        labels = root / f"cap-{index}.tsv"
        report = root / f"cap-{index}.json"
        result = run_generator(
            generator,
            normalized,
            labels,
            report,
            "--max-empty",
            "14",
            "--max-positions",
            "2",
            "--seed",
            "123",
        )
        if not require_success(result):
            return False
        outputs.append(labels.read_text(encoding="utf-8"))
        if load_json(report).get("selected_rows") != 2:
            print("max-positions report mismatch", file=sys.stderr)
            return False
    if outputs[0] != outputs[1]:
        print("max-positions output is not deterministic for same seed", file=sys.stderr)
        return False
    other = root / "cap-other.tsv"
    other_report = root / "cap-other.json"
    if not require_success(
        run_generator(
            generator,
            normalized,
            other,
            other_report,
            "--max-empty",
            "14",
            "--max-positions",
            "2",
            "--seed",
            "999",
        )
    ):
        return False
    if other.read_text(encoding="utf-8") == outputs[0]:
        print("different seed did not alter capped selection in fixture", file=sys.stderr)
        return False
    return True


def check_failures(generator: Path, root: Path) -> bool:
    v1 = root / "schema-v1.tsv"
    row = fixture_rows(include_duplicate=False)[0]
    v1_row = {key: row[key] for key in NORMALIZED_HEADER_V1 if key in row}
    write_tsv(v1, NORMALIZED_HEADER_V1, [v1_row])
    if not require_failure(
        run_generator(generator, v1, root / "v1-labels.tsv", root / "v1-report.json"),
        "schema v1 is not supported",
    ):
        return False

    malformed = root / "malformed.tsv"
    bad = fixture_rows(include_duplicate=False)
    bad[0]["empty_count"] = "bad"
    write_tsv(malformed, NORMALIZED_HEADER_V2, bad)
    if not require_failure(
        run_generator(generator, malformed, root / "bad-labels.tsv", root / "bad-report.json"),
        "numeric fields must be integers",
    ):
        return False

    no_eligible = root / "no-eligible.tsv"
    write_tsv(no_eligible, NORMALIZED_HEADER_V2, fixture_rows(include_duplicate=False)[-1:])
    if not require_failure(
        run_generator(
            generator,
            no_eligible,
            root / "none-labels.tsv",
            root / "none-report.json",
            "--max-empty",
            "2",
        ),
        "no eligible normalized schema v2 rows",
    ):
        return False
    return True


def check_overlay_dataset_trainer(args: argparse.Namespace, root: Path) -> bool:
    normalized = root / "integration-normalized.tsv"
    labels = root / "integration-labels.tsv"
    generator_report = root / "integration-generator-report.json"
    teacher_normalized = root / "teacher-normalized.tsv"
    overlay_report = root / "overlay-report.json"
    dataset = root / "pattern-dataset.tsv"
    dataset_report = root / "dataset-report.json"
    weights = root / "weights.json"
    trainer_report = root / "trainer-report.json"
    write_tsv(normalized, NORMALIZED_HEADER_V2, fixture_rows())
    if not require_success(
        run_generator(
            args.generator,
            normalized,
            labels,
            generator_report,
            "--max-empty",
            "2",
            "--seed",
            "7",
        )
    ):
        return False
    if not require_success(
        run(
            [
                sys.executable,
                str(args.overlay),
                "--normalized-tsv",
                str(normalized),
                "--teacher-labels",
                str(labels),
                "--output",
                str(teacher_normalized),
                "--report",
                str(overlay_report),
                "--missing-policy",
                "drop",
            ]
        )
    ):
        return False
    overlay = load_json(overlay_report)
    if overlay.get("matched_rows") != 5 or overlay.get("dropped_rows") != 1:
        print(f"overlay coverage mismatch: {overlay!r}", file=sys.stderr)
        return False
    overlaid_rows = load_tsv(teacher_normalized)
    if not overlaid_rows or {row["label_kind"] for row in overlaid_rows} != {"teacher_exact_final_disc_diff"}:
        print(f"teacher-normalized labels were not overlaid: {overlaid_rows!r}", file=sys.stderr)
        return False
    dataset_result = run(
        [
            str(args.dataset_exe),
            "--normalized-tsv",
            str(teacher_normalized),
            "--report",
            str(dataset_report),
            "--output-format",
            "compact-tsv",
            "--pattern-set",
            "pattern-v2-endgame-lite",
        ]
    )
    if dataset_result.returncode != 0:
        sys.stderr.write(dataset_result.stderr)
        sys.stderr.write(dataset_result.stdout)
        return False
    dataset.write_text(dataset_result.stdout, encoding="utf-8")
    dataset_summary = load_json(dataset_report)
    if dataset_summary.get("pattern_set_id") != "pattern-v2-endgame-lite":
        print(f"dataset did not use endgame-lite: {dataset_summary!r}", file=sys.stderr)
        return False
    if dataset_summary.get("feature_occurrence_count") != len(overlaid_rows) * 58:
        print(f"unexpected endgame-lite feature count: {dataset_summary!r}", file=sys.stderr)
        return False
    if dataset_summary.get("total_table_entries") != 185895:
        print(f"unexpected endgame-lite table size: {dataset_summary!r}", file=sys.stderr)
        return False
    if not require_success(
        run(
            [
                sys.executable,
                str(args.trainer),
                "--dataset",
                str(dataset),
                "--mode",
                "pattern-sgd-v0c",
                "--epochs",
                "2",
                "--learning-rate",
                "0.01",
                "--weights-out",
                str(weights),
                "--report-out",
                str(trainer_report),
                "--seed",
                "3",
            ]
        )
    ):
        return False
    report = load_json(trainer_report)
    if report.get("trainer_version") != "pattern-sgd-v0c":
        print(f"trainer smoke did not run v0c: {report!r}", file=sys.stderr)
        return False
    if report.get("accepted_examples") != len(overlaid_rows):
        print(f"trainer accepted example mismatch: {report!r}", file=sys.stderr)
        return False
    return True


def main() -> int:
    args = parse_args()
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        checks = (
            check_basic(args.generator, root),
            check_max_positions(args.generator, root),
            check_failures(args.generator, root),
            check_overlay_dataset_trainer(args, root),
        )
        if not all(checks):
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
