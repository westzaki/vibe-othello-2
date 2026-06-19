#!/usr/bin/env python3
"""CTest wrapper for fixed-position learned pattern search smoke."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


def run_capture(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, capture_output=True, text=True)


def run_or_report(command: list[str]) -> subprocess.CompletedProcess[str] | None:
    result = run_capture(command)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return None
    return result


def parse_key_values(text: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in text.splitlines():
        key, separator, value = line.partition("=")
        if not separator:
            raise ValueError(f"line is missing '=': {line}")
        if key in values:
            raise ValueError(f"duplicate key: {key}")
        values[key] = value
    return values


def run_egaroucid_importer(importer: Path, fixture: Path, manifest: Path) -> str | None:
    result = run_or_report(
        [
            sys.executable,
            str(importer),
            "--input",
            str(fixture),
            "--manifest",
            str(manifest),
        ]
    )
    return None if result is None else result.stdout


def run_dataset_from_normalized(exe: Path, normalized_tsv: Path, report: Path) -> str | None:
    result = run_or_report(
        [
            str(exe),
            "--normalized-tsv",
            str(normalized_tsv),
            "--report",
            str(report),
        ]
    )
    return None if result is None else result.stdout


def run_trainer(
    script: Path,
    dataset: Path,
    mode: str,
    weights: Path,
    report: Path,
) -> bool:
    command = [
        sys.executable,
        str(script),
        "--dataset",
        str(dataset),
        "--mode",
        mode,
        "--weights-out",
        str(weights),
        "--report-out",
        str(report),
    ]
    if mode == "pattern-sgd-v0b":
        command.extend(["--epochs", "8", "--learning-rate", "0.9", "--l2", "0.0", "--seed", "7"])
    return run_or_report(command) is not None


def export_v0a(
    exporter: Path, weights_tsv: Path, weights_out: Path, manifest_out: Path
) -> dict[str, str] | None:
    result = run_or_report(
        [
            sys.executable,
            str(exporter),
            "--weights-tsv",
            str(weights_tsv),
            "--weights-out",
            str(weights_out),
            "--manifest-out",
            str(manifest_out),
        ]
    )
    if result is None:
        return None
    return parse_key_values(result.stdout)


def export_v0b(
    exporter: Path, weights_json: Path, weights_out: Path, manifest_out: Path
) -> dict[str, str] | None:
    result = run_or_report(
        [
            sys.executable,
            str(exporter),
            "--weights-json",
            str(weights_json),
            "--weights-out",
            str(weights_out),
            "--manifest-out",
            str(manifest_out),
        ]
    )
    if result is None:
        return None
    return parse_key_values(result.stdout)


def check_artifact_checksum(
    report: dict[str, Any], v0a_export: dict[str, str], v0b_export: dict[str, str]
) -> bool:
    artifact_checksums = report.get("artifact_checksums")
    if not isinstance(artifact_checksums, dict):
        print("report artifact_checksums must be an object", file=sys.stderr)
        return False
    expected = {
        "v0a": v0a_export.get("weights_checksum"),
        "v0b": v0b_export.get("weights_checksum"),
    }
    if artifact_checksums != expected:
        print(
            f"artifact checksums mismatch: report={artifact_checksums!r} expected={expected!r}",
            file=sys.stderr,
        )
        return False
    return True


def check_report(
    report_path: Path,
    first_summary: dict[str, str],
    v0a_export: dict[str, str],
    v0b_export: dict[str, str],
) -> bool:
    try:
        report: dict[str, Any] = json.loads(report_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"could not read search report JSON: {error}", file=sys.stderr)
        return False

    expected_notes = [
        "local smoke only",
        "not production benchmark",
        "not strength claim",
        "depth 1 only checks evaluator signal propagation into search score",
        "Egaroucid-derived artifacts are temp-only",
        "publication remains gated / unknown",
    ]
    expected_fields: dict[str, Any] = {
        "schema_version": 1,
        "source": "tiny-egaroucid-v0b-search-smoke",
        "pattern_set_id": "fixed-pattern-fixture-v1",
        "phase_count": 13,
        "notes": expected_notes,
    }
    for key, expected in expected_fields.items():
        if report.get(key) != expected:
            print(f"report field mismatch for {key}: {report.get(key)!r}", file=sys.stderr)
            return False

    if not check_artifact_checksum(report, v0a_export, v0b_export):
        return False

    search_config = report.get("search_config")
    expected_search_config = {
        "mode": "fixed_depth",
        "entry_point": "search_iterative",
        "depth": 1,
        "depth_policy": "smoke-only leaf-evaluator propagation",
        "time_control": "off",
        "midgame_tt": "off",
        "endgame_exact": "off",
        "endgame_tt": "off",
        "endgame_parity_ordering": "off",
        "threading": "single",
        "elapsed_ms_policy": "fixed-zero-for-deterministic-smoke",
    }
    if search_config != expected_search_config:
        print(f"unexpected search_config: {search_config!r}", file=sys.stderr)
        return False

    result_rows = report.get("result_rows")
    if not isinstance(result_rows, list) or not result_rows:
        print("report result_rows must be a non-empty array", file=sys.stderr)
        return False
    positions_count = report.get("positions_count")
    if not isinstance(positions_count, int) or positions_count <= 0:
        print(f"invalid positions_count: {positions_count!r}", file=sys.stderr)
        return False
    if len(result_rows) != positions_count * 2:
        print("result_rows must contain v0a and v0b rows for every position", file=sys.stderr)
        return False
    if str(positions_count) != first_summary.get("positions_count"):
        print("summary positions_count does not match report", file=sys.stderr)
        return False
    if str(len(result_rows)) != first_summary.get("result_rows"):
        print("summary result_rows does not match report", file=sys.stderr)
        return False

    score_diff_count = report.get("v0a_v0b_score_different_count")
    best_move_diff_count = report.get("v0a_v0b_best_move_different_count")
    if not isinstance(score_diff_count, int) or score_diff_count <= 0:
        print("report did not record any v0a/v0b search score differences", file=sys.stderr)
        return False
    if not isinstance(best_move_diff_count, int) or best_move_diff_count < 0:
        print("best-move difference count must be a non-negative integer", file=sys.stderr)
        return False
    if str(score_diff_count) != first_summary.get("v0a_v0b_score_different_count"):
        print("summary score difference count does not match report", file=sys.stderr)
        return False
    if str(best_move_diff_count) != first_summary.get("v0a_v0b_best_move_different_count"):
        print("summary best-move difference count does not match report", file=sys.stderr)
        return False

    checksum = report.get("checksum")
    if not isinstance(checksum, str) or not checksum.startswith("0x"):
        print(f"report checksum is invalid: {checksum!r}", file=sys.stderr)
        return False
    if checksum != first_summary.get("checksum"):
        print("summary checksum does not match report", file=sys.stderr)
        return False

    required_row_fields = {
        "position_id",
        "disc_count",
        "phase",
        "evaluator",
        "best_move",
        "score",
        "nodes",
        "depth",
        "elapsed_ms",
    }
    rows_by_position: dict[str, dict[str, dict[str, Any]]] = {}
    for row in result_rows:
        if not isinstance(row, dict) or set(row) != required_row_fields:
            print(f"unexpected result row shape: {row!r}", file=sys.stderr)
            return False
        if row["evaluator"] not in {"v0a", "v0b"}:
            print(f"unexpected evaluator in row: {row!r}", file=sys.stderr)
            return False
        if row["depth"] != 1 or row["elapsed_ms"] != 0:
            print(f"unexpected deterministic search row fields: {row!r}", file=sys.stderr)
            return False
        if not isinstance(row["nodes"], int) or row["nodes"] <= 0:
            print(f"nodes must be positive: {row!r}", file=sys.stderr)
            return False
        rows_by_position.setdefault(row["position_id"], {})[row["evaluator"]] = row

    position_diffs = report.get("position_diffs")
    if not isinstance(position_diffs, list) or len(position_diffs) != positions_count:
        print("position_diffs must contain one row per position", file=sys.stderr)
        return False
    score_delta_nonzero = 0
    for diff in position_diffs:
        if not isinstance(diff, dict) or set(diff) != {
            "position_id",
            "score_delta",
            "best_move_different",
        }:
            print(f"unexpected position diff shape: {diff!r}", file=sys.stderr)
            return False
        row_pair = rows_by_position.get(diff["position_id"], {})
        if set(row_pair) != {"v0a", "v0b"}:
            print(f"missing v0a/v0b row pair for {diff['position_id']!r}", file=sys.stderr)
            return False
        expected_delta = row_pair["v0b"]["score"] - row_pair["v0a"]["score"]
        if diff["score_delta"] != expected_delta:
            print(f"score_delta mismatch: {diff!r}", file=sys.stderr)
            return False
        if diff["score_delta"] != 0:
            score_delta_nonzero += 1
        expected_best_move_diff = row_pair["v0b"]["best_move"] != row_pair["v0a"]["best_move"]
        if diff["best_move_different"] != expected_best_move_diff:
            print(f"best_move_different mismatch: {diff!r}", file=sys.stderr)
            return False
    if score_delta_nonzero != score_diff_count:
        print("score difference count does not match position_diffs", file=sys.stderr)
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bench-exe", required=True, type=Path)
    parser.add_argument("--v0a-exporter", required=True, type=Path)
    parser.add_argument("--v0b-exporter", required=True, type=Path)
    parser.add_argument("--trainer-v0a", required=True, type=Path)
    parser.add_argument("--dataset-exe", required=True, type=Path)
    parser.add_argument("--egaroucid-importer", required=True, type=Path)
    parser.add_argument("--egaroucid-fixture", required=True, type=Path)
    parser.add_argument("--egaroucid-manifest", required=True, type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        normalized_tsv = temp_dir / "egaroucid-normalized.tsv"
        pattern_dataset = temp_dir / "egaroucid-pattern-dataset.tsv"
        dataset_report = temp_dir / "egaroucid-dataset-report.json"

        imported_tsv = run_egaroucid_importer(
            args.egaroucid_importer,
            args.egaroucid_fixture,
            args.egaroucid_manifest,
        )
        if imported_tsv is None:
            return 1
        normalized_tsv.write_text(imported_tsv, encoding="utf-8")

        dataset_text = run_dataset_from_normalized(args.dataset_exe, normalized_tsv, dataset_report)
        if dataset_text is None:
            return 1
        pattern_dataset.write_text(dataset_text, encoding="utf-8")

        v0a_weights_tsv = temp_dir / "v0a-weights.tsv"
        v0a_report = temp_dir / "v0a-report.json"
        v0b_weights_json = temp_dir / "v0b-weights.json"
        v0b_report = temp_dir / "v0b-report.json"
        if not run_trainer(
            args.trainer_v0a, pattern_dataset, "phase-bias-v0a", v0a_weights_tsv, v0a_report
        ):
            return 1
        if not run_trainer(
            args.trainer_v0a, pattern_dataset, "pattern-sgd-v0b", v0b_weights_json, v0b_report
        ):
            return 1

        v0a_weights = temp_dir / "v0a.weights.bin"
        v0a_manifest = temp_dir / "v0a.manifest.json"
        v0b_weights = temp_dir / "v0b.weights.bin"
        v0b_manifest = temp_dir / "v0b.manifest.json"
        v0a_export = export_v0a(args.v0a_exporter, v0a_weights_tsv, v0a_weights, v0a_manifest)
        v0b_export = export_v0b(args.v0b_exporter, v0b_weights_json, v0b_weights, v0b_manifest)
        if v0a_export is None or v0b_export is None:
            return 1

        first_report = temp_dir / "fixed-position-search-report.json"
        second_report = temp_dir / "fixed-position-search-report-second.json"
        command = [
            str(args.bench_exe),
            "--positions-tsv",
            str(normalized_tsv),
            "--v0a-weights",
            str(v0a_weights),
            "--v0b-weights",
            str(v0b_weights),
            "--v0a-artifact-checksum",
            v0a_export["weights_checksum"],
            "--v0b-artifact-checksum",
            v0b_export["weights_checksum"],
        ]
        first = run_or_report([*command, "--report-out", str(first_report)])
        if first is None:
            return 1
        second = run_or_report([*command, "--report-out", str(second_report)])
        if second is None:
            return 1

        try:
            first_summary = parse_key_values(first.stdout)
            second_summary = parse_key_values(second.stdout)
        except ValueError as error:
            print(error, file=sys.stderr)
            return 1
        if first_summary != second_summary:
            print("search bench summary is not deterministic", file=sys.stderr)
            return 1
        if first_report.read_bytes() != second_report.read_bytes():
            print("search bench report JSON is not deterministic", file=sys.stderr)
            return 1
        if not check_report(first_report, first_summary, v0a_export, v0b_export):
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
