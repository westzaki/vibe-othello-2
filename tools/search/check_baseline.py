#!/usr/bin/env python3
"""Sanity-check checked-in search benchmark aggregate baseline JSON."""

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
    "mode",
    "depth",
    "tt_mode",
    "results",
)

RESULT_FIELDS = (
    "position_id",
    "category",
    "mode",
    "tt_mode",
    "depth",
    "evaluator",
    "score",
    "best_move",
    "pv",
    "root_moves",
    "nodes",
    "eval_calls",
    "beta_cutoffs",
    "alpha_updates",
    "tt_probes",
    "tt_hits",
    "tt_stores",
    "tt_cutoffs",
    "elapsed_ns",
    "nps",
)

OPTIONAL_INT_RESULT_FIELDS = (
    "tt_overwrites",
    "tt_collisions",
    "tt_rejected_stores",
    "tt_invalid_best_move_stores",
)

ROOT_MOVE_FIELDS = ("move", "score", "bound", "depth", "exact", "selective")


def load_json(path: Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as input_file:
            data = json.load(input_file)
    except json.JSONDecodeError as error:
        raise SystemExit(f"{path}: invalid JSON: {error}") from error

    if not isinstance(data, dict):
        raise SystemExit(f"{path}: expected a JSON object")
    return data


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


def require_string(record: dict[str, Any], field: str, label: str, errors: list[str]) -> None:
    value = record.get(field)
    if not isinstance(value, str) or not value:
        errors.append(f"{label}.{field}: expected non-empty string")


def require_int(record: dict[str, Any], field: str, label: str, errors: list[str]) -> None:
    value = record.get(field)
    if not isinstance(value, int):
        errors.append(f"{label}.{field}: expected integer")


def require_number(record: dict[str, Any], field: str, label: str, errors: list[str]) -> None:
    value = record.get(field)
    if not isinstance(value, int | float):
        errors.append(f"{label}.{field}: expected number")


def require_string_list(record: dict[str, Any], field: str, label: str, errors: list[str]) -> None:
    value = record.get(field)
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        errors.append(f"{label}.{field}: expected array of strings")


def check_root_moves(value: Any, label: str, errors: list[str]) -> None:
    if not isinstance(value, list):
        errors.append(f"{label}: expected array")
        return

    seen_moves: set[str] = set()
    for index, root_move in enumerate(value):
        root_label = f"{label}[{index}]"
        if not isinstance(root_move, dict):
            errors.append(f"{root_label}: expected object")
            continue

        for field in ROOT_MOVE_FIELDS:
            if field not in root_move:
                errors.append(f"{root_label}.{field}: missing required field")

        require_string(root_move, "move", root_label, errors)
        require_string(root_move, "bound", root_label, errors)
        require_int(root_move, "score", root_label, errors)
        require_int(root_move, "depth", root_label, errors)
        if not isinstance(root_move.get("exact"), bool):
            errors.append(f"{root_label}.exact: expected boolean")
        if not isinstance(root_move.get("selective"), bool):
            errors.append(f"{root_label}.selective: expected boolean")

        move = root_move.get("move")
        if isinstance(move, str):
            if move in seen_moves:
                errors.append(f"{root_label}.move: duplicate {move!r}")
            seen_moves.add(move)


def check_result(result: dict[str, Any], label: str, errors: list[str]) -> None:
    for field in RESULT_FIELDS:
        if field not in result:
            errors.append(f"{label}.{field}: missing required field")

    require_string(result, "position_id", label, errors)
    require_string(result, "category", label, errors)
    require_string(result, "mode", label, errors)
    require_string(result, "tt_mode", label, errors)
    require_string(result, "evaluator", label, errors)
    require_string(result, "best_move", label, errors)
    require_string_list(result, "pv", label, errors)
    check_root_moves(result.get("root_moves"), f"{label}.root_moves", errors)

    for field in (
        "depth",
        "score",
        "nodes",
        "eval_calls",
        "beta_cutoffs",
        "alpha_updates",
        "tt_probes",
        "tt_hits",
        "tt_stores",
        "tt_cutoffs",
        "elapsed_ns",
    ):
        require_int(result, field, label, errors)
    for field in OPTIONAL_INT_RESULT_FIELDS:
        if field in result:
            require_int(result, field, label, errors)
    require_number(result, "nps", label, errors)


def check_baseline(data: dict[str, Any]) -> list[str]:
    errors: list[str] = []

    for field in TOP_LEVEL_FIELDS:
        if field not in data:
            errors.append(f"{field}: missing required field")

    if data.get("schema_version") != 1:
        errors.append("schema_version: expected 1")
    if data.get("benchmark") != "search_iterative_discdiff":
        errors.append("benchmark: expected 'search_iterative_discdiff'")
    require_string(data, "measured_commit", "baseline", errors)
    require_string(data, "measured_revision", "baseline", errors)
    require_string(data, "command", "baseline", errors)
    require_string(data, "corpus", "baseline", errors)
    require_string(data, "mode", "baseline", errors)
    require_string(data, "tt_mode", "baseline", errors)
    require_int(data, "depth", "baseline", errors)

    results = data.get("results")
    if not isinstance(results, list) or not results:
        errors.append("results: expected non-empty array")
        return errors

    seen_keys: set[tuple[str, str, str, int]] = set()
    for index, result in enumerate(results):
        label = f"results[{index}]"
        if not isinstance(result, dict):
            errors.append(f"{label}: expected object")
            continue
        check_result(result, label, errors)

        position_id = result.get("position_id")
        mode = result.get("mode")
        tt_mode = result.get("tt_mode")
        depth = result.get("depth")
        if (
            isinstance(position_id, str)
            and isinstance(mode, str)
            and isinstance(tt_mode, str)
            and isinstance(depth, int)
        ):
            key = (position_id, mode, tt_mode, depth)
            if key in seen_keys:
                errors.append(f"{label}: duplicate position/mode/tt/depth key {key!r}")
            seen_keys.add(key)

    return errors


def write_aggregate(args: argparse.Namespace) -> None:
    records = load_jsonl(args.raw_jsonl)
    aggregate = {
        "schema_version": 1,
        "benchmark": "search_iterative_discdiff",
        "measured_commit": args.measured_commit,
        "measured_revision": args.measured_revision,
        "measured_at": args.measured_at,
        "machine": args.machine,
        "os": args.os,
        "compiler": args.compiler,
        "build_type": args.build_type,
        "command": args.command,
        "corpus": args.corpus,
        "mode": args.mode,
        "depth": args.depth,
        "tt_mode": args.tt_mode,
        "selection_policy": "single search benchmark invocation",
        "results": records,
    }
    errors = check_baseline(aggregate)
    if errors:
        print("search aggregate baseline schema check failed:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        raise SystemExit(1)

    args.write_aggregate.parent.mkdir(parents=True, exist_ok=True)
    with args.write_aggregate.open("w", encoding="utf-8") as output_file:
        json.dump(aggregate, output_file, ensure_ascii=False, indent=2)
        output_file.write("\n")
    print(f"wrote search aggregate baseline: {args.write_aggregate}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Sanity-check checked-in search benchmark aggregate baseline JSON."
    )
    parser.add_argument(
        "--write-aggregate",
        type=Path,
        metavar="OUTPUT_JSON",
        help="write aggregate search baseline JSON from raw search_bench JSONL",
    )
    parser.add_argument("--measured-commit", default="")
    parser.add_argument("--measured-revision", default="")
    parser.add_argument("--measured-at", default="")
    parser.add_argument("--machine", default="")
    parser.add_argument("--os", default="")
    parser.add_argument("--compiler", default="")
    parser.add_argument("--build-type", default="Release")
    parser.add_argument("--command", default="")
    parser.add_argument("--corpus", default="")
    parser.add_argument("--mode", default="iterative")
    parser.add_argument("--depth", type=int, default=5)
    parser.add_argument("--tt-mode", default="both")
    parser.add_argument("baseline_json", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.write_aggregate is not None:
        args.raw_jsonl = args.baseline_json
        write_aggregate(args)
        return 0

    errors = check_baseline(load_json(args.baseline_json))
    if errors:
        print("search aggregate baseline schema check failed:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1

    print(f"search aggregate baseline schema ok: {args.baseline_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
