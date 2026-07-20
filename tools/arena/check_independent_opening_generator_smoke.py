#!/usr/bin/env python3
"""Check deterministic independent opening generation."""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path


def generate(exe: Path, root: Path, name: str, seed: int) -> tuple[bytes, dict[str, object]]:
    openings = root / f"{name}.txt"
    report = root / f"{name}.json"
    completed = subprocess.run(
        [
            str(exe),
            "--output",
            str(openings),
            "--report-out",
            str(report),
            "--count",
            "64",
            "--plies",
            "16",
            "--seed",
            str(seed),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"generator failed ({completed.returncode})\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    return openings.read_bytes(), json.loads(report.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="vibe-othello-independent-openings-") as temp:
        root = Path(temp)
        first_bytes, first = generate(args.exe, root, "first", 20260727)
        repeat_bytes, repeat = generate(args.exe, root, "repeat", 20260727)
        other_bytes, _other = generate(args.exe, root, "other", 20260728)

        if first_bytes != repeat_bytes or first["opening_checksum"] != repeat["opening_checksum"]:
            raise RuntimeError("same seed did not reproduce byte-identical openings")
        if first_bytes == other_bytes:
            raise RuntimeError("different seeds produced identical opening suites")
        if first.get("generator_version") != "board-core-uniform-legal-v1":
            raise RuntimeError("generator version is missing")
        if first.get("opening_pairs") != 64 or first.get("opening_plies") != 16:
            raise RuntimeError("generator report counts are incorrect")
        lines = first_bytes.decode("utf-8").splitlines()
        if len(lines) != 64:
            raise RuntimeError(f"expected 64 openings, got {len(lines)}")
        for line in lines:
            _identifier, separator, transcript = line.partition(": ")
            if separator != ": " or len(transcript.split()) != 16:
                raise RuntimeError(f"invalid opening line: {line!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
