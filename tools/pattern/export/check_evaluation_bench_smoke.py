#!/usr/bin/env python3
"""CTest wrapper for fixed-position learned pattern evaluation smoke."""

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


def run_dataset_from_normalized(
    exe: Path, normalized_tsv: Path, report: Path, pattern_set: str
) -> str | None:
    result = run_or_report(
        [
            str(exe),
            "--normalized-tsv",
            str(normalized_tsv),
            "--report",
            str(report),
            "--pattern-set",
            pattern_set,
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
        command.extend(["--epochs", "8", "--learning-rate", "0.5", "--l2", "0.0", "--seed", "7"])
    return run_or_report(command) is not None


def export_v0a(
    exporter: Path, weights_tsv: Path, weights_out: Path, manifest_out: Path, pattern_set: str
) -> str | None:
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
            "--pattern-set",
            pattern_set,
        ]
    )
    if result is None:
        return None
    return parse_key_values(result.stdout)["weights_checksum"]


def export_v0b(
    exporter: Path, weights_json: Path, weights_out: Path, manifest_out: Path, pattern_set: str
) -> str | None:
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
            "--pattern-set",
            pattern_set,
        ]
    )
    if result is None:
        return None
    return parse_key_values(result.stdout)["weights_checksum"]


def smoke_source_for(pattern_set: str) -> str:
    if pattern_set == "fixed-pattern-fixture-v1" or pattern_set == "tiny":
        return "tiny-egaroucid-v0b-smoke"
    if pattern_set == "pattern-v2-endgame-lite" or pattern_set == "endgame-lite":
        return "endgame-lite-egaroucid-v0b-smoke"
    return "buro-lite-egaroucid-v0b-smoke"


def canonical_pattern_set_id(pattern_set: str) -> str:
    aliases = {
        "tiny": "fixed-pattern-fixture-v1",
        "buro-lite": "pattern-v1-buro-lite",
        "endgame-lite": "pattern-v2-endgame-lite",
    }
    return aliases.get(pattern_set, pattern_set)


def check_report(report_path: Path, first_summary: dict[str, str], pattern_set: str) -> bool:
    try:
        report: dict[str, Any] = json.loads(report_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"could not read evaluation report JSON: {error}", file=sys.stderr)
        return False

    expected_notes = [
        "local smoke only",
        "not production benchmark",
        "Egaroucid-derived artifacts are temp-only",
        "publication remains gated / unknown",
    ]
    expected_fields: dict[str, Any] = {
        "schema_version": 1,
        "source": smoke_source_for(pattern_set),
        "pattern_set_id": canonical_pattern_set_id(pattern_set),
        "phase_count": 13,
        "notes": expected_notes,
    }
    for key, expected in expected_fields.items():
        if report.get(key) != expected:
            print(f"report field mismatch for {key}: {report.get(key)!r}", file=sys.stderr)
            return False

    score_rows = report.get("score_rows")
    if not isinstance(score_rows, list) or not score_rows:
        print("report score_rows must be a non-empty array", file=sys.stderr)
        return False
    if report.get("positions_count") != len(score_rows):
        print("report positions_count does not match score_rows", file=sys.stderr)
        return False
    if str(report.get("positions_count")) != first_summary.get("positions_count"):
        print("summary positions_count does not match report", file=sys.stderr)
        return False
    if report.get("v0a_v0b_different_count", 0) <= 0:
        print("report did not record any v0a/v0b score differences", file=sys.stderr)
        return False
    if str(report.get("v0a_v0b_different_count")) != first_summary.get(
        "v0a_v0b_different_count"
    ):
        print("summary difference count does not match report", file=sys.stderr)
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
        "v0a_score",
        "v0b_score",
        "score_delta",
    }
    for row in score_rows:
        if not isinstance(row, dict) or set(row) != required_row_fields:
            print(f"unexpected score row shape: {row!r}", file=sys.stderr)
            return False
        if row["score_delta"] != row["v0b_score"] - row["v0a_score"]:
            print(f"score_delta mismatch: {row!r}", file=sys.stderr)
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
    parser.add_argument("--pattern-set", default="fixed-pattern-fixture-v1")
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

        dataset_text = run_dataset_from_normalized(
            args.dataset_exe, normalized_tsv, dataset_report, args.pattern_set
        )
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
        v0a_checksum = export_v0a(
            args.v0a_exporter, v0a_weights_tsv, v0a_weights, v0a_manifest, args.pattern_set
        )
        v0b_checksum = export_v0b(
            args.v0b_exporter, v0b_weights_json, v0b_weights, v0b_manifest, args.pattern_set
        )
        if v0a_checksum is None or v0b_checksum is None:
            return 1

        first_report = temp_dir / "fixed-position-report.json"
        second_report = temp_dir / "fixed-position-report-second.json"
        command = [
            str(args.bench_exe),
            "--positions-tsv",
            str(normalized_tsv),
            "--v0a-weights",
            str(v0a_weights),
            "--v0b-weights",
            str(v0b_weights),
            "--v0a-artifact-checksum",
            v0a_checksum,
            "--v0b-artifact-checksum",
            v0b_checksum,
            "--pattern-set",
            args.pattern_set,
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
            print("evaluation bench summary is not deterministic", file=sys.stderr)
            return 1
        if first_report.read_bytes() != second_report.read_bytes():
            print("evaluation bench report JSON is not deterministic", file=sys.stderr)
            return 1
        if not check_report(first_report, first_summary, args.pattern_set):
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
