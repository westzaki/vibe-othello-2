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
    args = parser.parse_args(argv)

    cases = [
        ("", 1, "bestmove d3 score 3 depth 1"),
        ("", 0, "bestmove none score 0 depth 0"),
        (FORCED_PASS_MOVES, 2, "bestmove pass score 0 depth 2"),
        (TERMINAL_MOVES, 4, "bestmove none score 0 depth 4"),
    ]
    for moves, depth, expected in cases:
        run_case(args.exe, moves, depth, expected)

    with tempfile.TemporaryDirectory() as temp_dir:
        weights = Path(temp_dir) / "zero-tiny.weights.bin"
        make_zero_tiny_pattern_artifact(weights)
        run_case(
            args.exe,
            "",
            1,
            "bestmove d3 score 0 depth 1",
            ["--eval", "pattern", "--pattern-set", "tiny", "--pattern-weights", str(weights)],
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
