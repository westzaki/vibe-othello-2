#!/usr/bin/env python3
"""CTest wrapper for Egaroucid importer -> pattern dataset smoke."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
import tempfile
from io import StringIO
from pathlib import Path


DATASET_ID = "egaroucid-train-data-board-score-v2025-02-02"
REPEATED_BOARD = "XXOOO-----------------------------------------------------------"
EXPECTED_DATASET_HEADER = (
    "record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_id\tinstance\tternary_index"
)
EXPECTED_COMPACT_HEADER = (
    "record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_features"
)


def run_capture(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, capture_output=True, text=True)


def import_tiny_fixture(importer: Path, fixture: Path, manifest: Path) -> str | None:
    result = run_capture(
        [
            sys.executable,
            str(importer),
            "--input",
            str(fixture),
            "--manifest",
            str(manifest),
        ]
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return None
    return result.stdout


def parse_import_rows(tsv_text: str) -> list[dict[str, str]]:
    reader = csv.DictReader(StringIO(tsv_text), delimiter="\t")
    return list(reader)


def run_dataset(
    exe: Path,
    normalized_tsv: Path,
    report: Path,
    pattern_set: str = "tiny",
    output_format: str = "expanded-tsv",
) -> subprocess.CompletedProcess[str]:
    return run_capture(
        [
            str(exe),
            "--normalized-tsv",
            str(normalized_tsv),
            "--report",
            str(report),
            "--pattern-set",
            pattern_set,
            "--output-format",
            output_format,
        ]
    )


def dataset_lines(
    result: subprocess.CompletedProcess[str], expected_line_count: int
) -> list[str] | None:
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return None
    lines = result.stdout.splitlines()
    if not lines or lines[0] != EXPECTED_DATASET_HEADER:
        print("missing or invalid dataset TSV header", file=sys.stderr)
        return None
    if len(lines) != expected_line_count:
        print(f"unexpected dataset line count: {len(lines)}", file=sys.stderr)
        return None
    return lines


def compact_dataset_lines(
    result: subprocess.CompletedProcess[str], expected_line_count: int
) -> list[str] | None:
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return None
    lines = result.stdout.splitlines()
    if not lines or lines[0] != EXPECTED_COMPACT_HEADER:
        print("missing or invalid compact dataset TSV header", file=sys.stderr)
        return None
    if len(lines) != expected_line_count:
        print(f"unexpected compact dataset line count: {len(lines)}", file=sys.stderr)
        return None
    return lines


def check_report(report: dict[str, object]) -> bool:
    expected = {
        "schema_version": 1,
        "source_dataset_ids": [DATASET_ID],
        "input_rows": 7,
        "accepted_rows": 7,
        "rejected_rows": 0,
        "counts_by_split": {"test": 1, "train": 5, "validation": 1},
        "counts_by_phase": {"0": 6, "12": 1},
        "counts_by_label_kind": {"engine_disc_estimate": 7},
        "label_min": -3,
        "label_max": 5,
        "label_mean": 0.285714,
        "repeated_position_count": 1,
        "exact_duplicate_record_count": 1,
        "split_policy": "position-sha256",
        "duplicate_policy": "keep_all_input_order",
    }
    for key, expected_value in expected.items():
        if report.get(key) != expected_value:
            print(
                f"report field mismatch for {key}: {report.get(key)!r} != {expected_value!r}",
                file=sys.stderr,
            )
            return False
    checksum = report.get("checksum")
    if not isinstance(checksum, str) or not checksum.startswith("0x") or len(checksum) != 18:
        print(f"invalid report checksum: {checksum!r}", file=sys.stderr)
        return False
    return True


def check_compact_output(
    compact_lines: list[str],
    compact_report: dict[str, object],
    expanded_feature_rows: int,
    expected_examples: int,
    expected_features_per_row: int,
) -> bool:
    if compact_report.get("output_format") != "compact-tsv":
        print(f"unexpected compact output format: {compact_report.get('output_format')!r}", file=sys.stderr)
        return False
    if compact_report.get("example_rows") != expected_examples:
        print(f"unexpected compact example rows: {compact_report.get('example_rows')!r}", file=sys.stderr)
        return False
    if compact_report.get("feature_occurrence_count") != expanded_feature_rows:
        print(
            f"compact feature occurrence count mismatch: {compact_report.get('feature_occurrence_count')!r}",
            file=sys.stderr,
        )
        return False
    if compact_report.get("max_features_per_example") != expected_features_per_row:
        print(
            f"unexpected compact max features: {compact_report.get('max_features_per_example')!r}",
            file=sys.stderr,
        )
        return False
    for line in compact_lines[1:]:
        fields = line.split("\t")
        if len(fields) != 6:
            print(f"compact line did not have 6 fields: {line!r}", file=sys.stderr)
            return False
        tokens = fields[5].split(",")
        if len(tokens) != expected_features_per_row:
            print(f"unexpected compact token count: {len(tokens)}", file=sys.stderr)
            return False
        if any(len(token.split(":")) != 3 for token in tokens):
            print(f"malformed compact token list: {fields[5]!r}", file=sys.stderr)
            return False
    return True


def check_repeated_positions(
    import_rows: list[dict[str, str]], dataset: list[str], expected_features_per_row: int
) -> bool:
    dataset_rows = dataset[1:]
    for row in import_rows:
        expected_ply = int(row["occupied_count"]) - 4
        prefix = (
            f"{row['record_id']}\t{expected_ply}\t"
            f"{row['split']}\t{row['label_score_side_to_move']}\t"
        )
        matching = [line for line in dataset_rows if line.startswith(prefix)]
        if len(matching) != expected_features_per_row:
            print(
                f"expected {expected_features_per_row} pattern rows with imported ply for "
                f"{row['record_id']}",
                file=sys.stderr,
            )
            return False

    repeated_rows = [row for row in import_rows if row["board_a1_to_h8"] == REPEATED_BOARD]
    if len(repeated_rows) != 3:
        print("expected three imported rows for the repeated board", file=sys.stderr)
        return False
    if len({row["position_id"] for row in repeated_rows}) != 1:
        print("same board with different scores did not keep one position_id", file=sys.stderr)
        return False
    if len({row["split"] for row in repeated_rows}) != 1:
        print("same board with different scores did not keep one split", file=sys.stderr)
        return False
    if {row["label_score_side_to_move"] for row in repeated_rows} != {"-3", "5"}:
        print("repeated board did not preserve different scores", file=sys.stderr)
        return False

    exact_duplicate_ids = [
        row["record_id"] for row in repeated_rows if row["label_score_side_to_move"] == "-3"
    ]
    if exact_duplicate_ids != [
        f"{DATASET_ID}-09a5338318be196c-000001",
        f"{DATASET_ID}-09a5338318be196c-000002",
    ]:
        print("exact duplicate record ids changed", file=sys.stderr)
        return False
    return True


def check_invalid_validation(exe: Path, valid_tsv: str, temp_dir: Path) -> bool:
    lines = valid_tsv.splitlines()
    header = lines[0].split("\t")
    rows = [line.split("\t") for line in lines[1:]]
    cases = {
        "bad-split": ("split", "oops", "split must be train"),
        "bad-kind": ("label_kind", "final_disc_diff", "label_kind must be"),
        "bad-score": ("label_score_side_to_move", "65", "label_score_side_to_move must be"),
        "bad-count": ("player_disc_count", "99", "board counts do not match"),
        "bad-occupied-range": ("occupied_count", "64", "occupied_count must be in [4, 63]"),
        "bad-phase-range": ("phase", "13", "phase must be in [0, 12]"),
        "bad-phase-mismatch": ("phase", "1", "phase must match occupied_count"),
    }
    for name, (field, value, expected_error) in cases.items():
        field_index = header.index(field)
        mutated_rows = [row.copy() for row in rows]
        mutated_rows[0][field_index] = value
        mutated = "\n".join(["\t".join(header)] + ["\t".join(row) for row in mutated_rows]) + "\n"
        tsv_path = temp_dir / f"{name}.tsv"
        report_path = temp_dir / f"{name}.json"
        tsv_path.write_text(mutated, encoding="utf-8")
        result = run_dataset(exe, tsv_path, report_path)
        if result.returncode == 0:
            print(f"{name}: dataset builder unexpectedly accepted invalid TSV", file=sys.stderr)
            return False
        if expected_error not in result.stderr:
            print(f"{name}: missing expected error {expected_error!r}", file=sys.stderr)
            sys.stderr.write(result.stderr)
            return False
    return True


def check_unknown_output_format(exe: Path, valid_tsv: str, temp_dir: Path) -> bool:
    tsv_path = temp_dir / "valid-for-format.tsv"
    report_path = temp_dir / "bad-format-report.json"
    tsv_path.write_text(valid_tsv, encoding="utf-8")
    result = run_capture(
        [
            str(exe),
            "--normalized-tsv",
            str(tsv_path),
            "--report",
            str(report_path),
            "--output-format",
            "binary",
        ]
    )
    if result.returncode == 0:
        print("dataset builder unexpectedly accepted unknown output format", file=sys.stderr)
        return False
    if "--output-format must be expanded-tsv or compact-tsv" not in result.stderr:
        print("unknown output format error was not clear", file=sys.stderr)
        sys.stderr.write(result.stderr)
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--importer", required=True, type=Path)
    parser.add_argument("--fixture", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    args = parser.parse_args()

    imported_tsv = import_tiny_fixture(args.importer, args.fixture, args.manifest)
    if imported_tsv is None:
        return 1
    import_rows = parse_import_rows(imported_tsv)

    with tempfile.TemporaryDirectory() as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        normalized_tsv = temp_dir / "egaroucid-normalized.tsv"
        first_report = temp_dir / "first-report.json"
        second_report = temp_dir / "second-report.json"
        normalized_tsv.write_text(imported_tsv, encoding="utf-8")

        first = run_dataset(args.exe, normalized_tsv, first_report)
        first_lines = dataset_lines(first, expected_line_count=57)
        if first_lines is None:
            return 1
        first_report_text = first_report.read_text(encoding="utf-8")

        second = run_dataset(args.exe, normalized_tsv, second_report)
        second_lines = dataset_lines(second, expected_line_count=57)
        if second_lines is None:
            return 1
        second_report_text = second_report.read_text(encoding="utf-8")

        if first.stdout != second.stdout:
            print("dataset TSV is not deterministic across repeated runs", file=sys.stderr)
            return 1
        if first_report_text != second_report_text:
            print("dataset report is not deterministic across repeated runs", file=sys.stderr)
            return 1

        try:
            report = json.loads(first_report_text)
        except json.JSONDecodeError as error:
            print(f"dataset report is not valid JSON: {error}", file=sys.stderr)
            return 1
        if not check_report(report):
            return 1
        if not check_repeated_positions(import_rows, first_lines, expected_features_per_row=8):
            return 1
        compact_report_path = temp_dir / "compact-report.json"
        compact = run_dataset(
            args.exe,
            normalized_tsv,
            compact_report_path,
            output_format="compact-tsv",
        )
        compact_lines = compact_dataset_lines(compact, expected_line_count=8)
        if compact_lines is None:
            return 1
        compact_report = json.loads(compact_report_path.read_text(encoding="utf-8"))
        if compact_report.get("counts_by_split") != report.get("counts_by_split"):
            print("compact split counts do not match expanded report", file=sys.stderr)
            return 1
        if compact_report.get("counts_by_phase") != report.get("counts_by_phase"):
            print("compact phase counts do not match expanded report", file=sys.stderr)
            return 1
        if not check_compact_output(
            compact_lines,
            compact_report,
            expanded_feature_rows=len(first_lines) - 1,
            expected_examples=7,
            expected_features_per_row=8,
        ):
            return 1
        if not check_invalid_validation(args.exe, imported_tsv, temp_dir):
            return 1
        if not check_unknown_output_format(args.exe, imported_tsv, temp_dir):
            return 1

        buro_report = temp_dir / "buro-lite-report.json"
        buro = run_dataset(
            args.exe, normalized_tsv, buro_report, pattern_set="pattern-v1-buro-lite"
        )
        buro_lines = dataset_lines(buro, expected_line_count=183)
        if buro_lines is None:
            return 1
        if not any("\tnear-edge-8\t" in line for line in buro_lines[1:]):
            print("buro-lite dataset rows are missing near-edge-8", file=sys.stderr)
            return 1
        if not any("\tcorner-2x5\t" in line for line in buro_lines[1:]):
            print("buro-lite dataset rows are missing corner-2x5", file=sys.stderr)
            return 1
        if not check_repeated_positions(
            import_rows, buro_lines, expected_features_per_row=26
        ):
            return 1
        compact_buro_report = temp_dir / "buro-lite-compact-report.json"
        compact_buro = run_dataset(
            args.exe,
            normalized_tsv,
            compact_buro_report,
            pattern_set="pattern-v1-buro-lite",
            output_format="compact-tsv",
        )
        compact_buro_lines = compact_dataset_lines(compact_buro, expected_line_count=8)
        if compact_buro_lines is None:
            return 1
        if not check_compact_output(
            compact_buro_lines,
            json.loads(compact_buro_report.read_text(encoding="utf-8")),
            expanded_feature_rows=len(buro_lines) - 1,
            expected_examples=7,
            expected_features_per_row=26,
        ):
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
