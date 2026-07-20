#!/usr/bin/env python3
"""Smoke-check streaming Egaroucid board-score pretraining."""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path


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
    parser.add_argument("--trainer", required=True, type=Path)
    parser.add_argument("--artifact-manifest", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        fixture = root / "fixture.txt"
        fixture.write_text("\n".join(ROWS) + "\n", encoding="utf-8")
        outputs: list[bytes] = []
        for run in range(2):
            weights = root / f"weights-{run}.json"
            report = root / f"report-{run}.json"
            subprocess.run(
                [
                    str(args.trainer),
                    "--input",
                    str(fixture),
                    "--initial-artifact",
                    str(args.artifact_manifest),
                    "--weights-out",
                    str(weights),
                    "--report-out",
                    str(report),
                    "--epochs",
                    "2",
                    "--learning-rate",
                    "0.01",
                    "--learning-rate",
                    "0.02",
                    "--quantize-between-epochs",
                    "--validation-modulus",
                    "0",
                    "--seed",
                    "7",
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            weights_payload = json.loads(weights.read_text(encoding="utf-8"))
            report_payload = json.loads(report.read_text(encoding="utf-8"))
            assert weights_payload["weights_schema_version"] == "pattern-eval-weights-v2"
            assert weights_payload["pattern_set_id"] == "pattern-v2-endgame-lite"
            assert report_payload["trainer_algorithm"] == (
                "egaroucid-board-score-streaming-huber-v1"
            )
            assert report_payload["learning_rate_schedule"] == [0.01, 0.02]
            assert report_payload["quantize_between_epochs"] is True
            assert len(report_payload["metrics_by_epoch"]) == 2
            for epoch in report_payload["metrics_by_epoch"]:
                assert epoch["input_position_count"] == len(ROWS)
                assert epoch["train_position_count"] == len(ROWS)
                assert epoch["validation_position_count"] == 0
                assert epoch["updated_weight_count"] > 0
            assert report_payload["frozen_phases"] == [10, 11, 12]
            assert report_payload["source_files"][0]["name"] == fixture.name
            serialized_report = report.read_text(encoding="utf-8")
            assert str(root) not in serialized_report
            outputs.append(weights.read_bytes())
        assert outputs[0] == outputs[1]
    print("Egaroucid board-score pretrain smoke: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
