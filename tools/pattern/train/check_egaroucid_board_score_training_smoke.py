#!/usr/bin/env python3
"""End-to-end smoke check for local Egaroucid board-score value training."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from typing import Any


DATASET_ID = "egaroucid-train-data-board-score-v2025-02-02"
ROWS = [
    "---------------------------OX------XO--------------------------- 8",
    "-------------------O-------OO------OX--------------------------- 7",
    "------------------OX-------OX------XO--------------------------- 5",
    "------------------XO------OOO------OX--------------------------- -9",
    "--------------------------OOO------OX--------------------------- -3",
    "OOOO-XOOOOXXXOOOOXOXOXOOOOXOOXOOOXOOOXOOOOXOOOOOO-XXXOOO--XXXOOO 4",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--importer", required=True, type=Path)
    parser.add_argument("--dataset-exe", required=True, type=Path)
    parser.add_argument("--trainer", required=True, type=Path)
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_fixture(archive: Path, manifest: Path) -> None:
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
        output.writestr("synthetic/0000000.txt", "\n".join(ROWS) + "\n")
    data: dict[str, Any] = {
        "dataset_id": DATASET_ID,
        "source_name": "synthetic Egaroucid board-score training smoke fixture",
        "source_url": "local://synthetic-egaroucid-board-score-training-smoke",
        "retrieved_at": "2026-01-01",
        "license_or_terms": "repo-owned synthetic fixture",
        "redistribution_allowed": True,
        "commercial_use_allowed": True,
        "derived_weights_allowed": True,
        "required_attribution": "none",
        "local_path": "corpora/Egaroucid_Train_Data.zip",
        "sha256": sha256_file(archive),
        "notes": "Generated at smoke-test runtime.",
    }
    manifest.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def run(
    args: argparse.Namespace,
    local_root: Path,
    manifest: Path,
    output_dir: Path,
    *,
    dataset_exe: Path | None = None,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(args.runner),
            "--local-root",
            str(local_root),
            "--corpus",
            str(local_root / "corpora/Egaroucid_Train_Data.zip"),
            "--manifest",
            str(manifest),
            "--output-dir",
            str(output_dir),
            "--positions-per-phase",
            "10",
            "--epochs",
            "1",
            "--progress-every-rows",
            "100",
            "--progress-every-examples",
            "100",
            "--importer",
            str(args.importer),
            "--dataset-exe",
            str(dataset_exe or args.dataset_exe),
            "--trainer",
            str(args.trainer),
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def main() -> int:
    args = parse_args()
    try:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            local_root = root / "vibe-othello-local"
            corpora = local_root / "corpora"
            corpora.mkdir(parents=True)
            archive = corpora / "Egaroucid_Train_Data.zip"
            manifest = root / "manifest.json"
            output_dir = local_root / "training/egaroucid-board-score/smoke"
            write_fixture(archive, manifest)
            source_checksum_before = sha256_file(archive)

            result = run(args, local_root, manifest, output_dir)
            if result.returncode != 0:
                raise AssertionError(f"runner failed:\n{result.stdout}\n{result.stderr}")
            expected_files = (
                "source-manifest.json",
                "run-report.json",
                "normalized/positions.tsv",
                "normalized/report.json",
                "dataset/pattern-dataset.tsv",
                "dataset/report.json",
                "training/weights.json",
                "training/report.json",
                "logs/import.log",
                "logs/dataset.log",
                "logs/train.log",
            )
            missing = [name for name in expected_files if not (output_dir / name).is_file()]
            if missing:
                raise AssertionError(f"missing outputs: {missing}")

            report_text = (output_dir / "run-report.json").read_text(encoding="utf-8")
            report = json.loads(report_text)
            if report.get("status") != "complete":
                raise AssertionError(f"run did not complete: {report}")
            stages = report.get("stages")
            if not isinstance(stages, dict) or any(
                stages.get(stage, {}).get("status") != "complete"
                for stage in ("import", "dataset", "train")
            ):
                raise AssertionError(f"stage status mismatch: {stages}")
            if str(root) in report_text:
                raise AssertionError("run report leaked an absolute local path")
            if sha256_file(archive) != source_checksum_before:
                raise AssertionError("runner modified the raw archive")

            repeated = run(args, local_root, manifest, output_dir)
            if repeated.returncode == 0 or "output directory already exists" not in repeated.stderr:
                raise AssertionError("runner overwrote an existing run directory")

            failing_dataset = root / "failing-dataset"
            failing_dataset.write_text("#!/bin/sh\nexit 1\n", encoding="utf-8")
            failing_dataset.chmod(0o755)
            failed_output = local_root / "training/egaroucid-board-score/failed-smoke"
            failed = run(
                args,
                local_root,
                manifest,
                failed_output,
                dataset_exe=failing_dataset,
            )
            if failed.returncode == 0:
                raise AssertionError("runner did not propagate a failed stage")
            failed_report_text = (failed_output / "run-report.json").read_text(encoding="utf-8")
            failed_report = json.loads(failed_report_text)
            if failed_report.get("status") != "failed":
                raise AssertionError("failed run report did not preserve failed status")
            if str(root) in failed_report_text:
                raise AssertionError("failed run report leaked an absolute local path")
    except (AssertionError, OSError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 1

    print("egaroucid board-score training smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
