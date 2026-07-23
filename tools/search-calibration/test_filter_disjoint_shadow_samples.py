#!/usr/bin/env python3
"""Smoke-test scheduler-node disjoint holdout filtering."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def sample(identity: str, pair_index: int) -> dict[str, object]:
    return {
        "canonical_position_hash": identity,
        "phase": 4,
        "occupied_count": 24,
        "empties": 40,
        "ply": 1,
        "search_role": "non_pv_scout",
        "deep_depth": 7,
        "official_alpha": 3,
        "official_beta": 4,
        "search_mode": "move",
        "exact_handoff_enabled": True,
        "exact_handoff_threshold": 8,
        "exact_handoff_distance": 32,
        "same_deep_pair_index": pair_index,
    }


def write_jsonl(path: Path, rows: list[dict[str, object]]) -> None:
    path.write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--filter", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        training = root / "training.jsonl"
        holdout = root / "holdout.jsonl"
        output = root / "disjoint.jsonl"
        write_jsonl(training, [sample("0000000000000001", 0), sample("0000000000000001", 1)])
        write_jsonl(
            holdout,
            [
                {**sample("0000000000000001", 0), "ply": 9, "official_beta": 12},
                {**sample("0000000000000001", 1), "ply": 9, "official_beta": 12},
                sample("0000000000000002", 0),
                sample("0000000000000002", 1),
            ],
        )
        completed = subprocess.run(
            [
                sys.executable,
                str(args.filter),
                "--training",
                str(training),
                "--holdout",
                str(holdout),
                "--output",
                str(output),
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.returncode != 0:
            raise AssertionError(completed.stderr)
        summary = json.loads(completed.stdout)
        if summary["overlapping_positions_removed"] != 1:
            raise AssertionError(summary)
        retained = [json.loads(line) for line in output.read_text(encoding="utf-8").splitlines()]
        if len(retained) != 2 or {row["canonical_position_hash"] for row in retained} != {
            "0000000000000002"
        }:
            raise AssertionError(retained)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
