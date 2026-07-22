#!/usr/bin/env python3
"""Verify the checked-in constexpr profile matches its reviewed TSV."""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exporter", type=Path, required=True)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--expected", type=Path, required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory) / "production_probcut_profile_data.inc"
        completed = subprocess.run(
            [
                sys.executable,
                str(args.exporter),
                str(args.profile),
                "--weights-checksum",
                "0xfe3d38f9",
                "--maximum-margin",
                "22",
                "--maximum-shallow-overhead-ratio",
                "0.005",
                "--output",
                str(output),
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.returncode != 0:
            raise AssertionError(completed.stderr)
        if output.read_bytes() != args.expected.read_bytes():
            raise AssertionError("checked-in constexpr profile is stale")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
