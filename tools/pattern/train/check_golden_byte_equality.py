#!/usr/bin/env python3
"""Byte-equality golden checks for train_pattern.py outputs."""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path


MODES = (
    "phase-bias-v0a",
    "pattern-sgd-v0b",
    "pattern-sgd-v0c",
    "pattern-sgd-v0d",
)
COMMON_ARGS = (
    "--epochs",
    "4",
    "--learning-rate",
    "0.07",
    "--l2",
    "0.01",
    "--seed",
    "11",
    "--pattern-set-id",
    "golden-pattern-set",
    "--pattern-contract-digest",
    "sha256:golden-pattern-contract",
    "--index-mode",
    "raw",
)


def expected_name(mode: str, kind: str) -> str:
    extension = "tsv" if mode == "phase-bias-v0a" and kind == "weights" else "json"
    return f"{mode}.{kind}.{extension}"


def first_difference(left: bytes, right: bytes) -> str:
    for index, (left_byte, right_byte) in enumerate(zip(left, right)):
        if left_byte != right_byte:
            return (
                f"first differing byte at offset {index}: "
                f"actual=0x{left_byte:02x} expected=0x{right_byte:02x}"
            )
    if len(left) != len(right):
        return f"length differs: actual={len(left)} expected={len(right)}"
    return "bytes differ"


def compare_bytes(actual: Path, expected: Path) -> bool:
    actual_bytes = actual.read_bytes()
    expected_bytes = expected.read_bytes()
    if actual_bytes == expected_bytes:
        return True
    print(
        f"golden mismatch for {expected.name}: {first_difference(actual_bytes, expected_bytes)}",
        file=sys.stderr,
    )
    print(f"actual output: {actual}", file=sys.stderr)
    print(f"expected output: {expected}", file=sys.stderr)
    return False


def run_mode(trainer: Path, dataset: Path, expected_dir: Path, temp_dir: Path, mode: str) -> bool:
    weights_path = temp_dir / expected_name(mode, "weights")
    report_path = temp_dir / expected_name(mode, "report")
    result = subprocess.run(
        [
            sys.executable,
            str(trainer),
            "--dataset",
            str(dataset),
            "--mode",
            mode,
            "--weights-out",
            str(weights_path),
            "--report-out",
            str(report_path),
            *COMMON_ARGS,
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return False
    return compare_bytes(weights_path, expected_dir / expected_name(mode, "weights")) and compare_bytes(
        report_path, expected_dir / expected_name(mode, "report")
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trainer", required=True, type=Path)
    args = parser.parse_args()

    fixture_dir = Path(__file__).resolve().parent / "golden_train_pattern"
    dataset = fixture_dir / "dataset.tsv"
    expected_dir = fixture_dir / "expected"
    with tempfile.TemporaryDirectory() as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        for mode in MODES:
            if not run_mode(args.trainer, dataset, expected_dir, temp_dir, mode):
                return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
