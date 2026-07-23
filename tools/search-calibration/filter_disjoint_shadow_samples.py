#!/usr/bin/env python3
"""Remove holdout positions also observed in MPC training samples."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Iterable


POSITION_ID_FIELD = "canonical_position_hash"


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
                if POSITION_ID_FIELD not in value:
                    raise FilterError(
                        f"{path}:{line_number}: missing position identity field "
                        f"{POSITION_ID_FIELD}"
                    )
                rows.append((value, raw_line if raw_line.endswith("\n") else raw_line + "\n"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise FilterError(f"cannot read {path}: {error}") from error
    if not rows:
        raise FilterError(f"{path}: no samples")
    return rows


def position_identity(sample: dict[str, Any]) -> Any:
    return sample[POSITION_ID_FIELD]


def filter_disjoint(training: Path, holdout: Path, output: Path) -> dict[str, int]:
    if output.resolve() in {training.resolve(), holdout.resolve()}:
        raise FilterError("output must not overwrite an input")
    training_rows = read_rows(training)
    holdout_rows = read_rows(holdout)
    training_positions = {position_identity(row) for row, _ in training_rows}
    holdout_positions = {position_identity(row) for row, _ in holdout_rows}
    overlapping_positions = training_positions & holdout_positions
    retained = [
        raw for row, raw in holdout_rows if position_identity(row) not in training_positions
    ]
    if not retained:
        raise FilterError("filter would remove every holdout sample")
    try:
        with output.open("w", encoding="utf-8", newline="") as destination:
            destination.writelines(retained)
    except OSError as error:
        raise FilterError(f"cannot write {output}: {error}") from error
    return {
        "training_positions": len(training_positions),
        "holdout_positions_before": len(holdout_positions),
        "overlapping_positions_removed": len(overlapping_positions),
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
