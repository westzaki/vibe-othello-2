#!/usr/bin/env python3
"""Materialize exact move-teacher labels from a local split-independent cache."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import sys
import time
from collections import defaultdict
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

CACHE_SCHEMA_VERSION = 1
CACHE_SEMANTIC_VERSION = "exact-move-teacher-cache-v1"
SOLVER_SEMANTIC_VERSION = "vibe-othello-exact-endgame-v1"
GENERATOR_SEMANTIC_VERSION = "exact-move-teacher-v1"
TEACHER_SOURCE = "exact-move-teacher-v1"
CHILD_LABEL_KIND = "teacher_exact_move_child_final_disc_diff"
LABEL_UNIT = "disc"
LABEL_PERSPECTIVE = "side_to_move"
FNV64_OFFSET = 14695981039346656037
FNV64_PRIME = 1099511628211


class CacheError(RuntimeError):
    pass


class CacheMissError(CacheError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--normalized-tsv", required=True, type=Path)
    parser.add_argument("--cache-dir", required=True, type=Path)
    parser.add_argument("--move-teacher-out", type=Path)
    parser.add_argument("--child-normalized-out", type=Path)
    parser.add_argument("--report-out", required=True, type=Path)
    parser.add_argument("--max-empty", type=int, default=12)
    parser.add_argument("--max-roots", type=int)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--source-move-teacher-tsv", type=Path)
    parser.add_argument("--populate-only", action="store_true")
    parser.add_argument("--require-full-hit", action="store_true", default=True)
    args = parser.parse_args()

    if args.max_empty < 0 or args.max_empty > 64:
        parser.error("--max-empty must be in [0, 64]")
    if args.max_roots is not None and args.max_roots <= 0:
        parser.error("--max-roots must be positive")
    if args.seed < 0:
        parser.error("--seed must be non-negative")
    if args.populate_only:
        if args.source_move_teacher_tsv is None:
            parser.error("--populate-only requires --source-move-teacher-tsv")
    else:
        if args.move_teacher_out is None or args.child_normalized_out is None:
            parser.error("--move-teacher-out and --child-normalized-out are required unless --populate-only")
    return args


def stable_json(data: Any) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def sha256_text(text: str) -> str:
    return f"sha256:{hashlib.sha256(text.encode('utf-8')).hexdigest()}"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            digest.update(chunk)
    return f"sha256:{digest.hexdigest()}"


def report_path(path: Path | None) -> str | None:
    if path is None:
        return None
    return path.name if path.is_absolute() else str(path)


def fnv1a64_update(value: int, text: str) -> int:
    for byte in text.encode("utf-8"):
        value ^= byte
        value = (value * FNV64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return value


def fnv_sample_key(board_id: str, seed: int) -> int:
    value = FNV64_OFFSET
    value = fnv1a64_update(value, str(seed))
    value = fnv1a64_update(value, "\t")
    value = fnv1a64_update(value, board_id)
    return value


def mix_output_checksum(value: int, line: str) -> int:
    value = fnv1a64_update(value, line)
    value ^= ord("\n")
    value = (value * FNV64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return value


def checksum_string(value: int) -> str:
    return f"0x{value:016x}"


def parse_int(text: str, field: str) -> int:
    try:
        return int(text)
    except ValueError as error:
        raise CacheError(f"{field} must be an integer: {text}") from error


def phase_for_occupied(occupied_count: int) -> int:
    return min(12, ((occupied_count - 4) * 13) // 60)


def board_counts(board: str) -> dict[str, int]:
    if len(board) != 64:
        raise CacheError("board_a1_to_h8 must be exactly 64 characters")
    player = board.count("X")
    opponent = board.count("O")
    empty = board.count("-")
    if player + opponent + empty != 64:
        raise CacheError("board_a1_to_h8 contains characters outside X/O/-")
    return {
        "occupied_count": player + opponent,
        "player_disc_count": player,
        "opponent_disc_count": opponent,
        "empty_count": empty,
    }


def validate_counts(row: dict[str, str], *, prefix: str = "") -> None:
    board_key = f"{prefix}board_a1_to_h8" if prefix else "board_a1_to_h8"
    counts = board_counts(row[board_key])
    for key in ("occupied_count", "player_disc_count", "opponent_disc_count", "empty_count"):
        row_key = f"{prefix}{key}" if prefix else key
        if row_key in row and row[row_key] != "":
            value = parse_int(row[row_key], row_key)
            if value != counts[key]:
                raise CacheError(f"{row_key} does not match {board_key}")


def load_tsv(path: Path, expected_header: list[str]) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if reader.fieldnames != expected_header:
            raise CacheError(f"{path} has unexpected TSV header: {reader.fieldnames}")
        return list(reader)


def write_tsv(path: Path, header: list[str], rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=header, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row[key] for key in header})


def select_roots(path: Path, max_empty: int, max_roots: int | None, seed: int) -> tuple[list[dict[str, str]], dict[str, Any]]:
    rows = load_tsv(path, NORMALIZED_HEADER_V2)
    seen_boards: set[str] = set()
    unique_eligible: dict[str, dict[str, str]] = {}
    duplicate_board_rows = 0
    skipped_too_many_empty = 0
    for row in rows:
        board_id = row["board_id"]
        if not board_id:
            raise CacheError("board_id must be non-empty")
        if board_id in seen_boards:
            duplicate_board_rows += 1
        seen_boards.add(board_id)
        if row["split"] not in ("train", "validation", "test"):
            raise CacheError(f"{board_id}: split must be train, validation, or test")
        if row["label_perspective"] != LABEL_PERSPECTIVE:
            raise CacheError(f"{board_id}: label_perspective must be {LABEL_PERSPECTIVE}")
        validate_counts(row)
        occupied = parse_int(row["occupied_count"], "occupied_count")
        phase = parse_int(row["phase"], "phase")
        empty = parse_int(row["empty_count"], "empty_count")
        if phase != phase_for_occupied(occupied):
            raise CacheError(f"{board_id}: phase does not match occupied_count")
        if empty > max_empty:
            skipped_too_many_empty += 1
            continue
        if board_id not in unique_eligible:
            selected = dict(row)
            selected["_sample_key"] = str(fnv_sample_key(board_id, seed))
            unique_eligible[board_id] = selected

    roots = list(unique_eligible.values())
    if max_roots is not None and len(roots) > max_roots:
        sampled = sorted(roots, key=lambda row: (int(row["_sample_key"]), row["board_id"]))[:max_roots]
        sampled_ids = {row["board_id"] for row in sampled}
        roots = [row for row in roots if row["board_id"] in sampled_ids]
    for row in roots:
        row.pop("_sample_key", None)

    identities = [f"{row['board_id']}\t{row['board_a1_to_h8']}\t{row['empty_count']}" for row in roots]
    sorted_identities = sorted(identities)
    stats = {
        "input_rows": len(rows),
        "eligible_rows": sum(1 for row in rows if parse_int(row["empty_count"], "empty_count") <= max_empty),
        "selected_roots": len(roots),
        "unique_roots_seen": len(seen_boards),
        "unique_roots_selected": len(roots),
        "duplicate_board_rows": duplicate_board_rows,
        "skipped_too_many_empty": skipped_too_many_empty,
        "ordered_root_digest": sha256_text("\n".join(identities) + "\n"),
        "unordered_root_digest": sha256_text("\n".join(sorted_identities) + "\n"),
        "board_contents_digest": sha256_text(
            "\n".join(f"{row['board_id']}\t{row['board_a1_to_h8']}" for row in sorted(roots, key=lambda item: item["board_id"]))
            + "\n"
        ),
    }
    return roots, stats


def cache_base(cache_dir: Path, max_empty: int) -> Path:
    return cache_dir / "schema-v1" / "exact-final-disc-diff" / f"max-empty-{max_empty}"


def root_cache_path(cache_dir: Path, max_empty: int, board_id: str) -> Path:
    digest = hashlib.sha256(board_id.encode("utf-8")).hexdigest()
    return cache_base(cache_dir, max_empty) / "roots" / digest[:2] / f"{digest}.json"


def child_counts_payload(board: str) -> dict[str, int]:
    counts = board_counts(board)
    return {
        "child_occupied_count": counts["occupied_count"],
        "child_player_disc_count": counts["player_disc_count"],
        "child_opponent_disc_count": counts["opponent_disc_count"],
        "child_empty_count": counts["empty_count"],
        "child_phase": phase_for_occupied(counts["occupied_count"]),
    }


def cache_entry_from_rows(root: dict[str, str], rows: list[dict[str, str]], max_empty: int) -> dict[str, Any]:
    if not rows:
        raise CacheError(f"{root['board_id']}: cannot cache empty move row set")
    cached_rows: list[dict[str, Any]] = []
    moves: list[str] = []
    for row in sorted(rows, key=lambda item: item["move"]):
        if row["root_board_id"] != root["board_id"]:
            raise CacheError(f"{root['board_id']}: root_board_id mismatch in move-teacher row")
        if row["teacher_source"] != TEACHER_SOURCE:
            raise CacheError(f"{root['board_id']}: teacher_source must be {TEACHER_SOURCE}")
        if row["root_empty_count"] != root["empty_count"]:
            raise CacheError(f"{root['board_id']}: root_empty_count does not match normalized TSV")
        counts = child_counts_payload(row["child_board_a1_to_h8"])
        child_empty = parse_int(row["child_empty_count"], "child_empty_count")
        child_phase = parse_int(row["child_phase"], "child_phase")
        teacher_depth = parse_int(row["teacher_depth"], "teacher_depth")
        if child_empty != counts["child_empty_count"] or child_phase != counts["child_phase"]:
            raise CacheError(f"{root['board_id']} {row['move']}: child counts do not match child board")
        if teacher_depth != child_empty:
            raise CacheError(f"{root['board_id']} {row['move']}: teacher_depth must equal child_empty_count")
        payload = {
            "move": row["move"],
            "child_board_id": row["child_board_id"],
            "child_board_a1_to_h8": row["child_board_a1_to_h8"],
            "child_empty_count": child_empty,
            "child_phase": child_phase,
            "child_occupied_count": counts["child_occupied_count"],
            "child_player_disc_count": counts["child_player_disc_count"],
            "child_opponent_disc_count": counts["child_opponent_disc_count"],
            "root_move_score_side_to_move": parse_int(
                row["root_move_score_side_to_move"], "root_move_score_side_to_move"
            ),
            "child_label_score_side_to_move": parse_int(
                row["child_label_score_side_to_move"], "child_label_score_side_to_move"
            ),
            "is_best_move": parse_int(row["is_best_move"], "is_best_move"),
            "best_move_tie_count": parse_int(row["best_move_tie_count"], "best_move_tie_count"),
            "move_rank": parse_int(row["move_rank"], "move_rank"),
            "best_score_margin": parse_int(row["best_score_margin"], "best_score_margin"),
            "teacher_depth": teacher_depth,
            "teacher_nodes": parse_int(row["teacher_nodes"], "teacher_nodes"),
        }
        if payload["is_best_move"] not in (0, 1):
            raise CacheError(f"{root['board_id']} {row['move']}: is_best_move must be 0 or 1")
        moves.append(row["move"])
        cached_rows.append(payload)

    return {
        "schema_version": CACHE_SCHEMA_VERSION,
        "cache_semantic_version": CACHE_SEMANTIC_VERSION,
        "generator_semantic_version": GENERATOR_SEMANTIC_VERSION,
        "solver_semantic_version": SOLVER_SEMANTIC_VERSION,
        "label_kind": CHILD_LABEL_KIND,
        "label_unit": LABEL_UNIT,
        "label_perspective": LABEL_PERSPECTIVE,
        "teacher_source": TEACHER_SOURCE,
        "max_empty": max_empty,
        "root": {
            "board_id": root["board_id"],
            "board_a1_to_h8": root["board_a1_to_h8"],
            "empty_count": parse_int(root["empty_count"], "empty_count"),
        },
        "terminal_root": False,
        "forced_pass": moves == ["pass"],
        "legal_moves": moves,
        "rows": cached_rows,
    }


def normalize_entry_for_compare(entry: dict[str, Any]) -> dict[str, Any]:
    return json.loads(stable_json(entry))


def validate_cache_entry(entry: dict[str, Any], root: dict[str, str], max_empty: int) -> None:
    expected = {
        "schema_version": CACHE_SCHEMA_VERSION,
        "cache_semantic_version": CACHE_SEMANTIC_VERSION,
        "generator_semantic_version": GENERATOR_SEMANTIC_VERSION,
        "solver_semantic_version": SOLVER_SEMANTIC_VERSION,
        "label_kind": CHILD_LABEL_KIND,
        "label_unit": LABEL_UNIT,
        "label_perspective": LABEL_PERSPECTIVE,
        "teacher_source": TEACHER_SOURCE,
    }
    for key, value in expected.items():
        if entry.get(key) != value:
            raise CacheError(f"{root['board_id']}: cache {key} mismatch: {entry.get(key)!r} != {value!r}")
    if entry.get("max_empty") != max_empty:
        raise CacheError(f"{root['board_id']}: cache max_empty mismatch")
    cached_root = entry.get("root")
    if not isinstance(cached_root, dict):
        raise CacheError(f"{root['board_id']}: cache root metadata missing")
    if cached_root.get("board_id") != root["board_id"]:
        raise CacheError(f"{root['board_id']}: cache board_id mismatch")
    if cached_root.get("board_a1_to_h8") != root["board_a1_to_h8"]:
        raise CacheError(f"{root['board_id']}: cache board contents mismatch")
    if cached_root.get("empty_count") != parse_int(root["empty_count"], "empty_count"):
        raise CacheError(f"{root['board_id']}: cache empty_count mismatch")
    if entry.get("terminal_root") is not False:
        raise CacheError(f"{root['board_id']}: terminal cache entries are not supported yet")
    rows = entry.get("rows")
    moves = entry.get("legal_moves")
    if not isinstance(rows, list) or not rows:
        raise CacheError(f"{root['board_id']}: cache rows must be a non-empty list")
    if not isinstance(moves, list) or moves != [row.get("move") for row in rows]:
        raise CacheError(f"{root['board_id']}: cache legal_moves do not match rows")


def write_cache_entry(cache_dir: Path, max_empty: int, entry: dict[str, Any]) -> None:
    board_id = entry["root"]["board_id"]
    path = root_cache_path(cache_dir, max_empty, board_id)
    if path.exists():
        existing = json.loads(path.read_text(encoding="utf-8"))
        if normalize_entry_for_compare(existing) != normalize_entry_for_compare(entry):
            raise CacheError(f"{board_id}: existing cache entry differs from source move-teacher rows")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(stable_json(entry), encoding="utf-8")


def populate_cache(
    roots: list[dict[str, str]],
    source_move_teacher: Path,
    cache_dir: Path,
    max_empty: int,
) -> dict[str, Any]:
    root_by_id = {row["board_id"]: row for row in roots}
    move_rows = load_tsv(source_move_teacher, MOVE_TEACHER_HEADER)
    grouped: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in move_rows:
        grouped[row["root_board_id"]].append(row)
    written = 0
    reused = 0
    teacher_nodes = 0
    for board_id, rows in sorted(grouped.items()):
        if board_id not in root_by_id:
            raise CacheError(f"{board_id}: source move-teacher row is not in selected normalized roots")
        entry = cache_entry_from_rows(root_by_id[board_id], rows, max_empty)
        before = root_cache_path(cache_dir, max_empty, board_id).exists()
        write_cache_entry(cache_dir, max_empty, entry)
        if before:
            reused += 1
        else:
            written += 1
        teacher_nodes += sum(row["teacher_nodes"] for row in entry["rows"])
    return {
        "source_move_teacher_tsv": report_path(source_move_teacher),
        "source_move_teacher_sha256": sha256_file(source_move_teacher),
        "root_entries_written": written,
        "root_entries_already_present": reused,
        "root_entries_total": len(grouped),
        "move_rows": len(move_rows),
        "teacher_nodes_cached": teacher_nodes,
    }


def move_teacher_materialized_row(root: dict[str, str], cached: dict[str, Any]) -> dict[str, str]:
    return {
        "root_board_id": root["board_id"],
        "root_record_id": root["record_id"],
        "root_split": root["split"],
        "root_phase": root["phase"],
        "root_empty_count": root["empty_count"],
        "move": str(cached["move"]),
        "child_board_id": str(cached["child_board_id"]),
        "child_board_a1_to_h8": str(cached["child_board_a1_to_h8"]),
        "child_empty_count": str(cached["child_empty_count"]),
        "child_phase": str(cached["child_phase"]),
        "root_move_score_side_to_move": str(cached["root_move_score_side_to_move"]),
        "child_label_score_side_to_move": str(cached["child_label_score_side_to_move"]),
        "is_best_move": str(cached["is_best_move"]),
        "best_move_tie_count": str(cached["best_move_tie_count"]),
        "move_rank": str(cached["move_rank"]),
        "best_score_margin": str(cached["best_score_margin"]),
        "teacher_source": TEACHER_SOURCE,
        "teacher_depth": str(cached["teacher_depth"]),
        "teacher_nodes": str(cached["teacher_nodes"]),
    }


def child_normalized_row(root: dict[str, str], cached: dict[str, Any]) -> dict[str, str]:
    child_board_id = str(cached["child_board_id"])
    return {
        "record_id": child_board_id,
        "position_id": child_board_id,
        "game_group_id": root["game_group_id"],
        "board_id": child_board_id,
        "source_occurrence_id": f"{child_board_id}:source",
        "source_dataset_id": TEACHER_SOURCE,
        "split": root["split"],
        "board_a1_to_h8": str(cached["child_board_a1_to_h8"]),
        "label_kind": CHILD_LABEL_KIND,
        "label_unit": LABEL_UNIT,
        "label_perspective": LABEL_PERSPECTIVE,
        "label_score_side_to_move": str(cached["child_label_score_side_to_move"]),
        "occupied_count": str(cached["child_occupied_count"]),
        "phase": str(cached["child_phase"]),
        "player_disc_count": str(cached["child_player_disc_count"]),
        "opponent_disc_count": str(cached["child_opponent_disc_count"]),
        "empty_count": str(cached["child_empty_count"]),
    }


def load_cache_entries(
    roots: list[dict[str, str]], cache_dir: Path, max_empty: int
) -> tuple[list[tuple[dict[str, str], dict[str, Any]]], list[str]]:
    hits: list[tuple[dict[str, str], dict[str, Any]]] = []
    misses: list[str] = []
    for root in roots:
        path = root_cache_path(cache_dir, max_empty, root["board_id"])
        if not path.exists():
            misses.append(root["board_id"])
            continue
        entry = json.loads(path.read_text(encoding="utf-8"))
        validate_cache_entry(entry, root, max_empty)
        hits.append((root, entry))
    return hits, misses


def materialize(
    roots: list[dict[str, str]],
    root_stats: dict[str, Any],
    args: argparse.Namespace,
    started: float,
) -> dict[str, Any]:
    hits, misses = load_cache_entries(roots, args.cache_dir, args.max_empty)
    if misses and args.require_full_hit:
        raise CacheMissError(
            f"move-teacher cache full hit required but {len(misses)} roots are missing; "
            f"first missing board_id: {misses[0]}"
        )

    move_rows: list[dict[str, str]] = []
    child_rows: list[dict[str, str]] = []
    roots_with_pass_move = 0
    roots_with_normal_moves = 0
    teacher_nodes_sum = 0
    root_scores: list[int] = []
    child_scores: list[int] = []
    checksum = FNV64_OFFSET
    for root, entry in hits:
        if entry.get("forced_pass") is True:
            roots_with_pass_move += 1
        else:
            roots_with_normal_moves += 1
        for cached in entry["rows"]:
            move_row = move_teacher_materialized_row(root, cached)
            child_row = child_normalized_row(root, cached)
            move_rows.append(move_row)
            child_rows.append(child_row)
            move_line = "\t".join(move_row[key] for key in MOVE_TEACHER_HEADER)
            child_line = "\t".join(child_row[key] for key in NORMALIZED_HEADER_V2)
            checksum = mix_output_checksum(checksum, move_line)
            checksum = mix_output_checksum(checksum, child_line)
            teacher_nodes = int(move_row["teacher_nodes"])
            teacher_nodes_sum += teacher_nodes
            root_scores.append(int(move_row["root_move_score_side_to_move"]))
            child_scores.append(int(move_row["child_label_score_side_to_move"]))

    write_tsv(args.move_teacher_out, MOVE_TEACHER_HEADER, move_rows)
    write_tsv(args.child_normalized_out, NORMALIZED_HEADER_V2, child_rows)
    wall_time = time.monotonic() - started
    return {
        "checksum": checksum_string(checksum),
        "child_label_score_sum": sum(child_scores),
        "child_label_score_max": max(child_scores) if child_scores else None,
        "child_label_score_min": min(child_scores) if child_scores else None,
        "child_normalized_out": report_path(args.child_normalized_out),
        "child_normalized_rows": len(child_rows),
        "duplicate_board_rows": root_stats["duplicate_board_rows"],
        "eligible_rows": root_stats["eligible_rows"],
        "input_rows": root_stats["input_rows"],
        "max_empty": args.max_empty,
        "max_roots": args.max_roots,
        "move_rows": len(move_rows),
        "move_teacher_out": report_path(args.move_teacher_out),
        "moves_per_sec": len(move_rows) / wall_time if wall_time > 0 else 0.0,
        "normalized_input_path": report_path(args.normalized_tsv),
        "notes": [
            "local-only exact move-teacher cache materialization",
            "cache entries are split-independent but materialized rows use the current normalized split and record ids",
            "not a strength claim",
            "not an Elo result",
            "not self-play",
            "not a production artifact",
            "generated cache, labels, and artifacts must not be committed",
        ],
        "roots_with_normal_moves": roots_with_normal_moves,
        "roots_with_pass_move": roots_with_pass_move,
        "root_move_score_sum": sum(root_scores),
        "root_move_score_max": max(root_scores) if root_scores else None,
        "root_move_score_min": min(root_scores) if root_scores else None,
        "schema_version": 1,
        "seed": args.seed,
        "selected_roots": len(roots),
        "skipped_too_many_empty": root_stats["skipped_too_many_empty"],
        "solve_failures": 0,
        "teacher_nodes_sum": teacher_nodes_sum,
        "terminal_roots_skipped": 0,
        "unique_roots_seen": root_stats["unique_roots_seen"],
        "unique_roots_selected": len(roots),
        "wall_time_sec": wall_time,
        "cache": {
            "cache_schema_version": CACHE_SCHEMA_VERSION,
            "cache_semantic_version": CACHE_SEMANTIC_VERSION,
            "cache_base": report_path(cache_base(args.cache_dir, args.max_empty)),
            "cache_mode": "cache-only-materialization",
            "root_count_requested": len(roots),
            "root_hits": len(hits),
            "root_misses": len(misses),
            "cache_hit_ratio": (len(hits) / len(roots)) if roots else 0.0,
            "rows_materialized": len(move_rows),
            "exact_nodes_reused": teacher_nodes_sum,
            "exact_nodes_saved_estimate": teacher_nodes_sum,
            "exact_nodes_newly_solved": 0,
            "missing_board_ids_sample": misses[:10],
            "normalized_input_sha256": sha256_file(args.normalized_tsv),
            "move_teacher_sha256": sha256_file(args.move_teacher_out),
            "child_normalized_sha256": sha256_file(args.child_normalized_out),
            "ordered_root_digest": root_stats["ordered_root_digest"],
            "unordered_root_digest": root_stats["unordered_root_digest"],
            "board_contents_digest": root_stats["board_contents_digest"],
            "generator_semantic_version": GENERATOR_SEMANTIC_VERSION,
            "solver_semantic_version": SOLVER_SEMANTIC_VERSION,
            "label_kind": CHILD_LABEL_KIND,
            "label_unit": LABEL_UNIT,
            "label_perspective": LABEL_PERSPECTIVE,
        },
    }


def populate_report(
    roots: list[dict[str, str]],
    root_stats: dict[str, Any],
    args: argparse.Namespace,
    populate: dict[str, Any],
    started: float,
) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "cache": {
            "cache_schema_version": CACHE_SCHEMA_VERSION,
            "cache_semantic_version": CACHE_SEMANTIC_VERSION,
            "cache_base": report_path(cache_base(args.cache_dir, args.max_empty)),
            "cache_mode": "populate-from-existing-move-teacher",
            "root_count_requested": len(roots),
            "root_entries_written": populate["root_entries_written"],
            "root_entries_already_present": populate["root_entries_already_present"],
            "root_entries_total": populate["root_entries_total"],
            "move_rows": populate["move_rows"],
            "teacher_nodes_cached": populate["teacher_nodes_cached"],
            "exact_nodes_newly_solved": 0,
            "normalized_input_sha256": sha256_file(args.normalized_tsv),
            "ordered_root_digest": root_stats["ordered_root_digest"],
            "unordered_root_digest": root_stats["unordered_root_digest"],
            "board_contents_digest": root_stats["board_contents_digest"],
            "source_move_teacher_tsv": populate["source_move_teacher_tsv"],
            "source_move_teacher_sha256": populate["source_move_teacher_sha256"],
            "generator_semantic_version": GENERATOR_SEMANTIC_VERSION,
            "solver_semantic_version": SOLVER_SEMANTIC_VERSION,
            "label_kind": CHILD_LABEL_KIND,
            "label_unit": LABEL_UNIT,
            "label_perspective": LABEL_PERSPECTIVE,
        },
        "max_empty": args.max_empty,
        "max_roots": args.max_roots,
        "seed": args.seed,
        "normalized_input_path": report_path(args.normalized_tsv),
        "wall_time_sec": time.monotonic() - started,
        "notes": [
            "local-only exact move-teacher cache population",
            "not a strength claim",
            "not an Elo result",
            "not self-play",
            "not a production artifact",
            "generated cache files must not be committed",
        ],
    }


def main() -> int:
    args = parse_args()
    started = time.monotonic()
    try:
        roots, root_stats = select_roots(args.normalized_tsv, args.max_empty, args.max_roots, args.seed)
        if not roots:
            raise CacheError("no eligible roots selected")
        args.report_out.parent.mkdir(parents=True, exist_ok=True)
        if args.source_move_teacher_tsv is not None:
            populate = populate_cache(roots, args.source_move_teacher_tsv, args.cache_dir, args.max_empty)
            if args.populate_only:
                args.report_out.write_text(
                    stable_json(populate_report(roots, root_stats, args, populate, started)),
                    encoding="utf-8",
                )
                print(f"cache_report={args.report_out}")
                return 0
        report = materialize(roots, root_stats, args, started)
        args.report_out.write_text(stable_json(report), encoding="utf-8")
        print(f"move_teacher={args.move_teacher_out}")
        print(f"child_normalized={args.child_normalized_out}")
        print(f"report={args.report_out}")
        print(f"cache_hits={report['cache']['root_hits']}")
        return 0
    except CacheMissError as error:
        print(error, file=sys.stderr)
        return 3
    except (CacheError, OSError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
