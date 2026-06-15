#!/usr/bin/env python3
"""Import Egaroucid board-score training data as normalized TSV rows."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import re
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, TextIO


DATASET_ID = "egaroucid-train-data-board-score-v2025-02-02"
LABEL_KIND = "engine_disc_estimate"
LABEL_UNIT = "final_disc_diff"
LABEL_PERSPECTIVE = "side_to_move"
PHASE_COUNT = 13
MIN_OCCUPIED_COUNT = 4
MAX_EGAROUCID_OCCUPIED_COUNT = 63
SCORE_MIN = -64
SCORE_MAX = 64
BOARD_RE = re.compile(r"^[XO-]{64}$")
SCORE_RE = re.compile(r"^-?[0-9]+$")
HEADER = (
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
)


class ImportErrorWithLocation(ValueError):
    pass


@dataclass(frozen=True)
class NormalizedRow:
    record_id: str
    position_id: str
    source_dataset_id: str
    split: str
    board: str
    score: int
    exact_duplicate_index: int
    occupied_count: int
    phase: int
    player_disc_count: int
    opponent_disc_count: int
    empty_count: int


@dataclass
class Summary:
    source_files: int = 0
    imported_rows: int = 0
    train_rows: int = 0
    validation_rows: int = 0
    test_rows: int = 0
    exact_duplicate_rows: int = 0

    def count_split(self, split: str) -> None:
        if split == "train":
            self.train_rows += 1
        elif split == "validation":
            self.validation_rows += 1
        elif split == "test":
            self.test_rows += 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        action="append",
        required=True,
        type=Path,
        help="Egaroucid_Train_Data.zip, an extracted .txt file, or an extracted directory.",
    )
    parser.add_argument("--manifest", type=Path, help="Optional committed dataset manifest.")
    parser.add_argument(
        "--dataset-id",
        default=DATASET_ID,
        help="Dataset id salt for deterministic record ids and splits.",
    )
    parser.add_argument(
        "--validate-only",
        action="store_true",
        help="Validate inputs and print only the stderr summary.",
    )
    return parser.parse_args()


def phase_for_occupied_count(occupied_count: int) -> int:
    phase = (
        (occupied_count - MIN_OCCUPIED_COUNT)
        * PHASE_COUNT
        // (MAX_EGAROUCID_OCCUPIED_COUNT - MIN_OCCUPIED_COUNT + 1)
    )
    return min(PHASE_COUNT - 1, max(0, phase))


def digest_for_parts(*parts: object) -> str:
    payload = "\t".join(str(part) for part in parts).encode("ascii")
    return hashlib.sha256(payload).hexdigest()


def split_for_digest(digest: str) -> str:
    bucket = int(digest[:16], 16) % 10
    if bucket == 0:
        return "validation"
    if bucket == 1:
        return "test"
    return "train"


def load_manifest_dataset_id(path: Path) -> str:
    try:
        with path.open("r", encoding="utf-8") as handle:
            manifest = json.load(handle)
    except json.JSONDecodeError as exc:
        raise ImportErrorWithLocation(f"{path}: manifest is not valid JSON: {exc}") from exc
    except OSError as exc:
        raise ImportErrorWithLocation(f"{path}: cannot read manifest: {exc}") from exc
    if not isinstance(manifest, dict):
        raise ImportErrorWithLocation(f"{path}: manifest root must be a JSON object")
    dataset_id = manifest.get("dataset_id")
    if not isinstance(dataset_id, str) or not dataset_id:
        raise ImportErrorWithLocation(f"{path}: manifest dataset_id must be a non-empty string")
    return dataset_id


def parse_line(
    dataset_id: str,
    source_ref: str,
    line_number: int,
    line: str,
    exact_duplicate_counts: dict[tuple[str, int], int],
) -> NormalizedRow:
    text = line.rstrip("\n")
    if text.endswith("\r"):
        text = text[:-1]

    fields = text.strip().split()
    if len(fields) != 2:
        raise ImportErrorWithLocation(
            f"{source_ref}:{line_number}: expected board and score separated by whitespace"
        )

    board, score_text = fields
    if BOARD_RE.fullmatch(board) is None:
        if len(board) != 64:
            raise ImportErrorWithLocation(
                f"{source_ref}:{line_number}: board must be exactly 64 characters"
            )
        invalid = "".join(sorted(set(board).difference({"X", "O", "-"})))
        raise ImportErrorWithLocation(
            f"{source_ref}:{line_number}: board contains invalid character(s): {invalid!r}"
        )

    if SCORE_RE.fullmatch(score_text) is None:
        raise ImportErrorWithLocation(f"{source_ref}:{line_number}: score must be an integer")
    score = int(score_text)
    if score < SCORE_MIN or score > SCORE_MAX:
        raise ImportErrorWithLocation(
            f"{source_ref}:{line_number}: score must be in [{SCORE_MIN}, {SCORE_MAX}]"
        )

    player_disc_count = board.count("X")
    opponent_disc_count = board.count("O")
    empty_count = board.count("-")
    occupied_count = player_disc_count + opponent_disc_count
    if occupied_count < MIN_OCCUPIED_COUNT or occupied_count > MAX_EGAROUCID_OCCUPIED_COUNT:
        raise ImportErrorWithLocation(
            f"{source_ref}:{line_number}: occupied_count must be in "
            f"[{MIN_OCCUPIED_COUNT}, {MAX_EGAROUCID_OCCUPIED_COUNT}]"
        )
    if empty_count + occupied_count != 64:
        raise ImportErrorWithLocation(f"{source_ref}:{line_number}: board cell counts are invalid")

    position_digest = digest_for_parts(dataset_id, board)
    label_digest = digest_for_parts(dataset_id, board, score)
    split = split_for_digest(position_digest)
    duplicate_key = (board, score)
    exact_duplicate_index = exact_duplicate_counts.get(duplicate_key, 0) + 1
    exact_duplicate_counts[duplicate_key] = exact_duplicate_index
    return NormalizedRow(
        record_id=f"{dataset_id}-{label_digest[:16]}-{exact_duplicate_index:06d}",
        position_id=f"{dataset_id}-{position_digest[:16]}",
        source_dataset_id=dataset_id,
        split=split,
        board=board,
        score=score,
        exact_duplicate_index=exact_duplicate_index,
        occupied_count=occupied_count,
        phase=phase_for_occupied_count(occupied_count),
        player_disc_count=player_disc_count,
        opponent_disc_count=opponent_disc_count,
        empty_count=empty_count,
    )


def write_row(row: NormalizedRow, output: TextIO) -> None:
    output.write(
        "\t".join(
            (
                row.record_id,
                row.position_id,
                row.source_dataset_id,
                row.split,
                row.board,
                LABEL_KIND,
                LABEL_UNIT,
                LABEL_PERSPECTIVE,
                str(row.score),
                str(row.occupied_count),
                str(row.phase),
                str(row.player_disc_count),
                str(row.opponent_disc_count),
                str(row.empty_count),
            )
        )
        + "\n"
    )


def process_text_stream(
    dataset_id: str,
    source_ref: str,
    handle: Iterable[str],
    summary: Summary,
    output: TextIO,
    validate_only: bool,
    exact_duplicate_counts: dict[tuple[str, int], int],
) -> None:
    summary.source_files += 1
    for line_number, line in enumerate(handle, start=1):
        row = parse_line(dataset_id, source_ref, line_number, line, exact_duplicate_counts)
        summary.imported_rows += 1
        if row.exact_duplicate_index > 1:
            summary.exact_duplicate_rows += 1
        summary.count_split(row.split)
        if not validate_only:
            write_row(row, output)


def process_zip(
    dataset_id: str,
    path: Path,
    summary: Summary,
    output: TextIO,
    validate_only: bool,
    exact_duplicate_counts: dict[tuple[str, int], int],
) -> None:
    try:
        archive = zipfile.ZipFile(path)
    except zipfile.BadZipFile as exc:
        raise ImportErrorWithLocation(f"{path}: invalid zip archive") from exc
    with archive:
        text_entries = [
            info
            for info in archive.infolist()
            if not info.is_dir() and info.filename.lower().endswith(".txt")
        ]
        if not text_entries:
            raise ImportErrorWithLocation(f"{path}: zip archive contains no .txt files")
        for info in sorted(text_entries, key=lambda item: item.filename):
            with archive.open(info) as raw:
                with io.TextIOWrapper(raw, encoding="utf-8", newline="") as text:
                    process_text_stream(
                        dataset_id,
                        f"{path}!{info.filename}",
                        text,
                        summary,
                        output,
                        validate_only,
                        exact_duplicate_counts,
                    )


def process_plain_file(
    dataset_id: str,
    path: Path,
    summary: Summary,
    output: TextIO,
    validate_only: bool,
    exact_duplicate_counts: dict[tuple[str, int], int],
) -> None:
    with path.open("r", encoding="utf-8", newline="") as handle:
        process_text_stream(
            dataset_id, str(path), handle, summary, output, validate_only, exact_duplicate_counts
        )


def process_directory(
    dataset_id: str,
    path: Path,
    summary: Summary,
    output: TextIO,
    validate_only: bool,
    exact_duplicate_counts: dict[tuple[str, int], int],
) -> None:
    text_paths = sorted(child for child in path.rglob("*.txt") if child.is_file())
    if not text_paths:
        raise ImportErrorWithLocation(f"{path}: directory contains no .txt files")
    for text_path in text_paths:
        process_plain_file(
            dataset_id, text_path, summary, output, validate_only, exact_duplicate_counts
        )


def process_input(
    dataset_id: str,
    path: Path,
    summary: Summary,
    output: TextIO,
    validate_only: bool,
    exact_duplicate_counts: dict[tuple[str, int], int],
) -> None:
    if not path.exists():
        raise ImportErrorWithLocation(f"{path}: input does not exist")
    if path.is_dir():
        process_directory(dataset_id, path, summary, output, validate_only, exact_duplicate_counts)
    elif path.suffix.lower() == ".zip":
        process_zip(dataset_id, path, summary, output, validate_only, exact_duplicate_counts)
    else:
        process_plain_file(dataset_id, path, summary, output, validate_only, exact_duplicate_counts)


def main() -> int:
    args = parse_args()
    summary = Summary()
    exact_duplicate_counts: dict[tuple[str, int], int] = {}
    try:
        if args.manifest is not None:
            manifest_dataset_id = load_manifest_dataset_id(args.manifest)
            if manifest_dataset_id != args.dataset_id:
                raise ImportErrorWithLocation(
                    f"{args.manifest}: manifest dataset_id {manifest_dataset_id!r} does not match "
                    f"--dataset-id {args.dataset_id!r}"
                )
        if not args.validate_only:
            sys.stdout.write("\t".join(HEADER) + "\n")
        for input_path in args.input:
            process_input(
                args.dataset_id,
                input_path,
                summary,
                sys.stdout,
                args.validate_only,
                exact_duplicate_counts,
            )
    except (OSError, UnicodeDecodeError, ImportErrorWithLocation) as exc:
        print(exc, file=sys.stderr)
        return 1

    print(
        "summary "
        f"source_files={summary.source_files} "
        f"imported_rows={summary.imported_rows} "
        f"train_rows={summary.train_rows} "
        f"validation_rows={summary.validation_rows} "
        f"test_rows={summary.test_rows} "
        f"exact_duplicate_rows={summary.exact_duplicate_rows} "
        f"label_kind={LABEL_KIND} "
        f"label_unit={LABEL_UNIT} "
        f"label_perspective={LABEL_PERSPECTIVE} "
        f"phase_count={PHASE_COUNT} "
        "split_policy=position-sha256 duplicate_policy=keep_all_input_order",
        file=sys.stderr,
    )
    if summary.imported_rows == 0:
        print("no rows imported", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
