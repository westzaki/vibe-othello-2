#!/usr/bin/env python3
"""Check WTHOR opening overlap detection and the zero-overlap gate."""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


VALID_GAME = (
    "d3 c3 b3 e3 f3 c5 f6 g2 b5 c6 f4 a5 h1 f5 d6 e7 d7 e6 d8 c4 "
    "c7 b7 a8 b6 a4 f8 g4 b4 e8 a3 a7 g5 g8 c2 h4 g3 a2 h3 c1 d1 "
    "d2 e1 f1 f7 a6 h6 e2 b8 g7 c8 h5 g6 h2 h7 h8 g1 b2 f2 a1 b1"
)


def wthor_move(token: str) -> int:
    return (int(token[1]) * 10) + (ord(token[0]) - ord("a") + 1)


def write_fixture(path: Path) -> None:
    values = [wthor_move(token) for token in VALID_GAME.split()]
    record = struct.pack("<HHHBB60B", 1, 2, 3, 40, 40, *values)
    header = struct.pack("<4BIHH4B", 20, 26, 2, 24, 1, 0, 2025, 8, 0, 24, 0)
    path.write_bytes(header + record)


def run_audit(
    auditor: Path,
    replay_helper: Path,
    fixture: Path,
    openings: Path,
    report: Path,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(auditor),
            "--input",
            str(fixture),
            "--openings",
            str(openings),
            "--replay-helper",
            str(replay_helper),
            "--report",
            str(report),
            "--opening-generation-seed",
            "20260727",
            "--opening-generation-plies",
            "16",
            "--require-zero-overlap",
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--auditor", required=True, type=Path)
    parser.add_argument("--generator", required=True, type=Path)
    parser.add_argument("--replay-helper", required=True, type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="vibe-othello-wthor-overlap-") as temp:
        root = Path(temp)
        fixture = root / "fixture.wtb"
        write_fixture(fixture)

        leaked = root / "leaked.txt"
        leaked.write_text(
            "leaked: " + " ".join(VALID_GAME.split()[:16]) + "\n",
            encoding="utf-8",
        )
        leaked_report = root / "leaked-report.json"
        leaked_result = run_audit(
            args.auditor,
            args.replay_helper,
            fixture,
            leaked,
            leaked_report,
        )
        if leaked_result.returncode != 2:
            sys.stderr.write(leaked_result.stdout)
            sys.stderr.write(leaked_result.stderr)
            raise RuntimeError("known WTHOR opening did not fail the zero-overlap gate")
        leaked_payload = json.loads(leaked_report.read_text(encoding="utf-8"))
        if (
            leaked_payload.get("overlap_with_wthor_training_boards") != 1
            or leaked_payload.get("overlap_with_wthor_training_games") != 1
        ):
            raise RuntimeError(f"known overlap was not reported: {leaked_payload!r}")

        independent = root / "independent.txt"
        generator_report = root / "generator-report.json"
        generated = subprocess.run(
            [
                str(args.generator),
                "--output",
                str(independent),
                "--report-out",
                str(generator_report),
                "--count",
                "32",
                "--plies",
                "16",
                "--seed",
                "20260727",
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        if generated.returncode != 0:
            sys.stderr.write(generated.stdout)
            sys.stderr.write(generated.stderr)
            raise RuntimeError("independent opening generation failed")
        independent_report = root / "independent-report.json"
        independent_result = run_audit(
            args.auditor,
            args.replay_helper,
            fixture,
            independent,
            independent_report,
        )
        if independent_result.returncode != 0:
            sys.stderr.write(independent_result.stdout)
            sys.stderr.write(independent_result.stderr)
            raise RuntimeError("independent opening suite failed the zero-overlap gate")
        independent_payload = json.loads(independent_report.read_text(encoding="utf-8"))
        if (
            independent_payload.get("overlap_with_wthor_training_boards") != 0
            or independent_payload.get("overlap_with_wthor_training_games") != 0
        ):
            raise RuntimeError(f"unexpected independent overlap: {independent_payload!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
