#!/usr/bin/env python3
"""Shared normalized-v2 smoke pipeline helpers for pattern export tests."""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Callable


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


def add_bench_smoke_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--bench-exe", required=True, type=Path)
    parser.add_argument("--v0a-exporter", required=True, type=Path)
    parser.add_argument("--v0b-exporter", required=True, type=Path)
    parser.add_argument("--catalog-dump-exe", required=True, type=Path)
    parser.add_argument("--trainer", required=True, type=Path)
    parser.add_argument("--dataset-exe", required=True, type=Path)
    parser.add_argument("--normalized-tsv", required=True, type=Path)
    parser.add_argument("--pattern-set", default="fixed-pattern-fixture-v1")


def canonical_pattern_set_id(pattern_set: str) -> str:
    aliases = {
        "tiny": "fixed-pattern-fixture-v1",
        "buro-lite": "pattern-v1-buro-lite",
        "endgame-lite": "pattern-v2-endgame-lite",
    }
    return aliases.get(pattern_set, pattern_set)


def smoke_source_for(pattern_set: str, *, search: bool) -> str:
    prefix = {
        "fixed-pattern-fixture-v1": "tiny",
        "tiny": "tiny",
        "pattern-v2-endgame-lite": "endgame-lite",
        "endgame-lite": "endgame-lite",
    }.get(pattern_set, "buro-lite")
    suffix = "-search-smoke" if search else "-smoke"
    return f"{prefix}-synthetic-v0b{suffix}"


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
    extra_args: list[str] | None = None,
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
        *(extra_args or []),
    ]
    return run_or_report(command) is not None


def train_v0a_v0b_from_normalized(
    trainer: Path,
    dataset_exe: Path,
    normalized_tsv: Path,
    temp_dir: Path,
    pattern_set: str,
    v0b_extra_args: list[str],
) -> tuple[Path, Path] | None:
    dataset_report = temp_dir / "synthetic-dataset-report.json"
    pattern_dataset = temp_dir / "synthetic-pattern-dataset.tsv"
    dataset_text = run_dataset_from_normalized(
        dataset_exe, normalized_tsv, dataset_report, pattern_set
    )
    if dataset_text is None:
        return None
    pattern_dataset.write_text(dataset_text, encoding="utf-8")

    v0a_weights = temp_dir / "v0a-weights.tsv"
    v0a_report = temp_dir / "v0a-report.json"
    v0b_weights = temp_dir / "v0b-weights.json"
    v0b_report = temp_dir / "v0b-report.json"
    if not run_trainer(trainer, pattern_dataset, "phase-bias-v0a", v0a_weights, v0a_report):
        return None
    if not run_trainer(
        trainer,
        pattern_dataset,
        "pattern-sgd-v0b",
        v0b_weights,
        v0b_report,
        [*v0b_extra_args, "--dataset-report", str(dataset_report)],
    ):
        return None
    return v0a_weights, v0b_weights


def export_v0a(
    exporter: Path, weights_tsv: Path, weights_out: Path, manifest_out: Path, pattern_set: str
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
            "--pattern-set",
            pattern_set,
        ]
    )
    if result is None:
        return None
    return parse_key_values(result.stdout)


def export_v0b(
    exporter: Path,
    weights_json: Path,
    weights_out: Path,
    manifest_out: Path,
    pattern_set: str,
    catalog_dump_exe: Path,
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
            "--pattern-set",
            pattern_set,
            "--catalog-dump-exe",
            str(catalog_dump_exe),
        ]
    )
    if result is None:
        return None
    return parse_key_values(result.stdout)


def build_v0a_v0b_artifacts(
    *,
    trainer: Path,
    dataset_exe: Path,
    normalized_tsv: Path,
    temp_dir: Path,
    pattern_set: str,
    v0a_exporter: Path,
    v0b_exporter: Path,
    catalog_dump_exe: Path,
    v0b_extra_args: list[str],
) -> tuple[Path, Path, dict[str, str], dict[str, str]] | None:
    trained = train_v0a_v0b_from_normalized(
        trainer, dataset_exe, normalized_tsv, temp_dir, pattern_set, v0b_extra_args
    )
    if trained is None:
        return None
    v0a_weights_tsv, v0b_weights_json = trained

    v0a_weights = temp_dir / "v0a.weights.bin"
    v0a_manifest = temp_dir / "v0a.manifest.json"
    v0b_weights = temp_dir / "v0b.weights.bin"
    v0b_manifest = temp_dir / "v0b.manifest.json"
    v0a_export = export_v0a(v0a_exporter, v0a_weights_tsv, v0a_weights, v0a_manifest, pattern_set)
    v0b_export = export_v0b(
        v0b_exporter, v0b_weights_json, v0b_weights, v0b_manifest, pattern_set, catalog_dump_exe
    )
    if v0a_export is None or v0b_export is None:
        return None
    return v0a_weights, v0b_weights, v0a_export, v0b_export


def run_deterministic_bench_smoke(
    args: argparse.Namespace,
    *,
    report_stem: str,
    label: str,
    check_report: Callable[
        [Path, dict[str, str], dict[str, str], dict[str, str], str], bool
    ],
) -> bool:
    with tempfile.TemporaryDirectory() as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        artifacts = build_v0a_v0b_artifacts(
            trainer=args.trainer,
            dataset_exe=args.dataset_exe,
            normalized_tsv=args.normalized_tsv,
            temp_dir=temp_dir,
            pattern_set=args.pattern_set,
            v0a_exporter=args.v0a_exporter,
            v0b_exporter=args.v0b_exporter,
            catalog_dump_exe=args.catalog_dump_exe,
            v0b_extra_args=[
                "--epochs",
                "8",
                "--learning-rate",
                "0.5",
                "--l2",
                "0.0",
                "--seed",
                "7",
            ],
        )
        if artifacts is None:
            return False
        v0a_weights, v0b_weights, v0a_export, v0b_export = artifacts

        first_report = temp_dir / f"{report_stem}.json"
        second_report = temp_dir / f"{report_stem}-second.json"
        command = [
            str(args.bench_exe),
            "--positions-tsv",
            str(args.normalized_tsv),
            "--v0a-weights",
            str(v0a_weights),
            "--v0b-weights",
            str(v0b_weights),
            "--v0a-artifact-checksum",
            v0a_export["weights_checksum"],
            "--v0b-artifact-checksum",
            v0b_export["weights_checksum"],
            "--pattern-set",
            args.pattern_set,
        ]
        first = run_or_report([*command, "--report-out", str(first_report)])
        second = run_or_report([*command, "--report-out", str(second_report)])
        if first is None or second is None:
            return False

        try:
            first_summary = parse_key_values(first.stdout)
            second_summary = parse_key_values(second.stdout)
        except ValueError as error:
            print(error, file=sys.stderr)
            return False
        if first_summary != second_summary:
            print(f"{label} bench summary is not deterministic", file=sys.stderr)
            return False
        if first_report.read_bytes() != second_report.read_bytes():
            print(f"{label} bench report JSON is not deterministic", file=sys.stderr)
            return False
        return check_report(
            first_report, first_summary, v0a_export, v0b_export, args.pattern_set
        )
