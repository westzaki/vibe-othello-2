#!/usr/bin/env python3
"""Run full-game arena sanity comparisons without publishing measurement output."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


def run(command: list[str]) -> dict[str, object]:
    completed = subprocess.run(command, check=False, text=True, capture_output=True)
    if completed.returncode != 0:
        raise RuntimeError(
            f"arena failed ({completed.returncode})\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    report = Path(command[command.index("--report-out") + 1])
    return json.loads(report.read_text(encoding="utf-8"))


def assert_color_swap(report: dict[str, object]) -> None:
    sanity = report.get("paired_sanity")
    if not isinstance(sanity, dict) or sanity.get("paired_color_swap_complete") is not True:
        raise RuntimeError("paired color-swap sanity failed")


def score(report: dict[str, object]) -> float:
    paired = report["results"]["paired_score"]
    if not isinstance(paired, dict):
        raise RuntimeError("report lacks paired score")
    return float(paired["point_estimate"])


def disc_diff(report: dict[str, object]) -> int:
    sanity = report["paired_sanity"]
    if not isinstance(sanity, dict):
        raise RuntimeError("report lacks paired sanity")
    return int(sanity["paired_disc_diff_sum"])


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--candidate-manifest", required=True)
    parser.add_argument("--baseline-manifest", required=True)
    parser.add_argument("--openings", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--limit-mode", choices=("depth", "nodes", "time"), required=True)
    parser.add_argument("--depth", type=int)
    parser.add_argument("--nodes", type=int)
    parser.add_argument("--time-ms", type=int)
    parser.add_argument("--search-preset", choices=("basic", "full"), default="full")
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--bootstrap-seed", type=int, default=0)
    parser.add_argument("--bootstrap-samples", type=int, default=10000)
    args = parser.parse_args(argv)

    selected_limit = {
        "depth": args.depth,
        "nodes": args.nodes,
        "time": args.time_ms,
    }[args.limit_mode]
    if selected_limit is None or selected_limit <= 0:
        parser.error(f"--{args.limit_mode if args.limit_mode != 'time' else 'time-ms'} must be positive")

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    limit_flag = {"depth": "--depth", "nodes": "--nodes", "time": "--time-ms"}[args.limit_mode]
    common = [
        "--openings",
        args.openings,
        "--limit-mode",
        args.limit_mode,
        limit_flag,
        str(selected_limit),
        "--search-preset",
        args.search_preset,
        "--seed",
        str(args.seed),
        "--bootstrap-seed",
        str(args.bootstrap_seed),
        "--bootstrap-samples",
        str(args.bootstrap_samples),
    ]

    def command(candidate: str, baseline: str, name: str) -> list[str]:
        return [
            args.exe,
            "--candidate-manifest",
            candidate,
            "--baseline-manifest",
            baseline,
            "--report-out",
            str(output_dir / f"{name}.json"),
            *common,
        ]

    same = run(command(args.candidate_manifest, args.candidate_manifest, "same-artifact"))
    forward = run(command(args.candidate_manifest, args.baseline_manifest, "candidate-vs-baseline"))
    reverse = run(command(args.baseline_manifest, args.candidate_manifest, "baseline-vs-candidate"))
    assert_color_swap(same)
    assert_color_swap(forward)
    assert_color_swap(reverse)
    same_sanity = same["paired_sanity"]
    if not isinstance(same_sanity, dict) or same_sanity.get("same_artifact_neutral") is not True:
        raise RuntimeError("same-artifact paired score or disc differential was not exactly neutral")
    if abs((score(forward) + score(reverse)) - 1.0) > 1e-9 or disc_diff(forward) != -disc_diff(reverse):
        raise RuntimeError("candidate/baseline argument-order sanity failed")

    summary = {
        "schema_version": 1,
        "sanity_version": "full-game-artifact-arena-sanity-v1",
        "same_artifact_report": "same-artifact.json",
        "forward_report": "candidate-vs-baseline.json",
        "reverse_report": "baseline-vs-candidate.json",
        "color_swap_passed": True,
        "same_artifact_passed": True,
        "argument_order_passed": True,
    }
    (output_dir / "sanity-summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
