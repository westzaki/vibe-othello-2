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


def make_tiny_pattern_artifact(
    weights_path: Path,
    manifest_path: Path,
    overrides: dict[int, int] | None = None,
) -> None:
    pattern_set_id = "fixed-pattern-fixture-v1"
    phase_count = 13
    phase_stride = 1 + 3**8 + 3**9
    weight_count = phase_stride * phase_count
    weights = [0] * weight_count
    for index, value in (overrides or {}).items():
        weights[index] = value

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
    for weight in weights:
        payload.extend(struct.pack("<i", weight))
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


def make_zero_tiny_pattern_artifact(weights_path: Path, manifest_path: Path) -> None:
    make_tiny_pattern_artifact(weights_path, manifest_path)


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
    terminal_board, _ = play_prefix(len(FORCED_PASS_MOVES.split()))
    terminal_black_diff = terminal_board.count("X") - terminal_board.count("O")
    label_score = terminal_black_diff if side == "X" else -terminal_black_diff
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
        "label_kind": "observed_final_disc_diff",
        "label_unit": "disc",
        "label_perspective": "side-to-move",
        "label_score_side_to_move": str(label_score),
        "occupied_count": occupied_count,
        "phase": phase,
        "player_disc_count": player_count,
        "opponent_disc_count": opponent_count,
        "empty_count": empty_count,
    }


def write_positions_tsv(path: Path) -> list[dict[str, str | int]]:
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
    return rows


TINY_EDGE_INSTANCES = (
    tuple(range(0, 8)),
    tuple(range(56, 64)),
    tuple(range(0, 64, 8)),
    tuple(range(7, 64, 8)),
)


def ternary_index(board_text: str, squares: tuple[int, ...]) -> int:
    index = 0
    place = 1
    for square in squares:
        cell = board_text[square]
        if cell == "X":
            digit = 1
        elif cell == "O":
            digit = 2
        else:
            digit = 0
        index += digit * place
        place *= 3
    return index


def edge_weight_overrides(rows: list[dict[str, str | int]], value: int) -> dict[int, int]:
    phase_stride = 1 + 3**8 + 3**9
    edge_offset = 1
    overrides: dict[int, int] = {}
    for row in rows:
        phase = int(row["phase"])
        board_text = str(row["board_a1_to_h8"])
        for squares in TINY_EDGE_INSTANCES:
            offset = phase * phase_stride + edge_offset + ternary_index(board_text, squares)
            overrides[offset] = value
    return overrides


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, capture_output=True, text=True)


def run_arena(
    exe: str,
    temp_dir: Path,
    report_name: str,
    summary_name: str,
    *,
    candidate_prefix: str = "candidate",
    baseline_prefix: str = "baseline",
    diagnostics_name: str | None = None,
    extra_args: list[str] | None = None,
) -> dict[str, object]:
    report = temp_dir / report_name
    summary = temp_dir / summary_name
    positions = temp_dir / "positions.tsv"
    candidate_weights = temp_dir / f"{candidate_prefix}.weights.bin"
    candidate_manifest = temp_dir / f"{candidate_prefix}.manifest.json"
    baseline_weights = temp_dir / f"{baseline_prefix}.weights.bin"
    baseline_manifest = temp_dir / f"{baseline_prefix}.manifest.json"
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
    if diagnostics_name is not None:
        command.extend(["--diagnostics-out", str(temp_dir / diagnostics_name)])
    if extra_args:
        command.extend(extra_args)
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
    loaded_report = json.loads(report.read_text(encoding="utf-8"))
    if diagnostics_name is not None:
        diagnostics = temp_dir / diagnostics_name
        if not diagnostics.exists():
            raise AssertionError("arena did not produce diagnostics report")
        loaded_report["diagnostics"] = json.loads(diagnostics.read_text(encoding="utf-8"))
    return loaded_report


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


def assert_same_artifact_diagnostics(report: dict[str, object]) -> None:
    diagnostics = report["diagnostics"]
    if diagnostics["selected_positions"] != 2:
        raise AssertionError(f"unexpected diagnostics selected positions: {diagnostics!r}")
    if diagnostics["static_score_diff_zero_count"] != 2:
        raise AssertionError(f"same artifact static scores should match: {diagnostics!r}")
    if diagnostics["static_score_diff_nonzero_count"] != 0:
        raise AssertionError(f"same artifact static scores unexpectedly differ: {diagnostics!r}")
    if diagnostics["best_move_disagreement_count"] != 0:
        raise AssertionError(f"same artifact best moves unexpectedly differ: {diagnostics!r}")
    if diagnostics["phase_mapping_mismatch_count"] != 0:
        raise AssertionError(f"runtime phase mapping mismatch: {diagnostics!r}")
    if diagnostics["label_perspective_mismatch_count"] != 0:
        raise AssertionError(f"label perspective mismatch: {diagnostics!r}")
    if diagnostics["results_by_depth"].keys() != {"1"}:
        raise AssertionError(f"depth sweep diagnostics missing depth 1: {diagnostics!r}")
    if diagnostics["sanity_checks"] != {
        "same_artifact_mirror": True,
        "same_artifact_side_swapped_tie": True,
    }:
        raise AssertionError(f"same artifact sanity checks missing: {diagnostics!r}")
    feature_activation = diagnostics["feature_activation"]["candidate"]
    if feature_activation["by_family"]["edge-8"]["instances_evaluated"] != 8:
        raise AssertionError(f"unexpected edge activation count: {feature_activation!r}")
    if feature_activation["by_family"]["corner-3x3"]["instances_evaluated"] != 8:
        raise AssertionError(f"unexpected corner activation count: {feature_activation!r}")
    if diagnostics["suspicious_sign_or_perspective_indicators"]:
        raise AssertionError(
            "same-artifact diagnostics should not flag suspicious indicators: "
            f"{diagnostics['suspicious_sign_or_perspective_indicators']!r}"
        )


def assert_signal_diagnostics(report: dict[str, object], expected_static_score: int) -> None:
    diagnostics = report["diagnostics"]
    if diagnostics["selected_positions"] != 2:
        raise AssertionError(f"unexpected diagnostics selected positions: {diagnostics!r}")
    if diagnostics["static_score_diff_nonzero_count"] != 2:
        raise AssertionError(f"signal artifact should produce static deltas: {diagnostics!r}")
    if diagnostics["candidate_static_score_range"] != {
        "min": expected_static_score,
        "max": expected_static_score,
    }:
        raise AssertionError(
            f"runtime score did not include expected edge contribution: {diagnostics!r}"
        )
    if diagnostics["baseline_static_score_range"] != {"min": 0, "max": 0}:
        raise AssertionError(f"baseline zero artifact scored unexpectedly: {diagnostics!r}")
    if diagnostics["labeled_static_sign_checked_count"] != 2:
        raise AssertionError(f"static sign sanity did not inspect both rows: {diagnostics!r}")
    if diagnostics["candidate_static_label_sign_agreement_count"] != 2:
        raise AssertionError(
            f"candidate static score sign should match fixture labels: {diagnostics!r}"
        )
    if diagnostics["candidate_static_label_sign_opposition_count"] != 0:
        raise AssertionError(
            f"candidate static score sign unexpectedly opposed labels: {diagnostics!r}"
        )
    if len(diagnostics["position_diagnostics"]) != 2:
        raise AssertionError(f"per-position diagnostics missing rows: {diagnostics!r}")
    for row in diagnostics["position_diagnostics"]:
        if row["candidate_static_score"] != expected_static_score:
            raise AssertionError(f"unexpected candidate static row score: {row!r}")
        if row["baseline_static_score"] != 0:
            raise AssertionError(f"unexpected baseline static row score: {row!r}")


def assert_swap_complements(forward: dict[str, object], reverse: dict[str, object]) -> None:
    if not math.isclose(
        forward["candidate_score_rate"] + reverse["candidate_score_rate"],
        1.0,
        abs_tol=1e-12,
    ):
        raise AssertionError(
            "candidate/baseline swap score rates do not complement: "
            f"{forward['candidate_score_rate']!r}, {reverse['candidate_score_rate']!r}"
        )
    if not math.isclose(
        forward["average_disc_diff_candidate_perspective"],
        -reverse["average_disc_diff_candidate_perspective"],
        abs_tol=1e-12,
    ):
        raise AssertionError(
            "candidate/baseline swap disc diffs do not negate: "
            f"{forward['average_disc_diff_candidate_perspective']!r}, "
            f"{reverse['average_disc_diff_candidate_perspective']!r}"
        )


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
        rows = write_positions_tsv(temp_dir / "positions.tsv")
        make_zero_tiny_pattern_artifact(
            temp_dir / "candidate.weights.bin", temp_dir / "candidate.manifest.json"
        )
        make_zero_tiny_pattern_artifact(
            temp_dir / "baseline.weights.bin", temp_dir / "baseline.manifest.json"
        )
        make_tiny_pattern_artifact(
            temp_dir / "signal.weights.bin",
            temp_dir / "signal.manifest.json",
            edge_weight_overrides(rows, -5),
        )

        first_report = run_arena(args.exe, temp_dir, "report-a.json", "summary-a.md")
        second_report = run_arena(args.exe, temp_dir, "report-b.json", "summary-b.md")
        assert_success_report(first_report)
        if stable_report(first_report) != stable_report(second_report):
            raise AssertionError("stable arena report payload changed between identical runs")
        same_artifact_report = run_arena(
            args.exe,
            temp_dir,
            "same-report.json",
            "same-summary.md",
            diagnostics_name="same-diagnostics.json",
            extra_args=[
                "--compare-static-scores",
                "--compare-best-moves",
                "--depth-sweep",
                "1",
            ],
        )
        assert_same_artifact_diagnostics(same_artifact_report)

        signal_report = run_arena(
            args.exe,
            temp_dir,
            "signal-report.json",
            "signal-summary.md",
            candidate_prefix="signal",
            baseline_prefix="baseline",
            diagnostics_name="signal-diagnostics.json",
            extra_args=[
                "--compare-static-scores",
                "--compare-best-moves",
                "--exact-adjudicate-disagreements",
                "--max-disagreements",
                "4",
                "--depth-sweep",
                "1",
            ],
        )
        assert_signal_diagnostics(signal_report, -20)

        reverse_signal_report = run_arena(
            args.exe,
            temp_dir,
            "reverse-signal-report.json",
            "reverse-signal-summary.md",
            candidate_prefix="baseline",
            baseline_prefix="signal",
        )
        assert_swap_complements(signal_report, reverse_signal_report)

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
