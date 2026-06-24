#!/usr/bin/env python3
"""CTest smoke coverage for local exact move-teacher cache materialization."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

import normalized_v2_contract as normalized_contract


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
TEACHER_SOURCE = "exact-move-teacher-v2"
POSITIONS = [
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
    parser.add_argument("--materializer", required=True, type=Path)
    parser.add_argument("--generator", required=True, type=Path)
    parser.add_argument("--campaign-helper", required=True, type=Path)
    parser.add_argument("--matrix-helper", required=True, type=Path)
    parser.add_argument("--growth-cycle-helper", required=True, type=Path)
    return parser.parse_args()


def phase_for_occupied(occupied: int) -> int:
    return min(12, ((occupied - 4) * 13) // 60)


def board(player: int, opponent: int, empty: int) -> str:
    if player + opponent + empty != 64:
        raise AssertionError("bad board counts")
    return ("X" * player) + ("O" * opponent) + ("-" * empty)


def relative_board(serialized: str) -> str:
    board_text, side = serialized.split()
    player_disc = "B" if side == "b" else "W"
    opponent_disc = "W" if side == "b" else "B"
    values = ["-"] * 64
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
                values[index] = "X"
            elif value == opponent_disc:
                values[index] = "O"
            elif value == ".":
                values[index] = "-"
            else:
                raise AssertionError(serialized)
    return "".join(values)


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
        "teacher_source": TEACHER_SOURCE,
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


def cache_entry_path(cache_dir: Path, schema_namespace: str, board_id: str) -> Path:
    digest = hashlib.sha256(board_id.encode("utf-8")).hexdigest()
    return cache_dir / schema_namespace / "exact-final-disc-diff" / "max-empty-12" / "roots" / digest[:2] / f"{digest}.json"


def make_executable(path: Path, text: str) -> Path:
    path.write_text(text, encoding="utf-8")
    path.chmod(0o755)
    return path


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


def selection_options(max_empty: int, max_roots: int | None, seed: int) -> list[str]:
    options = ["--max-empty", str(max_empty), "--seed", str(seed)]
    if max_roots is not None:
        options.extend(["--max-roots", str(max_roots)])
    return options


def run_generator(
    generator: Path,
    normalized: Path,
    move_out: Path,
    child_out: Path,
    report: Path,
    max_empty: int,
    max_roots: int | None,
    seed: int,
) -> subprocess.CompletedProcess[str]:
    return run(
        [
            str(generator),
            "--normalized-tsv",
            str(normalized),
            "--move-teacher-out",
            str(move_out),
            "--child-normalized-out",
            str(child_out),
            "--report-out",
            str(report),
            *selection_options(max_empty, max_roots, seed),
        ]
    )


def materialize_command_with_selection(
    args: argparse.Namespace,
    normalized: Path,
    cache_dir: Path,
    move_out: Path,
    child_out: Path,
    report: Path,
    max_empty: int,
    max_roots: int | None,
    seed: int,
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
        *selection_options(max_empty, max_roots, seed),
        *extra,
    ]


def populate_command_with_selection(
    args: argparse.Namespace,
    normalized: Path,
    cache_dir: Path,
    source: Path,
    report: Path,
    max_empty: int,
    max_roots: int | None,
    seed: int,
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
        *selection_options(max_empty, max_roots, seed),
        "--source-move-teacher-tsv",
        str(source),
        "--populate-only",
    ]


def probe_command_with_selection(
    args: argparse.Namespace,
    normalized: Path,
    cache_dir: Path,
    report: Path,
    max_empty: int,
    max_roots: int | None,
    seed: int,
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
        *selection_options(max_empty, max_roots, seed),
        "--probe-only",
    ]


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


def partial_source(path: Path, roots: list[dict[str, str]], include: set[str]) -> Path:
    rows: list[dict[str, str]] = []
    for root in roots:
        if root["board_id"] not in include:
            continue
        if root["board_id"] == "root-a":
            rows.extend(
                [
                    move_row(root, "a1", board(62, 2, 0), 8, 1, True, 0, 11),
                    move_row(root, "b1", board(61, 3, 0), 2, 2, False, 6, 17),
                ]
            )
        elif root["board_id"] == "root-b":
            rows.append(move_row(root, "pass", board(57, 7, 0), -4, 1, True, 0, 23))
    write_tsv(path, MOVE_TEACHER_HEADER, rows)
    return path


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


def direct_fixture_rows(include_duplicate: bool) -> list[dict[str, str]]:
    rows = [
        normalized_row(index, f"direct-{name}", split, relative_board(serialized), f"direct-game-{index}")
        for index, (name, split, serialized) in enumerate(POSITIONS[:3], start=1)
    ]
    rows = [rows[2], rows[0], rows[1]]
    if include_duplicate:
        duplicate = dict(rows[1])
        duplicate["record_id"] = "direct-record-duplicate"
        duplicate["position_id"] = "direct-position-duplicate"
        duplicate["source_occurrence_id"] = "direct-source-duplicate"
        rows.append(duplicate)
    return rows


def distinct_root_order(move_rows: list[dict[str, str]]) -> list[str]:
    order: list[str] = []
    seen: set[str] = set()
    for row in move_rows:
        board_id = row["root_board_id"]
        if board_id not in seen:
            seen.add(board_id)
            order.append(board_id)
    return order


def assert_digest_parity(
    direct_report: dict[str, Any],
    cache_report: dict[str, Any],
    expected_stats: dict[str, Any],
) -> bool:
    cache = cache_report.get("cache", {})
    for key in ("ordered_root_digest", "unordered_root_digest", "board_contents_digest"):
        expected = expected_stats[key]
        if direct_report.get(key) != expected:
            print(f"direct report {key} mismatch: {direct_report.get(key)!r} != {expected!r}", file=sys.stderr)
            return False
        if cache_report.get(key) != expected:
            print(f"cache report {key} mismatch: {cache_report.get(key)!r} != {expected!r}", file=sys.stderr)
            return False
        if cache.get(key) != expected:
            print(f"cache section {key} mismatch: {cache.get(key)!r} != {expected!r}", file=sys.stderr)
            return False
    return True


def check_direct_cache_parity_case(
    args: argparse.Namespace,
    root: Path,
    name: str,
    rows: list[dict[str, str]],
    max_roots: int | None,
    seed: int,
) -> bool:
    normalized = root / f"{name}-normalized.tsv"
    write_tsv(normalized, NORMALIZED_HEADER_V2, rows)
    expected_roots, expected_stats = normalized_contract.select_roots(normalized, 2, max_roots, seed)

    direct_move = root / f"{name}-direct-move.tsv"
    direct_child = root / f"{name}-direct-child.tsv"
    direct_report_path = root / f"{name}-direct-report.json"
    if not require_success(
        run_generator(args.generator, normalized, direct_move, direct_child, direct_report_path, 2, max_roots, seed)
    ):
        return False
    direct_report = load_json(direct_report_path)
    direct_move_rows = load_tsv(direct_move)
    direct_order = distinct_root_order(direct_move_rows)
    if direct_order != sorted(direct_order):
        print(f"direct root order is not board_id order: {direct_order}", file=sys.stderr)
        return False
    if direct_report.get("selected_roots") != len(expected_roots):
        print(f"direct selected root count mismatch: {direct_report}", file=sys.stderr)
        return False
    if direct_report.get("duplicate_board_rows") != expected_stats["duplicate_board_rows"]:
        print(f"direct duplicate count mismatch: {direct_report}", file=sys.stderr)
        return False

    cache_dir = root / f"{name}-cache"
    if not require_success(
        run(populate_command_with_selection(args, normalized, cache_dir, direct_move, root / f"{name}-populate.json", 2, max_roots, seed))
    ):
        return False
    cache_move = root / f"{name}-cache-move.tsv"
    cache_child = root / f"{name}-cache-child.tsv"
    cache_report_path = root / f"{name}-cache-report.json"
    if not require_success(
        run(
            materialize_command_with_selection(
                args,
                normalized,
                cache_dir,
                cache_move,
                cache_child,
                cache_report_path,
                2,
                max_roots,
                seed,
            )
        )
    ):
        return False
    cache_report = load_json(cache_report_path)
    if direct_move.read_bytes() != cache_move.read_bytes():
        print(f"{name}: direct and cache move-teacher TSV differ", file=sys.stderr)
        return False
    if direct_child.read_bytes() != cache_child.read_bytes():
        print(f"{name}: direct and cache child-normalized TSV differ", file=sys.stderr)
        return False
    cache_order = distinct_root_order(load_tsv(cache_move))
    if cache_order != direct_order:
        print(f"{name}: direct/cache root order mismatch: {direct_order} != {cache_order}", file=sys.stderr)
        return False
    if cache_report.get("cache", {}).get("root_hits") != len(expected_roots):
        print(f"{name}: cache materialization was not full-hit: {cache_report}", file=sys.stderr)
        return False
    return assert_digest_parity(direct_report, cache_report, expected_stats)


def check_direct_cache_contract_parity(args: argparse.Namespace, root: Path) -> bool:
    if not require_success(run([str(args.generator), "--self-test-contract"])):
        return False

    unique_rows = direct_fixture_rows(include_duplicate=False)
    duplicate_rows = direct_fixture_rows(include_duplicate=True)
    cases = [
        ("unique-uncapped", unique_rows, None, 0),
        ("duplicate-uncapped", duplicate_rows, None, 0),
        ("duplicate-capped-seed0", duplicate_rows, 2, 0),
        ("duplicate-capped-seed1", duplicate_rows, 2, 1),
    ]
    selected_orders: list[list[str]] = []
    for name, rows, max_roots, seed in cases:
        if not check_direct_cache_parity_case(args, root, name, rows, max_roots, seed):
            return False
        normalized = root / f"{name}-normalized.tsv"
        selected, _ = normalized_contract.select_roots(normalized, 2, max_roots, seed)
        selected_orders.append([row["board_id"] for row in selected])
    if selected_orders[2] == selected_orders[3]:
        print(f"capped selections did not vary across seeds: {selected_orders[2]}", file=sys.stderr)
        return False

    conflict_rows = direct_fixture_rows(include_duplicate=False)
    conflict = dict(conflict_rows[1])
    conflict["record_id"] = "direct-conflict-record"
    conflict["position_id"] = "direct-conflict-position"
    conflict["source_occurrence_id"] = "direct-conflict-source"
    conflict["board_a1_to_h8"] = conflict_rows[2]["board_a1_to_h8"]
    conflict["occupied_count"] = conflict_rows[2]["occupied_count"]
    conflict["phase"] = conflict_rows[2]["phase"]
    conflict["player_disc_count"] = conflict_rows[2]["player_disc_count"]
    conflict["opponent_disc_count"] = conflict_rows[2]["opponent_disc_count"]
    conflict["empty_count"] = conflict_rows[2]["empty_count"]
    conflict_rows.append(conflict)
    conflict_normalized = root / "conflicting-duplicate.tsv"
    write_tsv(conflict_normalized, NORMALIZED_HEADER_V2, conflict_rows)
    if not require_failure(
        run_generator(
            args.generator,
            conflict_normalized,
            root / "conflict-direct-move.tsv",
            root / "conflict-direct-child.tsv",
            root / "conflict-direct-report.json",
            2,
            None,
            0,
        ),
        "same board_id has different board contents",
    ):
        return False
    if not require_failure(
        run(probe_command_with_selection(args, conflict_normalized, root / "conflict-cache", root / "conflict-probe.json", 2, None, 0)),
        "same board_id has different board contents",
    ):
        return False

    for name, field, value in (
        ("occupied-leading-space", "occupied_count", " 63"),
        ("phase-plus-sign", "phase", "+12"),
        ("empty-underscore", "empty_count", "1_0"),
    ):
        malformed_rows = [dict(direct_fixture_rows(include_duplicate=False)[0])]
        malformed_rows[0][field] = value
        malformed = root / f"{name}.tsv"
        write_tsv(malformed, NORMALIZED_HEADER_V2, malformed_rows)
        if not require_failure(
            run_generator(
                args.generator,
                malformed,
                root / f"{name}-direct-move.tsv",
                root / f"{name}-direct-child.tsv",
                root / f"{name}-direct-report.json",
                2,
                None,
                0,
            ),
            "numeric fields must be integers",
        ):
            return False
        if not require_failure(
            run(probe_command_with_selection(args, malformed, root / f"{name}-cache", root / f"{name}-probe.json", 2, None, 0)),
            "must be an integer",
        ):
            return False
    return True


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
    if not require_success(run(materialize_command(args, remapped, cache_dir, move_out, child_out, report_out))):
        return False
    move_rows = load_tsv(move_out)
    child_rows = load_tsv(child_out)
    if [row["root_board_id"] for row in move_rows] != ["root-a", "root-a", "root-b"]:
        print(f"materialized row order did not follow selected board_id order: {move_rows}", file=sys.stderr)
        return False
    root_b_row = [row for row in move_rows if row["root_board_id"] == "root-b"][0]
    if root_b_row["root_record_id"] != "remap-b" or root_b_row["root_split"] != "test":
        print(f"current root metadata was not used: {root_b_row}", file=sys.stderr)
        return False
    child_b_row = [row for row in child_rows if row["board_id"] == "move-teacher-v1:root-b:pass"][0]
    if child_b_row["split"] != "test" or child_b_row["game_group_id"] != "connected-b":
        print(f"child normalized row did not use remapped split/game: {child_b_row}", file=sys.stderr)
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

    cache_files = sorted((cache_dir / "schema-v2" / "exact-final-disc-diff" / "max-empty-12" / "roots").glob("*/*.json"))
    if not cache_files:
        print("cache files were not written", file=sys.stderr)
        return False
    first = cache_files[0]
    data = load_json(first)
    data["solver_semantic_version"] = "bad-version"
    first.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if not require_failure(
        run(materialize_command(args, normalized, cache_dir, root / "semantic-move.tsv", root / "semantic-child.tsv", root / "semantic.json")),
        "cache solver_semantic_version mismatch",
    ):
        return False

    conflict_cache = root / "conflict-cache"
    if not require_success(
        run(populate_command(args, normalized, conflict_cache, source_move_teacher, root / "populate-conflict.json"))
    ):
        return False
    changed_source = root / "changed-source.tsv"
    changed_rows = load_tsv(source_move_teacher)
    changed_rows[0]["teacher_nodes"] = "999"
    write_tsv(changed_source, MOVE_TEACHER_HEADER, changed_rows)
    if not require_failure(
        run(populate_command(args, normalized, conflict_cache, changed_source, root / "populate-conflict-fail.json")),
        "existing cache entry differs",
    ):
        return False

    wrong_source = root / "wrong-source.tsv"
    wrong_rows = load_tsv(source_move_teacher)
    wrong_rows[0]["root_board_id"] = "not-selected"
    write_tsv(wrong_source, MOVE_TEACHER_HEADER, wrong_rows)
    before_files = sorted(conflict_cache.glob("schema-v2/exact-final-disc-diff/max-empty-12/roots/*/*.json"))
    if not require_failure(
        run(populate_command(args, normalized, conflict_cache, wrong_source, root / "populate-wrong-fail.json")),
        "source move-teacher row is not in selected normalized roots",
    ):
        return False
    after_files = sorted(conflict_cache.glob("schema-v2/exact-final-disc-diff/max-empty-12/roots/*/*.json"))
    if before_files != after_files:
        print("failed cache populate changed valid cache entries", file=sys.stderr)
        return False
    return True


def check_v1_v2_namespace_safety(args: argparse.Namespace, root: Path) -> bool:
    normalized, source_move_teacher, _ = fixture(root)
    cache_dir = root / "namespace-cache"
    v1_path = cache_entry_path(cache_dir, "schema-v1", "root-a")
    v1_path.parent.mkdir(parents=True, exist_ok=True)
    v1_payload = {
        "schema_version": 1,
        "cache_semantic_version": "exact-move-teacher-cache-v1",
        "generator_semantic_version": "exact-move-teacher-v1",
        "sentinel": "must-remain-untouched",
    }
    v1_text = json.dumps(v1_payload, indent=2, sort_keys=True) + "\n"
    v1_path.write_text(v1_text, encoding="utf-8")

    if not require_failure(
        run(materialize_command(args, normalized, cache_dir, root / "v1-only-move.tsv", root / "v1-only-child.tsv", root / "v1-only.json")),
        "cache full hit required",
    ):
        return False
    if v1_path.read_text(encoding="utf-8") != v1_text:
        print("v1 cache entry changed during v2 materialize miss", file=sys.stderr)
        return False

    if not require_success(run(populate_command(args, normalized, cache_dir, source_move_teacher, root / "populate-v2.json"))):
        return False
    if v1_path.read_text(encoding="utf-8") != v1_text:
        print("v1 cache entry changed during v2 populate", file=sys.stderr)
        return False
    v2_files = sorted((cache_dir / "schema-v2" / "exact-final-disc-diff" / "max-empty-12" / "roots").glob("*/*.json"))
    if len(v2_files) != 2:
        print(f"v2 cache entries were not written in schema-v2 namespace: {v2_files}", file=sys.stderr)
        return False

    report_out = root / "namespace-materialized.json"
    if not require_success(
        run(
            materialize_command(args, normalized, cache_dir, root / "namespace-move.tsv", root / "namespace-child.tsv", report_out)
        )
    ):
        return False
    cache = load_json(report_out).get("cache", {})
    if (
        cache.get("cache_schema_version") != 2
        or cache.get("cache_semantic_version") != "exact-move-teacher-cache-v2"
    ):
        print(f"materialized report did not identify v2 cache namespace: {cache}", file=sys.stderr)
        return False
    if not (cache_dir / "schema-v2" / "exact-final-disc-diff" / "max-empty-12").exists():
        print("schema-v2 cache namespace was not created", file=sys.stderr)
        return False
    return True


def fake_campaign_tools(root: Path) -> dict[str, Path]:
    generator = make_executable(
        root / "fake-generator.py",
        """#!/usr/bin/env python3
import argparse, csv, json
from pathlib import Path

NORMALIZED_HEADER = %(normalized_header)r
MOVE_HEADER = %(move_header)r
TEACHER_SOURCE = %(teacher_source)r

def phase(occupied):
    return min(12, ((occupied - 4) * 13) // 60)

def counts(board):
    return {
        "occupied_count": board.count("X") + board.count("O"),
        "player_disc_count": board.count("X"),
        "opponent_disc_count": board.count("O"),
        "empty_count": board.count("-"),
    }

parser = argparse.ArgumentParser()
parser.add_argument("--normalized-tsv", required=True)
parser.add_argument("--move-teacher-out", required=True)
parser.add_argument("--child-normalized-out", required=True)
parser.add_argument("--report-out", required=True)
parser.add_argument("--max-empty")
parser.add_argument("--max-roots")
parser.add_argument("--seed")
parser.add_argument("--progress-every")
args = parser.parse_args()

with open(args.normalized_tsv, newline="", encoding="utf-8") as handle:
    roots = list(csv.DictReader(handle, delimiter="\\t"))

move_rows = []
child_rows = []
for index, root in enumerate(roots):
    child_board = root["board_a1_to_h8"].replace("-", "X", 1)
    child_counts = counts(child_board)
    child_id = f"fake-child:{root['board_id']}:a1"
    move_rows.append({
        "root_board_id": root["board_id"],
        "root_record_id": root["record_id"],
        "root_split": root["split"],
        "root_phase": root["phase"],
        "root_empty_count": root["empty_count"],
        "move": "a1",
        "child_board_id": child_id,
        "child_board_a1_to_h8": child_board,
        "child_empty_count": str(child_counts["empty_count"]),
        "child_phase": str(phase(child_counts["occupied_count"])),
        "root_move_score_side_to_move": "5",
        "child_label_score_side_to_move": "-5",
        "is_best_move": "1",
        "best_move_tie_count": "1",
        "move_rank": "1",
        "best_score_margin": "0",
        "teacher_source": TEACHER_SOURCE,
        "teacher_depth": str(child_counts["empty_count"]),
        "teacher_nodes": "31",
    })
    child_rows.append({
        "record_id": child_id,
        "position_id": child_id,
        "game_group_id": root["game_group_id"],
        "board_id": child_id,
        "source_occurrence_id": child_id + ":source",
        "source_dataset_id": TEACHER_SOURCE,
        "split": root["split"],
        "board_a1_to_h8": child_board,
        "label_kind": "teacher_exact_move_child_final_disc_diff",
        "label_unit": "disc",
        "label_perspective": "side_to_move",
        "label_score_side_to_move": "-5",
        "occupied_count": str(child_counts["occupied_count"]),
        "phase": str(phase(child_counts["occupied_count"])),
        "player_disc_count": str(child_counts["player_disc_count"]),
        "opponent_disc_count": str(child_counts["opponent_disc_count"]),
        "empty_count": str(child_counts["empty_count"]),
    })

for path, header, rows in [
    (args.move_teacher_out, MOVE_HEADER, move_rows),
    (args.child_normalized_out, NORMALIZED_HEADER, child_rows),
]:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=header, delimiter="\\t", lineterminator="\\n")
        writer.writeheader()
        writer.writerows(rows)

Path(args.report_out).write_text(json.dumps({
    "schema_version": 1,
    "selected_roots": len(roots),
    "move_rows": len(move_rows),
    "child_normalized_rows": len(child_rows),
    "teacher_nodes_sum": sum(int(row["teacher_nodes"]) for row in move_rows),
    "wall_time_sec": 0.0,
}, indent=2, sort_keys=True) + "\\n", encoding="utf-8")
Path(args.report_out).with_suffix(".input.json").write_text(json.dumps({
    "normalized_tsv": args.normalized_tsv,
    "root_board_ids": [row["board_id"] for row in roots],
}, indent=2, sort_keys=True) + "\\n", encoding="utf-8")
""" % {"normalized_header": NORMALIZED_HEADER_V2, "move_header": MOVE_TEACHER_HEADER, "teacher_source": TEACHER_SOURCE},
    )
    dataset = make_executable(
        root / "fake-dataset.py",
        """#!/usr/bin/env python3
import argparse, json
parser = argparse.ArgumentParser()
parser.add_argument("--normalized-tsv")
parser.add_argument("--report")
parser.add_argument("--output-format")
parser.add_argument("--pattern-set")
args = parser.parse_args()
print("record_id\\tsplit\\tphase\\tlabel\\tfeatures")
print("example\\ttrain\\t12\\t0\\t0:0:0")
open(args.report, "w", encoding="utf-8").write(json.dumps({"accepted_rows": 1}, sort_keys=True) + "\\n")
""",
    )
    trainer = make_executable(
        root / "fake-trainer.py",
        """#!/usr/bin/env python3
import argparse, json
parser = argparse.ArgumentParser()
parser.add_argument("--dataset")
parser.add_argument("--mode")
parser.add_argument("--epochs")
parser.add_argument("--learning-rate")
parser.add_argument("--lr-schedule")
parser.add_argument("--weight-decay")
parser.add_argument("--weights-out")
parser.add_argument("--report-out")
parser.add_argument("--seed")
args, _ = parser.parse_known_args()
open(args.weights_out, "w", encoding="utf-8").write(json.dumps({"weights": []}, sort_keys=True) + "\\n")
open(args.report_out, "w", encoding="utf-8").write(json.dumps({"ok": True}, sort_keys=True) + "\\n")
""",
    )
    exporter = make_executable(
        root / "fake-exporter.py",
        """#!/usr/bin/env python3
import argparse, json
parser = argparse.ArgumentParser()
parser.add_argument("--weights-json")
parser.add_argument("--weights-out")
parser.add_argument("--manifest-out")
parser.add_argument("--pattern-set")
parser.add_argument("--catalog-dump-exe")
args = parser.parse_args()
open(args.weights_out, "wb").write(b"fake-weights")
open(args.manifest_out, "w", encoding="utf-8").write(json.dumps({"pattern_set": args.pattern_set}, sort_keys=True) + "\\n")
""",
    )
    catalog = make_executable(
        root / "fake-catalog-dump.py",
        """#!/usr/bin/env python3
print('{"schema_version": 1, "pattern_sets": []}')
""",
    )
    ranking = make_executable(
        root / "fake-ranking.py",
        """#!/usr/bin/env python3
import argparse, json
parser = argparse.ArgumentParser()
parser.add_argument("--move-teacher")
parser.add_argument("--weights")
parser.add_argument("--manifest")
parser.add_argument("--pattern-set")
parser.add_argument("--report-out")
parser.add_argument("--summary-out")
args = parser.parse_args()
report = {
    "root_count": 2,
    "legal_move_count": 3,
    "top1_accuracy": 1.0,
    "top1_tie_aware_accuracy": 1.0,
    "best_move_in_top2_rate": 1.0,
    "pairwise_accuracy": 1.0,
    "pairwise_count": 1,
    "mean_teacher_regret": 0.0,
    "median_teacher_regret": 0.0,
    "exact_best_predicted_score_rank_mean": 1.0,
    "exact_best_predicted_score_rank_median": 1.0,
    "predicted_best_exact_margin_mean": 0.0,
    "roots_with_all_moves_same_predicted_score": 0,
}
open(args.report_out, "w", encoding="utf-8").write(json.dumps(report, sort_keys=True) + "\\n")
open(args.summary_out, "w", encoding="utf-8").write("# fake ranking\\n")
""",
    )
    return {
        "generator": generator,
        "dataset": dataset,
        "trainer": trainer,
        "exporter": exporter,
        "catalog": catalog,
        "ranking": ranking,
    }


def campaign_command(
    args: argparse.Namespace,
    normalized: Path,
    output_dir: Path,
    cache_dir: Path,
    tools: dict[str, Path],
    *extra: str,
) -> list[str]:
    return [
        sys.executable,
        str(args.campaign_helper),
        "--normalized-tsv",
        str(normalized),
        "--output-dir",
        str(output_dir),
        "--max-empty",
        "12",
        "--max-roots",
        "10",
        "--seed",
        "0",
        "--pattern-set",
        "pattern-v2-endgame-lite",
        "--trainer-mode",
        "pattern-sgd-v0c",
        "--epochs",
        "1",
        "--move-teacher-cache-dir",
        str(cache_dir),
        "--move-teacher-cache-helper",
        str(args.materializer),
        "--reuse-move-teacher-cache",
        "--write-move-teacher-cache",
        "--generator",
        str(tools["generator"]),
        "--dataset-exe",
        str(tools["dataset"]),
        "--trainer",
        str(tools["trainer"]),
        "--exporter",
        str(tools["exporter"]),
        "--catalog-dump-exe",
        str(tools["catalog"]),
        "--ranking-evaluator",
        str(tools["ranking"]),
        *extra,
    ]


def check_partial_miss_campaign(args: argparse.Namespace, root: Path) -> bool:
    normalized, _, roots = fixture(root)
    cache_dir = root / "partial-cache"
    root_a_source = partial_source(root / "root-a-source.tsv", roots, {"root-a"})
    if not require_success(run(populate_command(args, normalized, cache_dir, root_a_source, root / "populate-root-a.json"))):
        return False
    tools = fake_campaign_tools(root)
    no_allow = run(campaign_command(args, normalized, root / "campaign-no-allow", cache_dir, tools))
    if not require_failure(no_allow, "hits=1 misses=1"):
        return False
    allowed = run(
        campaign_command(
            args,
            normalized,
            root / "campaign-allow",
            cache_dir,
            tools,
            "--allow-cache-miss-solve",
        )
    )
    if not require_success(allowed):
        return False
    output_dir = root / "campaign-allow"
    invocation = load_json(output_dir / "missing-move-teacher-report.input.json")
    if invocation.get("root_board_ids") != ["root-b"]:
        print(f"fake generator was not limited to missing roots: {invocation}", file=sys.stderr)
        return False
    move_rows = load_tsv(output_dir / "move-teacher.tsv")
    if [row["root_board_id"] for row in move_rows] != ["root-a", "root-a", "root-b"]:
        print(f"final materialization did not preserve selected board_id order: {move_rows}", file=sys.stderr)
        return False
    report = load_json(output_dir / "move-teacher-report.json")
    cache = report.get("cache", {})
    if cache.get("root_hits") != 2 or cache.get("root_misses") != 0:
        print(f"final cache report is not full hit: {cache}", file=sys.stderr)
        return False
    if cache.get("root_hits_initial") != 1 or cache.get("root_misses_initial") != 1:
        print(f"initial hit/miss counts missing: {cache}", file=sys.stderr)
        return False
    if cache.get("roots_newly_solved") != 1 or cache.get("exact_nodes_newly_solved") != 31:
        print(f"new solve counts missing: {cache}", file=sys.stderr)
        return False
    if cache.get("exact_nodes_saved_estimate") != 28:
        print(f"saved node estimate should only count initial hits: {cache}", file=sys.stderr)
        return False
    campaign = load_json(output_dir / "campaign-report.json")
    if campaign.get("partial_cache_miss_solved") is not True:
        print(f"campaign did not record partial solve: {campaign}", file=sys.stderr)
        return False
    return True


def check_pass_through(args: argparse.Namespace, root: Path) -> bool:
    normalized, _, _ = fixture(root)
    catalog = make_executable(
        root / "fake-pass-through-catalog.py",
        """#!/usr/bin/env python3
print('{"schema_version": 1, "pattern_sets": []}')
""",
    )
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
            "--allow-cache-miss-solve",
            "--catalog-dump-exe",
            str(catalog),
            "--dry-run",
        ]
    )
    if not require_success(matrix):
        return False
    if (
        "--reuse-move-teacher-cache" not in matrix.stdout
        or "--write-move-teacher-cache" not in matrix.stdout
        or "--allow-cache-miss-solve" not in matrix.stdout
    ):
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
            "--allow-cache-miss-solve",
            "--catalog-dump-exe",
            str(catalog),
            "--dry-run",
        ]
    )
    if not require_success(growth):
        return False
    report = load_json(root / "growth" / "growth-cycle-report.json")
    command = report.get("stages", {}).get("decision_leverage_matrix", {}).get("command", [])
    if (
        "--reuse-move-teacher-cache" not in command
        or "--write-move-teacher-cache" not in command
        or "--allow-cache-miss-solve" not in command
    ):
        print(f"growth dry-run did not pass cache args: {command}", file=sys.stderr)
        return False
    return True


def main() -> int:
    args = parse_args()
    with tempfile.TemporaryDirectory() as temp_text:
        root = Path(temp_text)
        if not check_direct_cache_contract_parity(args, root):
            return 1
        if not check_full_hit_and_remap(args, root):
            return 1
        if not check_failure_modes(args, root):
            return 1
        if not check_v1_v2_namespace_safety(args, root):
            return 1
        if not check_partial_miss_campaign(args, root):
            return 1
        if not check_pass_through(args, root):
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
