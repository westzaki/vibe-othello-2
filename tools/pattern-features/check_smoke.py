#!/usr/bin/env python3
"""CTest wrapper for the tiny pattern feature extractor smoke CLI."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


EXPECTED_HEADER = "record_id\tply\tpattern_id\tinstance\tphase\tternary_index"
EXPECTED_REJECTS = (
    "tiny-bad-coordinate",
    "tiny-bad-illegal-move",
    "tiny-bad-pass",
    "tiny-bad-malformed-label",
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--records", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    args = parser.parse_args()

    result = subprocess.run(
        [
            str(args.exe),
            "--records",
            str(args.records),
            "--manifest",
            str(args.manifest),
        ],
        check=False,
        capture_output=True,
        text=True,
    )

    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return result.returncode

    lines = result.stdout.splitlines()
    if not lines or lines[0] != EXPECTED_HEADER:
        print("missing or invalid TSV header", file=sys.stderr)
        return 1

    data_lines = lines[1:]
    if not any("\tedge-8\t" in line for line in data_lines):
        print("missing edge-8 feature rows", file=sys.stderr)
        return 1
    if not any("\tcorner-3x3\t" in line for line in data_lines):
        print("missing corner-3x3 feature rows", file=sys.stderr)
        return 1
    if not any(line == "tiny-legal-prefix\t3\tcorner-3x3\t0\t0\t6561" for line in data_lines):
        print("missing deterministic representative ternary index", file=sys.stderr)
        return 1

    for record_id in EXPECTED_REJECTS:
        if any(line.startswith(record_id + "\t") for line in data_lines):
            print(f"unexpected feature row for rejected record: {record_id}", file=sys.stderr)
            return 1
        if record_id not in result.stderr:
            print(f"missing expected reject reason for {record_id}", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
