#!/usr/bin/env python3
"""Audit promotion opening boards against local Egaroucid board-score rows."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import zipfile
from pathlib import Path
from typing import Any

from import_egaroucid_sequences import ImportErrorWithLocation, ReplayHelper


AUDITOR_VERSION = "egaroucid-board-score-opening-overlap-v1"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--openings", required=True, type=Path)
    parser.add_argument("--replay-helper", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--opening-generation-seed", required=True, type=int)
    parser.add_argument("--opening-generation-plies", required=True, type=int)
    parser.add_argument("--require-zero-overlap", action="store_true")
    parser.add_argument("--progress-every-rows", type=int)
    args = parser.parse_args()
    if args.opening_generation_seed < 0:
        parser.error("--opening-generation-seed must be non-negative")
    if not 0 <= args.opening_generation_plies <= 60:
        parser.error("--opening-generation-plies must be in [0, 60]")
    if args.progress_every_rows is not None and args.progress_every_rows <= 0:
        parser.error("--progress-every-rows must be positive")
    return args


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def opening_lines(path: Path) -> list[tuple[str, str]]:
    result: list[tuple[str, str]] = []
    for line_number, raw_line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        identifier, separator, transcript = line.partition(":")
        if not separator:
            identifier = f"opening-{line_number}"
            transcript = line
        identifier = identifier.strip()
        transcript = transcript.strip()
        if not identifier or not transcript:
            raise ImportErrorWithLocation(
                f"{path.name}:{line_number}: opening id and transcript must be non-empty"
            )
        result.append((identifier, transcript))
    if not result:
        raise ImportErrorWithLocation(f"{path.name}: opening file is empty")
    return result


def replay_openings(
    path: Path, helper: ReplayHelper, expected_plies: int
) -> dict[str, str]:
    boards: dict[str, str] = {}
    identifiers: set[str] = set()
    for identifier, transcript in opening_lines(path):
        if identifier in identifiers:
            raise ImportErrorWithLocation(f"{path.name}: duplicate opening id {identifier!r}")
        replay = helper.replay(f"opening:{identifier}", transcript)
        if not replay.accepted or not replay.snapshots:
            raise ImportErrorWithLocation(
                f"{path.name}: opening {identifier!r} failed replay: {replay.error}"
            )
        normal_plies = sum(token != "pass" for token in replay.canonical_moves.split())
        if normal_plies != expected_plies:
            raise ImportErrorWithLocation(
                f"{path.name}: opening {identifier!r} has {normal_plies} normal plies, "
                f"expected {expected_plies}"
            )
        board_text = replay.snapshots[-1].board_text
        if board_text in boards:
            raise ImportErrorWithLocation(
                f"{path.name}: duplicate final board for {identifier!r}"
            )
        identifiers.add(identifier)
        boards[board_text] = identifier
    return boards


def load_manifest(path: Path, archive_checksum: str) -> dict[str, Any]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ImportErrorWithLocation(f"{path.name}: manifest root must be an object")
    if payload.get("sha256") != archive_checksum:
        raise ImportErrorWithLocation(
            f"{path.name}: manifest sha256 does not match the input archive"
        )
    if payload.get("derived_weights_allowed") is not True:
        raise ImportErrorWithLocation(
            f"{path.name}: derived_weights_allowed must be true"
        )
    return payload


def audit(args: argparse.Namespace) -> dict[str, Any]:
    archive_checksum = sha256_file(args.input)
    manifest = load_manifest(args.manifest, archive_checksum)
    with ReplayHelper(args.replay_helper) as helper:
        opening_boards = replay_openings(
            args.openings, helper, args.opening_generation_plies
        )

    overlap_counts = {identifier: 0 for identifier in opening_boards.values()}
    input_rows = 0
    source_files = 0
    with zipfile.ZipFile(args.input) as archive:
        members = sorted(
            (
                info
                for info in archive.infolist()
                if not info.is_dir() and info.filename.lower().endswith(".txt")
            ),
            key=lambda info: info.filename,
        )
        if not members:
            raise ImportErrorWithLocation(f"{args.input.name}: no .txt members")
        for member in members:
            source_files += 1
            with archive.open(member) as source:
                for line_number, raw_line in enumerate(source, start=1):
                    fields = raw_line.split()
                    if len(fields) != 2 or len(fields[0]) != 64:
                        raise ImportErrorWithLocation(
                            f"{member.filename}:{line_number}: malformed board-score row"
                        )
                    input_rows += 1
                    identifier = opening_boards.get(fields[0].decode("ascii"))
                    if identifier is not None:
                        overlap_counts[identifier] += 1
                    if (
                        args.progress_every_rows is not None
                        and input_rows % args.progress_every_rows == 0
                    ):
                        print(f"audited_rows={input_rows}", file=sys.stderr)

    overlapping = sorted(
        identifier for identifier, count in overlap_counts.items() if count > 0
    )
    return {
        "schema_version": 1,
        "auditor_version": AUDITOR_VERSION,
        "source_dataset_id": manifest["dataset_id"],
        "source_archive_sha256": f"sha256:{archive_checksum}",
        "source_file_count": source_files,
        "source_position_count": input_rows,
        "opening_source": "independent-board-core-random",
        "opening_checksum": f"sha256:{sha256_file(args.openings)}",
        "opening_generation_seed": args.opening_generation_seed,
        "opening_generation_plies": args.opening_generation_plies,
        "opening_pairs": len(opening_boards),
        "board_identity_policy": "side-to-move-relative-a1-to-h8",
        "overlap_with_training_boards": len(overlapping),
        "overlap_occurrences": sum(overlap_counts.values()),
        "overlapping_opening_ids": overlapping,
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
    except (
        ImportErrorWithLocation,
        json.JSONDecodeError,
        OSError,
        UnicodeDecodeError,
        ValueError,
        zipfile.BadZipFile,
    ) as error:
        print(error, file=sys.stderr)
        return 1

    print(
        f"opening_pairs={report['opening_pairs']} "
        f"source_positions={report['source_position_count']} "
        f"board_overlap={report['overlap_with_training_boards']}"
    )
    if args.require_zero_overlap and report["overlap_with_training_boards"] != 0:
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
