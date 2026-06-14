#!/usr/bin/env python3
"""CTest wrapper for the tiny pattern dataset builder smoke CLI."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


EXPECTED_HEADER = (
    "record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_id\tinstance\tternary_index"
)
EXPECTED_REJECTS = (
    "tiny-bad-coordinate",
    "tiny-bad-illegal-move",
    "tiny-bad-pass",
    "tiny-bad-malformed-label",
)
EXPECTED_ACCEPTS = ("tiny-legal-prefix", "tiny-alt-prefix")
EXPECTED_REPRESENTATIVE_ROW = (
    "tiny-legal-prefix\t3\ttest\t3\t0\tcorner-3x3\t0\t6561"
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
            "--split-policy",
            "tiny-cycle",
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
    if len(data_lines) != 48:
        print(f"unexpected dataset row count: {len(data_lines)}", file=sys.stderr)
        return 1

    for split in ("train", "validation", "test"):
        if not any(line.split("\t")[2] == split for line in data_lines):
            print(f"missing deterministic split: {split}", file=sys.stderr)
            return 1

    if not any("\tedge-8\t" in line for line in data_lines):
        print("missing edge-8 dataset rows", file=sys.stderr)
        return 1
    if not any("\tcorner-3x3\t" in line for line in data_lines):
        print("missing corner-3x3 dataset rows", file=sys.stderr)
        return 1
    if EXPECTED_REPRESENTATIVE_ROW not in data_lines:
        print("missing deterministic representative dataset row", file=sys.stderr)
        return 1

    for record_id in EXPECTED_ACCEPTS:
        if not any(line.startswith(record_id + "\t") for line in data_lines):
            print(f"missing expected-good dataset rows: {record_id}", file=sys.stderr)
            return 1

    for record_id in EXPECTED_REJECTS:
        if any(line.startswith(record_id + "\t") for line in data_lines):
            print(f"unexpected dataset row for rejected record: {record_id}", file=sys.stderr)
            return 1
        if record_id not in result.stderr:
            print(f"missing expected reject reason for {record_id}", file=sys.stderr)
            return 1

    expected_summary_parts = (
        "summary total_records=6 accepted_records=2 rejected_records=4 emitted_rows=48",
        "train_rows=16 validation_rows=16 test_rows=16",
        "split_policy=tiny-cycle duplicate_policy=keep_all_input_order",
    )
    for part in expected_summary_parts:
        if part not in result.stderr:
            print(f"missing summary part: {part}", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
