#!/usr/bin/env python3
"""Sanity-check checked-in exact endgame benchmark baseline JSON."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


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


def load_json(path: Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as input_file:
            data = json.load(input_file)
    except json.JSONDecodeError as error:
        raise SystemExit(f"{path}: invalid JSON: {error}") from error

    if not isinstance(data, dict):
        raise SystemExit(f"{path}: expected a JSON object")
    return data


def require_string(record: dict[str, Any], field: str, label: str, errors: list[str]) -> None:
    value = record.get(field)
    if not isinstance(value, str) or not value:
        errors.append(f"{label}.{field}: expected non-empty string")


def require_int(record: dict[str, Any], field: str, label: str, errors: list[str]) -> None:
    value = record.get(field)
    if not isinstance(value, int):
        errors.append(f"{label}.{field}: expected integer")


def check_baseline(data: dict[str, Any]) -> list[str]:
    errors: list[str] = []

    for field in TOP_LEVEL_FIELDS:
        if field not in data:
            errors.append(f"{field}: missing required field")

    if data.get("schema_version") != 1:
        errors.append("schema_version: expected 1")
    require_string(data, "benchmark", "baseline", errors)
    require_string(data, "measured_commit", "baseline", errors)
    require_string(data, "measured_revision", "baseline", errors)
    require_string(data, "command", "baseline", errors)
    require_string(data, "corpus", "baseline", errors)

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

        require_string(result, "position_id", label, errors)
        require_string(result, "best_move", label, errors)
        require_int(result, "score", label, errors)
        require_int(result, "nodes", label, errors)
        require_int(result, "endgame_nodes", label, errors)

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
    errors = check_baseline(load_json(args.baseline_json))
    if errors:
        print("endgame baseline schema check failed:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1

    print(f"endgame baseline schema ok: {args.baseline_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
