#!/usr/bin/env python3
"""Smoke checks for the Egaroucid board-score importer."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from typing import Any


DATASET_ID = "egaroucid-train-data-board-score-v2025-02-02"
HEADER = [
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
BOARD_ROWS = [
    "---------------------------OX------XO--------------------------- 8",
    "-------------------O-------OO------OX--------------------------- 7",
    "------------------OX-------OX------XO--------------------------- 5",
    "------------------XO------OOO------OX--------------------------- -9",
    "--------------------------OOO------OX--------------------------- -3",
    "OOOO-XOOOOXXXOOOOXOXOXOOOOXOOXOOOXOOOXOOOOXOOOOOO-XXXOOO--XXXOOO 4",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--importer", required=True, type=Path)
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_archive(path: Path, *, reverse: bool) -> None:
    members = [
        (
            "board-score/b.txt",
            "\n".join([BOARD_ROWS[3], BOARD_ROWS[4], BOARD_ROWS[0], "invalid row"]) + "\n",
        ),
        (
            "board-score/a.txt",
            "\n".join([BOARD_ROWS[0], BOARD_ROWS[1], BOARD_ROWS[2], BOARD_ROWS[5]]) + "\n",
        ),
    ]
    if reverse:
        members.reverse()
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, text in members:
            archive.writestr(name, text)
        archive.writestr("README_EN.md", "synthetic smoke fixture\n")


def write_manifest(path: Path, archive: Path, *, checksum: str | None = None) -> None:
    manifest: dict[str, Any] = {
        "dataset_id": DATASET_ID,
        "source_name": "synthetic Egaroucid board-score smoke fixture",
        "source_url": "local://synthetic-egaroucid-board-score-smoke",
        "retrieved_at": "2026-01-01",
        "license_or_terms": "repo-owned synthetic fixture",
        "redistribution_allowed": True,
        "commercial_use_allowed": True,
        "derived_weights_allowed": True,
        "required_attribution": "none",
        "local_path": archive.name,
        "sha256": checksum or sha256_file(archive),
        "notes": "Generated at smoke-test runtime.",
    }
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def run_import(
    importer: Path,
    archive: Path,
    manifest: Path,
    output: Path,
    report: Path,
    *extra: str,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(importer),
            "--input",
            str(archive),
            "--manifest",
            str(manifest),
            "--output",
            str(output),
            "--report",
            str(report),
            "--max-positions-per-phase",
            "2",
            "--seed",
            "17",
            "--progress-every-rows",
            "100",
            *extra,
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if reader.fieldnames != HEADER:
            raise AssertionError(f"unexpected normalized header: {reader.fieldnames}")
        return list(reader)


def check_rows(rows: list[dict[str, str]]) -> None:
    if len(rows) != 3:
        raise AssertionError(f"expected two phase-0 rows and one phase-12 row, got {len(rows)}")
    if {row["source_dataset_id"] for row in rows} != {DATASET_ID}:
        raise AssertionError("unexpected source_dataset_id")
    if {row["label_kind"] for row in rows} != {"teacher_value_disc_diff"}:
        raise AssertionError("unexpected label_kind")
    if {row["label_unit"] for row in rows} != {"disc"}:
        raise AssertionError("unexpected label_unit")
    if {row["label_perspective"] for row in rows} != {"side_to_move"}:
        raise AssertionError("unexpected label_perspective")
    if {int(row["phase"]) for row in rows} != {0, 12}:
        raise AssertionError("unexpected phase coverage")
    splits_by_board: dict[str, set[str]] = {}
    for row in rows:
        board = row["board_a1_to_h8"]
        if len(board) != 64 or set(board).difference({"X", "O", "-"}):
            raise AssertionError(f"invalid board: {board!r}")
        if int(row["occupied_count"]) != board.count("X") + board.count("O"):
            raise AssertionError("occupied_count mismatch")
        splits_by_board.setdefault(row["board_id"], set()).add(row["split"])
    if any(len(splits) != 1 for splits in splits_by_board.values()):
        raise AssertionError("same board crossed splits")


def main() -> int:
    args = parse_args()
    try:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive_a = root / "fixture-a.zip"
            archive_b = root / "fixture-b.zip"
            manifest_a = root / "manifest-a.json"
            manifest_b = root / "manifest-b.json"
            write_archive(archive_a, reverse=False)
            write_archive(archive_b, reverse=True)
            write_manifest(manifest_a, archive_a)
            write_manifest(manifest_b, archive_b)

            output_a = root / "normalized-a.tsv"
            output_b = root / "normalized-b.tsv"
            report_a = root / "report-a.json"
            report_b = root / "report-b.json"
            result_a = run_import(
                args.importer, archive_a, manifest_a, output_a, report_a
            )
            result_b = run_import(
                args.importer, archive_b, manifest_b, output_b, report_b
            )
            for result in (result_a, result_b):
                if result.returncode != 0:
                    raise AssertionError(result.stderr)
            if output_a.read_bytes() != output_b.read_bytes():
                raise AssertionError("member insertion order changed normalized output")

            rows = load_rows(output_a)
            check_rows(rows)
            report = json.loads(report_a.read_text(encoding="utf-8"))
            expected = {
                "schema_version": 2,
                "importer_version": "egaroucid-board-score-v1",
                "source_dataset_id": DATASET_ID,
                "sampling_mode": "full-scan-phase-topk",
                "source_scan_complete": True,
                "source_files_seen": 2,
                "source_files_processed": 2,
                "input_rows": 8,
                "valid_rows": 7,
                "rejected_rows": 1,
                "emitted_positions": 3,
            }
            for key, value in expected.items():
                if report.get(key) != value:
                    raise AssertionError(f"report mismatch {key}: {report.get(key)!r}")
            if report.get("candidate_counts_by_label_generation") != {
                "enumerated_static_eval_negamax": 6,
                "selfplay_terminal_outcome": 1,
            }:
                raise AssertionError("candidate label-generation counts are incorrect")
            if report.get("counts_by_label_generation") != {
                "enumerated_static_eval_negamax": 2,
                "selfplay_terminal_outcome": 1,
            }:
                raise AssertionError("emitted label-generation counts are incorrect")
            generation = report.get("label_generation_by_occupied_range")
            if not isinstance(generation, list) or [
                item.get("engine") for item in generation
            ] != [
                "Egaroucid for Console 7.4.0 lv17",
                "Egaroucid for Console 7.5.1 lv17",
            ]:
                raise AssertionError("label-generation provenance is incorrect")
            serialized_report = report_a.read_text(encoding="utf-8")
            if str(root) in serialized_report:
                raise AssertionError("report leaked an absolute temporary path")

            bounded_output = root / "bounded.tsv"
            bounded_report = root / "bounded.json"
            bounded = run_import(
                args.importer,
                archive_a,
                manifest_a,
                bounded_output,
                bounded_report,
                "--max-source-files",
                "1",
            )
            if bounded.returncode != 0:
                raise AssertionError(bounded.stderr)
            bounded_data = json.loads(bounded_report.read_text(encoding="utf-8"))
            if bounded_data.get("source_scan_complete") is not False:
                raise AssertionError("bounded scan was not marked incomplete")
            if bounded_data.get("sampling_mode") != "bounded-phase-topk":
                raise AssertionError("unexpected bounded sampling mode")

            wrong_manifest = root / "wrong-manifest.json"
            write_manifest(wrong_manifest, archive_a, checksum="0" * 64)
            mismatch = run_import(
                args.importer,
                archive_a,
                wrong_manifest,
                root / "wrong.tsv",
                root / "wrong.json",
            )
            if (
                mismatch.returncode == 0
                or "source checksum does not match manifest" not in mismatch.stderr
            ):
                raise AssertionError("checksum mismatch was not rejected")
    except (AssertionError, OSError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 1

    print("egaroucid board-score importer smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
