#!/usr/bin/env python3
"""CTest wrapper for the persistent pattern artifact arena smoke cases."""

from __future__ import annotations

import argparse
import json
import math
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path


FORCED_PASS_MOVES = (
    "e6 d6 c5 f6 c4 c3 d3 b4 c6 b3 b2 a1 f5 c7 a3 g5 b7 a2 c1 "
    "a7 a8 e2 d2 e1 e7 d7 h5 e8 a6 b5 c2 f4 f2 f1 g7 h4 d8 h8 "
    "c8 d1 f3 g2 h1 a4 g6 b1 g8 h6 g3 g4 f8 h2 h3 f7 e3 b8 h7 b6 a5"
)

DIRECTIONS = (
    (-1, -1),
    (0, -1),
    (1, -1),
    (-1, 0),
    (1, 0),
    (-1, 1),
    (0, 1),
    (1, 1),
)


def make_zero_tiny_pattern_artifact(weights_path: Path, manifest_path: Path) -> None:
    pattern_set_id = "fixed-pattern-fixture-v1"
    phase_count = 13
    phase_stride = 1 + 3**8 + 3**9
    weight_count = phase_stride * phase_count

    payload = bytearray(b"VOPWGT\0\0")
    payload.extend(
        struct.pack(
            "<HHHHHHHHI",
            1,
            1,
            1,
            1,
            phase_count,
            2,
            len(pattern_set_id),
            0,
            weight_count,
        )
    )
    payload.extend(pattern_set_id.encode("utf-8"))
    payload.extend(b"\0" * (weight_count * 4))
    checksum = f"0x{zlib.crc32(payload) & 0xFFFFFFFF:08x}"
    payload.extend(struct.pack("<I", int(checksum, 16)))
    weights_path.write_bytes(payload)

    manifest_path.write_text(
        json.dumps(
            {
                "format_version": 1,
                "bit_order": "a1-lsb",
                "score_unit": "disc-diff",
                "score_scale": 1,
                "phase_count": phase_count,
                "pattern_set_id": pattern_set_id,
                "weights_file": weights_path.name,
                "weights_checksum": checksum,
                "notes": "local arena smoke artifact; not production",
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )


def initial_board() -> list[str]:
    board = ["-"] * 64
    board[index_for("d4")] = "O"
    board[index_for("e4")] = "X"
    board[index_for("d5")] = "X"
    board[index_for("e5")] = "O"
    return board


def index_for(move: str) -> int:
    file_index = ord(move[0]) - ord("a")
    rank_index = int(move[1]) - 1
    return rank_index * 8 + file_index


def flips_for(board: list[str], move_index: int, side: str) -> list[int]:
    opponent = "O" if side == "X" else "X"
    move_x = move_index % 8
    move_y = move_index // 8
    flips: list[int] = []
    for delta_x, delta_y in DIRECTIONS:
        x = move_x + delta_x
        y = move_y + delta_y
        run: list[int] = []
        while 0 <= x < 8 and 0 <= y < 8:
            index = y * 8 + x
            if board[index] == opponent:
                run.append(index)
                x += delta_x
                y += delta_y
                continue
            if board[index] == side and run:
                flips.extend(run)
            break
    return flips


def play_prefix(move_count: int) -> tuple[list[str], str]:
    board = initial_board()
    side = "X"
    moves = FORCED_PASS_MOVES.split()
    for move in moves[:move_count]:
        move_index = index_for(move)
        flips = flips_for(board, move_index, side)
        if board[move_index] != "-" or not flips:
            raise AssertionError(f"fixture move {move} is not legal for {side}")
        board[move_index] = side
        for flip in flips:
            board[flip] = side
        side = "O" if side == "X" else "X"
    return board, side


def relative_board_text(board: list[str], side_to_move: str) -> str:
    if side_to_move == "X":
        return "".join(board)
    return "".join("X" if cell == "O" else "O" if cell == "X" else "-" for cell in board)


def make_position_row(board_id: str, move_count: int, split: str) -> dict[str, str | int]:
    board, side = play_prefix(move_count)
    board_text = relative_board_text(board, side)
    occupied_count = sum(cell != "-" for cell in board_text)
    player_count = board_text.count("X")
    opponent_count = board_text.count("O")
    empty_count = board_text.count("-")
    if occupied_count + empty_count != 64:
        raise AssertionError("bad fixture board counts")
    phase = min(12, max(0, ((occupied_count - 4) * 13) // 60))
    return {
        "record_id": board_id,
        "position_id": f"{board_id}-pos",
        "game_group_id": "arena-smoke",
        "board_id": board_id,
        "source_occurrence_id": f"{board_id}-src",
        "source_dataset_id": "arena-smoke",
        "split": split,
        "board_a1_to_h8": board_text,
        "label_kind": "disc-diff",
        "label_unit": "disc",
        "label_perspective": "side-to-move",
        "label_score_side_to_move": "0",
        "occupied_count": occupied_count,
        "phase": phase,
        "player_disc_count": player_count,
        "opponent_disc_count": opponent_count,
        "empty_count": empty_count,
    }


def write_positions_tsv(path: Path) -> None:
    header = [
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
    rows = [
        make_position_row("board-a", 56, "train"),
        make_position_row("board-a", 56, "train"),
        make_position_row("board-b", 58, "validation"),
        make_position_row("board-c-high-empty", 40, "test"),
    ]
    with path.open("w", encoding="utf-8", newline="") as output:
        output.write("\t".join(header) + "\n")
        for row in rows:
            output.write("\t".join(str(row[column]) for column in header) + "\n")


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, capture_output=True, text=True)


def run_arena(exe: str, temp_dir: Path, report_name: str, summary_name: str) -> dict[str, object]:
    report = temp_dir / report_name
    summary = temp_dir / summary_name
    positions = temp_dir / "positions.tsv"
    candidate_weights = temp_dir / "candidate.weights.bin"
    candidate_manifest = temp_dir / "candidate.manifest.json"
    baseline_weights = temp_dir / "baseline.weights.bin"
    baseline_manifest = temp_dir / "baseline.manifest.json"
    command = [
        exe,
        "--positions-tsv",
        str(positions),
        "--candidate-weights",
        str(candidate_weights),
        "--candidate-manifest",
        str(candidate_manifest),
        "--candidate-name",
        "tiny",
        "--baseline-weights",
        str(baseline_weights),
        "--baseline-manifest",
        str(baseline_manifest),
        "--baseline-name",
        "tiny",
        "--max-empty",
        "4",
        "--max-positions",
        "2",
        "--seed",
        "0",
        "--side-swap",
        "--depth",
        "1",
        "--report-out",
        str(report),
        "--summary-out",
        str(summary),
        "--progress-every",
        "2",
    ]
    completed = run(command)
    if completed.returncode != 0:
        raise AssertionError(
            f"{command} exited {completed.returncode}\n"
            f"stdout:\n{completed.stdout}\n"
            f"stderr:\n{completed.stderr}"
        )
    if "pattern-artifact-arena progress games=2" not in completed.stderr:
        raise AssertionError(f"progress stderr missing expected marker:\n{completed.stderr}")
    if not report.exists() or not summary.exists():
        raise AssertionError("arena did not produce report and summary")
    return json.loads(report.read_text(encoding="utf-8"))


def stable_report(report: dict[str, object]) -> dict[str, object]:
    stable = dict(report)
    for key in ("wall_time_sec", "games_per_sec", "checksum", "command"):
        stable.pop(key, None)
    return stable


def assert_success_report(report: dict[str, object]) -> None:
    if report["input_rows"] != 4:
        raise AssertionError(f"unexpected input_rows: {report['input_rows']!r}")
    if report["eligible_rows"] != 3:
        raise AssertionError(f"unexpected eligible_rows: {report['eligible_rows']!r}")
    if report["duplicate_board_id_rows"] != 1:
        raise AssertionError(
            f"unexpected duplicate count: {report['duplicate_board_id_rows']!r}"
        )
    if report["selected_positions"] != 2:
        raise AssertionError(f"unexpected selected_positions: {report['selected_positions']!r}")
    if report["games_played"] != 4:
        raise AssertionError(f"unexpected games_played: {report['games_played']!r}")
    total = report["candidate_wins"] + report["baseline_wins"] + report["draws"]
    if total != report["games_played"]:
        raise AssertionError(f"W/L/D does not sum to games_played: {total!r}")
    if report["illegal_or_failed_games"] != 0:
        raise AssertionError(
            f"unexpected illegal_or_failed_games: {report['illegal_or_failed_games']!r}"
        )
    if not math.isclose(
        report["candidate_score_rate"],
        report["candidate_score"] / report["games_played"],
    ):
        raise AssertionError("candidate_score_rate does not match score/games")
    expected_side_keys = {"candidate_side_to_move", "candidate_opponent"}
    if set(report["results_by_side_assignment"].keys()) != expected_side_keys:
        raise AssertionError(
            f"unexpected side assignment buckets: {report['results_by_side_assignment']!r}"
        )
    if "12" not in report["results_by_phase"]:
        raise AssertionError("phase bucket for late-game positions is missing")


def assert_failure(command: list[str], expected_stderr: str) -> None:
    completed = run(command)
    if completed.returncode == 0:
        raise AssertionError(f"{command} unexpectedly succeeded")
    if expected_stderr not in completed.stderr:
        raise AssertionError(
            f"{command} stderr did not contain {expected_stderr!r}\n{completed.stderr}"
        )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    args = parser.parse_args(argv)

    with tempfile.TemporaryDirectory() as temp:
        temp_dir = Path(temp)
        write_positions_tsv(temp_dir / "positions.tsv")
        make_zero_tiny_pattern_artifact(
            temp_dir / "candidate.weights.bin", temp_dir / "candidate.manifest.json"
        )
        make_zero_tiny_pattern_artifact(
            temp_dir / "baseline.weights.bin", temp_dir / "baseline.manifest.json"
        )

        first_report = run_arena(args.exe, temp_dir, "report-a.json", "summary-a.md")
        second_report = run_arena(args.exe, temp_dir, "report-b.json", "summary-b.md")
        assert_success_report(first_report)
        if stable_report(first_report) != stable_report(second_report):
            raise AssertionError("stable arena report payload changed between identical runs")

        common_command = [
            args.exe,
            "--positions-tsv",
            str(temp_dir / "positions.tsv"),
            "--candidate-weights",
            str(temp_dir / "candidate.weights.bin"),
            "--candidate-manifest",
            str(temp_dir / "candidate.manifest.json"),
            "--candidate-name",
            "tiny",
            "--baseline-weights",
            str(temp_dir / "baseline.weights.bin"),
            "--baseline-manifest",
            str(temp_dir / "baseline.manifest.json"),
            "--baseline-name",
            "tiny",
            "--report-out",
            str(temp_dir / "bad-report.json"),
            "--summary-out",
            str(temp_dir / "bad-summary.md"),
        ]
        missing_weights = common_command.copy()
        missing_weights[missing_weights.index(str(temp_dir / "candidate.weights.bin"))] = str(
            temp_dir / "missing.weights.bin"
        )
        assert_failure(missing_weights, "cannot read artifact weights")

        mismatched_pattern = common_command.copy()
        mismatched_pattern[mismatched_pattern.index("tiny")] = "pattern-v1-buro-lite"
        assert_failure(mismatched_pattern, "manifest pattern_set_id mismatch")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
