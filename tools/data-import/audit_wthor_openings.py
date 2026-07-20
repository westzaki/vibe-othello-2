#!/usr/bin/env python3
"""Audit a promotion opening suite for overlap with local WTHOR training games."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from dataclasses import dataclass
from pathlib import Path

from import_egaroucid_sequences import ImportErrorWithLocation, ReplayHelper
from import_wthor import (
    aggregate_source_checksum,
    board_id_for,
    discover_sources,
    parse_header,
    parse_record,
    transcript_for,
)


AUDITOR_VERSION = "wthor-opening-overlap-v1"


@dataclass(frozen=True)
class Opening:
    identifier: str
    transcript: str
    canonical_tokens: tuple[str, ...]
    board_id: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", action="append", required=True, type=Path)
    parser.add_argument("--openings", required=True, type=Path)
    parser.add_argument("--replay-helper", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument(
        "--opening-source",
        default="independent-board-core-random",
    )
    parser.add_argument("--opening-generation-seed", required=True, type=int)
    parser.add_argument("--opening-generation-plies", required=True, type=int)
    parser.add_argument("--require-zero-overlap", action="store_true")
    parser.add_argument("--progress-every-games", type=int)
    args = parser.parse_args()
    if args.opening_generation_seed < 0:
        parser.error("--opening-generation-seed must be non-negative")
    if not 0 <= args.opening_generation_plies <= 60:
        parser.error("--opening-generation-plies must be in [0, 60]")
    if args.progress_every_games is not None and args.progress_every_games <= 0:
        parser.error("--progress-every-games must be positive")
    return args


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return f"sha256:{digest.hexdigest()}"


def opening_lines(path: Path) -> list[tuple[str, str]]:
    result: list[tuple[str, str]] = []
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        identifier, separator, transcript = line.partition(":")
        if not separator:
            identifier = line
            transcript = line
        identifier = identifier.strip()
        transcript = transcript.strip()
        if not identifier or not transcript:
            raise ImportErrorWithLocation(
                f"{path}:{line_number}: opening id and transcript must be non-empty"
            )
        result.append((identifier, transcript))
    if not result:
        raise ImportErrorWithLocation(f"{path}: opening file is empty")
    return result


def replay_openings(path: Path, helper: ReplayHelper, expected_plies: int) -> list[Opening]:
    openings: list[Opening] = []
    seen_ids: set[str] = set()
    seen_boards: set[str] = set()
    seen_transcripts: set[tuple[str, ...]] = set()
    for identifier, transcript in opening_lines(path):
        if identifier in seen_ids:
            raise ImportErrorWithLocation(f"{path}: duplicate opening id {identifier!r}")
        replay = helper.replay(f"opening:{identifier}", transcript)
        if not replay.accepted or not replay.snapshots:
            raise ImportErrorWithLocation(
                f"{path}: opening {identifier!r} failed replay: {replay.error}"
            )
        tokens = tuple(replay.canonical_moves.split())
        normal_plies = sum(token != "pass" for token in tokens)
        if normal_plies != expected_plies:
            raise ImportErrorWithLocation(
                f"{path}: opening {identifier!r} has {normal_plies} normal plies, "
                f"expected {expected_plies}"
            )
        board_id = board_id_for(replay.snapshots[-1].board_text)
        if board_id in seen_boards:
            raise ImportErrorWithLocation(f"{path}: duplicate final opening board {board_id}")
        if tokens in seen_transcripts:
            raise ImportErrorWithLocation(f"{path}: duplicate opening transcript {identifier!r}")
        seen_ids.add(identifier)
        seen_boards.add(board_id)
        seen_transcripts.add(tokens)
        openings.append(
            Opening(
                identifier=identifier,
                transcript=transcript,
                canonical_tokens=tokens,
                board_id=board_id,
            )
        )
    return openings


def starts_with(tokens: tuple[str, ...], prefix: tuple[str, ...]) -> bool:
    return len(tokens) >= len(prefix) and tokens[: len(prefix)] == prefix


def audit(args: argparse.Namespace) -> dict[str, object]:
    sources = discover_sources(args.input)
    with ReplayHelper(args.replay_helper) as helper:
        openings = replay_openings(
            args.openings,
            helper,
            args.opening_generation_plies,
        )
        opening_by_board = {opening.board_id: opening.identifier for opening in openings}
        opening_prefixes = {
            opening.canonical_tokens: opening.identifier for opening in openings
        }
        overlapping_boards: set[str] = set()
        overlapping_games: set[str] = set()
        exact_transcript_games: set[str] = set()
        board_overlap_occurrences = 0
        game_count = 0
        accepted_games = 0

        source_files: list[dict[str, object]] = []
        for source in sources:
            header = parse_header(source)
            source_files.append(
                {
                    "sha256": source.sha256,
                    "size_bytes": len(source.data),
                    "game_count": header.game_count,
                }
            )
            for record_index in range(header.game_count):
                game_count += 1
                record = parse_record(source, record_index)
                game_identity = f"{source.sha256}:{record_index}"
                replay = helper.replay(game_identity, transcript_for(record))
                if not replay.accepted:
                    raise ImportErrorWithLocation(
                        f"{source.source_ref}: record {record_index + 1}: {replay.error}"
                    )
                accepted_games += 1
                for snapshot in replay.snapshots:
                    board_id = board_id_for(snapshot.board_text)
                    opening_id = opening_by_board.get(board_id)
                    if opening_id is not None:
                        overlapping_boards.add(opening_id)
                        board_overlap_occurrences += 1

                game_tokens = tuple(replay.canonical_moves.split())
                for prefix, opening_id in opening_prefixes.items():
                    if game_tokens == prefix:
                        exact_transcript_games.add(game_identity)
                    if starts_with(game_tokens, prefix):
                        overlapping_games.add(game_identity)
                        overlapping_boards.add(opening_id)
                if (
                    args.progress_every_games is not None
                    and game_count % args.progress_every_games == 0
                ):
                    print(f"audited_games={game_count}", file=sys.stderr)

    return {
        "schema_version": 1,
        "auditor_version": AUDITOR_VERSION,
        "board_identity_policy": "board-v1-side-to-move-relative-a1-to-h8",
        "game_overlap_policy": "canonical-replayed-WTHOR-transcript-has-opening-prefix",
        "opening_source": args.opening_source,
        "opening_checksum": sha256_file(args.openings),
        "opening_generation_seed": args.opening_generation_seed,
        "opening_generation_plies": args.opening_generation_plies,
        "opening_pairs": len(openings),
        "wthor_source_file_count": len(sources),
        "wthor_training_games": game_count,
        "wthor_accepted_games": accepted_games,
        "wthor_aggregate_source_checksum": aggregate_source_checksum(source_files),
        "overlap_with_wthor_training_boards": len(overlapping_boards),
        "overlap_with_wthor_training_board_occurrences": board_overlap_occurrences,
        "overlap_with_wthor_training_games": len(overlapping_games),
        "exact_transcript_overlap_with_wthor_training_games": len(exact_transcript_games),
        "overlapping_opening_ids": sorted(overlapping_boards),
    }


def main() -> int:
    args = parse_args()
    try:
        report = audit(args)
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    except (ImportErrorWithLocation, OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1

    board_overlap = int(report["overlap_with_wthor_training_boards"])
    game_overlap = int(report["overlap_with_wthor_training_games"])
    print(
        f"opening_pairs={report['opening_pairs']} "
        f"board_overlap={board_overlap} game_overlap={game_overlap}"
    )
    if args.require_zero_overlap and (board_overlap != 0 or game_overlap != 0):
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
