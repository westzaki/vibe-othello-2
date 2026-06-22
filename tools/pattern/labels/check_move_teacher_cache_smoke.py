#!/usr/bin/env python3
"""CTest smoke coverage for local exact move-teacher cache materialization."""

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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--materializer", required=True, type=Path)
    parser.add_argument("--matrix-helper", required=True, type=Path)
    parser.add_argument("--growth-cycle-helper", required=True, type=Path)
    return parser.parse_args()


def phase_for_occupied(occupied: int) -> int:
    return min(12, ((occupied - 4) * 13) // 60)


def board(player: int, opponent: int, empty: int) -> str:
    if player + opponent + empty != 64:
        raise AssertionError("bad board counts")
    return ("X" * player) + ("O" * opponent) + ("-" * empty)


def normalized_row(record: str, board_id: str, split: str, board_text: str, game: str = "game") -> dict[str, str]:
    occupied = board_text.count("X") + board_text.count("O")
    empty = board_text.count("-")
    return {
        "record_id": record,
        "position_id": f"{record}:position",
        "game_group_id": game,
        "board_id": board_id,
        "source_occurrence_id": f"{record}:source",
        "source_dataset_id": "synthetic-cache-smoke",
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


def move_row(
    root: dict[str, str],
    move: str,
    child_board: str,
    score: int,
    rank: int,
    best: bool,
    margin: int,
    nodes: int,
) -> dict[str, str]:
    child_occupied = child_board.count("X") + child_board.count("O")
    child_empty = child_board.count("-")
    child_id = f"move-teacher-v1:{root['board_id']}:{move}"
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
        "is_best_move": "1" if best else "0",
        "best_move_tie_count": "1",
        "move_rank": str(rank),
        "best_score_margin": str(margin),
        "teacher_source": "exact-move-teacher-v1",
        "teacher_depth": str(child_empty),
        "teacher_nodes": str(nodes),
    }


def write_tsv(path: Path, header: list[str], rows: list[dict[str, str]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=header, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row[key] for key in header})


def load_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


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


def materialize_command(
    args: argparse.Namespace,
    normalized: Path,
    cache_dir: Path,
    move_out: Path,
    child_out: Path,
    report: Path,
    *extra: str,
) -> list[str]:
    return [
        sys.executable,
        str(args.materializer),
        "--normalized-tsv",
        str(normalized),
        "--cache-dir",
        str(cache_dir),
        "--move-teacher-out",
        str(move_out),
        "--child-normalized-out",
        str(child_out),
        "--report-out",
        str(report),
        "--max-empty",
        "12",
        "--max-roots",
        "10",
        "--seed",
        "0",
        *extra,
    ]


def populate_command(
    args: argparse.Namespace,
    normalized: Path,
    cache_dir: Path,
    source: Path,
    report: Path,
) -> list[str]:
    return [
        sys.executable,
        str(args.materializer),
        "--normalized-tsv",
        str(normalized),
        "--cache-dir",
        str(cache_dir),
        "--report-out",
        str(report),
        "--max-empty",
        "12",
        "--max-roots",
        "10",
        "--seed",
        "0",
        "--source-move-teacher-tsv",
        str(source),
        "--populate-only",
    ]


def fixture(root: Path) -> tuple[Path, Path, list[dict[str, str]]]:
    root_a = normalized_row("record-a", "root-a", "train", board(61, 2, 1), "game-a")
    root_b = normalized_row("record-b", "root-b", "validation", board(58, 5, 1), "game-b")
    normalized = root / "normalized.tsv"
    source_move_teacher = root / "source-move-teacher.tsv"
    write_tsv(normalized, NORMALIZED_HEADER_V2, [root_b, root_a])
    write_tsv(
        source_move_teacher,
        MOVE_TEACHER_HEADER,
        [
            move_row(root_a, "a1", board(62, 2, 0), 8, 1, True, 0, 11),
            move_row(root_a, "b1", board(61, 3, 0), 2, 2, False, 6, 17),
            move_row(root_b, "pass", board(57, 7, 0), -4, 1, True, 0, 23),
        ],
    )
    return normalized, source_move_teacher, [root_b, root_a]


def check_full_hit_and_remap(args: argparse.Namespace, root: Path) -> bool:
    normalized, source_move_teacher, _ = fixture(root)
    cache_dir = root / "cache"
    if not require_success(run(populate_command(args, normalized, cache_dir, source_move_teacher, root / "populate.json"))):
        return False

    remapped_rows = [
        normalized_row("remap-b", "root-b", "test", board(58, 5, 1), "connected-b"),
        normalized_row("remap-a", "root-a", "validation", board(61, 2, 1), "connected-a"),
    ]
    remapped = root / "remapped.tsv"
    write_tsv(remapped, NORMALIZED_HEADER_V2, remapped_rows)
    move_out = root / "materialized-move.tsv"
    child_out = root / "materialized-child.tsv"
    report_out = root / "materialized-report.json"
    if not require_success(
        run(materialize_command(args, remapped, cache_dir, move_out, child_out, report_out, "--require-full-hit"))
    ):
        return False
    move_rows = load_tsv(move_out)
    child_rows = load_tsv(child_out)
    if [row["root_board_id"] for row in move_rows] != ["root-b", "root-a", "root-a"]:
        print(f"materialized row order did not follow current normalized order: {move_rows}", file=sys.stderr)
        return False
    if move_rows[0]["root_record_id"] != "remap-b" or move_rows[0]["root_split"] != "test":
        print(f"current root metadata was not used: {move_rows[0]}", file=sys.stderr)
        return False
    if child_rows[0]["split"] != "test" or child_rows[0]["game_group_id"] != "connected-b":
        print(f"child normalized row did not use remapped split/game: {child_rows[0]}", file=sys.stderr)
        return False
    report = load_json(report_out)
    cache = report.get("cache", {})
    expected_nodes = 11 + 17 + 23
    if cache.get("root_hits") != 2 or cache.get("root_misses") != 0:
        print(f"unexpected cache hit report: {cache}", file=sys.stderr)
        return False
    if cache.get("exact_nodes_newly_solved") != 0 or cache.get("exact_nodes_saved_estimate") != expected_nodes:
        print(f"unexpected node savings report: {cache}", file=sys.stderr)
        return False
    return True


def check_failure_modes(args: argparse.Namespace, root: Path) -> bool:
    normalized, source_move_teacher, _ = fixture(root)
    cache_dir = root / "failure-cache"
    if not require_success(run(populate_command(args, normalized, cache_dir, source_move_teacher, root / "populate-fail.json"))):
        return False

    changed = root / "changed-board.tsv"
    write_tsv(
        changed,
        NORMALIZED_HEADER_V2,
        [
            normalized_row("changed-a", "root-a", "train", board(60, 3, 1)),
            normalized_row("changed-b", "root-b", "validation", board(58, 5, 1)),
        ],
    )
    if not require_failure(
        run(materialize_command(args, changed, cache_dir, root / "bad-move.tsv", root / "bad-child.tsv", root / "bad.json")),
        "cache board contents mismatch",
    ):
        return False

    missing = root / "missing.tsv"
    write_tsv(
        missing,
        NORMALIZED_HEADER_V2,
        [
            normalized_row("record-a", "root-a", "train", board(61, 2, 1)),
            normalized_row("record-c", "root-c", "train", board(59, 4, 1)),
        ],
    )
    if not require_failure(
        run(
            materialize_command(
                args, missing, cache_dir, root / "missing-move.tsv", root / "missing-child.tsv", root / "missing.json"
            )
        ),
        "cache full hit required",
    ):
        return False

    cache_files = sorted((cache_dir / "schema-v1" / "exact-final-disc-diff" / "max-empty-12" / "roots").glob("*/*.json"))
    if not cache_files:
        print("cache files were not written", file=sys.stderr)
        return False
    first = cache_files[0]
    data = load_json(first)
    data["solver_semantic_version"] = "bad-version"
    first.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return require_failure(
        run(materialize_command(args, normalized, cache_dir, root / "semantic-move.tsv", root / "semantic-child.tsv", root / "semantic.json")),
        "cache solver_semantic_version mismatch",
    )


def check_pass_through(args: argparse.Namespace, root: Path) -> bool:
    normalized, _, _ = fixture(root)
    matrix = run(
        [
            sys.executable,
            str(args.matrix_helper),
            "--normalized-tsv",
            str(normalized),
            "--output-dir",
            str(root / "matrix"),
            "--root-counts",
            "2",
            "--seeds",
            "0",
            "--move-teacher-cache-dir",
            str(root / "cache-pass-through"),
            "--reuse-move-teacher-cache",
            "--write-move-teacher-cache",
            "--dry-run",
        ]
    )
    if not require_success(matrix):
        return False
    if "--reuse-move-teacher-cache" not in matrix.stdout or "--write-move-teacher-cache" not in matrix.stdout:
        print(f"matrix dry-run did not pass cache args: {matrix.stdout}", file=sys.stderr)
        return False

    growth = run(
        [
            sys.executable,
            str(args.growth_cycle_helper),
            "--normalized-tsv",
            str(normalized),
            "--output-dir",
            str(root / "growth"),
            "--root-counts",
            "2",
            "--seeds",
            "0",
            "--move-teacher-cache-dir",
            str(root / "cache-pass-through"),
            "--reuse-move-teacher-cache",
            "--write-move-teacher-cache",
            "--dry-run",
        ]
    )
    if not require_success(growth):
        return False
    report = load_json(root / "growth" / "growth-cycle-report.json")
    command = report.get("stages", {}).get("decision_leverage_matrix", {}).get("command", [])
    if "--reuse-move-teacher-cache" not in command or "--write-move-teacher-cache" not in command:
        print(f"growth dry-run did not pass cache args: {command}", file=sys.stderr)
        return False
    return True


def main() -> int:
    args = parse_args()
    with tempfile.TemporaryDirectory() as temp_text:
        root = Path(temp_text)
        if not check_full_hit_and_remap(args, root):
            return 1
        if not check_failure_modes(args, root):
            return 1
        if not check_pass_through(args, root):
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
