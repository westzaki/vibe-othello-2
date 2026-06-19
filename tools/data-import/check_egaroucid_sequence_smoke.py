#!/usr/bin/env python3
"""CTest wrapper for the Egaroucid sequence transcript importer."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
import tempfile
import zipfile
from io import StringIO
from pathlib import Path
from typing import Any


DATASET_ID = "egaroucid-sequence-v0002-local"
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


def run_importer(
    importer: Path,
    fixture: Path,
    manifest: Path,
    report: Path,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(importer),
            "--input",
            str(fixture),
            "--manifest",
            str(manifest),
            "--report",
            str(report),
            "--min-ply",
            "4",
            "--max-ply",
            "12",
            "--ply-stride",
            "4",
            "--seed",
            "7",
            "--emit-terminal",
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def parse_rows(text: str) -> list[dict[str, str]] | None:
    reader = csv.DictReader(StringIO(text), delimiter="\t")
    if reader.fieldnames != EXPECTED_HEADER:
        print(f"unexpected TSV header: {reader.fieldnames}", file=sys.stderr)
        return None
    rows = list(reader)
    if not rows:
        print("expected emitted rows", file=sys.stderr)
        return None
    return rows


def load_report(path: Path) -> dict[str, Any] | None:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"could not read report: {error}", file=sys.stderr)
        return None
    if not isinstance(data, dict):
        print("report root must be object", file=sys.stderr)
        return None
    return data


def check_rows(rows: list[dict[str, str]]) -> bool:
    if len(rows) != 9:
        print(f"unexpected emitted row count: {len(rows)}", file=sys.stderr)
        return False
    labels = {int(row["label_score_side_to_move"]) for row in rows}
    if len(labels) < 2:
        print(f"expected multiple final-disc-diff labels, got {labels!r}", file=sys.stderr)
        return False
    source_ids = {row["source_dataset_id"] for row in rows}
    if source_ids != {DATASET_ID}:
        print(f"unexpected source ids: {source_ids}", file=sys.stderr)
        return False
    if {row["label_kind"] for row in rows} != {"engine_disc_estimate"}:
        print("unexpected label kind", file=sys.stderr)
        return False
    if {row["label_unit"] for row in rows} != {"final_disc_diff"}:
        print("unexpected label unit", file=sys.stderr)
        return False
    if {row["label_perspective"] for row in rows} != {"side_to_move"}:
        print("unexpected label perspective", file=sys.stderr)
        return False
    games_by_prefix: dict[str, set[str]] = {}
    for row in rows:
        board = row["board_a1_to_h8"]
        if len(board) != 64 or set(board).difference({"X", "O", "-"}):
            print(f"bad board: {board!r}", file=sys.stderr)
            return False
        occupied = board.count("X") + board.count("O")
        if int(row["occupied_count"]) != occupied:
            print(f"bad occupied count: {row}", file=sys.stderr)
            return False
        if int(row["player_disc_count"]) != board.count("X"):
            print(f"bad player count: {row}", file=sys.stderr)
            return False
        if int(row["opponent_disc_count"]) != board.count("O"):
            print(f"bad opponent count: {row}", file=sys.stderr)
            return False
        if int(row["empty_count"]) != board.count("-"):
            print(f"bad empty count: {row}", file=sys.stderr)
            return False
        if int(row["label_score_side_to_move"]) < -64 or int(row["label_score_side_to_move"]) > 64:
            print(f"unexpected label: {row}", file=sys.stderr)
            return False
        prefix = f"{DATASET_ID}-"
        if not row["position_id"].startswith(prefix):
            print(f"unexpected position id: {row['position_id']!r}", file=sys.stderr)
            return False
        game_prefix = row["position_id"][len(prefix) :].split("-", 1)[0]
        games_by_prefix.setdefault(game_prefix, set()).add(row["split"])
    if any(len(splits) != 1 for splits in games_by_prefix.values()):
        print("positions from one game did not stay in one split", file=sys.stderr)
        return False
    return True


def check_report(report: dict[str, Any], rows: list[dict[str, str]]) -> bool:
    expected = {
        "schema_version": 1,
        "importer_version": "egaroucid-sequence-v0",
        "source_dataset_id": DATASET_ID,
        "source_kind": "egaroucid-sequence-local",
        "input_kind": "sequence-transcript",
        "input_games": 4,
        "accepted_games": 3,
        "rejected_games": 1,
        "emitted_positions": len(rows),
        "invalid_move_count": 1,
        "terminal_count": 3,
    }
    for key, value in expected.items():
        if report.get(key) != value:
            print(f"report mismatch for {key}: {report.get(key)!r}", file=sys.stderr)
            return False
    if not isinstance(report.get("checksum"), str) or not report["checksum"].startswith("sha256:"):
        print(f"bad checksum: {report.get('checksum')!r}", file=sys.stderr)
        return False
    if not isinstance(report.get("pass_count"), int) or report["pass_count"] < 2:
        print(f"bad pass count: {report.get('pass_count')!r}", file=sys.stderr)
        return False
    notes = report.get("notes")
    if not isinstance(notes, list) or "not a teacher-search label" not in notes:
        print(f"missing notes: {notes!r}", file=sys.stderr)
        return False
    return True


def check_zip_and_directory(importer: Path, fixture: Path, manifest: Path, expected_rows: int) -> bool:
    fixture_text = fixture.read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory() as temp:
        temp_path = Path(temp)
        zip_path = temp_path / "Egaroucid_Train_Data_v0002_0.zip"
        with zipfile.ZipFile(zip_path, "w") as archive:
            archive.writestr("0002/10/0000000.txt", fixture_text)
        zip_report = temp_path / "zip-report.json"
        zip_result = run_importer(importer, zip_path, manifest, zip_report)
        zip_rows = parse_rows(zip_result.stdout) if zip_result.returncode == 0 else None
        zip_report_data = load_report(zip_report) if zip_result.returncode == 0 else None
        if (
            zip_result.returncode != 0
            or zip_rows is None
            or zip_report_data is None
            or len(zip_rows) != expected_rows
            or zip_report_data.get("accepted_games") != 3
        ):
            sys.stderr.write(zip_result.stderr)
            sys.stderr.write(zip_result.stdout)
            print("zip import did not produce the expected rows/report", file=sys.stderr)
            return False

        nested = temp_path / "extracted" / "0002" / "10"
        nested.mkdir(parents=True)
        (nested / "0000000.txt").write_text(fixture_text, encoding="utf-8")
        dir_report = temp_path / "dir-report.json"
        dir_result = run_importer(importer, temp_path / "extracted", manifest, dir_report)
        dir_rows = parse_rows(dir_result.stdout) if dir_result.returncode == 0 else None
        dir_report_data = load_report(dir_report) if dir_result.returncode == 0 else None
        if (
            dir_result.returncode != 0
            or dir_rows is None
            or dir_report_data is None
            or len(dir_rows) != expected_rows
            or dir_report_data.get("accepted_games") != 3
        ):
            sys.stderr.write(dir_result.stderr)
            sys.stderr.write(dir_result.stdout)
            print("directory import did not produce the expected rows/report", file=sys.stderr)
            return False
    return True


def check_manifest_validation(importer: Path, fixture: Path, manifest: Path) -> bool:
    with tempfile.TemporaryDirectory() as temp:
        temp_path = Path(temp)
        mismatch = temp_path / "mismatch.manifest.json"
        mismatch.write_text(
            manifest.read_text(encoding="utf-8").replace(DATASET_ID, "other-sequence", 1),
            encoding="utf-8",
        )
        result = run_importer(importer, fixture, mismatch, temp_path / "bad-report.json")
        if result.returncode == 0 or "does not match --dataset-id" not in result.stderr:
            print("manifest mismatch was not rejected", file=sys.stderr)
            sys.stderr.write(result.stderr)
            return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--importer", required=True, type=Path)
    parser.add_argument("--fixture", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--dataset-exe", required=True, type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as temp:
        temp_path = Path(temp)
        first_report_path = temp_path / "first-report.json"
        second_report_path = temp_path / "second-report.json"
        first = run_importer(args.importer, args.fixture, args.manifest, first_report_path)
        second = run_importer(args.importer, args.fixture, args.manifest, second_report_path)
        if first.returncode != 0:
            sys.stderr.write(first.stderr)
            sys.stderr.write(first.stdout)
            return 1
        if second.returncode != 0:
            sys.stderr.write(second.stderr)
            sys.stderr.write(second.stdout)
            return 1
        if first.stdout != second.stdout:
            print("import output is not deterministic", file=sys.stderr)
            return 1
        rows = parse_rows(first.stdout)
        first_report = load_report(first_report_path)
        second_report = load_report(second_report_path)
        if rows is None or first_report is None or second_report is None:
            return 1
        if first_report != second_report:
            print("report is not deterministic", file=sys.stderr)
            return 1
        if not check_rows(rows) or not check_report(first_report, rows):
            return 1

        normalized = temp_path / "sequence-normalized.tsv"
        dataset_report = temp_path / "dataset-report.json"
        normalized.write_text(first.stdout, encoding="utf-8")
        dataset = subprocess.run(
            [
                str(args.dataset_exe),
                "--normalized-tsv",
                str(normalized),
                "--report",
                str(dataset_report),
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        if dataset.returncode != 0:
            sys.stderr.write(dataset.stderr)
            sys.stderr.write(dataset.stdout)
            return 1
        if not dataset.stdout.strip():
            print("dataset builder emitted no pattern rows", file=sys.stderr)
            return 1

        if not check_zip_and_directory(args.importer, args.fixture, args.manifest, len(rows)):
            return 1
        if not check_manifest_validation(args.importer, args.fixture, args.manifest):
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
