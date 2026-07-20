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


def artifact_content_identity(report: dict[str, object], role: str) -> tuple[object, object]:
    inputs = report.get("inputs")
    if not isinstance(inputs, dict):
        raise RuntimeError("report lacks input fingerprints")
    identity = (
        inputs.get(f"{role}_manifest_checksum"),
        inputs.get(f"{role}_weights_checksum"),
    )
    if any(not isinstance(value, str) or value == "unavailable" for value in identity):
        raise RuntimeError(f"{role} content fingerprint is unavailable")
    return identity


def assert_clean_run(report: dict[str, object]) -> None:
    if report.get("failed_games") != 0 or report.get("illegal_games") != 0:
        raise RuntimeError("sanity report contains failed or illegal games")
    gate = report.get("strength_gate")
    if not isinstance(gate, dict) or gate.get("eligible") is not True:
        raise RuntimeError(f"sanity report is not strength-gate eligible: {gate!r}")


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
    parser.add_argument("--candidate-weights")
    parser.add_argument("--baseline-manifest", required=True)
    parser.add_argument("--baseline-weights")
    parser.add_argument("--openings", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--limit-mode", choices=("depth", "nodes", "time"), required=True)
    parser.add_argument("--depth", type=int)
    parser.add_argument("--nodes", type=int)
    parser.add_argument("--time-ms", type=int)
    parser.add_argument("--search-preset", choices=("basic", "full"), default="full")
    parser.add_argument("--persistent-session", action="store_true")
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--bootstrap-seed", type=int, default=0)
    parser.add_argument("--bootstrap-samples", type=int, default=10000)
    parser.add_argument("--exact-endgame-empties", type=int, default=0)
    parser.add_argument("--opening-limit", type=int, default=0)
    parser.add_argument("--minimum-opening-pairs", type=int, default=1)
    args = parser.parse_args(argv)

    selected_limit = {
        "depth": args.depth,
        "nodes": args.nodes,
        "time": args.time_ms,
    }[args.limit_mode]
    if selected_limit is None or selected_limit <= 0:
        parser.error(f"--{args.limit_mode if args.limit_mode != 'time' else 'time-ms'} must be positive")
    if args.exact_endgame_empties < 0 or args.exact_endgame_empties > 64:
        parser.error("--exact-endgame-empties must be in [0, 64]")
    if args.opening_limit < 0:
        parser.error("--opening-limit must be non-negative")
    if args.minimum_opening_pairs <= 0:
        parser.error("--minimum-opening-pairs must be positive")

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
        "--minimum-opening-pairs",
        str(args.minimum_opening_pairs),
    ]
    if args.exact_endgame_empties > 0:
        common.extend(("--exact-endgame-empties", str(args.exact_endgame_empties)))
    if args.persistent_session:
        common.append("--persistent-session")
    if args.opening_limit > 0:
        common.extend(("--opening-limit", str(args.opening_limit)))

    def command(
        candidate: str,
        candidate_weights: str | None,
        baseline: str,
        baseline_weights: str | None,
        name: str,
    ) -> list[str]:
        result = [
            args.exe,
            "--candidate-manifest",
            candidate,
            "--baseline-manifest",
            baseline,
            "--report-out",
            str(output_dir / f"{name}.json"),
        ]
        if candidate_weights is not None:
            result.extend(("--candidate-weights", candidate_weights))
        if baseline_weights is not None:
            result.extend(("--baseline-weights", baseline_weights))
        return [*result, *common]

    same_candidate = run(
        command(
            args.candidate_manifest,
            args.candidate_weights,
            args.candidate_manifest,
            args.candidate_weights,
            "same-candidate",
        )
    )
    same_baseline = run(
        command(
            args.baseline_manifest,
            args.baseline_weights,
            args.baseline_manifest,
            args.baseline_weights,
            "same-baseline",
        )
    )
    forward = run(
        command(
            args.candidate_manifest,
            args.candidate_weights,
            args.baseline_manifest,
            args.baseline_weights,
            "candidate-vs-baseline",
        )
    )
    reverse = run(
        command(
            args.baseline_manifest,
            args.baseline_weights,
            args.candidate_manifest,
            args.candidate_weights,
            "baseline-vs-candidate",
        )
    )
    reports = (same_candidate, same_baseline, forward, reverse)
    for report in reports:
        assert_color_swap(report)
        assert_clean_run(report)
    selected_checksums = {report.get("selected_openings_checksum") for report in reports}
    if len(selected_checksums) != 1:
        raise RuntimeError("sanity reports selected different openings")
    search_configs = [report.get("search_config") for report in reports]
    if any(config != search_configs[0] for config in search_configs[1:]):
        raise RuntimeError("sanity reports used different search configurations")
    candidate_identity = artifact_content_identity(forward, "candidate")
    baseline_identity = artifact_content_identity(forward, "baseline")
    if candidate_identity != artifact_content_identity(reverse, "baseline"):
        raise RuntimeError("candidate content identity changed after argument reversal")
    if baseline_identity != artifact_content_identity(reverse, "candidate"):
        raise RuntimeError("baseline content identity changed after argument reversal")
    if candidate_identity != artifact_content_identity(
        same_candidate, "candidate"
    ) or candidate_identity != artifact_content_identity(same_candidate, "baseline"):
        raise RuntimeError("candidate same-artifact sanity used different content")
    if baseline_identity != artifact_content_identity(
        same_baseline, "candidate"
    ) or baseline_identity != artifact_content_identity(same_baseline, "baseline"):
        raise RuntimeError("baseline same-artifact sanity used different content")
    for same_report in (same_candidate, same_baseline):
        same_sanity = same_report["paired_sanity"]
        if not isinstance(same_sanity, dict) or same_sanity.get("same_artifact_neutral") is not True:
            raise RuntimeError("same-artifact paired score or disc differential was not exactly neutral")
    if abs((score(forward) + score(reverse)) - 1.0) > 1e-9 or disc_diff(forward) != -disc_diff(reverse):
        raise RuntimeError("candidate/baseline argument-order sanity failed")

    summary = {
        "schema_version": 2,
        "sanity_version": "full-game-artifact-arena-sanity-v2",
        "same_candidate_report": "same-candidate.json",
        "same_baseline_report": "same-baseline.json",
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
