#!/usr/bin/env python3
"""CTest wrapper for the tiny pattern dataset builder smoke CLI."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


EXPECTED_HEADER = (
    "record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_id\tinstance\tternary_index"
)
EXPECTED_COMPACT_HEADER = "record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_features"
EXPECTED_REJECTS = (
    "tiny-bad-coordinate",
    "tiny-bad-illegal-move",
    "tiny-bad-pass",
    "tiny-bad-malformed-label",
)
EXPECTED_ACCEPTS = ("tiny-legal-prefix", "tiny-alt-prefix")
EXPECTED_REPRESENTATIVE_ROW = (
    "tiny-legal-prefix\t3\ttest\t3\t0\tcorner-3x3\t0\t6561"
)
EXPECTED_FEATURE_ORDER = (
    "edge-8:0",
    "edge-8:1",
    "edge-8:2",
    "edge-8:3",
    "corner-3x3:0",
    "corner-3x3:1",
    "corner-3x3:2",
    "corner-3x3:3",
)


def run_cli(
    exe: Path,
    records: Path,
    manifest: Path,
    index_mode: str,
    output_format: str = "expanded-tsv",
) -> subprocess.CompletedProcess[str]:
    command = [
        str(exe),
        "--records",
        str(records),
        "--manifest",
        str(manifest),
        "--split-policy",
        "tiny-cycle",
    ]
    if index_mode == "canonical":
        command.extend(["--index-mode", "canonical"])
    if output_format != "expanded-tsv":
        command.extend(["--output-format", output_format])

    return subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
    )


def checked_lines(result: subprocess.CompletedProcess[str]) -> list[str] | None:
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return None

    lines = result.stdout.splitlines()
    if not lines or lines[0] != EXPECTED_HEADER:
        print("missing or invalid TSV header", file=sys.stderr)
        return None

    data_lines = lines[1:]
    if len(data_lines) != 48:
        print(f"unexpected dataset row count: {len(data_lines)}", file=sys.stderr)
        return None

    for split in ("train", "validation", "test"):
        if not any(line.split("\t")[2] == split for line in data_lines):
            print(f"missing deterministic split: {split}", file=sys.stderr)
            return None

    if not any("\tedge-8\t" in line for line in data_lines):
        print("missing edge-8 dataset rows", file=sys.stderr)
        return None
    if not any("\tcorner-3x3\t" in line for line in data_lines):
        print("missing corner-3x3 dataset rows", file=sys.stderr)
        return None

    for record_id in EXPECTED_ACCEPTS:
        if not any(line.startswith(record_id + "\t") for line in data_lines):
            print(f"missing expected-good dataset rows: {record_id}", file=sys.stderr)
            return None

    for record_id in EXPECTED_REJECTS:
        if any(line.startswith(record_id + "\t") for line in data_lines):
            print(f"unexpected dataset row for rejected record: {record_id}", file=sys.stderr)
            return None
        if record_id not in result.stderr:
            print(f"missing expected reject reason for {record_id}", file=sys.stderr)
            return None

    expected_summary_parts = (
        "summary total_records=6 accepted_records=2 rejected_records=4 emitted_rows=48",
        "train_rows=16 validation_rows=16 test_rows=16",
        "split_policy=tiny-cycle duplicate_policy=keep_all_input_order",
    )
    for part in expected_summary_parts:
        if part not in result.stderr:
            print(f"missing summary part: {part}", file=sys.stderr)
            return None

    return lines


def checked_compact_lines(result: subprocess.CompletedProcess[str]) -> list[str] | None:
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return None

    lines = result.stdout.splitlines()
    if not lines or lines[0] != EXPECTED_COMPACT_HEADER:
        print("missing or invalid compact TSV header", file=sys.stderr)
        return None

    data_lines = lines[1:]
    if len(data_lines) != 6:
        print(f"unexpected compact dataset row count: {len(data_lines)}", file=sys.stderr)
        return None

    expected_summary_parts = (
        "summary total_records=6 accepted_records=2 rejected_records=4 emitted_rows=6",
        "output_format=compact-tsv",
        "split_policy=tiny-cycle duplicate_policy=keep_all_input_order",
    )
    for part in expected_summary_parts:
        if part not in result.stderr:
            print(f"missing compact summary part: {part}", file=sys.stderr)
            return None

    return lines


def parse_rows(lines: list[str]) -> dict[tuple[str, ...], int] | None:
    rows: dict[tuple[str, ...], int] = {}
    for line in lines[1:]:
        fields = line.split("\t")
        if len(fields) != 8:
            print(f"invalid dataset row: {line}", file=sys.stderr)
            return None
        key = tuple(fields[:7])
        if key in rows:
            print(f"duplicate dataset key: {key}", file=sys.stderr)
            return None
        try:
            rows[key] = int(fields[7])
        except ValueError:
            print(f"invalid ternary index: {line}", file=sys.stderr)
            return None
    return rows


def expanded_feature_groups(lines: list[str]) -> dict[tuple[str, ...], list[str]] | None:
    groups: dict[tuple[str, ...], list[str]] = {}
    for line in lines[1:]:
        fields = line.split("\t")
        if len(fields) != 8:
            print(f"invalid expanded dataset row: {line}", file=sys.stderr)
            return None
        key = tuple(fields[:5])
        token = f"{fields[5]}:{fields[6]}:{fields[7]}"
        groups.setdefault(key, []).append(token)

    for key, tokens in groups.items():
        actual_order = tuple(":".join(token.split(":")[:2]) for token in tokens)
        if actual_order != EXPECTED_FEATURE_ORDER:
            print(f"unexpected feature occurrence order for {key}: {actual_order}", file=sys.stderr)
            return None

    return groups


def compact_feature_groups(lines: list[str]) -> dict[tuple[str, ...], list[str]] | None:
    groups: dict[tuple[str, ...], list[str]] = {}
    for line in lines[1:]:
        fields = line.split("\t")
        if len(fields) != 6:
            print(f"invalid compact dataset row: {line}", file=sys.stderr)
            return None
        key = tuple(fields[:5])
        tokens = fields[5].split(",")
        if len(tokens) != len(EXPECTED_FEATURE_ORDER) or not all(
            len(token.split(":")) == 3 for token in tokens
        ):
            print(f"invalid compact feature list: {fields[5]}", file=sys.stderr)
            return None
        groups[key] = tokens
    return groups


def check_compact_parity(args: argparse.Namespace, expanded_lines: list[str]) -> int:
    compact_result = run_cli(args.exe, args.records, args.manifest, args.index_mode, "compact-tsv")
    compact_lines = checked_compact_lines(compact_result)
    if compact_lines is None:
        return 1

    expanded_groups = expanded_feature_groups(expanded_lines)
    compact_groups = compact_feature_groups(compact_lines)
    if expanded_groups is None or compact_groups is None:
        return 1
    if expanded_groups != compact_groups:
        print("compact feature payloads do not match expanded feature rows", file=sys.stderr)
        return 1
    return 0


def check_canonical(args: argparse.Namespace, canonical_lines: list[str]) -> int:
    raw_result = run_cli(args.exe, args.records, args.manifest, "raw")
    raw_lines = checked_lines(raw_result)
    if raw_lines is None:
        return 1

    raw_rows = parse_rows(raw_lines)
    canonical_rows = parse_rows(canonical_lines)
    if raw_rows is None or canonical_rows is None:
        return 1
    if len(raw_rows) != len(canonical_rows):
        print(
            f"raw/canonical row count mismatch: {len(raw_rows)} != {len(canonical_rows)}",
            file=sys.stderr,
        )
        return 1
    if raw_rows.keys() != canonical_rows.keys():
        print("raw/canonical dataset keys differ", file=sys.stderr)
        return 1

    changed_rows = 0
    corner_changed = False
    for key, raw_index in raw_rows.items():
        canonical_index = canonical_rows[key]
        if canonical_index > raw_index:
            print(
                f"canonical index is greater than raw for {key}: {canonical_index} > {raw_index}",
                file=sys.stderr,
            )
            return 1
        if canonical_index != raw_index:
            changed_rows += 1
            corner_changed = corner_changed or key[5] == "corner-3x3"

    if changed_rows == 0:
        print("canonical mode did not change any ternary_index values", file=sys.stderr)
        return 1
    if not corner_changed:
        print("canonical mode did not change a corner-3x3 representative row", file=sys.stderr)
        return 1

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--records", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--index-mode", choices=("raw", "canonical"), default="raw")
    args = parser.parse_args()

    result = run_cli(args.exe, args.records, args.manifest, args.index_mode)
    lines = checked_lines(result)
    if lines is None:
        return result.returncode if result.returncode != 0 else 1

    if args.index_mode == "raw":
        if EXPECTED_REPRESENTATIVE_ROW not in lines[1:]:
            print("missing deterministic representative dataset row", file=sys.stderr)
            return 1
        return check_compact_parity(args, lines)

    return check_canonical(args, lines)


if __name__ == "__main__":
    raise SystemExit(main())
