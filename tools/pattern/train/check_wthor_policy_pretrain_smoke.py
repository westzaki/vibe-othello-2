#!/usr/bin/env python3
"""Smoke-check streaming WTHOR played-move pretraining."""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
import tempfile
from pathlib import Path


HEADER = struct.Struct("<4BIHH4B")
RECORD = struct.Struct("<HHHBB60B")
TRANSCRIPT = (
    "d3 c3 b3 e3 f3 c5 f6 g2 b5 c6 f4 a5 h1 f5 d6 e7 d7 e6 d8 c4 "
    "c7 b7 a8 b6 a4 f8 g4 b4 e8 a3 a7 g5 g8 c2 h4 g3 a2 h3 c1 d1 "
    "d2 e1 f1 f7 a6 h6 e2 b8 g7 c8 h5 g6 h2 h7 h8 g1 b2 f2 a1 b1"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trainer", required=True, type=Path)
    parser.add_argument("--artifact-manifest", required=True, type=Path)
    return parser.parse_args()


def encoded_move(token: str) -> int:
    return (int(token[1]) * 10) + (ord(token[0]) - ord("a") + 1)


def write_fixture(path: Path) -> None:
    moves = [encoded_move(token) for token in TRANSCRIPT.split()]
    payload = HEADER.pack(20, 26, 7, 20, 2, 0, 25, 8, 0, 22, 0)
    for player_offset in (0, 1):
        payload += RECORD.pack(
            1,
            10 + player_offset,
            20 + player_offset,
            32,
            32,
            *(moves + [0] * (60 - len(moves))),
        )
    path.write_bytes(payload)


def main() -> int:
    args = parse_args()
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        fixture = root / "fixture.wtb"
        write_fixture(fixture)
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
                    "1",
                    "--learning-rate",
                    "0.01",
                    "--negative-count",
                    "2",
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
            epoch = report_payload["metrics_by_epoch"][0]
            assert epoch["game_count"] == 2
            assert epoch["normal_move_count"] == 120
            assert epoch["decision_count"] == 120
            assert epoch["train_decision_count"] > 0
            assert epoch["updated_weight_count"] > 0
            assert report_payload["frozen_phases"] == [10, 11, 12]
            outputs.append(weights.read_bytes())
        assert outputs[0] == outputs[1]
    print("wthor policy pretrain smoke: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
