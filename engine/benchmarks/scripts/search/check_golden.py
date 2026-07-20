#!/usr/bin/env python3
"""Compare deterministic search_bench JSONL fields against a golden file."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "common"))

from golden_common import GoldenSpec, run  # noqa: E402


def record_sort_key(record: dict[str, Any]) -> tuple[str, int, str, str]:
    return (
        str(record.get("position_id", "")),
        int(record.get("depth", -1)) if isinstance(record.get("depth"), int) else -1,
        str(record.get("mode", "")),
        str(record.get("tt_mode", "")),
    )


def record_label(record: dict[str, Any], index: int) -> str:
    return (
        f"record {index + 1} position={record.get('position_id', '<missing-position>')} "
        f"depth={record.get('depth', '<missing-depth>')} "
        f"mode={record.get('mode', '<missing-mode>')} "
        f"tt={record.get('tt_mode', '<missing-tt-mode>')}"
    )


SPEC = GoldenSpec(
    label="search",
    description="Compare deterministic search_bench JSONL fields against a golden file.",
    top_level_fields=(
        "position_id",
        "category",
        "mode",
        "variant_id",
        "tt_mode",
        "depth",
        "evaluator",
        "pvs",
        "aspiration",
        "history",
        "killers",
        "iid",
        "midgame_mobility",
        "score",
        "score_kind",
        "best_move",
        "pv",
    ),
    record_sort_key=record_sort_key,
    record_label=record_label,
)


if __name__ == "__main__":
    raise SystemExit(run(SPEC))
