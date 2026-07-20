#!/usr/bin/env python3
"""CTest wrapper for the engine CLI bestmove protocol smoke cases."""

from __future__ import annotations

import argparse
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

TERMINAL_MOVES = FORCED_PASS_MOVES + " pass g1"


def make_zero_tiny_pattern_artifact(path: Path) -> None:
    pattern_set_id = "fixed-pattern-fixture-v1"
    phase_count = 13
    phase_stride = 1 + 3**8 + 3**9
    weight_count = phase_stride * phase_count

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
    checksum = zlib.crc32(payload) & 0xFFFFFFFF
    payload.extend(struct.pack("<I", checksum))
    path.write_bytes(payload)


def make_legacy_tiny_pattern_manifest(weights_path: Path) -> Path:
    checksum = struct.unpack("<I", weights_path.read_bytes()[-4:])[0]
    manifest = weights_path.with_name("manifest.json")
    manifest.write_text(
        "{\n"
        '  "artifact_id": "legacy-tiny-test-artifact",\n'
        '  "format": "vibe-othello-pattern-eval",\n'
        '  "format_version": 1,\n'
        '  "bit_order": "a1-lsb",\n'
        '  "score_unit": "disc-diff",\n'
        '  "score_scale": 1,\n'
        '  "phase_count": 13,\n'
        '  "pattern_set_id": "fixed-pattern-fixture-v1",\n'
        '  "weights_file": "weights.bin",\n'
        f'  "weights_checksum": "0x{checksum:08x}"\n'
        "}\n",
        encoding="utf-8",
    )
    return manifest


def run_case(
    exe: str, moves: str, depth: int, expected: str, extra_args: list[str] | None = None
) -> None:
    command = [exe, "bestmove", "--moves", moves, "--depth", str(depth), *(extra_args or [])]
    completed = subprocess.run(command, check=False, text=True, capture_output=True)
    if completed.returncode != 0:
        raise AssertionError(
            f"{command} exited {completed.returncode}\n"
            f"stdout:\n{completed.stdout}\n"
            f"stderr:\n{completed.stderr}"
        )
    actual = completed.stdout.strip()
    if actual != expected:
        raise AssertionError(
            f"{command} output mismatch\nexpected: {expected!r}\nactual:   {actual!r}"
        )
    if completed.stderr:
        raise AssertionError(f"{command} wrote unexpected stderr:\n{completed.stderr}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--source-dir", required=True, type=Path)
    args = parser.parse_args(argv)

    cases = [
        ("", 1, "bestmove d3 score 3 depth 1", ["--eval-mode", "static"]),
        ("", 0, "bestmove none score 0 depth 0", ["--eval-mode", "static"]),
        (FORCED_PASS_MOVES, 2, "bestmove pass score 0 depth 2", ["--eval-mode", "static"]),
        (TERMINAL_MOVES, 4, "bestmove none score 0 depth 4", ["--eval-mode", "static"]),
    ]
    for moves, depth, expected, extra_args in cases:
        run_case(args.exe, moves, depth, expected, extra_args)

    default_manifest = (
        args.source_dir
        / "data/eval/artifacts/pattern-v2-egaroucid-lv17-full-value-v1/manifest.json"
    )
    run_case(args.exe, "", 1, "bestmove f5 score -4 depth 1")
    run_case(
        args.exe,
        "",
        1,
        "bestmove f5 score -4 depth 1",
        ["--eval-artifact", str(default_manifest)],
    )
    run_case(args.exe, "d3 c3", 1, "bestmove c4 score 1 depth 1")

    with tempfile.TemporaryDirectory() as temp_dir:
        weights = Path(temp_dir) / "weights.bin"
        make_zero_tiny_pattern_artifact(weights)
        run_case(
            args.exe,
            "",
            1,
            "bestmove d3 score 0 depth 1",
            ["--eval", "pattern", "--pattern-set", "tiny", "--pattern-weights", str(weights)],
        )
        legacy_manifest = make_legacy_tiny_pattern_manifest(weights)
        run_case(
            args.exe,
            "",
            1,
            "bestmove d3 score 0 depth 1",
            ["--eval-artifact", str(legacy_manifest)],
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
