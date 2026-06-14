#!/usr/bin/env python3
"""Aggregate exact endgame benchmark JSONL output."""

from __future__ import annotations

import argparse
import json
import math
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any


DEFAULT_GROUP_BY = ("empties", "parity_ordering", "tt_mode", "root_mode")
PERCENTILE_FIELDS = ("elapsed_ms", "nodes", "endgame_nodes", "nps")
REQUIRED_FIELDS = (
    *DEFAULT_GROUP_BY,
    *PERCENTILE_FIELDS,
    "tt_probes",
    "tt_hits",
    "tt_cutoffs",
)
PERCENTILES = (("p50", 0.50), ("p90", 0.90), ("p95", 0.95))


def fail(message: str) -> None:
    raise SystemExit(message)


def load_records(paths: list[Path]) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for path in paths:
        with path.open("r", encoding="utf-8") as input_file:
            for line_number, line in enumerate(input_file, start=1):
                stripped = line.strip()
                if not stripped:
                    continue
                try:
                    record = json.loads(stripped)
                except json.JSONDecodeError as error:
                    fail(f"{path}:{line_number}: invalid JSON: {error}")
                if not isinstance(record, dict):
                    fail(f"{path}:{line_number}: expected a JSON object")
                records.append(record)
    return records


def parse_group_by(text: str) -> tuple[str, ...]:
    fields = tuple(field.strip() for field in text.split(",") if field.strip())
    if not fields:
        fail("--group-by must include at least one field")
    return fields


def require_number(record: dict[str, Any], field: str, label: str) -> int | float:
    value = record.get(field)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        fail(f"{label}: {field}: expected number")
    return value


def validate_record(record: dict[str, Any], group_by: tuple[str, ...], index: int) -> None:
    label = f"record {index + 1}"
    for field in (*group_by, *REQUIRED_FIELDS):
        if field not in record:
            fail(f"{label}: {field}: missing required field")
    for field in PERCENTILE_FIELDS:
        require_number(record, field, label)
    for field in ("tt_probes", "tt_hits", "tt_cutoffs"):
        value = require_number(record, field, label)
        if not isinstance(value, int):
            fail(f"{label}: {field}: expected integer")


def nearest_rank(values: list[int | float], percentile: float) -> int | float:
    """Return nearest-rank percentile: sorted_values[ceil(p * n) - 1]."""
    if not values:
        fail("internal error: percentile requested for empty group")
    sorted_values = sorted(values)
    index = math.ceil(percentile * len(sorted_values)) - 1
    index = max(0, min(index, len(sorted_values) - 1))
    return sorted_values[index]


def sort_key(row: dict[str, Any]) -> tuple[Any, ...]:
    empties = row.get("empties")
    if isinstance(empties, bool) or not isinstance(empties, int):
        empties_key: tuple[int, int | str] = (1, "")
    else:
        empties_key = (0, empties)
    return (
        empties_key,
        str(row.get("parity_ordering", "")),
        str(row.get("tt_mode", "")),
        str(row.get("root_mode", "")),
        tuple(str(row[key]) for key in sorted(row)),
    )


def aggregate(records: list[dict[str, Any]], group_by: tuple[str, ...]) -> list[dict[str, Any]]:
    for index, record in enumerate(records):
        validate_record(record, group_by, index)

    groups: dict[tuple[Any, ...], list[dict[str, Any]]] = defaultdict(list)
    for record in records:
        groups[tuple(record[field] for field in group_by)].append(record)

    rows: list[dict[str, Any]] = []
    for key, group_records in groups.items():
        row = dict(zip(group_by, key, strict=True))
        row["count"] = len(group_records)
        for field in PERCENTILE_FIELDS:
            values = [record[field] for record in group_records]
            for name, percentile in PERCENTILES:
                row[f"{field}_{name}"] = nearest_rank(values, percentile)

        tt_probes = sum(int(record["tt_probes"]) for record in group_records)
        tt_hits = sum(int(record["tt_hits"]) for record in group_records)
        tt_cutoffs = sum(int(record["tt_cutoffs"]) for record in group_records)
        row["tt_hit_rate"] = tt_hits / tt_probes if tt_probes else 0.0
        row["tt_cutoff_rate"] = tt_cutoffs / tt_probes if tt_probes else 0.0
        rows.append(row)

    return sorted(rows, key=sort_key)


def output_columns(group_by: tuple[str, ...]) -> list[str]:
    columns = list(group_by)
    columns.append("count")
    for field in PERCENTILE_FIELDS:
        for name, _ in PERCENTILES:
            columns.append(f"{field}_{name}")
    columns.extend(("tt_hit_rate", "tt_cutoff_rate"))
    return columns


def format_value(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, float):
        return f"{value:.6g}"
    return str(value)


def print_markdown(rows: list[dict[str, Any]], columns: list[str]) -> None:
    print("| " + " | ".join(columns) + " |")
    print("| " + " | ".join("---" for _ in columns) + " |")
    for row in rows:
        print("| " + " | ".join(format_value(row[column]) for column in columns) + " |")


def print_tsv(rows: list[dict[str, Any]], columns: list[str]) -> None:
    print("\t".join(columns))
    for row in rows:
        print("\t".join(format_value(row[column]) for column in columns))


def print_json(rows: list[dict[str, Any]]) -> None:
    json.dump(rows, sys.stdout, ensure_ascii=False, indent=2, sort_keys=True)
    print()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Aggregate vibe_othello_endgame_bench JSONL output."
    )
    parser.add_argument(
        "--group-by",
        default=",".join(DEFAULT_GROUP_BY),
        help="comma-separated JSON fields to group by",
    )
    parser.add_argument(
        "--format",
        choices=("markdown", "tsv", "json"),
        default="markdown",
        help="output format",
    )
    parser.add_argument("jsonl", type=Path, nargs="+", help="input JSON Lines file")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    group_by = parse_group_by(args.group_by)
    rows = aggregate(load_records(args.jsonl), group_by)
    columns = output_columns(group_by)

    if args.format == "markdown":
        print_markdown(rows, columns)
    elif args.format == "tsv":
        print_tsv(rows, columns)
    elif args.format == "json":
        print_json(rows)
    else:
        fail(f"unsupported output format: {args.format}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
