#!/usr/bin/env python3
"""Smoke-check Egaroucid board-score opening-overlap auditing."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

from import_egaroucid_sequences import ReplayHelper


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--auditor", required=True, type=Path)
    parser.add_argument("--replay-helper", required=True, type=Path)
    return parser.parse_args()


def write_archive(path: Path, rows: list[str]) -> str:
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("fixture/positions.txt", "\n".join(rows) + "\n")
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_manifest(path: Path, checksum: str) -> None:
    path.write_text(
        json.dumps(
            {
                "dataset_id": "synthetic-board-score-overlap-smoke",
                "sha256": checksum,
                "derived_weights_allowed": True,
            }
        )
        + "\n",
        encoding="utf-8",
    )


def run_audit(
    args: argparse.Namespace,
    archive: Path,
    manifest: Path,
    openings: Path,
    report: Path,
    *,
    require_zero: bool,
) -> subprocess.CompletedProcess[str]:
    command = [
        sys.executable,
        str(args.auditor),
        "--input",
        str(archive),
        "--manifest",
        str(manifest),
        "--openings",
        str(openings),
        "--replay-helper",
        str(args.replay_helper),
        "--report",
        str(report),
        "--opening-generation-seed",
        "7",
        "--opening-generation-plies",
        "2",
    ]
    if require_zero:
        command.append("--require-zero-overlap")
    return subprocess.run(command, check=False, capture_output=True, text=True)


def main() -> int:
    args = parse_args()
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        openings = root / "openings.txt"
        openings.write_text("smoke-opening: d3 c3\n", encoding="utf-8")
        with ReplayHelper(args.replay_helper) as helper:
            replay = helper.replay("smoke-opening", "d3 c3")
        assert replay.accepted and replay.snapshots
        opening_board = replay.snapshots[-1].board_text

        archive = root / "positions.zip"
        manifest = root / "manifest.json"
        report = root / "report.json"
        checksum = write_archive(
            archive,
            [
                f"{opening_board} 4",
                "---------------------------OX------XO--------------------------- 0",
            ],
        )
        write_manifest(manifest, checksum)

        overlap = run_audit(
            args, archive, manifest, openings, report, require_zero=False
        )
        assert overlap.returncode == 0, overlap.stderr
        payload = json.loads(report.read_text(encoding="utf-8"))
        assert payload["source_position_count"] == 2
        assert payload["overlap_with_training_boards"] == 1
        assert payload["overlap_occurrences"] == 1
        rejected = run_audit(
            args, archive, manifest, openings, report, require_zero=True
        )
        assert rejected.returncode == 2

        checksum = write_archive(
            archive,
            ["---------------------------OX------XO--------------------------- 0"],
        )
        write_manifest(manifest, checksum)
        clean = run_audit(
            args, archive, manifest, openings, report, require_zero=True
        )
        assert clean.returncode == 0, clean.stderr
        payload = json.loads(report.read_text(encoding="utf-8"))
        assert payload["overlap_with_training_boards"] == 0
    print("Egaroucid board-score opening overlap smoke: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
