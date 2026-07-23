#!/usr/bin/env python3
"""Run high-empty endgame benchmark probes one position/mode at a time."""

from __future__ import annotations

import argparse
import importlib.util
import json
import re
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


DEFAULT_POSITION_IDS = (
    "fourteen_empty_simple",
    "sixteen_empty_simple",
    "eighteen_empty_simple",
    "twenty_empty_simple",
)
DEFAULT_ROOT_MODE = "best"
DEFAULT_PARITY = "on"
DEFAULT_TT = "on"
DEFAULT_PVS = "on"
DEFAULT_MODE = "exact-score"
DEFAULT_ENTRY = "direct"
DEFAULT_REPEAT = 1
DEFAULT_TIMEOUT_SEC = 120
DEFAULT_FORMAT = "markdown"
DEFAULT_CORPUS = Path("engine/fixtures/endgame/positions.tsv")
DEFAULT_AGGREGATE_SCRIPT = Path("engine/benchmarks/scripts/endgame/aggregate_endgame_bench.py")
MODE_CHOICES = ("exact-score", "wld", "both")
BENCHMARK_MODE_NAMES = {
    "exact-score": "exact_score",
    "wld": "wld",
}
WLD_RESULT_BY_SCORE = {
    -1: "loss",
    0: "draw",
    1: "win",
}
SUMMARY_COLUMNS = (
    "position_id",
    "empties",
    "threshold",
    "triggered",
    "status",
    "entry",
    "mode",
    "stability_mode",
    "pvs",
    "elapsed_wall_ms",
    "output_jsonl",
    "elapsed_ms_p50",
    "nodes_p50",
    "endgame_nodes_p50",
    "eval_calls",
    "nps_p50",
    "tt_hit_rate",
    "tt_cutoff_rate",
    "root_moves_searched_p50",
    "score",
    "wld_result",
    "best_move",
    "exact",
    "stopped",
    "completed_depth",
    "error",
)


@dataclass
class ProbeResult:
    position_id: str
    mode: str
    entry: str
    threshold: int
    status: str
    elapsed_wall_ms: int
    output_jsonl: Path
    aggregate_rows: list[dict[str, Any]] = field(default_factory=list)
    summary_rows: list[dict[str, Any]] = field(default_factory=list)
    error: str = ""


def fail(message: str) -> None:
    raise SystemExit(message)


def positive_int(text: str) -> int:
    try:
        value = int(text, 10)
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"expected integer, got {text!r}") from error
    if value <= 0:
        raise argparse.ArgumentTypeError("expected a positive integer")
    return value


def load_corpus_ids(path: Path) -> set[str]:
    try:
        with path.open("r", encoding="utf-8") as input_file:
            lines = list(input_file)
    except OSError as error:
        fail(f"failed to read corpus {path}: {error}")

    ids: set[str] = set()
    saw_header = False
    for line_number, line in enumerate(lines, start=1):
        stripped = line.rstrip("\n")
        if not stripped or stripped.startswith("#"):
            continue

        fields = stripped.split("\t")
        if not saw_header:
            if fields != ["id", "category", "position", "expected_empties", "notes"]:
                fail(f"{path}:{line_number}: invalid endgame corpus header")
            saw_header = True
            continue

        if len(fields) != 5:
            fail(f"{path}:{line_number}: invalid endgame corpus row")
        ids.add(fields[0])

    if not saw_header:
        fail(f"{path}: missing endgame corpus header")
    return ids


def selected_position_ids(args: argparse.Namespace) -> list[str]:
    position_ids = args.position_id or list(DEFAULT_POSITION_IDS)
    corpus_ids = load_corpus_ids(args.corpus)
    missing = [position_id for position_id in position_ids if position_id not in corpus_ids]
    if missing:
        fail(f"unknown position id(s) for {args.corpus}: {', '.join(missing)}")
    return position_ids


def selected_modes(args: argparse.Namespace) -> list[str]:
    if args.mode == "both":
        return ["exact-score", "wld"]
    return [args.mode]


def selected_thresholds(args: argparse.Namespace) -> list[int]:
    if args.entry != "iterative-root":
        return [0]
    return args.threshold or [args.endgame_wld_empties]


def safe_filename(
    position_id: str,
    mode: str,
    entry: str,
    threshold: int,
    index: int,
    seen: dict[tuple[str, str, str, int], int],
) -> str:
    safe_id = re.sub(r"[^A-Za-z0-9_.-]+", "_", position_id).strip("._")
    if not safe_id:
        safe_id = "position"

    key = (position_id, mode, entry, threshold)
    seen[key] = seen.get(key, 0) + 1
    suffix = "" if seen[key] == 1 else f"-{seen[key]}"
    threshold_part = "" if entry == "direct" else f"-t{threshold}"
    return f"{index:02d}-{safe_id}{suffix}-{mode}-{entry}{threshold_part}.jsonl"


def load_aggregate_module(path: Path) -> Any:
    spec = importlib.util.spec_from_file_location("aggregate_endgame_bench", path)
    if spec is None or spec.loader is None:
        fail(f"failed to load aggregate script: {path}")
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
    except OSError as error:
        fail(f"failed to read aggregate script {path}: {error}")
    return module


def validate_jsonl(path: Path, position_id: str, mode: str) -> list[dict[str, Any]]:
    expected_mode = BENCHMARK_MODE_NAMES[mode]
    records: list[dict[str, Any]] = []
    try:
        with path.open("r", encoding="utf-8") as input_file:
            for line_number, line in enumerate(input_file, start=1):
                stripped = line.strip()
                if not stripped:
                    continue
                try:
                    record = json.loads(stripped)
                except json.JSONDecodeError as error:
                    raise ValueError(f"{path}:{line_number}: invalid JSON: {error}") from error
                if not isinstance(record, dict):
                    raise ValueError(f"{path}:{line_number}: expected a JSON object")
                actual_position_id = record.get("position_id")
                if actual_position_id != position_id:
                    raise ValueError(
                        f"{path}:{line_number}: expected position_id {position_id!r}, "
                        f"got {actual_position_id!r}"
                    )
                actual_mode = record.get("mode")
                if actual_mode != expected_mode:
                    raise ValueError(
                        f"{path}:{line_number}: expected mode {expected_mode!r}, "
                        f"got {actual_mode!r}"
                    )
                if mode == "wld" and record.get("status", "completed") == "completed":
                    validate_wld_record(path, line_number, record)
                records.append(record)
    except OSError as error:
        raise ValueError(f"failed to read {path}: {error}") from error

    if not records:
        raise ValueError(f"{path}: completed run produced no JSONL records")
    return records


def validate_wld_record(path: Path, line_number: int, record: dict[str, Any]) -> None:
    score = record.get("score")
    if not isinstance(score, int) or isinstance(score, bool) or score not in WLD_RESULT_BY_SCORE:
        raise ValueError(
            f"{path}:{line_number}: WLD score must be -1, 0, or 1; got {score!r}"
        )

    wld_result = record.get("wld_result")
    if wld_result != WLD_RESULT_BY_SCORE[score]:
        raise ValueError(
            f"{path}:{line_number}: expected wld_result {WLD_RESULT_BY_SCORE[score]!r}, "
            f"got {wld_result!r}"
        )


def aggregate_jsonl(aggregate_module: Any, path: Path) -> list[dict[str, Any]]:
    try:
        records = aggregate_module.load_records([path])
        return aggregate_module.aggregate(records, aggregate_module.DEFAULT_GROUP_BY)
    except SystemExit as error:
        raise ValueError(f"aggregate failed for {path}: {error}") from error


def nearest_rank(values: list[Any], percentile: float) -> Any:
    if not values:
        return ""
    sorted_values = sorted(values)
    index = round(percentile * (len(sorted_values) - 1))
    return sorted_values[index]


def group_key(record: dict[str, Any]) -> tuple[Any, ...]:
    return tuple(
        record.get(field)
        for field in (
            "mode",
            "empties",
            "parity_ordering",
            "tt_mode",
            "stability_mode",
            "pvs",
            "root_mode",
        )
    )


def same_or_join(values: list[Any]) -> Any:
    unique_values = sorted({format_value(value) for value in values})
    if len(unique_values) == 1:
        return unique_values[0]
    return ",".join(unique_values)


def build_summary_rows(
    position_id: str,
    mode: str,
    entry: str,
    threshold: int,
    status: str,
    elapsed_wall_ms: int,
    output_jsonl: Path,
    aggregate_rows: list[dict[str, Any]],
    records: list[dict[str, Any]],
    error: str = "",
) -> list[dict[str, Any]]:
    if status != "completed":
        first_record = records[0] if records else {}
        return [
            {
                "position_id": position_id,
                "empties": first_record.get("empties", ""),
                "threshold": first_record.get("threshold", threshold),
                "triggered": first_record.get("triggered", ""),
                "entry": entry,
                "status": status,
                "mode": mode,
                "stability_mode": first_record.get("stability_mode", ""),
                "pvs": first_record.get("pvs", ""),
                "elapsed_wall_ms": elapsed_wall_ms,
                "output_jsonl": str(output_jsonl),
                "score": first_record.get("score", ""),
                "wld_result": first_record.get("wld_result", "n/a"),
                "best_move": first_record.get("best_move", ""),
                "exact": first_record.get("exact", ""),
                "stopped": first_record.get("stopped", ""),
                "completed_depth": first_record.get("completed_depth", ""),
                "nodes_p50": first_record.get("nodes", ""),
                "endgame_nodes_p50": first_record.get("endgame_nodes", ""),
                "eval_calls": first_record.get("eval_calls", ""),
                "root_moves_searched_p50": first_record.get("root_moves_searched", ""),
                "error": error,
            }
        ]

    records_by_group: dict[tuple[Any, ...], list[dict[str, Any]]] = {}
    for record in records:
        records_by_group.setdefault(group_key(record), []).append(record)

    rows: list[dict[str, Any]] = []
    for aggregate_row in aggregate_rows:
        key = group_key(aggregate_row)
        group_records = records_by_group.get(key, [])
        row = {
            "position_id": position_id,
            "status": status,
            "entry": entry,
            "threshold": threshold,
            "mode": mode,
            "stability_mode": aggregate_row.get("stability_mode", ""),
            "pvs": aggregate_row.get("pvs", ""),
            "elapsed_wall_ms": elapsed_wall_ms,
            "output_jsonl": str(output_jsonl),
            "elapsed_ms_p50": aggregate_row.get("elapsed_ms_p50", ""),
            "nodes_p50": aggregate_row.get("nodes_p50", ""),
            "endgame_nodes_p50": aggregate_row.get("endgame_nodes_p50", ""),
            "eval_calls": "",
            "nps_p50": aggregate_row.get("nps_p50", ""),
            "tt_hit_rate": aggregate_row.get("tt_hit_rate", ""),
            "tt_cutoff_rate": aggregate_row.get("tt_cutoff_rate", ""),
            "root_moves_searched_p50": "",
            "score": "",
            "wld_result": "",
            "best_move": "",
            "exact": "",
            "stopped": "",
            "completed_depth": "",
            "empties": "",
            "triggered": "",
            "error": "",
        }
        if group_records:
            row["empties"] = same_or_join([record.get("empties", "") for record in group_records])
            row["threshold"] = same_or_join(
                [record.get("threshold", threshold) for record in group_records]
            )
            row["triggered"] = same_or_join(
                [record.get("triggered", "") for record in group_records]
            )
            root_moves = [
                record["root_moves_searched"]
                for record in group_records
                if isinstance(record.get("root_moves_searched"), int)
                and not isinstance(record.get("root_moves_searched"), bool)
            ]
            row["root_moves_searched_p50"] = nearest_rank(root_moves, 0.50)
            row["score"] = same_or_join([record.get("score", "") for record in group_records])
            row["wld_result"] = same_or_join(
                [record.get("wld_result", "n/a") for record in group_records]
            )
            row["best_move"] = same_or_join([record.get("best_move", "") for record in group_records])
            row["exact"] = same_or_join([record.get("exact", "") for record in group_records])
            row["stopped"] = same_or_join([record.get("stopped", "") for record in group_records])
            row["completed_depth"] = same_or_join(
                [record.get("completed_depth", "") for record in group_records]
            )
            row["eval_calls"] = same_or_join(
                [record.get("eval_calls", "") for record in group_records]
            )
        rows.append(row)
    return rows


def wld_result_for_exact_score(score: int) -> str:
    if score > 0:
        return "win"
    if score < 0:
        return "loss"
    return "draw"


def validate_paired_wld(results: list[ProbeResult]) -> list[str]:
    completed_by_position_mode: dict[tuple[str, str], ProbeResult] = {
        (result.position_id, result.mode): result
        for result in results
        if result.status == "completed"
    }

    errors: list[str] = []
    for (position_id, mode), exact_result in completed_by_position_mode.items():
        if mode != "exact-score":
            continue
        wld_result = completed_by_position_mode.get((position_id, "wld"))
        if wld_result is None:
            continue
        exact_scores = {
            row.get("score")
            for row in exact_result.summary_rows
            if row.get("status") == "completed" and row.get("score") != ""
        }
        wld_results = {
            row.get("wld_result")
            for row in wld_result.summary_rows
            if row.get("status") == "completed" and row.get("wld_result") not in ("", "n/a")
        }
        for score in exact_scores:
            try:
                expected = wld_result_for_exact_score(int(str(score)))
            except ValueError:
                errors.append(f"{position_id}: exact-score summary has non-integer score {score!r}")
                continue
            if expected not in wld_results:
                errors.append(
                    f"{position_id}: exact score {score} expects WLD {expected}, "
                    f"got {', '.join(sorted(str(result) for result in wld_results)) or '<none>'}"
                )
    return errors


def run_probe(
    args: argparse.Namespace,
    aggregate_module: Any,
    position_id: str,
    mode: str,
    threshold: int,
    output_jsonl: Path,
) -> ProbeResult:
    command = [
        str(args.bench),
        "--jsonl",
        "--position-id",
        position_id,
        "--root-mode",
        args.root_mode,
        "--parity",
        args.parity,
        "--tt",
        args.tt,
        "--pvs",
        args.pvs,
        "--mode",
        mode,
        "--entry",
        args.entry,
        "--repeat",
        str(args.repeat),
        "--corpus",
        str(args.corpus),
    ]
    if args.entry == "iterative-root":
        command.extend(["--endgame-wld-empties", str(threshold)])

    start = time.monotonic()
    try:
        with output_jsonl.open("w", encoding="utf-8") as output_file:
            completed = subprocess.run(
                command,
                stdout=output_file,
                stderr=subprocess.PIPE,
                text=True,
                timeout=args.timeout_sec,
                check=False,
            )
    except subprocess.TimeoutExpired as error:
        elapsed_wall_ms = round((time.monotonic() - start) * 1000)
        message = f"timed out after {args.timeout_sec} second(s)"
        if error.stderr:
            message = f"{message}; stderr: {error.stderr.strip()}"
        return ProbeResult(
            position_id,
            mode,
            args.entry,
            threshold,
            "timed_out",
            elapsed_wall_ms,
            output_jsonl,
            summary_rows=build_summary_rows(
                position_id,
                mode,
                args.entry,
                threshold,
                "timed_out",
                elapsed_wall_ms,
                output_jsonl,
                [],
                [],
                message,
            ),
            error=message,
        )
    except OSError as error:
        elapsed_wall_ms = round((time.monotonic() - start) * 1000)
        message = f"failed to run benchmark: {error}"
        return ProbeResult(
            position_id,
            mode,
            args.entry,
            threshold,
            "failed",
            elapsed_wall_ms,
            output_jsonl,
            summary_rows=build_summary_rows(
                position_id,
                mode,
                args.entry,
                threshold,
                "failed",
                elapsed_wall_ms,
                output_jsonl,
                [],
                [],
                message,
            ),
            error=message,
        )

    elapsed_wall_ms = round((time.monotonic() - start) * 1000)
    if completed.returncode != 0:
        error = completed.stderr.strip() if completed.stderr else f"exit code {completed.returncode}"
        return ProbeResult(
            position_id,
            mode,
            args.entry,
            threshold,
            "failed",
            elapsed_wall_ms,
            output_jsonl,
            summary_rows=build_summary_rows(
                position_id,
                mode,
                args.entry,
                threshold,
                "failed",
                elapsed_wall_ms,
                output_jsonl,
                [],
                [],
                error,
            ),
            error=error,
        )

    try:
        records = validate_jsonl(output_jsonl, position_id, mode)
    except ValueError as error:
        return ProbeResult(
            position_id,
            mode,
            args.entry,
            threshold,
            "failed",
            elapsed_wall_ms,
            output_jsonl,
            summary_rows=build_summary_rows(
                position_id,
                mode,
                args.entry,
                threshold,
                "failed",
                elapsed_wall_ms,
                output_jsonl,
                [],
                [],
                str(error),
            ),
            error=str(error),
        )

    record_statuses = {record.get("status", "completed") for record in records}
    status = "completed" if record_statuses == {"completed"} else same_or_join(list(record_statuses))
    if status == "completed":
        try:
            aggregate_rows = aggregate_jsonl(aggregate_module, output_jsonl)
        except ValueError as error:
            return ProbeResult(
                position_id,
                mode,
                args.entry,
                threshold,
                "failed",
                elapsed_wall_ms,
                output_jsonl,
                summary_rows=build_summary_rows(
                    position_id,
                    mode,
                    args.entry,
                    threshold,
                    "failed",
                    elapsed_wall_ms,
                    output_jsonl,
                    [],
                    [],
                    str(error),
                ),
                error=str(error),
            )
    else:
        aggregate_rows = []

    return ProbeResult(
        position_id,
        mode,
        args.entry,
        threshold,
        status,
        elapsed_wall_ms,
        output_jsonl,
        aggregate_rows,
        build_summary_rows(
            position_id,
            mode,
            args.entry,
            threshold,
            status,
            elapsed_wall_ms,
            output_jsonl,
            aggregate_rows,
            records,
        ),
    )


def format_value(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, float):
        return f"{value:.6g}"
    return str(value)


def markdown_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace("|", "\\|").replace("\n", " ")


def result_summary_rows(results: list[ProbeResult]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for result in results:
        rows.extend(result.summary_rows)
    return rows


def print_markdown(results: list[ProbeResult]) -> None:
    columns = SUMMARY_COLUMNS
    print("| " + " | ".join(columns) + " |")
    print("| " + " | ".join("---" for _ in columns) + " |")
    for row in result_summary_rows(results):
        values = tuple(format_value(row.get(column, "")) for column in columns)
        print("| " + " | ".join(markdown_escape(value) for value in values) + " |")


def print_tsv(results: list[ProbeResult]) -> None:
    columns = SUMMARY_COLUMNS
    print("\t".join(columns))
    for row in result_summary_rows(results):
        values = tuple(format_value(row.get(column, "")).replace("\t", " ") for column in columns)
        print("\t".join(values))


def print_json(results: list[ProbeResult]) -> None:
    json.dump(
        [
            {
                "position_id": result.position_id,
                "mode": result.mode,
                "entry": result.entry,
                "threshold": result.threshold,
                "status": result.status,
                "elapsed_wall_ms": result.elapsed_wall_ms,
                "output_jsonl": str(result.output_jsonl),
                "aggregate": result.aggregate_rows,
                "summary": result.summary_rows,
                "error": result.error,
            }
            for result in results
        ],
        sys.stdout,
        ensure_ascii=False,
        indent=2,
        sort_keys=True,
    )
    print()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run vibe_othello_endgame_bench high-empty probes one position/mode at a time. "
            "Each position/mode has its own subprocess timeout, so an expensive timed-out "
            "run does not discard earlier completed JSONL files."
        )
    )
    parser.add_argument("--bench", type=Path, required=True, help="path to vibe_othello_endgame_bench")
    parser.add_argument(
        "--corpus",
        type=Path,
        default=DEFAULT_CORPUS,
        help=f"endgame corpus TSV (default: {DEFAULT_CORPUS})",
    )
    parser.add_argument(
        "--position-id",
        action="append",
        help="position id to run; repeat to choose multiple positions",
    )
    parser.add_argument("--root-mode", choices=("all", "best"), default=DEFAULT_ROOT_MODE)
    parser.add_argument("--parity", choices=("on", "off", "both"), default=DEFAULT_PARITY)
    parser.add_argument("--tt", choices=("off", "on", "both"), default=DEFAULT_TT)
    parser.add_argument("--pvs", choices=("off", "on", "both"), default=DEFAULT_PVS)
    parser.add_argument("--mode", choices=MODE_CHOICES, default=DEFAULT_MODE)
    parser.add_argument(
        "--entry",
        choices=("direct", "iterative-root"),
        default=DEFAULT_ENTRY,
        help="benchmark entry point; iterative-root measures search_iterative WLD triggering",
    )
    parser.add_argument(
        "--endgame-wld-empties",
        type=positive_int,
        default=None,
        help="single iterative-root WLD threshold when --threshold is not supplied",
    )
    parser.add_argument(
        "--threshold",
        action="append",
        type=positive_int,
        help="iterative-root WLD threshold to run; repeat for a threshold matrix",
    )
    parser.add_argument("--repeat", type=positive_int, default=DEFAULT_REPEAT)
    parser.add_argument(
        "--timeout-sec",
        type=positive_int,
        default=DEFAULT_TIMEOUT_SEC,
        help=(
            "per-position subprocess timeout in seconds; timed-out runs are reported in the "
            "summary only and are not aggregated"
        ),
    )
    parser.add_argument("--output-dir", type=Path, required=True, help="directory for JSONL output")
    parser.add_argument(
        "--aggregate-script",
        type=Path,
        default=DEFAULT_AGGREGATE_SCRIPT,
        help=f"aggregate_endgame_bench.py path (default: {DEFAULT_AGGREGATE_SCRIPT})",
    )
    parser.add_argument("--format", choices=("markdown", "tsv", "json"), default=DEFAULT_FORMAT)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.entry == "iterative-root":
        if args.mode != "wld":
            fail("--entry iterative-root supports --mode wld only")
        if not args.threshold and args.endgame_wld_empties is None:
            fail("--entry iterative-root requires --threshold or --endgame-wld-empties")
    elif args.threshold or args.endgame_wld_empties is not None:
        fail("--threshold and --endgame-wld-empties require --entry iterative-root")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    aggregate_module = load_aggregate_module(args.aggregate_script)
    position_ids = selected_position_ids(args)
    modes = selected_modes(args)
    thresholds = selected_thresholds(args)

    results: list[ProbeResult] = []
    seen: dict[tuple[str, str, str, int], int] = {}
    for index, position_id in enumerate(position_ids, start=1):
        for mode in modes:
            for threshold in thresholds:
                output_jsonl = args.output_dir / safe_filename(
                    position_id, mode, args.entry, threshold, index, seen
                )
                results.append(
                    run_probe(args, aggregate_module, position_id, mode, threshold, output_jsonl)
                )

    paired_errors = validate_paired_wld(results)
    if paired_errors:
        for error in paired_errors:
            print(f"run_high_empty_probe.py: {error}", file=sys.stderr)

    if args.format == "markdown":
        print_markdown(results)
    elif args.format == "tsv":
        print_tsv(results)
    elif args.format == "json":
        print_json(results)
    else:
        fail(f"unsupported output format: {args.format}")

    completed_count = sum(1 for result in results if result.status == "completed")
    failed_count = sum(1 for result in results if result.status == "failed")
    return 1 if failed_count > 0 or paired_errors or completed_count == 0 else 0


if __name__ == "__main__":
    raise SystemExit(main())
