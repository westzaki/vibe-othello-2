#!/usr/bin/env python3
"""Shared deterministic JSONL golden normalization and comparison."""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable


ROOT_MOVE_FIELDS = ("move", "score", "score_kind", "bound", "depth", "exact", "selective")


@dataclass(frozen=True)
class GoldenSpec:
    label: str
    description: str
    top_level_fields: tuple[str, ...]
    record_sort_key: Callable[[dict[str, Any]], tuple[Any, ...]]
    record_label: Callable[[dict[str, Any], int], str]


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as input_file:
        for line_number, line in enumerate(input_file, start=1):
            stripped = line.strip()
            if not stripped:
                continue
            try:
                record = json.loads(stripped)
            except json.JSONDecodeError as error:
                raise SystemExit(f"{path}:{line_number}: invalid JSON: {error}") from error
            if not isinstance(record, dict):
                raise SystemExit(f"{path}:{line_number}: expected a JSON object")
            records.append(record)
    return records


def normalize_root_moves(root_moves: Any) -> dict[str, dict[str, Any]] | Any:
    if isinstance(root_moves, dict):
        normalized: dict[str, Any] = {}
        for move, root_move in root_moves.items():
            normalized[str(move)] = (
                {
                    field: root_move.get(field)
                    for field in ROOT_MOVE_FIELDS
                    if field != "move"
                }
                if isinstance(root_move, dict)
                else root_move
            )
        return dict(sorted(normalized.items()))

    if not isinstance(root_moves, list):
        return root_moves

    normalized = {}
    for root_move in root_moves:
        if not isinstance(root_move, dict) or not isinstance(root_move.get("move"), str):
            return root_moves
        normalized[root_move["move"]] = {
            field: root_move.get(field) for field in ROOT_MOVE_FIELDS if field != "move"
        }
    return dict(sorted(normalized.items()))


def normalize_record(record: dict[str, Any], spec: GoldenSpec) -> dict[str, Any]:
    normalized = {field: record.get(field) for field in spec.top_level_fields}
    normalized["root_moves"] = normalize_root_moves(record.get("root_moves", {}))
    return normalized


def normalized_records(
    records: list[dict[str, Any]], spec: GoldenSpec
) -> list[dict[str, Any]]:
    return [
        normalize_record(record, spec)
        for record in sorted(records, key=spec.record_sort_key)
    ]


def write_jsonl(records: list[dict[str, Any]], path: Path) -> None:
    with path.open("w", encoding="utf-8") as output_file:
        for record in records:
            output_file.write(
                json.dumps(record, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
            )
            output_file.write("\n")


def value_text(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True)


def collect_diffs(actual: Any, golden: Any, path: str, output: list[str]) -> None:
    if isinstance(actual, dict) and isinstance(golden, dict):
        for key in sorted(set(actual) | set(golden)):
            child_path = f"{path}.{key}" if path else key
            if key not in actual:
                output.append(f"{child_path}: missing in actual; expected {value_text(golden[key])}")
            elif key not in golden:
                output.append(f"{child_path}: unexpected actual value {value_text(actual[key])}")
            else:
                collect_diffs(actual[key], golden[key], child_path, output)
        return

    if isinstance(actual, list) and isinstance(golden, list):
        if len(actual) != len(golden):
            output.append(f"{path}: length actual={len(actual)} golden={len(golden)}")
        for index, (actual_item, golden_item) in enumerate(zip(actual, golden)):
            collect_diffs(actual_item, golden_item, f"{path}[{index}]", output)
        return

    if actual != golden:
        output.append(f"{path}: actual={value_text(actual)} golden={value_text(golden)}")


def compare_records(
    actual_records: list[dict[str, Any]],
    golden_records: list[dict[str, Any]],
    spec: GoldenSpec,
) -> list[str]:
    failures: list[str] = []
    actual_records = normalized_records(actual_records, spec)
    golden_records = normalized_records(golden_records, spec)
    if len(actual_records) != len(golden_records):
        failures.append(f"record count actual={len(actual_records)} golden={len(golden_records)}")

    for index, (actual_record, golden_record) in enumerate(
        zip(actual_records, golden_records)
    ):
        diffs: list[str] = []
        collect_diffs(actual_record, golden_record, "", diffs)
        if diffs:
            failures.append(spec.record_label(golden_record, index))
            failures.extend(f"  {diff}" for diff in diffs)

    if len(actual_records) > len(golden_records):
        for index in range(len(golden_records), len(actual_records)):
            failures.append(f"unexpected {spec.record_label(actual_records[index], index)}")
    elif len(golden_records) > len(actual_records):
        for index in range(len(actual_records), len(golden_records)):
            failures.append(f"missing {spec.record_label(golden_records[index], index)}")
    return failures


def run(spec: GoldenSpec) -> int:
    parser = argparse.ArgumentParser(description=spec.description)
    parser.add_argument(
        "--write-normalized",
        type=Path,
        metavar="OUTPUT_JSONL",
        help="write deterministic-only normalized JSONL instead of comparing",
    )
    parser.add_argument("actual_jsonl", type=Path)
    parser.add_argument("golden_jsonl", type=Path, nargs="?")
    args = parser.parse_args()

    actual_records = load_jsonl(args.actual_jsonl)
    if args.write_normalized is not None:
        write_jsonl(normalized_records(actual_records, spec), args.write_normalized)
        print(f"wrote normalized {spec.label} golden: {args.write_normalized}")
        return 0

    if args.golden_jsonl is None:
        print("golden_jsonl is required unless --write-normalized is used", file=sys.stderr)
        return 2

    failures = compare_records(actual_records, load_jsonl(args.golden_jsonl), spec)
    if failures:
        print(f"{spec.label} golden mismatch:", file=sys.stderr)
        for failure in failures:
            print(failure, file=sys.stderr)
        return 1
    print(f"{spec.label} golden match: {len(actual_records)} records")
    return 0
