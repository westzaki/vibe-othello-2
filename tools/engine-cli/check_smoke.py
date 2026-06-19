#!/usr/bin/env python3
"""CTest wrapper for the engine CLI bestmove protocol smoke cases."""

from __future__ import annotations

import argparse
import subprocess
import sys


FORCED_PASS_MOVES = (
    "e6 d6 c5 f6 c4 c3 d3 b4 c6 b3 b2 a1 f5 c7 a3 g5 b7 a2 c1 "
    "a7 a8 e2 d2 e1 e7 d7 h5 e8 a6 b5 c2 f4 f2 f1 g7 h4 d8 h8 "
    "c8 d1 f3 g2 h1 a4 g6 b1 g8 h6 g3 g4 f8 h2 h3 f7 e3 b8 h7 b6 a5"
)

TERMINAL_MOVES = FORCED_PASS_MOVES + " pass g1"


def run_case(exe: str, moves: str, depth: int, expected: str) -> None:
    command = [exe, "bestmove", "--moves", moves, "--depth", str(depth)]
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
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
