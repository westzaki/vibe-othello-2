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


def run_cli(exe: Path, records: Path, manifest: Path, index_mode: str) -> subprocess.CompletedProcess[str]:
    command = [
        str(exe),
        "--records",
        str(records),
        "--manifest",
        str(manifest),
    ]
    if index_mode == "canonical":
        command.extend(["--index-mode", "canonical"])

    return subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
    )


def checked_lines(result: subprocess.CompletedProcess[str]) -> list[str] | None:
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return None

    lines = result.stdout.splitlines()
    if not lines or lines[0] != EXPECTED_HEADER:
        print("missing or invalid TSV header", file=sys.stderr)
        return None

    data_lines = lines[1:]
    if not any("\tedge-8\t" in line for line in data_lines):
        print("missing edge-8 feature rows", file=sys.stderr)
        return None
    if not any("\tcorner-3x3\t" in line for line in data_lines):
        print("missing corner-3x3 feature rows", file=sys.stderr)
        return None

    for record_id in EXPECTED_REJECTS:
        if any(line.startswith(record_id + "\t") for line in data_lines):
            print(f"unexpected feature row for rejected record: {record_id}", file=sys.stderr)
            return None
        if record_id not in result.stderr:
            print(f"missing expected reject reason for {record_id}", file=sys.stderr)
            return None

    return lines


def parse_rows(lines: list[str]) -> dict[tuple[str, ...], int] | None:
    rows: dict[tuple[str, ...], int] = {}
    for line in lines[1:]:
        fields = line.split("\t")
        if len(fields) != 6:
            print(f"invalid feature row: {line}", file=sys.stderr)
            return None
        key = tuple(fields[:5])
        if key in rows:
            print(f"duplicate feature key: {key}", file=sys.stderr)
            return None
        try:
            rows[key] = int(fields[5])
        except ValueError:
            print(f"invalid ternary index: {line}", file=sys.stderr)
            return None
    return rows


def check_canonical(args: argparse.Namespace, canonical_lines: list[str]) -> int:
    raw_result = run_cli(args.exe, args.records, args.manifest, "raw")
    raw_lines = checked_lines(raw_result)
    if raw_lines is None:
        return 1

    raw_rows = parse_rows(raw_lines)
    canonical_rows = parse_rows(canonical_lines)
    if raw_rows is None or canonical_rows is None:
        return 1
    if len(raw_rows) != len(canonical_rows):
        print(
            f"raw/canonical row count mismatch: {len(raw_rows)} != {len(canonical_rows)}",
            file=sys.stderr,
        )
        return 1
    if raw_rows.keys() != canonical_rows.keys():
        print("raw/canonical feature keys differ", file=sys.stderr)
        return 1

    changed_rows = 0
    corner_changed = False
    for key, raw_index in raw_rows.items():
        canonical_index = canonical_rows[key]
        if canonical_index > raw_index:
            print(
                f"canonical index is greater than raw for {key}: {canonical_index} > {raw_index}",
                file=sys.stderr,
            )
            return 1
        if canonical_index != raw_index:
            changed_rows += 1
            corner_changed = corner_changed or key[2] == "corner-3x3"

    if changed_rows == 0:
        print("canonical mode did not change any ternary_index values", file=sys.stderr)
        return 1
    if not corner_changed:
        print("canonical mode did not change a corner-3x3 representative row", file=sys.stderr)
        return 1

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--records", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--index-mode", choices=("raw", "canonical"), default="raw")
    args = parser.parse_args()

    result = run_cli(args.exe, args.records, args.manifest, args.index_mode)
    lines = checked_lines(result)
    if lines is None:
        return result.returncode if result.returncode != 0 else 1

    data_lines = lines[1:]
    if args.index_mode == "raw":
        if not any(line == "tiny-legal-prefix\t3\tcorner-3x3\t0\t0\t6561" for line in data_lines):
            print("missing deterministic representative ternary index", file=sys.stderr)
            return 1
        return 0

    return check_canonical(args, lines)


if __name__ == "__main__":
    raise SystemExit(main())
