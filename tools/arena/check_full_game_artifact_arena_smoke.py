#!/usr/bin/env python3
"""CTest smoke coverage for the persistent full-game artifact arena."""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path


FORCED_PASS_MOVES = (
    "e6 d6 c5 f6 c4 c3 d3 b4 c6 b3 b2 a1 f5 c7 a3 g5 b7 a2 c1 "
    "a7 a8 e2 d2 e1 e7 d7 h5 e8 a6 b5 c2 f4 f2 f1 g7 h4 d8 h8 "
    "c8 d1 f3 g2 h1 a4 g6 b1 g8 h6 g3 g4 f8 h2 h3 f7 e3 b8 h7 b6 a5"
)


def make_tiny_artifact(weights_path: Path, manifest_path: Path) -> None:
    pattern_set_id = "fixed-pattern-fixture-v1"
    phase_count = 13
    weight_count = (1 + 3**8 + 3**9) * phase_count
    payload = bytearray(b"VOPWGT\0\0")
    payload.extend(
        struct.pack(
            "<HHHHHHHHI",
            1,
            1,
            1,
            1,
            phase_count,
            2,
            len(pattern_set_id),
            0,
            weight_count,
        )
    )
    payload.extend(pattern_set_id.encode("utf-8"))
    payload.extend(b"\0" * (weight_count * 4))
    checksum = f"0x{zlib.crc32(payload) & 0xFFFFFFFF:08x}"
    payload.extend(struct.pack("<I", int(checksum, 16)))
    weights_path.write_bytes(payload)
    manifest_path.write_text(
        json.dumps(
            {
                "format_version": 1,
                "bit_order": "a1-lsb",
                "score_unit": "disc-diff",
                "score_scale": 1,
                "phase_count": phase_count,
                "trained_phases": [10, 11, 12],
                "pattern_set_id": pattern_set_id,
                "weights_file": weights_path.name,
                "weights_checksum": checksum,
                "notes": "temporary full-game arena smoke artifact",
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, capture_output=True, text=True)


def run_arena(exe: str, temp_dir: Path, report_name: str) -> dict[str, object]:
    command = [
        exe,
        "--candidate-manifest",
        str(temp_dir / "candidate.manifest.json"),
        "--candidate-name",
        "smoke-candidate",
        "--baseline-manifest",
        str(temp_dir / "baseline.manifest.json"),
        "--baseline-name",
        "smoke-baseline",
        "--openings",
        str(temp_dir / "openings.txt"),
        "--report-out",
        str(temp_dir / report_name),
        "--depth",
        "1",
        "--search-preset",
        "full",
        "--seed",
        "7",
    ]
    completed = run(command)
    if completed.returncode != 0:
        raise AssertionError(
            f"{command} exited {completed.returncode}\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    report_path = temp_dir / report_name
    if not report_path.exists():
        raise AssertionError("full-game arena did not produce a report")
    return json.loads(report_path.read_text(encoding="utf-8"))


def assert_report(report: dict[str, object]) -> None:
    expected = {
        "candidate",
        "baseline",
        "search_config",
        "input_opening_count",
        "opening_count",
        "selected_openings",
        "games",
        "game_records",
        "results",
        "failed_games",
        "illegal_games",
        "elapsed_sec",
        "same_artifact_sanity",
        "report_checksum",
        "non_claim_notes",
    }
    missing = sorted(expected - set(report))
    if missing:
        raise AssertionError(f"report missing keys: {missing}")
    if report["candidate"]["evaluator_policy"] != "phase-aware-covered-phases":
        raise AssertionError(f"candidate did not use phase-aware evaluator: {report['candidate']!r}")
    if report["baseline"]["evaluator_policy"] != "phase-aware-covered-phases":
        raise AssertionError(f"baseline did not use phase-aware evaluator: {report['baseline']!r}")
    if report["search_config"]["entrypoint"] != "search_iterative":
        raise AssertionError(f"unexpected search entrypoint: {report['search_config']!r}")
    if report["search_config"]["limit_scope"] != "per-move":
        raise AssertionError(f"unexpected limit scope: {report['search_config']!r}")
    if report["search_config"]["resolved_options"]["use_pvs"] is not True:
        raise AssertionError(f"full preset did not enable PVS: {report['search_config']!r}")
    if report["input_opening_count"] != 4 or report["opening_count"] != 4:
        raise AssertionError(f"unexpected opening counts: {report!r}")
    if report["games"] != 8 or len(report["game_records"]) != 8:
        raise AssertionError(f"unexpected paired game count: {report!r}")
    overall = report["results"]["overall"]
    if overall["candidate_wins"] + overall["candidate_losses"] + overall["draws"] != 8:
        raise AssertionError(f"W/L/D does not sum to games: {overall!r}")
    if overall["passes_after_opening"] <= 0:
        raise AssertionError(f"forced-pass opening did not exercise pass handling: {overall!r}")
    if report["failed_games"] != 0 or report["illegal_games"] != 0:
        raise AssertionError(f"unexpected failed or illegal games: {report!r}")
    side_results = report["results"]["by_side_assignment"]
    if set(side_results) != {"candidate_black", "candidate_white"}:
        raise AssertionError(f"missing color-swap buckets: {side_results!r}")
    if side_results["candidate_black"]["games"] != 4 or side_results["candidate_white"]["games"] != 4:
        raise AssertionError(f"unpaired color swap: {side_results!r}")
    if len(report["results"]["by_opening"]) != 4:
        raise AssertionError(f"opening breakdown missing: {report['results']['by_opening']!r}")
    sanity = report["same_artifact_sanity"]
    if sanity != {
        "same_runtime_artifact": True,
        "paired_color_swap": True,
        "neutral": True,
    }:
        raise AssertionError(f"same-artifact sanity failed: {sanity!r}")
    if overall["candidate_score_rate"] != 0.5 or overall["average_disc_diff_candidate_perspective"] != 0.0:
        raise AssertionError(f"same artifact was not neutral: {overall!r}")


def assert_exact_guard(exe: str, temp_dir: Path) -> None:
    command = [
        exe,
        "--candidate-manifest",
        str(temp_dir / "candidate.manifest.json"),
        "--baseline-manifest",
        str(temp_dir / "baseline.manifest.json"),
        "--openings",
        str(temp_dir / "openings.txt"),
        "--report-out",
        str(temp_dir / "bad.json"),
        "--depth",
        "1",
        "--exact-endgame-empties",
        "8",
    ]
    completed = run(command)
    if completed.returncode == 0:
        raise AssertionError("depth-only exact endgame request unexpectedly succeeded")
    if "requires --nodes or --time-ms" not in completed.stderr:
        raise AssertionError(f"exact guard error missing:\n{completed.stderr}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    args = parser.parse_args(argv)
    with tempfile.TemporaryDirectory() as temp:
        temp_dir = Path(temp)
        make_tiny_artifact(temp_dir / "candidate.weights.bin", temp_dir / "candidate.manifest.json")
        make_tiny_artifact(temp_dir / "baseline.weights.bin", temp_dir / "baseline.manifest.json")
        (temp_dir / "openings.txt").write_text(
            "# empty start, shorthand, id form, and a position whose continuation includes a pass\n"
            "start:\n"
            "f5 d6:\n"
            "named: f5 d6\n"
            f"forced-pass: {FORCED_PASS_MOVES}\n",
            encoding="utf-8",
        )
        first = run_arena(args.exe, temp_dir, "report-a.json")
        second = run_arena(args.exe, temp_dir, "report-b.json")
        assert_report(first)
        assert_report(second)
        if first["report_checksum"] != second["report_checksum"]:
            raise AssertionError(
                f"deterministic report checksum changed: {first['report_checksum']!r}, "
                f"{second['report_checksum']!r}"
            )
        assert_exact_guard(args.exe, temp_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
