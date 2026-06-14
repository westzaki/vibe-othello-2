#!/usr/bin/env python3
"""Sanity-check checked-in search benchmark aggregate baseline JSON."""

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
    "endgame_exact_empties",
    "terminal_nodes",
    "pass_nodes",
    "pvs_researches",
    "aspiration_fail_lows",
    "aspiration_fail_highs",
    "iid_searches",
    "endgame_nodes",
    "tt_overwrites",
    "tt_collisions",
    "tt_rejected_stores",
    "tt_invalid_best_move_stores",
)

OPTIONAL_STRING_RESULT_FIELDS = (
    "variant_id",
    "pvs",
    "aspiration",
    "history",
    "killers",
    "iid",
    "endgame_tt",
    "endgame_parity",
)

OPTIONAL_BOOL_RESULT_FIELDS = ("exact_endgame",)

ROOT_MOVE_FIELDS = ("move", "score", "bound", "depth", "exact", "selective")


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

        baseline_common.require_string(root_move, "move", root_label, errors)
        baseline_common.require_string(root_move, "bound", root_label, errors)
        baseline_common.require_int(root_move, "score", root_label, errors)
        baseline_common.require_int(root_move, "depth", root_label, errors)
        baseline_common.require_bool(root_move, "exact", root_label, errors)
        baseline_common.require_bool(root_move, "selective", root_label, errors)

        move = root_move.get("move")
        if isinstance(move, str):
            if move in seen_moves:
                errors.append(f"{root_label}.move: duplicate {move!r}")
            seen_moves.add(move)


def check_result(result: dict[str, Any], label: str, errors: list[str]) -> None:
    for field in RESULT_FIELDS:
        if field not in result:
            errors.append(f"{label}.{field}: missing required field")

    baseline_common.require_string(result, "position_id", label, errors)
    baseline_common.require_string(result, "category", label, errors)
    baseline_common.require_string(result, "mode", label, errors)
    baseline_common.require_string(result, "tt_mode", label, errors)
    baseline_common.require_string(result, "evaluator", label, errors)
    baseline_common.require_string(result, "best_move", label, errors)
    for field in OPTIONAL_STRING_RESULT_FIELDS:
        if field in result:
            baseline_common.require_string(result, field, label, errors)
    for field in OPTIONAL_BOOL_RESULT_FIELDS:
        if field in result:
            baseline_common.require_bool(result, field, label, errors)
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
        baseline_common.require_int(result, field, label, errors)
    for field in OPTIONAL_INT_RESULT_FIELDS:
        if field in result:
            baseline_common.require_int(result, field, label, errors)
    baseline_common.require_number(result, "nps", label, errors)


def check_baseline(data: dict[str, Any]) -> list[str]:
    errors = baseline_common.check_common_envelope(
        data, required_fields=TOP_LEVEL_FIELDS, benchmark="search_iterative_discdiff"
    )
    baseline_common.require_string(data, "measured_commit", "baseline", errors)
    baseline_common.require_string(data, "measured_revision", "baseline", errors)
    baseline_common.require_string(data, "command", "baseline", errors)
    baseline_common.require_string(data, "corpus", "baseline", errors)
    baseline_common.require_string(data, "mode", "baseline", errors)
    baseline_common.require_string(data, "tt_mode", "baseline", errors)
    baseline_common.require_int(data, "depth", "baseline", errors)

    results = data.get("results")
    if not isinstance(results, list) or not results:
        errors.append("results: expected non-empty array")
        return errors

    seen_keys: set[tuple[str, str, str, str, str, int]] = set()
    for index, result in enumerate(results):
        label = f"results[{index}]"
        if not isinstance(result, dict):
            errors.append(f"{label}: expected object")
            continue
        check_result(result, label, errors)

        position_id = result.get("position_id")
        mode = result.get("mode")
        tt_mode = result.get("tt_mode")
        evaluator = result.get("evaluator")
        variant_id = result.get("variant_id", "")
        depth = result.get("depth")
        if (
            isinstance(position_id, str)
            and isinstance(mode, str)
            and isinstance(tt_mode, str)
            and isinstance(evaluator, str)
            and isinstance(variant_id, str)
            and isinstance(depth, int)
        ):
            key = (position_id, mode, tt_mode, evaluator, variant_id, depth)
            if key in seen_keys:
                errors.append(f"{label}: duplicate position/mode/tt/evaluator/variant/depth key {key!r}")
            seen_keys.add(key)

    return errors


def write_aggregate(args: argparse.Namespace) -> None:
    records = baseline_common.load_jsonl(args.raw_jsonl)
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

    baseline_common.write_pretty_json(args.write_aggregate, aggregate)
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

    errors = check_baseline(baseline_common.load_json(args.baseline_json))
    if errors:
        print("search aggregate baseline schema check failed:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1

    print(f"search aggregate baseline schema ok: {args.baseline_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
