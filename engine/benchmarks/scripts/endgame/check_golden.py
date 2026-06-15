#!/usr/bin/env python3
"""Compare deterministic endgame_bench JSONL fields against a golden file."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


ROOT_MOVE_FIELDS = ("move", "score", "score_kind", "bound", "depth", "exact", "selective")
TOP_LEVEL_FIELDS = (
    "position_id",
    "category",
    "position",
    "mode",
    "empties",
    "score",
    "score_kind",
    "best_move",
    "exact",
    "stopped",
    "completed_depth",
    "pv",
)


def record_sort_key(record: dict[str, Any]) -> tuple[str, int, str]:
    return (
        str(record.get("position_id", "")),
        int(record.get("empties", -1)) if isinstance(record.get("empties"), int) else -1,
        str(record.get("mode", "")),
    )


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
        normalized_root_moves: dict[str, dict[str, Any]] = {}
        for move, root_move in root_moves.items():
            if isinstance(root_move, dict):
                normalized_root_moves[str(move)] = {
                    field: root_move.get(field) for field in ROOT_MOVE_FIELDS if field != "move"
                }
            else:
                normalized_root_moves[str(move)] = root_move
        return dict(sorted(normalized_root_moves.items()))

    if not isinstance(root_moves, list):
        return root_moves

    normalized_root_moves = {}
    for root_move in root_moves:
        if not isinstance(root_move, dict):
            return root_moves
        move = root_move.get("move")
        if not isinstance(move, str):
            return root_moves
        normalized_root_moves[move] = {
            field: root_move.get(field) for field in ROOT_MOVE_FIELDS if field != "move"
        }
    return dict(sorted(normalized_root_moves.items()))


def normalize_record(record: dict[str, Any]) -> dict[str, Any]:
    normalized = {field: record.get(field) for field in TOP_LEVEL_FIELDS}
    normalized["root_moves"] = normalize_root_moves(record.get("root_moves", {}))
    return normalized


def normalized_records(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [normalize_record(record) for record in sorted(records, key=record_sort_key)]


def write_jsonl(records: list[dict[str, Any]], path: Path) -> None:
    with path.open("w", encoding="utf-8") as output_file:
        for record in records:
            output_file.write(
                json.dumps(record, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
            )
            output_file.write("\n")


def record_label(record: dict[str, Any], index: int) -> str:
    position_id = record.get("position_id", "<missing-position>")
    empties = record.get("empties", "<missing-empties>")
    mode = record.get("mode", "<missing-mode>")
    return f"record {index + 1} position={position_id} empties={empties} mode={mode}"


def value_text(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True)


def collect_diffs(actual: Any, golden: Any, path: str, output: list[str]) -> None:
    if isinstance(actual, dict) and isinstance(golden, dict):
        keys = sorted(set(actual) | set(golden))
        for key in keys:
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


def compare_records(actual_records: list[dict[str, Any]], golden_records: list[dict[str, Any]]) -> list[str]:
    failures: list[str] = []
    actual_records = normalized_records(actual_records)
    golden_records = normalized_records(golden_records)
    if len(actual_records) != len(golden_records):
        failures.append(f"record count actual={len(actual_records)} golden={len(golden_records)}")

    for index, (actual_record, golden_record) in enumerate(zip(actual_records, golden_records)):
        diffs: list[str] = []
        collect_diffs(actual_record, golden_record, "", diffs)
        if diffs:
            failures.append(record_label(golden_record, index))
            failures.extend(f"  {diff}" for diff in diffs)

    if len(actual_records) > len(golden_records):
        for index in range(len(golden_records), len(actual_records)):
            failures.append(f"unexpected {record_label(actual_records[index], index)}")
    elif len(golden_records) > len(actual_records):
        for index in range(len(actual_records), len(golden_records)):
            failures.append(f"missing {record_label(golden_records[index], index)}")

    return failures


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare deterministic endgame_bench JSONL fields against a golden file."
    )
    parser.add_argument(
        "--write-normalized",
        type=Path,
        metavar="OUTPUT_JSONL",
        help="write deterministic-only normalized JSONL instead of comparing",
    )
    parser.add_argument("actual_jsonl", type=Path)
    parser.add_argument("golden_jsonl", type=Path, nargs="?")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    actual_records = load_jsonl(args.actual_jsonl)
    if args.write_normalized is not None:
        write_jsonl(normalized_records(actual_records), args.write_normalized)
        print(f"wrote normalized endgame golden: {args.write_normalized}")
        return 0

    if args.golden_jsonl is None:
        print("golden_jsonl is required unless --write-normalized is used", file=sys.stderr)
        return 2

    golden_records = load_jsonl(args.golden_jsonl)
    failures = compare_records(actual_records, golden_records)
    if failures:
        print("endgame golden mismatch:", file=sys.stderr)
        for failure in failures:
            print(failure, file=sys.stderr)
        return 1
    print(f"endgame golden match: {len(actual_records)} records")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
