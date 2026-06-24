#!/usr/bin/env python3
"""CTest wrapper for tiny pattern artifact export and runtime round-trip."""

from __future__ import annotations

import argparse
import json
import sys
import tempfile
from pathlib import Path

from smoke_pipeline import (
    parse_key_values,
    run_capture,
    run_dataset_from_normalized,
    run_or_report,
    run_trainer,
)


EXPECTED_EXPORT_SUMMARY = {
    "format_version": "1",
    "bit_order": "a1-lsb",
    "score_unit": "disc-diff",
    "score_scale": "1",
    "phase_count": "2",
    "pattern_set_id": "fixed-pattern-fixture-v1",
    "weights_checksum": "0x3c640916",
    "weights_size_bytes": "210016",
    "source": "tools/pattern/train tiny deterministic smoke summary",
    "training_note": "tiny smoke phase-bias baseline; pattern tables are zero-filled",
}

EXPECTED_ROUNDTRIP_SUMMARY = {
    "loaded_pattern_set_id": "fixed-pattern-fixture-v1",
    "phase_count": "2",
    "phase_stride": "26245",
    "phase_bias[0]": "3",
    "phase_bias[1]": "0",
    "representative_score": "3",
}


def run_trainer_v0b(script: Path, dataset: Path, weights: Path, report: Path) -> bool:
    return run_trainer(
        script,
        dataset,
        "pattern-sgd-v0b",
        weights,
        report,
        ["--epochs", "8", "--learning-rate", "0.1", "--l2", "0.0", "--seed", "7"],
    )


def check_v0b_manifest(
    manifest_path: Path,
    weights_path: Path,
    export_summary: dict[str, str],
) -> bool:
    manifest_data = json.loads(manifest_path.read_text(encoding="utf-8"))
    expected = {
        "format_version": 1,
        "bit_order": "a1-lsb",
        "score_unit": "disc-diff",
        "score_scale": 1,
        "phase_count": 13,
        "pattern_set_id": "fixed-pattern-fixture-v1",
        "source_weights_schema_version": "pattern-eval-weights-v1",
        "notes": "local smoke artifact; not production",
    }
    for key, expected_value in expected.items():
        if manifest_data.get(key) != expected_value:
            print(
                f"v0b manifest field mismatch for {key}: {manifest_data.get(key)!r}",
                file=sys.stderr,
            )
            return False
    if manifest_data.get("weights_file") != weights_path.name:
        print("v0b manifest weights_file does not point at generated weights", file=sys.stderr)
        return False
    if manifest_data.get("weights_checksum") != export_summary.get("weights_checksum"):
        print("v0b manifest checksum does not match exporter summary", file=sys.stderr)
        return False
    source_checksum = manifest_data.get("source_weights_checksum")
    if not isinstance(source_checksum, str) or not source_checksum.startswith("sha256:"):
        print(f"invalid v0b source_weights_checksum: {source_checksum!r}", file=sys.stderr)
        return False
    if manifest_data.get("source_weights_checksum") != export_summary.get(
        "source_weights_checksum"
    ):
        print("v0b source checksum does not match exporter summary", file=sys.stderr)
        return False
    phase_bias = manifest_data.get("phase_bias")
    if not isinstance(phase_bias, list) or len(phase_bias) != 13:
        print(f"invalid v0b phase_bias manifest payload: {phase_bias!r}", file=sys.stderr)
        return False
    nonzero = manifest_data.get("nonzero_pattern_weights")
    if not isinstance(nonzero, int) or nonzero <= 0:
        print(f"invalid v0b nonzero pattern count: {nonzero!r}", file=sys.stderr)
        return False
    return True


def check_v0b_roundtrip(
    exporter: Path,
    roundtrip: Path,
    weights_json: Path,
    temp_dir: Path,
    v0a_representative_score: str,
) -> bool:
    first_weights = temp_dir / "v0b-smoke.weights.bin"
    first_manifest = temp_dir / "v0b-smoke.manifest.json"
    second_weights = temp_dir / "v0b-smoke-second.weights.bin"
    second_manifest = temp_dir / "v0b-smoke-second.manifest.json"

    first_export = run_or_report(
        [
            sys.executable,
            str(exporter),
            "--weights-json",
            str(weights_json),
            "--weights-out",
            str(first_weights),
            "--manifest-out",
            str(first_manifest),
        ]
    )
    if first_export is None:
        return False
    second_export = run_or_report(
        [
            sys.executable,
            str(exporter),
            "--weights-json",
            str(weights_json),
            "--weights-out",
            str(second_weights),
            "--manifest-out",
            str(second_manifest),
        ]
    )
    if second_export is None:
        return False

    try:
        first_summary = parse_key_values(first_export.stdout)
        second_summary = parse_key_values(second_export.stdout)
    except ValueError as error:
        print(error, file=sys.stderr)
        return False
    if first_summary != second_summary:
        print("v0b exporter summary is not deterministic", file=sys.stderr)
        return False
    if first_weights.read_bytes() != second_weights.read_bytes():
        print("v0b exported artifact bytes are not deterministic", file=sys.stderr)
        return False
    if not check_v0b_manifest(first_manifest, first_weights, first_summary):
        return False

    first_roundtrip = run_or_report(
        [str(roundtrip), "--weights", str(first_weights), "--phase-count", "13"]
    )
    if first_roundtrip is None:
        return False
    second_roundtrip = run_or_report(
        [str(roundtrip), "--weights", str(second_weights), "--phase-count", "13"]
    )
    if second_roundtrip is None:
        return False

    try:
        first_loaded = parse_key_values(first_roundtrip.stdout)
        second_loaded = parse_key_values(second_roundtrip.stdout)
    except ValueError as error:
        print(error, file=sys.stderr)
        return False
    if first_loaded != second_loaded:
        print("v0b loader roundtrip summary is not deterministic", file=sys.stderr)
        return False
    expected_loaded = {
        "loaded_pattern_set_id": "fixed-pattern-fixture-v1",
        "phase_count": "13",
        "phase_stride": "26245",
        "phase_bias[0]": "0",
        "phase_bias[1]": "0",
        "phase_bias[12]": "4",
    }
    for key, expected_value in expected_loaded.items():
        if first_loaded.get(key) != expected_value:
            print(
                f"v0b roundtrip field mismatch for {key}: {first_loaded.get(key)!r}",
                file=sys.stderr,
            )
            return False
    representative_score = first_loaded.get("representative_score")
    if representative_score is None:
        print("v0b roundtrip did not report representative_score", file=sys.stderr)
        return False
    if representative_score == v0a_representative_score:
        print("v0a and v0b representative scores unexpectedly matched", file=sys.stderr)
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exporter-exe", required=True, type=Path)
    parser.add_argument("--v0b-exporter", required=True, type=Path)
    parser.add_argument("--roundtrip-exe", required=True, type=Path)
    parser.add_argument("--trainer-exe", required=True, type=Path)
    parser.add_argument("--trainer", required=True, type=Path)
    parser.add_argument("--dataset-exe", required=True, type=Path)
    parser.add_argument("--normalized-tsv", required=True, type=Path)
    parser.add_argument("--records", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        dataset_path = temp_dir / "pattern-dataset.tsv"
        summary_path = temp_dir / "trainer-summary.txt"
        weights_path = temp_dir / "tiny-smoke.weights.bin"
        manifest_path = temp_dir / "tiny-smoke.manifest.json"

        dataset_result = run_capture(
            [
                str(args.dataset_exe),
                "--records",
                str(args.records),
                "--manifest",
                str(args.manifest),
                "--split-policy",
                "tiny-cycle",
            ]
        )
        if dataset_result.returncode != 0:
            sys.stderr.write(dataset_result.stderr)
            sys.stderr.write(dataset_result.stdout)
            return dataset_result.returncode
        dataset_path.write_text(dataset_result.stdout, encoding="utf-8")

        trainer_result = run_capture([str(args.trainer_exe), "--dataset", str(dataset_path)])
        if trainer_result.returncode != 0:
            sys.stderr.write(trainer_result.stderr)
            sys.stderr.write(trainer_result.stdout)
            return trainer_result.returncode
        summary_path.write_text(trainer_result.stdout, encoding="utf-8")

        export_result = run_capture(
            [
                str(args.exporter_exe),
                "--summary",
                str(summary_path),
                "--weights-out",
                str(weights_path),
                "--manifest-out",
                str(manifest_path),
            ]
        )
        if export_result.returncode != 0:
            sys.stderr.write(export_result.stderr)
            sys.stderr.write(export_result.stdout)
            return export_result.returncode

        try:
            export_summary = parse_key_values(export_result.stdout)
        except ValueError as error:
            print(error, file=sys.stderr)
            return 1
        if export_summary != EXPECTED_EXPORT_SUMMARY:
            print(f"unexpected export summary: {export_summary}", file=sys.stderr)
            return 1

        manifest_data = json.loads(manifest_path.read_text(encoding="utf-8"))
        expected_manifest_fields = {
            "format_version": 1,
            "bit_order": "a1-lsb",
            "score_unit": "disc-diff",
            "score_scale": 1,
            "phase_count": 2,
            "pattern_set_id": "fixed-pattern-fixture-v1",
            "weights_checksum": "0x3c640916",
            "source": "tools/pattern/train tiny deterministic smoke summary",
            "training_note": "tiny smoke phase-bias baseline; pattern tables are zero-filled",
            "phase_bias": [3, 0],
        }
        for key, expected_value in expected_manifest_fields.items():
            if manifest_data.get(key) != expected_value:
                print(
                    f"manifest field mismatch for {key}: {manifest_data.get(key)!r}",
                    file=sys.stderr,
                )
                return 1
        if manifest_data.get("weights_file") != weights_path.name:
            print("manifest weights_file does not point at generated weights", file=sys.stderr)
            return 1

        roundtrip_result = run_capture(
            [
                str(args.roundtrip_exe),
                "--weights",
                str(weights_path),
                "--expect-score",
                "3",
            ]
        )
        if roundtrip_result.returncode != 0:
            sys.stderr.write(roundtrip_result.stderr)
            sys.stderr.write(roundtrip_result.stdout)
            return roundtrip_result.returncode
        try:
            roundtrip_summary = parse_key_values(roundtrip_result.stdout)
        except ValueError as error:
            print(error, file=sys.stderr)
            return 1
        if roundtrip_summary != EXPECTED_ROUNDTRIP_SUMMARY:
            print(f"unexpected roundtrip summary: {roundtrip_summary}", file=sys.stderr)
            return 1

        normalized_tsv = args.normalized_tsv
        synthetic_dataset_report = temp_dir / "synthetic-dataset-report.json"
        synthetic_dataset = temp_dir / "synthetic-pattern-dataset.tsv"
        synthetic_dataset_text = run_dataset_from_normalized(
            args.dataset_exe,
            normalized_tsv,
            synthetic_dataset_report,
            "fixed-pattern-fixture-v1",
        )
        if synthetic_dataset_text is None:
            return 1
        synthetic_dataset.write_text(synthetic_dataset_text, encoding="utf-8")

        v0b_weights = temp_dir / "v0b-weights.json"
        v0b_report = temp_dir / "v0b-report.json"
        if not run_trainer_v0b(args.trainer, synthetic_dataset, v0b_weights, v0b_report):
            return 1
        if not check_v0b_roundtrip(
            args.v0b_exporter,
            args.roundtrip_exe,
            v0b_weights,
            temp_dir,
            roundtrip_summary["representative_score"],
        ):
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
