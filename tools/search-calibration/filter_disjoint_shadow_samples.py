#!/usr/bin/env python3
"""Remove holdout scheduler nodes also observed in MPC training samples."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Iterable


KEY_FIELDS = (
    "canonical_position_hash",
    "phase",
    "occupied_count",
    "empties",
    "ply",
    "search_role",
    "deep_depth",
    "official_alpha",
    "official_beta",
    "search_mode",
    "exact_handoff_enabled",
    "exact_handoff_threshold",
    "exact_handoff_distance",
)


class FilterError(ValueError):
    """Raised for malformed or unsafe filter inputs."""


def read_rows(path: Path) -> list[tuple[dict[str, Any], str]]:
    rows: list[tuple[dict[str, Any], str]] = []
    try:
        with path.open(encoding="utf-8") as source:
            for line_number, raw_line in enumerate(source, start=1):
                if not raw_line.strip():
                    continue
                value = json.loads(raw_line)
                if not isinstance(value, dict):
                    raise FilterError(f"{path}:{line_number}: sample must be an object")
                missing = [field for field in KEY_FIELDS if field not in value]
                if missing:
                    raise FilterError(
                        f"{path}:{line_number}: missing scheduler key field {missing[0]}"
                    )
                rows.append((value, raw_line if raw_line.endswith("\n") else raw_line + "\n"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise FilterError(f"cannot read {path}: {error}") from error
    if not rows:
        raise FilterError(f"{path}: no samples")
    return rows


def scheduler_key(sample: dict[str, Any]) -> tuple[Any, ...]:
    return tuple(sample[field] for field in KEY_FIELDS)


def filter_disjoint(training: Path, holdout: Path, output: Path) -> dict[str, int]:
    if output.resolve() in {training.resolve(), holdout.resolve()}:
        raise FilterError("output must not overwrite an input")
    training_rows = read_rows(training)
    holdout_rows = read_rows(holdout)
    training_keys = {scheduler_key(row) for row, _ in training_rows}
    overlapping_keys = {
        scheduler_key(row) for row, _ in holdout_rows if scheduler_key(row) in training_keys
    }
    retained = [raw for row, raw in holdout_rows if scheduler_key(row) not in training_keys]
    if not retained:
        raise FilterError("filter would remove every holdout sample")
    try:
        with output.open("w", encoding="utf-8", newline="") as destination:
            destination.writelines(retained)
    except OSError as error:
        raise FilterError(f"cannot write {output}: {error}") from error
    return {
        "training_nodes": len(training_keys),
        "holdout_nodes_before": len({scheduler_key(row) for row, _ in holdout_rows}),
        "overlapping_nodes_removed": len(overlapping_keys),
        "holdout_samples_before": len(holdout_rows),
        "holdout_samples_after": len(retained),
    }


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--training", type=Path, required=True)
    parser.add_argument("--holdout", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        summary = filter_disjoint(args.training, args.holdout, args.output)
    except FilterError as error:
        parser.error(str(error))
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
