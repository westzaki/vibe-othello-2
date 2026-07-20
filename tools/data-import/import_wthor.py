#!/usr/bin/env python3
"""Import WTHOR 8x8 game archives into local normalized training data.

The normalized TSV uses the engine's actual terminal disc difference and is
compatible with normalized schema v2.  WTHOR's theoretical score uses a
different 64-square scoring convention, so it is never emitted as an exact
disc-difference label.  ``--theoretical-wld-out`` safely exports only its exact
win/draw/loss signal at the WTHOR header's configured empty-count cutoff.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import struct
import sys
import zipfile
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, TextIO

from import_egaroucid_sequences import (
    HEADER,
    SPLIT_POLICIES,
    SPLIT_POLICY_VERSIONS,
    CandidateRow,
    ImportErrorWithLocation,
    ReplayHelper,
    ReplayResult,
    ReplaySnapshot,
    TopKRows,
    apply_split_policy,
    digest_for_parts,
    leakage_audits,
    load_manifest_dataset_id,
    phase_for_occupied_count,
    prefixed_id,
    split_for_digest,
)


DATASET_ID = "wthor-ffo-2026-02-24-local"
IMPORTER_VERSION = "wthor-wtb-v2"
IDENTITY_POLICY_VERSION = "wthor-game-identity-v1"
SOURCE_KIND = "wthor-wtb-local"
LABEL_KIND = "observed_final_disc_diff"
LABEL_UNIT = "final_disc_diff"
LABEL_PERSPECTIVE = "side_to_move"
WLD_HEADER = (
    "board_id",
    "board_a1_to_h8",
    "label_kind",
    "label_unit",
    "label_perspective",
    "label_score_side_to_move",
    "teacher_source",
    "teacher_empties",
    "source_dataset_id",
    "occurrence_count",
)
POLICY_HEADER = (
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
)
WTHOR_HEADER = struct.Struct("<4BIHH4B")
WTHOR_RECORD = struct.Struct("<HHHBB60B")


@dataclass(frozen=True)
class SourcePayload:
    source_ref: str
    data: bytes
    sha256: str


@dataclass(frozen=True)
class WthorHeader:
    created_year: int
    created_month: int
    created_day: int
    game_count: int
    year: int
    board_size: int
    game_type: int
    theoretical_empties: int | None


@dataclass(frozen=True)
class WthorRecord:
    tournament_id: int
    black_player_id: int
    white_player_id: int
    actual_black_score: int
    theoretical_black_score: int
    moves: tuple[int, ...]
    raw_sha256: str


@dataclass
class WldAggregate:
    board_id: str
    board_text: str
    score: int
    teacher_empties: int
    occurrence_count: int = 1


@dataclass(frozen=True)
class PolicyObservation:
    move: str
    outcome: int


@dataclass
class PolicyAggregate:
    root_board_id: str
    board_text: str
    split: str
    phase: int
    empty_count: int
    move: str
    occurrence_count: int = 0
    win_count: int = 0
    draw_count: int = 0
    loss_count: int = 0

    def add(self, outcome: int) -> None:
        self.occurrence_count += 1
        if outcome > 0:
            self.win_count += 1
        elif outcome < 0:
            self.loss_count += 1
        else:
            self.draw_count += 1


@dataclass
class Summary:
    source_files: list[dict[str, object]] = field(default_factory=list)
    input_games: int = 0
    accepted_games: int = 0
    rejected_games: int = 0
    invalid_move_count: int = 0
    pass_count: int = 0
    terminal_count: int = 0
    incomplete_games: int = 0
    normalized_label_eligible_games: int = 0
    candidate_positions: int = 0
    emitted_positions: int = 0
    game_group_occurrences: Counter[str] = field(default_factory=Counter)
    split_counts: Counter[str] = field(default_factory=Counter)
    phase_counts: Counter[str] = field(default_factory=Counter)
    year_counts: Counter[str] = field(default_factory=Counter)
    theoretical_empties_counts: Counter[str] = field(default_factory=Counter)
    theoretical_cutoff_games: int = 0
    theoretical_cutoff_missing_games: int = 0
    wthor_actual_score_matches_engine_black_discs: int = 0
    wthor_actual_score_mismatches_engine_black_discs: int = 0
    label_min: int | None = None
    label_max: int | None = None
    label_sum: int = 0
    policy_candidate_positions: int = 0
    policy_forced_pass_positions: int = 0
    policy_emitted_rows: int = 0
    policy_emitted_occurrences: int = 0
    rejected_reasons: list[str] = field(default_factory=list)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", action="append", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument(
        "--replay-helper",
        required=True,
        type=Path,
        help="Path to vibe-othello-wthor-sequence-replay.",
    )
    parser.add_argument("--dataset-id", default=DATASET_ID)
    parser.add_argument("--output", type=Path, help="Normalized TSV output; defaults to stdout.")
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument(
        "--theoretical-wld-out",
        type=Path,
        help="Optional exact WLD sidecar at each WTB file's theoretical cutoff.",
    )
    parser.add_argument(
        "--policy-out",
        type=Path,
        help=(
            "Optional aggregate WTHOR played-move policy sidecar for emitted "
            "non-terminal positions."
        ),
    )
    parser.add_argument("--min-ply", type=int, default=8)
    parser.add_argument("--max-ply", type=int)
    parser.add_argument("--ply-stride", type=int, default=4)
    parser.add_argument(
        "--selected-ply",
        action="append",
        type=int,
        default=[],
        metavar="PLY",
        help=(
            "Emit this exact normal-move ply; repeat for multiple cutoffs. "
            "When present, replaces --min-ply/--max-ply/--ply-stride selection."
        ),
    )
    parser.add_argument("--max-games", type=int)
    parser.add_argument("--max-positions", type=int)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--emit-terminal", action="store_true")
    parser.add_argument("--progress-every-games", type=int)
    parser.add_argument(
        "--split-policy",
        choices=SPLIT_POLICIES,
        default="connected-board-game",
    )
    parser.add_argument("--strict-board-disjoint-splits", action="store_true")
    args = parser.parse_args()
    if args.min_ply < 0:
        parser.error("--min-ply must be non-negative")
    if args.max_ply is not None and args.max_ply < args.min_ply:
        parser.error("--max-ply must be >= --min-ply")
    if args.ply_stride <= 0:
        parser.error("--ply-stride must be positive")
    if any(ply < 0 or ply > 60 for ply in args.selected_ply):
        parser.error("--selected-ply must be in [0, 60]")
    if len(set(args.selected_ply)) != len(args.selected_ply):
        parser.error("--selected-ply must not repeat a ply")
    args.selected_plies = frozenset(args.selected_ply)
    if args.max_games is not None and args.max_games <= 0:
        parser.error("--max-games must be positive")
    if args.max_positions is not None and args.max_positions <= 0:
        parser.error("--max-positions must be positive")
    if args.progress_every_games is not None and args.progress_every_games <= 0:
        parser.error("--progress-every-games must be positive")
    return args


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def discover_sources(inputs: Iterable[Path]) -> list[SourcePayload]:
    payloads: list[SourcePayload] = []
    for path in inputs:
        if not path.exists():
            raise ImportErrorWithLocation(f"{path}: input does not exist")
        if path.is_dir():
            children = sorted(
                child
                for child in path.rglob("*")
                if child.is_file() and child.suffix.lower() in {".wtb", ".zip"}
            )
            if not children:
                raise ImportErrorWithLocation(f"{path}: directory contains no .wtb or .zip files")
            for child in children:
                relative = str(child.relative_to(path))
                payloads.extend(payloads_from_file(child, relative))
        else:
            payloads.extend(payloads_from_file(path, path.name))
    if not payloads:
        raise ImportErrorWithLocation("no WTHOR .wtb payloads found")
    return sorted(payloads, key=lambda item: (item.source_ref, item.sha256))


def payloads_from_file(path: Path, display_name: str) -> list[SourcePayload]:
    if path.suffix.lower() == ".wtb":
        data = path.read_bytes()
        return [SourcePayload(display_name, data, sha256_bytes(data))]
    if path.suffix.lower() != ".zip":
        raise ImportErrorWithLocation(f"{path}: expected .wtb or .zip")
    try:
        with zipfile.ZipFile(path) as archive:
            entries = sorted(
                info
                for info in archive.infolist()
                if not info.is_dir() and info.filename.lower().endswith(".wtb")
            )
            if not entries:
                raise ImportErrorWithLocation(f"{path}: archive contains no .wtb files")
            result = []
            for info in entries:
                data = archive.read(info)
                result.append(
                    SourcePayload(
                        f"{display_name}!{info.filename}",
                        data,
                        sha256_bytes(data),
                    )
                )
            return result
    except zipfile.BadZipFile as error:
        raise ImportErrorWithLocation(f"{path}: invalid zip archive") from error


def parse_header(source: SourcePayload) -> WthorHeader:
    if len(source.data) < WTHOR_HEADER.size:
        raise ImportErrorWithLocation(f"{source.source_ref}: file is shorter than 16-byte header")
    (
        century,
        year_in_century,
        month,
        day,
        game_count,
        secondary_count,
        game_year,
        board_size,
        game_type,
        raw_depth,
        _reserved,
    ) = WTHOR_HEADER.unpack_from(source.data)
    expected_size = WTHOR_HEADER.size + game_count * WTHOR_RECORD.size
    if len(source.data) != expected_size:
        raise ImportErrorWithLocation(
            f"{source.source_ref}: size {len(source.data)} does not match "
            f"16 + {game_count} * 68 = {expected_size}"
        )
    if secondary_count != 0:
        raise ImportErrorWithLocation(
            f"{source.source_ref}: game file secondary record count must be zero"
        )
    if board_size not in {0, 8}:
        raise ImportErrorWithLocation(
            f"{source.source_ref}: unsupported WTHOR board size {board_size}"
        )
    if game_type != 0:
        raise ImportErrorWithLocation(
            f"{source.source_ref}: solitaire/unknown game type {game_type} is unsupported"
        )
    created_year = century * 100 + year_in_century
    theoretical_empties: int | None
    if raw_depth != 0:
        theoretical_empties = raw_depth
    elif created_year >= 2001:
        theoretical_empties = 22
    else:
        theoretical_empties = None
    if theoretical_empties is not None and not 0 <= theoretical_empties <= 60:
        raise ImportErrorWithLocation(
            f"{source.source_ref}: invalid theoretical empty count {theoretical_empties}"
        )
    return WthorHeader(
        created_year=created_year,
        created_month=month,
        created_day=day,
        game_count=game_count,
        year=game_year,
        board_size=8 if board_size == 0 else board_size,
        game_type=game_type,
        theoretical_empties=theoretical_empties,
    )


def parse_record(source: SourcePayload, index: int) -> WthorRecord:
    offset = WTHOR_HEADER.size + index * WTHOR_RECORD.size
    raw = source.data[offset : offset + WTHOR_RECORD.size]
    fields = WTHOR_RECORD.unpack(raw)
    if fields[3] > 64:
        raise ImportErrorWithLocation(f"invalid actual black score {fields[3]}")
    if fields[4] > 64:
        raise ImportErrorWithLocation(
            f"invalid theoretical black score {fields[4]}"
        )
    move_bytes = fields[5:]
    try:
        first_zero = move_bytes.index(0)
    except ValueError:
        first_zero = len(move_bytes)
    if any(value != 0 for value in move_bytes[first_zero:]):
        raise ImportErrorWithLocation("non-zero move follows WTHOR move-list terminator")
    moves = move_bytes[:first_zero]
    for move_index, value in enumerate(moves, start=1):
        column = value % 10
        row = value // 10
        if not 1 <= column <= 8 or not 1 <= row <= 8:
            raise ImportErrorWithLocation(
                f"move {move_index}: invalid WTHOR coordinate byte {value}"
            )
    return WthorRecord(
        tournament_id=fields[0],
        black_player_id=fields[1],
        white_player_id=fields[2],
        actual_black_score=fields[3],
        theoretical_black_score=fields[4],
        moves=tuple(moves),
        raw_sha256=sha256_bytes(raw),
    )


def transcript_for(record: WthorRecord) -> str:
    return " ".join(
        f"{chr(ord('a') + value % 10 - 1)}{value // 10}" for value in record.moves
    )


def side_to_move_is_black(canonical_moves: str, normal_ply: int) -> bool:
    black_to_move = True
    normal_moves = 0
    for token in canonical_moves.split():
        black_to_move = not black_to_move
        if token != "pass":
            normal_moves += 1
            if normal_moves == normal_ply:
                return black_to_move
    raise ImportErrorWithLocation(f"canonical replay has no normal ply {normal_ply}")


def next_played_move(canonical_moves: str, normal_ply: int) -> str | None:
    """Return the next decision move, excluding roots whose side must pass."""
    normal_moves = 0
    tokens = canonical_moves.split()
    for index, token in enumerate(tokens):
        if token == "pass":
            continue
        normal_moves += 1
        if normal_moves != normal_ply:
            continue
        if index + 1 >= len(tokens) or tokens[index + 1] == "pass":
            return None
        return tokens[index + 1]
    raise ImportErrorWithLocation(f"canonical replay has no normal ply {normal_ply}")


def board_id_for(board_text: str) -> str:
    return prefixed_id("board", digest_for_parts("board-v1", board_text))


def make_candidate_row(
    args: argparse.Namespace,
    source_occurrence_id: str,
    game_group_id: str,
    split: str,
    snapshot: ReplaySnapshot,
    row_occurrence: int,
) -> CandidateRow:
    board_text = snapshot.board_text
    board_id = board_id_for(board_text)
    position_digest = digest_for_parts(
        args.dataset_id, game_group_id, snapshot.ply, board_id
    )
    record_digest = digest_for_parts(
        args.dataset_id,
        source_occurrence_id,
        game_group_id,
        snapshot.ply,
        board_id,
        snapshot.label,
        row_occurrence,
    )
    position_id = (
        f"{args.dataset_id}-{game_group_id}-{snapshot.ply:03d}-{position_digest[:16]}"
    )
    record_id = f"{args.dataset_id}-{record_digest[:16]}-{row_occurrence:06d}"
    occupied_count = snapshot.player_disc_count + snapshot.opponent_disc_count
    phase = phase_for_occupied_count(occupied_count)
    fields = (
        record_id,
        position_id,
        game_group_id,
        board_id,
        source_occurrence_id,
        args.dataset_id,
        split,
        board_text,
        LABEL_KIND,
        LABEL_UNIT,
        LABEL_PERSPECTIVE,
        str(snapshot.label),
        str(occupied_count),
        str(phase),
        str(snapshot.player_disc_count),
        str(snapshot.opponent_disc_count),
        str(snapshot.empty_count),
    )
    line_text = "\t".join(fields)
    return CandidateRow(
        key=digest_for_parts(args.seed, "sample-v1", game_group_id, position_id),
        ordinal=0,
        fields=fields,
        line_text=line_text,
        position_id=position_id,
        game_group_id=game_group_id,
        board_id=board_id,
        split=split,
        phase=phase,
        label=snapshot.label,
    )


def should_emit(
    args: argparse.Namespace, replay: ReplayResult, snapshot: ReplaySnapshot
) -> bool:
    if args.selected_plies:
        if snapshot.ply not in args.selected_plies:
            return False
    else:
        if snapshot.ply < args.min_ply:
            return False
        if args.max_ply is not None and snapshot.ply > args.max_ply:
            return False
        if (snapshot.ply - args.min_ply) % args.ply_stride != 0:
            return False
    final_ply = replay.snapshots[-1].ply
    if snapshot.ply == final_ply and not args.emit_terminal:
        return False
    return True


def add_theoretical_wld(
    aggregates: dict[str, WldAggregate],
    header: WthorHeader,
    record: WthorRecord,
    replay: ReplayResult,
    summary: Summary,
) -> None:
    if header.theoretical_empties is None:
        summary.theoretical_cutoff_missing_games += 1
        return
    snapshot = next(
        (
            candidate
            for candidate in replay.snapshots
            if candidate.empty_count == header.theoretical_empties
        ),
        None,
    )
    if snapshot is None:
        summary.theoretical_cutoff_missing_games += 1
        return
    black_margin_64 = 2 * record.theoretical_black_score - 64
    black_to_move = side_to_move_is_black(replay.canonical_moves, snapshot.ply)
    side_margin_64 = black_margin_64 if black_to_move else -black_margin_64
    score = (side_margin_64 > 0) - (side_margin_64 < 0)
    board_id = board_id_for(snapshot.board_text)
    existing = aggregates.get(board_id)
    if existing is None:
        aggregates[board_id] = WldAggregate(
            board_id=board_id,
            board_text=snapshot.board_text,
            score=score,
            teacher_empties=header.theoretical_empties,
        )
    elif (
        existing.score != score
        or existing.board_text != snapshot.board_text
        or existing.teacher_empties != header.theoretical_empties
    ):
        raise ImportErrorWithLocation(
            f"conflicting WTHOR theoretical WLD labels for {board_id}"
        )
    else:
        existing.occurrence_count += 1
    summary.theoretical_cutoff_games += 1


def update_actual_score_audit(
    record: WthorRecord, replay: ReplayResult, summary: Summary
) -> None:
    final = replay.snapshots[-1]
    black_to_move = side_to_move_is_black(replay.canonical_moves, final.ply)
    black_discs = (
        final.player_disc_count if black_to_move else final.opponent_disc_count
    )
    if black_discs == record.actual_black_score:
        summary.wthor_actual_score_matches_engine_black_discs += 1
    else:
        summary.wthor_actual_score_mismatches_engine_black_discs += 1


def write_normalized(
    path: Path | None, rows: list[CandidateRow], summary: Summary
) -> str:
    handle: TextIO
    close_handle = False
    if path is None:
        handle = sys.stdout
    else:
        handle = path.open("w", encoding="utf-8", newline="")
        close_handle = True
    digest = hashlib.sha256()
    try:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(HEADER)
        for row in rows:
            writer.writerow(row.fields)
            digest.update(row.line_text.encode("utf-8"))
            digest.update(b"\n")
            summary.emitted_positions += 1
            summary.split_counts[row.split] += 1
            summary.phase_counts[str(row.phase)] += 1
            summary.label_min = (
                row.label if summary.label_min is None else min(summary.label_min, row.label)
            )
            summary.label_max = (
                row.label if summary.label_max is None else max(summary.label_max, row.label)
            )
            summary.label_sum += row.label
    finally:
        if close_handle:
            handle.close()
    return f"sha256:{digest.hexdigest()}"


def write_theoretical_wld(
    path: Path,
    aggregates: dict[str, WldAggregate],
    dataset_id: str,
) -> str:
    digest = hashlib.sha256()
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(WLD_HEADER)
        for item in sorted(aggregates.values(), key=lambda value: value.board_id):
            fields = (
                item.board_id,
                item.board_text,
                "wld",
                "wld",
                "side_to_move",
                str(item.score),
                "wthor-theoretical-score",
                str(item.teacher_empties),
                dataset_id,
                str(item.occurrence_count),
            )
            writer.writerow(fields)
            digest.update("\t".join(fields).encode("utf-8"))
            digest.update(b"\n")
    return f"sha256:{digest.hexdigest()}"


def write_policy(
    path: Path,
    rows: list[CandidateRow],
    observations: dict[str, PolicyObservation],
    dataset_id: str,
    summary: Summary,
) -> str:
    aggregates: dict[tuple[str, str], PolicyAggregate] = {}
    for row in rows:
        record_id = row.fields[0]
        observation = observations.get(record_id)
        if observation is None:
            continue
        key = (row.board_id, observation.move)
        existing = aggregates.get(key)
        board_text = row.fields[7]
        empty_count = int(row.fields[16])
        if existing is None:
            existing = PolicyAggregate(
                root_board_id=row.board_id,
                board_text=board_text,
                split=row.split,
                phase=row.phase,
                empty_count=empty_count,
                move=observation.move,
            )
            aggregates[key] = existing
        elif (
            existing.board_text != board_text
            or existing.split != row.split
            or existing.phase != row.phase
            or existing.empty_count != empty_count
        ):
            raise ImportErrorWithLocation(
                f"inconsistent played-move policy metadata for {row.board_id}"
            )
        existing.add(observation.outcome)

    digest = hashlib.sha256()
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(POLICY_HEADER)
        for item in sorted(
            aggregates.values(), key=lambda value: (value.root_board_id, value.move)
        ):
            fields = (
                item.root_board_id,
                item.board_text,
                item.split,
                str(item.phase),
                str(item.empty_count),
                item.move,
                str(item.occurrence_count),
                str(item.win_count),
                str(item.draw_count),
                str(item.loss_count),
                dataset_id,
            )
            writer.writerow(fields)
            digest.update("\t".join(fields).encode("utf-8"))
            digest.update(b"\n")
            summary.policy_emitted_rows += 1
            summary.policy_emitted_occurrences += item.occurrence_count
    return f"sha256:{digest.hexdigest()}"


def aggregate_source_checksum(source_files: list[dict[str, object]]) -> str:
    digest = hashlib.sha256()
    for item in sorted(source_files, key=lambda value: str(value["sha256"])):
        digest.update(str(item["sha256"]).encode("ascii"))
        digest.update(b"\t")
        digest.update(str(item["size_bytes"]).encode("ascii"))
        digest.update(b"\n")
    return f"sha256:{digest.hexdigest()}"


def report_for(
    args: argparse.Namespace,
    summary: Summary,
    normalized_checksum: str,
    theoretical_checksum: str | None,
    theoretical_rows: int,
    policy_checksum: str | None,
    audit_before: dict[str, dict[str, object]],
    audit_after: dict[str, dict[str, object]],
    split_report: dict[str, object],
) -> dict[str, object]:
    label_mean = None
    if summary.emitted_positions:
        label_mean = float(f"{summary.label_sum / summary.emitted_positions:.12g}")
    board_after = audit_after["board"]
    game_after = audit_after["game_group"]
    return {
        "schema_version": 1,
        "normalized_schema_version": 2,
        "importer_version": IMPORTER_VERSION,
        "identity_policy_version": IDENTITY_POLICY_VERSION,
        "source_dataset_id": args.dataset_id,
        "source_kind": SOURCE_KIND,
        "input_kind": "wthor-8x8-game-database",
        "source_files": summary.source_files,
        "source_file_count": len(summary.source_files),
        "aggregate_source_checksum": aggregate_source_checksum(summary.source_files),
        "year_counts": dict(sorted(summary.year_counts.items())),
        "theoretical_empties_counts": dict(
            sorted(summary.theoretical_empties_counts.items())
        ),
        "input_games": summary.input_games,
        "accepted_games": summary.accepted_games,
        "rejected_games": summary.rejected_games,
        "invalid_move_count": summary.invalid_move_count,
        "pass_count": summary.pass_count,
        "terminal_count": summary.terminal_count,
        "incomplete_games": summary.incomplete_games,
        "normalized_label_eligible_games": summary.normalized_label_eligible_games,
        "candidate_positions": summary.candidate_positions,
        "emitted_positions": summary.emitted_positions,
        "game_group_count": len(summary.game_group_occurrences),
        "duplicate_game_occurrence_count": sum(
            max(0, count - 1) for count in summary.game_group_occurrences.values()
        ),
        "split_counts": dict(sorted(summary.split_counts.items())),
        "phase_counts": dict(sorted(summary.phase_counts.items())),
        "label_min": summary.label_min,
        "label_max": summary.label_max,
        "label_mean": label_mean,
        "normalized_checksum": normalized_checksum,
        "theoretical_wld_rows": theoretical_rows,
        "theoretical_wld_checksum": theoretical_checksum,
        "played_move_policy_rows": summary.policy_emitted_rows,
        "played_move_policy_schema_version": 1 if args.policy_out is not None else None,
        "played_move_policy_occurrences": summary.policy_emitted_occurrences,
        "played_move_policy_candidate_positions": summary.policy_candidate_positions,
        "played_move_policy_forced_pass_positions": summary.policy_forced_pass_positions,
        "played_move_policy_checksum": policy_checksum,
        "theoretical_cutoff_games": summary.theoretical_cutoff_games,
        "theoretical_cutoff_missing_games": summary.theoretical_cutoff_missing_games,
        "wthor_actual_score_matches_engine_black_discs": (
            summary.wthor_actual_score_matches_engine_black_discs
        ),
        "wthor_actual_score_mismatches_engine_black_discs": (
            summary.wthor_actual_score_mismatches_engine_black_discs
        ),
        "unique_board_count": board_after["unique_board_count"],
        "cross_split_board_collision_count": board_after[
            "cross_split_board_collision_count"
        ],
        "cross_split_game_group_collision_count": game_after[
            "cross_split_game_group_collision_count"
        ],
        "board_leakage_audit_before": audit_before["board"],
        "board_leakage_audit_after": board_after,
        "game_group_leakage_audit_before": audit_before["game_group"],
        "game_group_leakage_audit_after": game_after,
        "split_policy": {
            "measurement_split_policy": args.split_policy,
            "measurement_split_policy_version": SPLIT_POLICY_VERSIONS[
                args.split_policy
            ],
            "ratio": "80/10/10 by hash bucket",
            **split_report,
        },
        "emit_policy": {
            "min_ply": args.min_ply,
            "max_ply": args.max_ply,
            "ply_stride": args.ply_stride,
            "selected_plies": sorted(args.selected_plies),
            "max_games": args.max_games,
            "max_positions": args.max_positions,
            "seed": args.seed,
            "emit_terminal": args.emit_terminal,
        },
        "rejected_reasons": summary.rejected_reasons,
        "notes": [
            "raw WTHOR payloads and generated outputs are local-only",
            "normalized labels are observed terminal actual-disc differences replayed by board core",
            "normalized labels are not WTHOR theoretical scores and are not searched teacher estimates",
            "played-move policy rows preserve WTHOR decisions and outcome counts but do not claim that every played move is optimal",
            "WTHOR actual/theoretical black scores use a 64-square scoring convention that can differ from engine terminal actual-disc difference",
            "theoretical sidecar exports only exact WLD at the declared cutoff and is not accepted by the current disc-difference pattern trainer",
            "training or publishing derived weights requires a separate review of WTHOR terms",
        ],
    }


def process_game(
    args: argparse.Namespace,
    source: SourcePayload,
    header: WthorHeader,
    record_index: int,
    replay_helper: ReplayHelper,
    rows: TopKRows,
    policy_observations: dict[str, PolicyObservation],
    theoretical: dict[str, WldAggregate],
    summary: Summary,
) -> None:
    summary.input_games += 1
    try:
        record = parse_record(source, record_index)
        source_occurrence_id = prefixed_id(
            "occ",
            digest_for_parts(
                args.dataset_id,
                "wthor-record-v1",
                source.sha256,
                record_index,
                record.raw_sha256,
            ),
        )
        replay = replay_helper.replay(source_occurrence_id, transcript_for(record))
        if not replay.accepted:
            raise ImportErrorWithLocation(replay.error)
        if not replay.snapshots:
            raise ImportErrorWithLocation("replay emitted no board snapshots")
    except ImportErrorWithLocation as error:
        summary.rejected_games += 1
        summary.invalid_move_count += 1
        if len(summary.rejected_reasons) < 20:
            summary.rejected_reasons.append(
                f"{source.source_ref}:record-{record_index + 1}: {error}"
            )
        return

    summary.accepted_games += 1
    summary.pass_count += replay.pass_count
    summary.terminal_count += int(replay.terminal)
    summary.incomplete_games += int(not replay.terminal)
    game_group_id = prefixed_id(
        "game",
        digest_for_parts(
            args.dataset_id,
            IDENTITY_POLICY_VERSION,
            replay.canonical_moves,
        ),
    )
    summary.game_group_occurrences[game_group_id] += 1
    split = split_for_digest(digest_for_parts(args.dataset_id, game_group_id))
    if replay.terminal:
        summary.normalized_label_eligible_games += 1
        row_occurrence = 0
        for snapshot in replay.snapshots:
            if not should_emit(args, replay, snapshot):
                continue
            row_occurrence += 1
            row = make_candidate_row(
                args,
                source_occurrence_id,
                game_group_id,
                split,
                snapshot,
                row_occurrence,
            )
            summary.candidate_positions += 1
            row.ordinal = summary.candidate_positions
            rows.consider(row)
            played_move = next_played_move(replay.canonical_moves, snapshot.ply)
            if played_move is None:
                summary.policy_forced_pass_positions += 1
            else:
                summary.policy_candidate_positions += 1
                policy_observations[row.fields[0]] = PolicyObservation(
                    move=played_move,
                    outcome=(snapshot.label > 0) - (snapshot.label < 0),
                )
    add_theoretical_wld(theoretical, header, record, replay, summary)
    if replay.terminal:
        update_actual_score_audit(record, replay, summary)


def main() -> int:
    args = parse_args()
    summary = Summary()
    rows = TopKRows(args.max_positions)
    policy_observations: dict[str, PolicyObservation] = {}
    theoretical: dict[str, WldAggregate] = {}
    try:
        manifest_dataset_id = load_manifest_dataset_id(args.manifest)
        if manifest_dataset_id != args.dataset_id:
            raise ImportErrorWithLocation(
                f"{args.manifest}: manifest dataset_id {manifest_dataset_id!r} "
                f"does not match --dataset-id {args.dataset_id!r}"
            )
        sources = discover_sources(args.input)
        with ReplayHelper(args.replay_helper) as replay_helper:
            stop = False
            for source in sources:
                header = parse_header(source)
                summary.source_files.append(
                    {
                        "source_ref": source.source_ref,
                        "sha256": source.sha256,
                        "size_bytes": len(source.data),
                        "year": header.year,
                        "game_count": header.game_count,
                        "theoretical_empties": header.theoretical_empties,
                        "created": (
                            f"{header.created_year:04d}-"
                            f"{header.created_month:02d}-"
                            f"{header.created_day:02d}"
                        ),
                    }
                )
                summary.year_counts[str(header.year)] += header.game_count
                summary.theoretical_empties_counts[
                    str(header.theoretical_empties)
                ] += header.game_count
                for record_index in range(header.game_count):
                    if (
                        args.max_games is not None
                        and summary.input_games >= args.max_games
                    ):
                        stop = True
                        break
                    process_game(
                        args,
                        source,
                        header,
                        record_index,
                        replay_helper,
                        rows,
                        policy_observations,
                        theoretical,
                        summary,
                    )
                    if (
                        args.progress_every_games is not None
                        and summary.input_games % args.progress_every_games == 0
                    ):
                        print(
                            "progress "
                            f"input_games={summary.input_games} "
                            f"accepted_games={summary.accepted_games} "
                            f"rejected_games={summary.rejected_games} "
                            f"candidate_positions={summary.candidate_positions}",
                            file=sys.stderr,
                        )
                if stop:
                    break

        selected_rows = rows.rows()
        audit_before = leakage_audits(selected_rows)
        selected_rows, split_report = apply_split_policy(selected_rows, args)
        audit_after = leakage_audits(selected_rows)
        if (
            args.strict_board_disjoint_splits
            and audit_after["board"]["cross_split_board_collision_count"] != 0
        ):
            raise ImportErrorWithLocation(
                "strict board-disjoint split check failed: "
                f"{audit_after['board']['cross_split_board_collision_count']} "
                "board ids cross splits"
            )
        normalized_checksum = write_normalized(args.output, selected_rows, summary)
        theoretical_checksum = None
        if args.theoretical_wld_out is not None:
            theoretical_checksum = write_theoretical_wld(
                args.theoretical_wld_out,
                theoretical,
                args.dataset_id,
            )
        policy_checksum = None
        if args.policy_out is not None:
            policy_checksum = write_policy(
                args.policy_out,
                selected_rows,
                policy_observations,
                args.dataset_id,
                summary,
            )
        report = report_for(
            args,
            summary,
            normalized_checksum,
            theoretical_checksum,
            len(theoretical),
            policy_checksum,
            audit_before,
            audit_after,
            split_report,
        )
        args.report.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    except (ImportErrorWithLocation, OSError) as error:
        print(error, file=sys.stderr)
        return 1

    print(
        "summary "
        f"source_files={len(summary.source_files)} "
        f"input_games={summary.input_games} "
        f"accepted_games={summary.accepted_games} "
        f"rejected_games={summary.rejected_games} "
        f"emitted_positions={summary.emitted_positions} "
        f"theoretical_wld_rows={len(theoretical)} "
        f"played_move_policy_rows={summary.policy_emitted_rows}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
