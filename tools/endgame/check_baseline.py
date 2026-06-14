#!/usr/bin/env python3
"""Sanity-check checked-in exact endgame benchmark baseline JSON."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any

TOOLS_BENCHMARKS = Path(__file__).resolve().parents[1] / "benchmarks"
sys.path.insert(0, str(TOOLS_BENCHMARKS))

import baseline_common


TOP_LEVEL_FIELDS = (
    "schema_version",
    "benchmark",
    "measured_commit",
    "measured_revision",
    "command",
    "corpus",
    "results",
)

RESULT_FIELDS = (
    "position_id",
    "score",
    "best_move",
    "nodes",
    "endgame_nodes",
)


def check_baseline(data: dict[str, Any]) -> list[str]:
    errors = baseline_common.check_common_envelope(data, required_fields=TOP_LEVEL_FIELDS)
    baseline_common.require_string(data, "benchmark", "baseline", errors)
    baseline_common.require_string(data, "measured_commit", "baseline", errors)
    baseline_common.require_string(data, "measured_revision", "baseline", errors)
    baseline_common.require_string(data, "command", "baseline", errors)
    baseline_common.require_string(data, "corpus", "baseline", errors)

    results = data.get("results")
    if not isinstance(results, list) or not results:
        errors.append("results: expected non-empty array")
        return errors

    seen_positions: set[str] = set()
    for index, result in enumerate(results):
        label = f"results[{index}]"
        if not isinstance(result, dict):
            errors.append(f"{label}: expected object")
            continue

        for field in RESULT_FIELDS:
            if field not in result:
                errors.append(f"{label}.{field}: missing required field")

        baseline_common.require_string(result, "position_id", label, errors)
        baseline_common.require_string(result, "best_move", label, errors)
        baseline_common.require_int(result, "score", label, errors)
        baseline_common.require_int(result, "nodes", label, errors)
        baseline_common.require_int(result, "endgame_nodes", label, errors)

        position_id = result.get("position_id")
        if isinstance(position_id, str):
            if position_id in seen_positions:
                errors.append(f"{label}.position_id: duplicate {position_id!r}")
            seen_positions.add(position_id)

    return errors


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Sanity-check checked-in exact endgame benchmark baseline JSON."
    )
    parser.add_argument("baseline_json", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    errors = check_baseline(baseline_common.load_json(args.baseline_json))
    if errors:
        print("endgame baseline schema check failed:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1

    print(f"endgame baseline schema ok: {args.baseline_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
