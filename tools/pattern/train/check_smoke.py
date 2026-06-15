#!/usr/bin/env python3
"""CTest wrapper for the tiny pattern trainer smoke CLI."""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path


EXPECTED_SUMMARY = {
    "model": "phase-bias-baseline",
    "input_rows": "48",
    "train_rows": "16",
    "validation_rows": "16",
    "test_rows": "16",
    "phases_seen": "1",
    "phase_bias[0]": "3",
    "checksum": "0x3b76cde99d45fa8f",
}


def parse_summary(text: str) -> dict[str, str]:
    summary: dict[str, str] = {}
    for line in text.splitlines():
        key, separator, value = line.partition("=")
        if not separator:
            raise ValueError(f"summary line is missing '=': {line}")
        if key in summary:
            raise ValueError(f"duplicate summary key: {key}")
        summary[key] = value
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trainer-exe", required=True, type=Path)
    parser.add_argument("--dataset-exe", required=True, type=Path)
    parser.add_argument("--records", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        dataset_path = temp_dir / "pattern-dataset.tsv"

        dataset_result = subprocess.run(
            [
                str(args.dataset_exe),
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
        if dataset_result.returncode != 0:
            sys.stderr.write(dataset_result.stderr)
            sys.stderr.write(dataset_result.stdout)
            return dataset_result.returncode
        dataset_path.write_text(dataset_result.stdout, encoding="utf-8")

        trainer_result = subprocess.run(
            [str(args.trainer_exe), "--dataset", str(dataset_path)],
            check=False,
            capture_output=True,
            text=True,
        )
        if trainer_result.returncode != 0:
            sys.stderr.write(trainer_result.stderr)
            sys.stderr.write(trainer_result.stdout)
            return trainer_result.returncode

        try:
            summary = parse_summary(trainer_result.stdout)
        except ValueError as error:
            print(error, file=sys.stderr)
            return 1

        if summary != EXPECTED_SUMMARY:
            print(f"unexpected trainer summary: {summary}", file=sys.stderr)
            return 1

        malformed_path = temp_dir / "malformed.tsv"
        malformed_path.write_text(
            "record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_id\tinstance\tternary_index\n"
            "tiny-bad\t1\ttrain\t3\n",
            encoding="utf-8",
        )
        malformed_result = subprocess.run(
            [str(args.trainer_exe), "--dataset", str(malformed_path)],
            check=False,
            capture_output=True,
            text=True,
        )
        if malformed_result.returncode == 0:
            print("malformed dataset unexpectedly succeeded", file=sys.stderr)
            return 1
        if "line 2: expected 8 TSV fields" not in malformed_result.stderr:
            print("malformed dataset did not report the expected error", file=sys.stderr)
            sys.stderr.write(malformed_result.stderr)
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
