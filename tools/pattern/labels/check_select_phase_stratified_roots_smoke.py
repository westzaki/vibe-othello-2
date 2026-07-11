#!/usr/bin/env python3
"""Smoke checks for the phase-stratified normalized-v2 root selector."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path


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


def occupied_for_phase(phase: int) -> int:
    for occupied in range(4, 65):
        if min(12, ((occupied - 4) * 13) // 60) == phase:
            return occupied
    raise AssertionError(f"no occupied count for phase {phase}")


def normalized_row(phase: int, variant: int, game_group_id: str, split: str, board_id: str | None = None) -> dict[str, str]:
    occupied = occupied_for_phase(phase)
    player = min(occupied, 1 + variant)
    opponent = occupied - player
    empty = 64 - occupied
    identity = board_id or f"board-p{phase:02d}-v{variant:02d}-{game_group_id}"
    return {
        "record_id": f"record-{identity}",
        "position_id": f"position-{identity}",
        "game_group_id": game_group_id,
        "board_id": identity,
        "source_occurrence_id": f"occurrence-{identity}",
        "source_dataset_id": "synthetic-phase-selector-smoke",
        "split": split,
        "board_a1_to_h8": "X" * player + "O" * opponent + "-" * empty,
        "label_kind": "observed_final_disc_diff",
        "label_unit": "disc",
        "label_perspective": "side_to_move",
        "label_score_side_to_move": "0",
        "occupied_count": str(occupied),
        "phase": str(phase),
        "player_disc_count": str(player),
        "opponent_disc_count": str(opponent),
        "empty_count": str(empty),
    }


def write_tsv(path: Path, header: list[str], rows: list[dict[str, str]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, delimiter="\t", lineterminator="\n", fieldnames=header)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row[field] for field in header})


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def run_selector(
    selector: Path,
    normalized: Path,
    output: Path,
    report: Path,
    roots_per_phase: int,
    *extra: str,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(selector),
            "--normalized-tsv",
            str(normalized),
            "--output-tsv",
            str(output),
            "--report-out",
            str(report),
            "--roots-per-phase",
            str(roots_per_phase),
            "--seed",
            "17",
            *extra,
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def phase_rows(per_phase: int, *, game_prefix: str = "game") -> list[dict[str, str]]:
    splits = ("train", "validation", "test")
    return [
        normalized_row(
            phase,
            variant,
            f"{game_prefix}-p{phase:02d}-v{variant:02d}",
            splits[(phase + variant) % len(splits)],
        )
        for phase in range(13)
        for variant in range(per_phase)
    ]


def load_report(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def check_exact_quota_and_dedupe(selector: Path, root: Path) -> bool:
    rows = phase_rows(3)
    duplicate = dict(rows[5 * 3])
    duplicate["record_id"] = "record-duplicate"
    duplicate["source_occurrence_id"] = "occurrence-duplicate"
    rows.append(duplicate)
    normalized = root / "exact-input.tsv"
    output = root / "exact-output.tsv"
    report_path = root / "exact-report.json"
    write_tsv(normalized, HEADER_V2, rows)
    result = run_selector(selector, normalized, output, report_path, 2)
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        return False
    selected = read_rows(output)
    report = load_report(report_path)
    selected_phase_counts = Counter(row["phase"] for row in selected)
    if len(selected) != 26 or selected_phase_counts != Counter({str(phase): 2 for phase in range(13)}):
        print(f"exact quota mismatch: {selected_phase_counts}", file=sys.stderr)
        return False
    if report.get("duplicate_board_rows") != 1 or report.get("duplicate_board_id_count") != 1:
        print(f"duplicate report mismatch: {report}", file=sys.stderr)
        return False
    if report.get("shortage_phases") != [] or report.get("all_phase_quotas_satisfied") is not True:
        print(f"quota status mismatch: {report}", file=sys.stderr)
        return False
    if report.get("input_checksum") != f"sha256:{hashlib.sha256(normalized.read_bytes()).hexdigest()}":
        print("input checksum mismatch", file=sys.stderr)
        return False
    if report.get("output_checksum") != f"sha256:{hashlib.sha256(output.read_bytes()).hexdigest()}":
        print("output checksum mismatch", file=sys.stderr)
        return False
    first_rows_by_board: dict[str, dict[str, str]] = {}
    for row in rows:
        first_rows_by_board.setdefault(row["board_id"], row)
    if any(row != first_rows_by_board[row["board_id"]] for row in selected):
        print("selected source row was modified", file=sys.stderr)
        return False
    return True


def check_shortage_and_partial(selector: Path, root: Path) -> bool:
    rows = phase_rows(2)
    rows = [row for row in rows if not (row["phase"] == "12" and row["board_id"].endswith("v01"))]
    normalized = root / "shortage-input.tsv"
    write_tsv(normalized, HEADER_V2, rows)
    strict_output = root / "strict-output.tsv"
    strict_report_path = root / "strict-report.json"
    strict = run_selector(selector, normalized, strict_output, strict_report_path, 2, "--require-all-phases")
    if strict.returncode == 0 or "phase quota shortage" not in strict.stderr:
        print(f"strict shortage should fail: {strict.stderr}", file=sys.stderr)
        return False
    strict_report = load_report(strict_report_path)
    if strict_report.get("shortage_phases") != [12] or strict_report.get("partial_selection") is not True:
        print(f"strict shortage report mismatch: {strict_report}", file=sys.stderr)
        return False
    if len(read_rows(strict_output)) != 25:
        print("strict shortage output should contain the diagnostic partial selection", file=sys.stderr)
        return False

    partial = run_selector(selector, normalized, root / "partial-output.tsv", root / "partial-report.json", 2)
    if partial.returncode != 0:
        print(f"partial selection should pass: {partial.stderr}", file=sys.stderr)
        return False
    return True


def check_determinism(selector: Path, root: Path) -> bool:
    normalized = root / "deterministic-input.tsv"
    write_tsv(normalized, HEADER_V2, phase_rows(12, game_prefix="deterministic"))
    output_a = root / "deterministic-a.tsv"
    output_b = root / "deterministic-b.tsv"
    first = run_selector(selector, normalized, output_a, root / "deterministic-a.json", 3)
    second = run_selector(selector, normalized, output_b, root / "deterministic-b.json", 3)
    if first.returncode != 0 or second.returncode != 0 or output_a.read_bytes() != output_b.read_bytes():
        print("same seed did not produce identical output", file=sys.stderr)
        return False
    different = subprocess.run(
        [
            sys.executable,
            str(selector),
            "--normalized-tsv",
            str(normalized),
            "--output-tsv",
            str(root / "deterministic-seed18.tsv"),
            "--report-out",
            str(root / "deterministic-seed18.json"),
            "--roots-per-phase",
            "3",
            "--seed",
            "18",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if different.returncode != 0:
        print(different.stderr, file=sys.stderr)
        return False
    if output_a.read_bytes() == (root / "deterministic-seed18.tsv").read_bytes():
        print("different seed did not change a surplus selection", file=sys.stderr)
        return False
    return True


def check_connected_split_preservation(selector: Path, root: Path) -> bool:
    positive_rows = phase_rows(1)
    normalized = root / "connected-input.tsv"
    output = root / "connected-output.tsv"
    write_tsv(normalized, HEADER_V2, positive_rows)
    result = run_selector(selector, normalized, output, root / "connected-report.json", 1)
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        return False
    input_splits = {row["board_id"]: row["split"] for row in positive_rows}
    if any(row["split"] != input_splits[row["board_id"]] for row in read_rows(output)):
        print("selector changed an input split", file=sys.stderr)
        return False

    board_leak_rows = phase_rows(1)
    conflicting = dict(board_leak_rows[0])
    conflicting["record_id"] = "record-cross-split-board"
    conflicting["source_occurrence_id"] = "occurrence-cross-split-board"
    conflicting["split"] = "test" if conflicting["split"] != "test" else "train"
    board_leak_rows.append(conflicting)
    board_leak = root / "board-leak.tsv"
    write_tsv(board_leak, HEADER_V2, board_leak_rows)
    rejected_board = run_selector(selector, board_leak, root / "board-leak-out.tsv", root / "board-leak.json", 1)
    if rejected_board.returncode == 0 or "board_id cross-split leakage" not in rejected_board.stderr:
        print(f"cross-split board should fail: {rejected_board.stderr}", file=sys.stderr)
        return False

    game_leak_rows = phase_rows(1)
    conflicting_game = dict(game_leak_rows[0])
    conflicting_game["record_id"] = "record-cross-split-game"
    conflicting_game["source_occurrence_id"] = "occurrence-cross-split-game"
    conflicting_game["board_id"] = "board-cross-split-game"
    conflicting_game["split"] = "test" if conflicting_game["split"] != "test" else "train"
    game_leak_rows.append(conflicting_game)
    game_leak = root / "game-leak.tsv"
    write_tsv(game_leak, HEADER_V2, game_leak_rows)
    rejected_game = run_selector(selector, game_leak, root / "game-leak-out.tsv", root / "game-leak.json", 1)
    if rejected_game.returncode == 0 or "game_group_id cross-split leakage" not in rejected_game.stderr:
        print(f"cross-split game should fail: {rejected_game.stderr}", file=sys.stderr)
        return False
    return True


def check_game_cap(selector: Path, root: Path) -> bool:
    rows = [
        normalized_row(phase, 1, "cap-game-a", "train", f"cap-a-p{phase:02d}")
        for phase in range(13)
    ] + [
        normalized_row(phase, 2, "cap-game-b", "validation", f"cap-b-p{phase:02d}")
        for phase in range(13)
    ]
    normalized = root / "cap-input.tsv"
    output = root / "cap-output.tsv"
    report_path = root / "cap-report.json"
    write_tsv(normalized, HEADER_V2, rows)
    result = run_selector(selector, normalized, output, report_path, 1, "--max-roots-per-game-group", "7")
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        return False
    selected = read_rows(output)
    selected_games = Counter(row["game_group_id"] for row in selected)
    report = load_report(report_path)
    if len(selected) != 13 or max(selected_games.values()) > 7:
        print(f"game cap mismatch: {selected_games}", file=sys.stderr)
        return False
    if report.get("maximum_selected_rows_under_game_cap") != 13:
        print(f"game cap report mismatch: {report}", file=sys.stderr)
        return False
    return True


def check_invalid_schema(selector: Path, root: Path) -> bool:
    valid_row = normalized_row(0, 1, "invalid-schema-game", "train")
    v1 = root / "schema-v1.tsv"
    write_tsv(v1, HEADER_V1, [valid_row])
    rejected_v1 = run_selector(selector, v1, root / "schema-v1-out.tsv", root / "schema-v1.json", 1)
    if rejected_v1.returncode == 0 or "schema v2" not in rejected_v1.stderr:
        print(f"schema v1 should fail: {rejected_v1.stderr}", file=sys.stderr)
        return False

    invalid = dict(valid_row)
    invalid["phase"] = "12"
    invalid_tsv = root / "invalid-phase.tsv"
    write_tsv(invalid_tsv, HEADER_V2, [invalid])
    rejected_invalid = run_selector(selector, invalid_tsv, root / "invalid-phase-out.tsv", root / "invalid-phase.json", 1)
    if rejected_invalid.returncode == 0 or "phase must be" not in rejected_invalid.stderr:
        print(f"invalid phase should fail: {rejected_invalid.stderr}", file=sys.stderr)
        return False
    return True


def check_selector(selector: Path) -> bool:
    with tempfile.TemporaryDirectory() as raw_root:
        root = Path(raw_root)
        return (
            check_exact_quota_and_dedupe(selector, root)
            and check_shortage_and_partial(selector, root)
            and check_determinism(selector, root)
            and check_connected_split_preservation(selector, root)
            and check_game_cap(selector, root)
            and check_invalid_schema(selector, root)
        )


def main() -> int:
    args = parse_args()
    return 0 if check_selector(args.selector) else 1


if __name__ == "__main__":
    sys.exit(main())
