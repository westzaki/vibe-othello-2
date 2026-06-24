"""Shared normalized schema-v2 root selection contract for label tooling."""

from __future__ import annotations

import csv
import hashlib
from pathlib import Path
from typing import Any


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
LABEL_PERSPECTIVE = "side_to_move"
VALID_SPLITS = ("train", "validation", "test")
FNV64_OFFSET = 14695981039346656037
FNV64_PRIME = 1099511628211


class NormalizedV2ContractError(RuntimeError):
    pass


def sha256_text(text: str) -> str:
    return f"sha256:{hashlib.sha256(text.encode('utf-8')).hexdigest()}"


def parse_int(text: str, field: str) -> int:
    if not text:
        raise NormalizedV2ContractError(f"{field} must be an integer: {text}")
    digits = text[1:] if text[0] == "-" else text
    if not digits or any(character < "0" or character > "9" for character in digits):
        raise NormalizedV2ContractError(f"{field} must be an integer: {text}")
    return int(text)


def phase_for_occupied(occupied_count: int) -> int:
    return min(12, ((occupied_count - 4) * 13) // 60)


def board_counts(board: str) -> dict[str, int]:
    if len(board) != 64:
        raise NormalizedV2ContractError("board_a1_to_h8 must be exactly 64 characters")
    player = board.count("X")
    opponent = board.count("O")
    empty = board.count("-")
    if player + opponent + empty != 64:
        raise NormalizedV2ContractError("board_a1_to_h8 contains invalid character")
    return {
        "occupied_count": player + opponent,
        "player_disc_count": player,
        "opponent_disc_count": opponent,
        "empty_count": empty,
    }


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


def _validate_counts(row: dict[str, str]) -> None:
    counts = board_counts(row["board_a1_to_h8"])
    for key in ("occupied_count", "player_disc_count", "opponent_disc_count", "empty_count"):
        value = parse_int(row[key], key)
        if value != counts[key]:
            raise NormalizedV2ContractError("board counts do not match count columns")


def _validate_row(row: dict[str, str]) -> None:
    required = (
        "record_id",
        "position_id",
        "game_group_id",
        "board_id",
        "source_occurrence_id",
        "source_dataset_id",
    )
    if any(not row[field] for field in required):
        raise NormalizedV2ContractError(
            "record_id, position_id, game_group_id, board_id, source_occurrence_id, and "
            "source_dataset_id must be non-empty"
        )
    if row["split"] not in VALID_SPLITS:
        raise NormalizedV2ContractError("split must be train, validation, or test")
    if row["label_perspective"] != LABEL_PERSPECTIVE:
        raise NormalizedV2ContractError(f"label_perspective must be {LABEL_PERSPECTIVE}")

    label = parse_int(row["label_score_side_to_move"], "label_score_side_to_move")
    occupied = parse_int(row["occupied_count"], "occupied_count")
    phase = parse_int(row["phase"], "phase")
    parse_int(row["player_disc_count"], "player_disc_count")
    parse_int(row["opponent_disc_count"], "opponent_disc_count")
    empty_count = parse_int(row["empty_count"], "empty_count")
    if label < -64 or label > 64:
        raise NormalizedV2ContractError("label_score_side_to_move must be in [-64, 64]")
    if occupied < 4 or occupied > 64 or empty_count < 0 or empty_count > 60:
        raise NormalizedV2ContractError("occupied_count or empty_count is outside normalized schema v2 range")
    if phase < 0 or phase > 12 or phase != phase_for_occupied(occupied):
        raise NormalizedV2ContractError("phase must be in [0, 12] and match occupied_count")
    _validate_counts(row)


def load_normalized_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.reader(handle, delimiter="\t")
        header = next(reader, None)
        if header is None:
            raise NormalizedV2ContractError("normalized TSV is empty")
        if header == NORMALIZED_HEADER_V1:
            raise NormalizedV2ContractError("normalized TSV must use schema v2; schema v1 is not supported")
        if header != NORMALIZED_HEADER_V2:
            raise NormalizedV2ContractError("normalized TSV must use schema v2 header")

        rows: list[dict[str, str]] = []
        for line_number, fields in enumerate(reader, start=2):
            if not fields:
                continue
            if len(fields) != len(NORMALIZED_HEADER_V2):
                raise NormalizedV2ContractError(f"line {line_number}: expected 17 TSV fields for normalized schema v2")
            row = dict(zip(NORMALIZED_HEADER_V2, fields, strict=True))
            try:
                _validate_row(row)
            except NormalizedV2ContractError as error:
                raise NormalizedV2ContractError(f"line {line_number}: {error}") from error
            rows.append(row)
    return rows


def _root_identity(row: dict[str, str]) -> str:
    return f"{row['board_id']}\t{row['board_a1_to_h8']}\t{row['empty_count']}"


def _digest_stats(roots: list[dict[str, str]]) -> dict[str, str]:
    identities = [_root_identity(row) for row in roots]
    sorted_identities = sorted(identities)
    board_contents = [
        f"{row['board_id']}\t{row['board_a1_to_h8']}" for row in sorted(roots, key=lambda item: item["board_id"])
    ]
    return {
        "ordered_root_digest": sha256_text("\n".join(identities) + "\n"),
        "unordered_root_digest": sha256_text("\n".join(sorted_identities) + "\n"),
        "board_contents_digest": sha256_text("\n".join(board_contents) + "\n"),
    }


def select_roots(path: Path, max_empty: int, max_roots: int | None, seed: int) -> tuple[list[dict[str, str]], dict[str, Any]]:
    rows = load_normalized_rows(path)
    seen_boards: set[str] = set()
    board_contents_by_id: dict[str, str] = {}
    unique_eligible: dict[str, dict[str, str]] = {}
    duplicate_board_rows = 0
    skipped_too_many_empty = 0
    eligible_rows = 0
    for row in rows:
        board_id = row["board_id"]
        if board_id in seen_boards:
            duplicate_board_rows += 1
        seen_boards.add(board_id)
        previous_board = board_contents_by_id.get(board_id)
        if previous_board is not None and previous_board != row["board_a1_to_h8"]:
            raise NormalizedV2ContractError(f"{board_id}: same board_id has different board contents")
        board_contents_by_id.setdefault(board_id, row["board_a1_to_h8"])

        empty = parse_int(row["empty_count"], "empty_count")
        if empty > max_empty:
            skipped_too_many_empty += 1
            continue
        eligible_rows += 1
        if board_id not in unique_eligible:
            selected = dict(row)
            selected["_sample_key"] = str(fnv_sample_key(board_id, seed))
            unique_eligible[board_id] = selected

    roots = list(unique_eligible.values())
    if max_roots is not None and len(roots) > max_roots:
        roots = sorted(roots, key=lambda row: (int(row["_sample_key"]), row["board_id"]))[:max_roots]
    roots = sorted(roots, key=lambda row: row["board_id"])
    for row in roots:
        row.pop("_sample_key", None)

    stats: dict[str, Any] = {
        "input_rows": len(rows),
        "eligible_rows": eligible_rows,
        "selected_roots": len(roots),
        "unique_roots_seen": len(seen_boards),
        "unique_roots_selected": len(roots),
        "duplicate_board_rows": duplicate_board_rows,
        "skipped_too_many_empty": skipped_too_many_empty,
    }
    stats.update(_digest_stats(roots))
    return roots, stats
