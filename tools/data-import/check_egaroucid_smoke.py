#!/usr/bin/env python3
"""CTest wrapper for the Egaroucid board-score importer."""

from __future__ import annotations

import argparse
import csv
import hashlib
import subprocess
import sys
import tempfile
import zipfile
from io import StringIO
from pathlib import Path


DATASET_ID = "egaroucid-train-data-board-score-v2025-02-02"
EXPECTED_HEADER = [
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
VALID_BOARD = "---------------------------OX------XO---------------------------"
BAD_SCORE_BOARD = "XXOOO-----------------------------------------------------------"
REPEATED_BOARD = "XXOOO-----------------------------------------------------------"


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


def phase_for_occupied_count(occupied_count: int) -> int:
    return min(12, ((occupied_count - 4) * 13) // 60)


def run_importer(
    importer: Path, fixture: Path, manifest: Path | None = None
) -> subprocess.CompletedProcess[str]:
    command = [sys.executable, str(importer), "--input", str(fixture)]
    if manifest is not None:
        command.extend(["--manifest", str(manifest)])
    return subprocess.run(command, check=False, capture_output=True, text=True)


def parse_tsv(text: str) -> list[dict[str, str]] | None:
    reader = csv.DictReader(StringIO(text), delimiter="\t")
    if reader.fieldnames != EXPECTED_HEADER:
        print(f"unexpected TSV header: {reader.fieldnames}", file=sys.stderr)
        return None
    rows = list(reader)
    if len(rows) != 7:
        print(f"unexpected row count: {len(rows)}", file=sys.stderr)
        return None
    return rows


def check_rows(rows: list[dict[str, str]]) -> bool:
    splits = set()
    rows_by_board: dict[str, list[dict[str, str]]] = {}
    exact_duplicate_counts: dict[tuple[str, int], int] = {}
    for row in rows:
        board = row["board_a1_to_h8"]
        score = int(row["label_score_side_to_move"])
        position_digest = digest_for_parts(DATASET_ID, board)
        label_digest = digest_for_parts(DATASET_ID, board, score)
        duplicate_key = (board, score)
        duplicate_index = exact_duplicate_counts.get(duplicate_key, 0) + 1
        exact_duplicate_counts[duplicate_key] = duplicate_index
        expected_record_id = f"{DATASET_ID}-{label_digest[:16]}-{duplicate_index:06d}"
        expected_position_id = f"{DATASET_ID}-{position_digest[:16]}"
        expected_split = split_for_digest(position_digest)
        occupied = board.count("X") + board.count("O")
        player_count = board.count("X")
        opponent_count = board.count("O")
        empty_count = board.count("-")

        checks = {
            "record_id": expected_record_id,
            "position_id": expected_position_id,
            "source_dataset_id": DATASET_ID,
            "split": expected_split,
            "label_kind": "engine_disc_estimate",
            "label_unit": "final_disc_diff",
            "label_perspective": "side_to_move",
            "occupied_count": str(occupied),
            "phase": str(phase_for_occupied_count(occupied)),
            "player_disc_count": str(player_count),
            "opponent_disc_count": str(opponent_count),
            "empty_count": str(empty_count),
        }
        for field, expected in checks.items():
            if row[field] != expected:
                print(
                    f"field mismatch for {field}: {row[field]!r} != {expected!r}",
                    file=sys.stderr,
                )
                return False
        splits.add(row["split"])
        rows_by_board.setdefault(board, []).append(row)

    if splits != {"train", "validation", "test"}:
        print(f"expected fixture to cover all splits, got {sorted(splits)}", file=sys.stderr)
        return False

    repeated_rows = rows_by_board.get(REPEATED_BOARD, [])
    if len(repeated_rows) != 3:
        print("expected fixture to include three rows for the repeated board", file=sys.stderr)
        return False
    if {row["label_score_side_to_move"] for row in repeated_rows} != {"-3", "5"}:
        print("expected repeated board to include different scores", file=sys.stderr)
        return False
    if len({row["split"] for row in repeated_rows}) != 1:
        print("same board with different scores did not stay in the same split", file=sys.stderr)
        return False
    if len({row["position_id"] for row in repeated_rows}) != 1:
        print("same board with different scores did not keep one position_id", file=sys.stderr)
        return False

    exact_duplicates = [
        row for row in repeated_rows if row["label_score_side_to_move"] == "-3"
    ]
    if len(exact_duplicates) != 2:
        print("expected two exact duplicate rows for repeated board and score", file=sys.stderr)
        return False
    expected_duplicate_ids = [
        f"{DATASET_ID}-09a5338318be196c-000001",
        f"{DATASET_ID}-09a5338318be196c-000002",
    ]
    if [row["record_id"] for row in exact_duplicates] != expected_duplicate_ids:
        print("exact duplicate record ids are not deterministic", file=sys.stderr)
        return False
    return True


def check_valid_import(importer: Path, fixture: Path, manifest: Path) -> str | None:
    first = run_importer(importer, fixture, manifest)
    second = run_importer(importer, fixture, manifest)
    if first.returncode != 0:
        sys.stderr.write(first.stderr)
        sys.stderr.write(first.stdout)
        return None
    if second.returncode != 0:
        sys.stderr.write(second.stderr)
        sys.stderr.write(second.stdout)
        return None
    if first.stdout != second.stdout:
        print("importer output is not deterministic across repeated runs", file=sys.stderr)
        return None
    rows = parse_tsv(first.stdout)
    if rows is None or not check_rows(rows):
        return None
    expected_summary = (
        "summary source_files=1 imported_rows=7 train_rows=5 validation_rows=1 test_rows=1 "
        "exact_duplicate_rows=1 label_kind=engine_disc_estimate label_unit=final_disc_diff "
        "label_perspective=side_to_move phase_count=13 split_policy=position-sha256 "
        "duplicate_policy=keep_all_input_order"
    )
    if expected_summary not in first.stderr:
        print("missing expected importer summary", file=sys.stderr)
        return None
    return first.stdout


def check_zip_and_directory(importer: Path, fixture: Path, expected_stdout: str) -> bool:
    fixture_text = fixture.read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory() as temp:
        temp_path = Path(temp)
        zip_path = temp_path / "Egaroucid_Train_Data.zip"
        with zipfile.ZipFile(zip_path, "w") as archive:
            archive.writestr("0000000.txt", fixture_text)
        zip_result = run_importer(importer, zip_path)
        if zip_result.returncode != 0 or zip_result.stdout != expected_stdout:
            sys.stderr.write(zip_result.stderr)
            sys.stderr.write(zip_result.stdout)
            print("zip import did not match plain file import", file=sys.stderr)
            return False

        extracted_dir = temp_path / "extracted"
        nested_dir = extracted_dir / "0000"
        nested_dir.mkdir(parents=True)
        (nested_dir / "0000000.txt").write_text(fixture_text, encoding="utf-8")
        dir_result = run_importer(importer, extracted_dir)
        if dir_result.returncode != 0 or dir_result.stdout != expected_stdout:
            sys.stderr.write(dir_result.stderr)
            sys.stderr.write(dir_result.stdout)
            print("directory import did not match plain file import", file=sys.stderr)
            return False
    return True


def check_invalid_cases(importer: Path) -> bool:
    cases = {
        "bad-field-count": (f"{VALID_BOARD} 0 extra\n", "expected board and score"),
        "bad-length": ("-" * 63 + " 0\n", "board must be exactly 64 characters"),
        "bad-character": (VALID_BOARD.replace("-", "Z", 1) + " 0\n", "invalid character"),
        "bad-score-text": (f"{VALID_BOARD} nope\n", "score must be an integer"),
        "bad-score-range": (f"{BAD_SCORE_BOARD} 65\n", "score must be in [-64, 64]"),
        "bad-stone-count": ("-" * 64 + " 0\n", "occupied_count must be in [4, 63]"),
        "bad-blank-line": ("\n", "expected board and score"),
    }
    with tempfile.TemporaryDirectory() as temp:
        temp_path = Path(temp)
        for name, (content, expected_error) in cases.items():
            path = temp_path / f"{name}.txt"
            path.write_text(content, encoding="utf-8")
            result = run_importer(importer, path)
            if result.returncode == 0:
                print(f"{name}: importer unexpectedly accepted invalid input", file=sys.stderr)
                return False
            if expected_error not in result.stderr:
                print(f"{name}: missing expected error {expected_error!r}", file=sys.stderr)
                sys.stderr.write(result.stderr)
                return False
    return True


def check_manifest_validation(importer: Path, fixture: Path, manifest: Path) -> bool:
    with tempfile.TemporaryDirectory() as temp:
        temp_path = Path(temp)
        bad_json = temp_path / "bad-json.manifest.json"
        bad_json.write_text("{not json", encoding="utf-8")
        bad_json_result = run_importer(importer, fixture, bad_json)
        if bad_json_result.returncode == 0 or "manifest is not valid JSON" not in bad_json_result.stderr:
            print("manifest JSON validation did not reject invalid JSON", file=sys.stderr)
            sys.stderr.write(bad_json_result.stderr)
            return False

        mismatch = temp_path / "mismatch.manifest.json"
        mismatch.write_text(
            manifest.read_text(encoding="utf-8").replace(DATASET_ID, "other-dataset", 1),
            encoding="utf-8",
        )
        mismatch_result = run_importer(importer, fixture, mismatch)
        if mismatch_result.returncode == 0 or "does not match --dataset-id" not in mismatch_result.stderr:
            print("manifest dataset_id validation did not reject mismatch", file=sys.stderr)
            sys.stderr.write(mismatch_result.stderr)
            return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--importer", required=True, type=Path)
    parser.add_argument("--fixture", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    args = parser.parse_args()

    expected_stdout = check_valid_import(args.importer, args.fixture, args.manifest)
    if expected_stdout is None:
        return 1
    if not check_zip_and_directory(args.importer, args.fixture, expected_stdout):
        return 1
    if not check_invalid_cases(args.importer):
        return 1
    if not check_manifest_validation(args.importer, args.fixture, args.manifest):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
