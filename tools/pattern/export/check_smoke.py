#!/usr/bin/env python3
"""CTest wrapper for tiny pattern artifact export and runtime round-trip."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path


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
    "phase_stride": "26245",
    "phase_bias[0]": "3",
    "phase_bias[1]": "0",
    "representative_score": "3",
}


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


def run_capture(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, capture_output=True, text=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exporter-exe", required=True, type=Path)
    parser.add_argument("--roundtrip-exe", required=True, type=Path)
    parser.add_argument("--trainer-exe", required=True, type=Path)
    parser.add_argument("--dataset-exe", required=True, type=Path)
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

        roundtrip_result = run_capture([str(args.roundtrip_exe), "--weights", str(weights_path)])
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

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
