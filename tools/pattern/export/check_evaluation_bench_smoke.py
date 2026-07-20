#!/usr/bin/env python3
"""CTest wrapper for fixed-position learned pattern evaluation smoke."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

from smoke_pipeline import (
    add_bench_smoke_arguments,
    canonical_pattern_set_id,
    run_deterministic_bench_smoke,
    smoke_source_for,
)


def check_report(
    report_path: Path,
    first_summary: dict[str, str],
    _v0a_export: dict[str, str],
    _v0b_export: dict[str, str],
    pattern_set: str,
) -> bool:
    try:
        report: dict[str, Any] = json.loads(report_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"could not read evaluation report JSON: {error}", file=sys.stderr)
        return False

    expected_notes = [
        "local smoke only",
        "not production benchmark",
        "synthetic artifacts are temp-only",
        "publication remains gated / unknown",
    ]
    expected_fields: dict[str, Any] = {
        "schema_version": 1,
        "source": smoke_source_for(pattern_set, search=False),
        "pattern_set_id": canonical_pattern_set_id(pattern_set),
        "phase_count": 13,
        "notes": expected_notes,
    }
    for key, expected in expected_fields.items():
        if report.get(key) != expected:
            print(f"report field mismatch for {key}: {report.get(key)!r}", file=sys.stderr)
            return False

    score_rows = report.get("score_rows")
    if not isinstance(score_rows, list) or not score_rows:
        print("report score_rows must be a non-empty array", file=sys.stderr)
        return False
    if report.get("positions_count") != len(score_rows):
        print("report positions_count does not match score_rows", file=sys.stderr)
        return False
    if str(report.get("positions_count")) != first_summary.get("positions_count"):
        print("summary positions_count does not match report", file=sys.stderr)
        return False
    if report.get("v0a_v0b_different_count", 0) <= 0:
        print("report did not record any v0a/v0b score differences", file=sys.stderr)
        return False
    if str(report.get("v0a_v0b_different_count")) != first_summary.get(
        "v0a_v0b_different_count"
    ):
        print("summary difference count does not match report", file=sys.stderr)
        return False
    checksum = report.get("checksum")
    if not isinstance(checksum, str) or not checksum.startswith("0x"):
        print(f"report checksum is invalid: {checksum!r}", file=sys.stderr)
        return False
    if checksum != first_summary.get("checksum"):
        print("summary checksum does not match report", file=sys.stderr)
        return False

    required_row_fields = {
        "position_id",
        "disc_count",
        "phase",
        "v0a_score",
        "v0b_score",
        "score_delta",
    }
    for row in score_rows:
        if not isinstance(row, dict) or set(row) != required_row_fields:
            print(f"unexpected score row shape: {row!r}", file=sys.stderr)
            return False
        if row["score_delta"] != row["v0b_score"] - row["v0a_score"]:
            print(f"score_delta mismatch: {row!r}", file=sys.stderr)
            return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    add_bench_smoke_arguments(parser)
    args = parser.parse_args()
    return (
        0
        if run_deterministic_bench_smoke(
            args,
            report_stem="fixed-position-report",
            label="evaluation",
            check_report=check_report,
        )
        else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
