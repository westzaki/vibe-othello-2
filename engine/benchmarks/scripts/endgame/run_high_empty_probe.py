#!/usr/bin/env python3
"""Run high-empty exact endgame benchmark probes one position at a time."""

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
DEFAULT_REPEAT = 1
DEFAULT_TIMEOUT_SEC = 120
DEFAULT_FORMAT = "markdown"
DEFAULT_CORPUS = Path("engine/fixtures/endgame/positions.tsv")
DEFAULT_AGGREGATE_SCRIPT = Path("engine/benchmarks/scripts/endgame/aggregate_endgame_bench.py")


@dataclass
class ProbeResult:
    position_id: str
    status: str
    elapsed_wall_ms: int
    output_jsonl: Path
    aggregate_rows: list[dict[str, Any]] = field(default_factory=list)
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


def safe_filename(position_id: str, index: int, seen: dict[str, int]) -> str:
    safe_id = re.sub(r"[^A-Za-z0-9_.-]+", "_", position_id).strip("._")
    if not safe_id:
        safe_id = "position"

    seen[position_id] = seen.get(position_id, 0) + 1
    suffix = "" if seen[position_id] == 1 else f"-{seen[position_id]}"
    return f"{index:02d}-{safe_id}{suffix}.jsonl"


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


def validate_jsonl(path: Path, position_id: str) -> list[dict[str, Any]]:
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
                records.append(record)
    except OSError as error:
        raise ValueError(f"failed to read {path}: {error}") from error

    if not records:
        raise ValueError(f"{path}: completed run produced no JSONL records")
    return records


def aggregate_jsonl(aggregate_module: Any, path: Path) -> list[dict[str, Any]]:
    try:
        records = aggregate_module.load_records([path])
        return aggregate_module.aggregate(records, aggregate_module.DEFAULT_GROUP_BY)
    except SystemExit as error:
        raise ValueError(f"aggregate failed for {path}: {error}") from error


def run_probe(
    args: argparse.Namespace,
    aggregate_module: Any,
    position_id: str,
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
        "--repeat",
        str(args.repeat),
        "--corpus",
        str(args.corpus),
    ]

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
        return ProbeResult(position_id, "timed_out", elapsed_wall_ms, output_jsonl, error=message)
    except OSError as error:
        elapsed_wall_ms = round((time.monotonic() - start) * 1000)
        return ProbeResult(
            position_id,
            "failed",
            elapsed_wall_ms,
            output_jsonl,
            error=f"failed to run benchmark: {error}",
        )

    elapsed_wall_ms = round((time.monotonic() - start) * 1000)
    if completed.returncode != 0:
        error = completed.stderr.strip() if completed.stderr else f"exit code {completed.returncode}"
        return ProbeResult(position_id, "failed", elapsed_wall_ms, output_jsonl, error=error)

    try:
        validate_jsonl(output_jsonl, position_id)
        aggregate_rows = aggregate_jsonl(aggregate_module, output_jsonl)
    except ValueError as error:
        return ProbeResult(position_id, "failed", elapsed_wall_ms, output_jsonl, error=str(error))

    return ProbeResult(position_id, "completed", elapsed_wall_ms, output_jsonl, aggregate_rows)


def format_value(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, float):
        return f"{value:.6g}"
    return str(value)


def compact_aggregate(rows: list[dict[str, Any]]) -> str:
    if not rows:
        return ""

    pieces: list[str] = []
    for row in rows:
        label = ",".join(
            f"{field}={format_value(row[field])}"
            for field in ("empties", "parity_ordering", "tt_mode", "root_mode")
            if field in row
        )
        metrics = ",".join(
            f"{field}={format_value(row[field])}"
            for field in (
                "count",
                "elapsed_ms_p50",
                "nodes_p50",
                "endgame_nodes_p50",
                "nps_p50",
                "tt_hit_rate",
                "tt_cutoff_rate",
            )
            if field in row
        )
        pieces.append(f"{label} {metrics}".strip())
    return "; ".join(pieces)


def markdown_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace("|", "\\|").replace("\n", " ")


def print_markdown(results: list[ProbeResult]) -> None:
    columns = ("position_id", "status", "elapsed_wall_ms", "output_jsonl", "aggregate")
    print("| " + " | ".join(columns) + " |")
    print("| " + " | ".join("---" for _ in columns) + " |")
    for result in results:
        values = (
            result.position_id,
            result.status,
            str(result.elapsed_wall_ms),
            str(result.output_jsonl),
            compact_aggregate(result.aggregate_rows) or result.error,
        )
        print("| " + " | ".join(markdown_escape(value) for value in values) + " |")


def print_tsv(results: list[ProbeResult]) -> None:
    columns = ("position_id", "status", "elapsed_wall_ms", "output_jsonl", "aggregate")
    print("\t".join(columns))
    for result in results:
        values = (
            result.position_id,
            result.status,
            str(result.elapsed_wall_ms),
            str(result.output_jsonl),
            compact_aggregate(result.aggregate_rows) or result.error.replace("\t", " "),
        )
        print("\t".join(values))


def print_json(results: list[ProbeResult]) -> None:
    json.dump(
        [
            {
                "position_id": result.position_id,
                "status": result.status,
                "elapsed_wall_ms": result.elapsed_wall_ms,
                "output_jsonl": str(result.output_jsonl),
                "aggregate": result.aggregate_rows,
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
            "Run vibe_othello_endgame_bench high-empty exact probes one position at a time. "
            "Each position has its own subprocess timeout, so an expensive timed-out position "
            "does not discard earlier completed JSONL files."
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
    args.output_dir.mkdir(parents=True, exist_ok=True)
    aggregate_module = load_aggregate_module(args.aggregate_script)
    position_ids = selected_position_ids(args)

    results: list[ProbeResult] = []
    seen: dict[str, int] = {}
    for index, position_id in enumerate(position_ids, start=1):
        output_jsonl = args.output_dir / safe_filename(position_id, index, seen)
        results.append(run_probe(args, aggregate_module, position_id, output_jsonl))

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
    return 1 if failed_count > 0 or completed_count == 0 else 0


if __name__ == "__main__":
    raise SystemExit(main())
