#!/usr/bin/env python3
"""Smoke-test the native shadow calibration JSONL collector."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--collector", type=Path, required=True)
    parser.add_argument("--analyzer", type=Path, required=True)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--positions", type=Path, required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        samples = root / "samples.jsonl"
        command = [
            str(args.collector),
            "--artifact-manifest",
            str(args.artifact),
            "--positions",
            str(args.positions),
            "--output",
            str(samples),
            "--repository-sha",
            "0123456789abcdef",
            "--search-config-id",
            "collector-smoke-full-depth4-v1",
            "--depth",
            "4",
            "--exact-endgame-empties",
            "0",
            "--depth-pair",
            "3:1",
            "--depth-pair",
            "3:2",
            "--max-samples-per-search",
            "2",
            "--root-phase",
            "0",
            "--root-phase",
            "1",
            "--position-limit-per-phase",
            "1",
        ]
        completed = subprocess.run(command, check=False, capture_output=True, text=True)
        if completed.returncode != 0:
            raise AssertionError(
                f"collector exited {completed.returncode}\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
            )
        summary = json.loads(completed.stdout)
        if summary["selected_rows"] != 2 or summary["samples"] <= 0:
            raise AssertionError(f"collector produced no usable samples: {summary!r}")

        report = root / "report.json"
        analyzed = subprocess.run(
            [
                sys.executable,
                str(args.analyzer),
                str(samples),
                "--minimum-exact-pairs",
                "2",
                "--json-output",
                str(report),
                "--markdown-output",
                str(root / "report.md"),
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        if analyzed.returncode != 0:
            raise AssertionError(
                f"analyzer rejected collector output\nstdout:\n{analyzed.stdout}\nstderr:\n{analyzed.stderr}"
            )
        payload = json.loads(report.read_text(encoding="utf-8"))
        if payload["schema_version"] != "mpc-shadow-calibration-report-v6":
            raise AssertionError(f"unexpected report schema: {payload!r}")
        if payload["sample_count"] != summary["samples"]:
            raise AssertionError(f"collector/analyzer sample count mismatch: {payload!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
