#!/usr/bin/env python3
"""Run a local-only fixed-time artifact strength campaign."""

from __future__ import annotations

import argparse
import hashlib
import json
import random
import statistics
import subprocess
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable


RUNNER_VERSION = "fixed-time-artifact-strength-campaign-v1"
DECISION_VERSION = "fixed-time-artifact-strength-decision-v1"
SCHEMA_VERSION = 1
VARIANTS = (
    "candidate_vs_baseline",
    "baseline_vs_candidate",
    "same_candidate",
    "same_baseline",
)


class CampaignError(RuntimeError):
    """Raised for invalid campaign inputs, reports, or resume state."""


def stable_json(value: Any) -> str:
    return json.dumps(value, indent=2, sort_keys=True) + "\n"


def sha256_text(value: str) -> str:
    return f"sha256:{hashlib.sha256(value.encode('utf-8')).hexdigest()}"


def write_json(path: Path, value: Any) -> None:
    path.write_text(stable_json(value), encoding="utf-8")


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CampaignError(f"cannot read JSON object {path}: {error}") from error
    if not isinstance(value, dict):
        raise CampaignError(f"JSON root must be an object: {path}")
    return value


def sha256_file(path: Path, cache: dict[Path, str] | None = None) -> str:
    resolved = path.resolve()
    if cache is not None and resolved in cache:
        return cache[resolved]
    digest = hashlib.sha256()
    try:
        with path.open("rb") as handle:
            while chunk := handle.read(1024 * 1024):
                digest.update(chunk)
    except OSError as error:
        raise CampaignError(f"cannot checksum {path}: {error}") from error
    value = f"sha256:{digest.hexdigest()}"
    if cache is not None:
        cache[resolved] = value
    return value


def display_path(path: Path, role: str) -> str:
    return f"<{role}>/{path.name}" if path.is_absolute() else str(path)


def sanitize_command(command: list[str]) -> list[str]:
    return [
        f"<local-path>/{Path(argument).name}" if Path(argument).is_absolute() else argument
        for argument in command
    ]


def sanitize_json_paths(value: Any) -> Any:
    if isinstance(value, dict):
        return {key: sanitize_json_paths(item) for key, item in value.items()}
    if isinstance(value, list):
        return [sanitize_json_paths(item) for item in value]
    if isinstance(value, str) and Path(value).is_absolute():
        return f"<local-path>/{Path(value).name}"
    return value


def parse_int_matrix(text: str, option: str, *, minimum: int) -> list[int]:
    values: list[int] = []
    for item in text.split(","):
        try:
            value = int(item.strip())
        except ValueError as error:
            raise argparse.ArgumentTypeError(f"{option} contains a non-integer: {item}") from error
        if value < minimum:
            raise argparse.ArgumentTypeError(f"{option} values must be >= {minimum}: {value}")
        if value in values:
            raise argparse.ArgumentTypeError(f"{option} contains a duplicate: {value}")
        values.append(value)
    if not values:
        raise argparse.ArgumentTypeError(f"{option} must not be empty")
    return values


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-manifest", type=Path, required=True)
    parser.add_argument("--candidate-weights", type=Path, required=True)
    parser.add_argument("--baseline-manifest", type=Path, required=True)
    parser.add_argument("--baseline-weights", type=Path, required=True)
    parser.add_argument("--opening-corpus", type=Path, required=True)
    parser.add_argument("--holdout-opening-corpus", type=Path)
    parser.add_argument("--arena-executable", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--campaign-seed", type=int, required=True)
    parser.add_argument("--opening-count", type=int, required=True)
    parser.add_argument("--holdout-opening-count", type=int)
    parser.add_argument(
        "--time-limits-ms",
        type=lambda value: parse_int_matrix(value, "--time-limits-ms", minimum=1),
        default=parse_int_matrix("50,100,500", "--time-limits-ms", minimum=1),
    )
    parser.add_argument(
        "--exact-thresholds",
        type=lambda value: parse_int_matrix(value, "--exact-thresholds", minimum=0),
        default=parse_int_matrix("8,10,12", "--exact-thresholds", minimum=0),
    )
    parser.add_argument("--tt-bytes", type=int, default=16 * 1024 * 1024)
    session = parser.add_mutually_exclusive_group()
    session.add_argument("--persistent-session", dest="persistent_session", action="store_true")
    session.add_argument("--no-persistent-session", dest="persistent_session", action="store_false")
    parser.set_defaults(persistent_session=True)
    parser.add_argument("--bootstrap-iterations", type=int, required=True)
    parser.add_argument("--confidence-level", type=float, required=True)
    parser.add_argument("--primary-time-ms", type=int)
    parser.add_argument("--primary-exact-threshold", type=int)
    parser.add_argument("--promotion-score-threshold", type=float, default=0.5)
    parser.add_argument("--promotion-ci-lower-threshold", type=float, default=0.5)
    parser.add_argument("--minimum-completed-depth-ratio", type=float, default=0.9)
    parser.add_argument("--minimum-promotion-cells", type=int, default=2)
    parser.add_argument("--search-preset", choices=("basic", "full"), default="full")
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args(argv)

    if args.opening_count <= 0:
        parser.error("--opening-count must be positive")
    if args.campaign_seed < 0:
        parser.error("--campaign-seed must be non-negative")
    if args.holdout_opening_count is not None and args.holdout_opening_count <= 0:
        parser.error("--holdout-opening-count must be positive")
    if args.holdout_opening_count is not None and args.holdout_opening_corpus is None:
        parser.error("--holdout-opening-count requires --holdout-opening-corpus")
    if args.tt_bytes < 0:
        parser.error("--tt-bytes must be non-negative")
    if args.bootstrap_iterations <= 0:
        parser.error("--bootstrap-iterations must be positive")
    if not 0.5 < args.confidence_level < 1.0:
        parser.error("--confidence-level must be between 0.5 and 1.0")
    if not 0.0 <= args.promotion_score_threshold <= 1.0:
        parser.error("--promotion-score-threshold must be in [0, 1]")
    if not 0.0 <= args.promotion_ci_lower_threshold <= 1.0:
        parser.error("--promotion-ci-lower-threshold must be in [0, 1]")
    if not 0.0 < args.minimum_completed_depth_ratio <= 1.0:
        parser.error("--minimum-completed-depth-ratio must be in (0, 1]")
    if args.minimum_promotion_cells < 2:
        parser.error("--minimum-promotion-cells must be at least 2")
    args.primary_time_ms = args.primary_time_ms or max(args.time_limits_ms)
    args.primary_exact_threshold = (
        args.primary_exact_threshold
        if args.primary_exact_threshold is not None
        else max(args.exact_thresholds)
    )
    if args.primary_time_ms not in args.time_limits_ms:
        parser.error("--primary-time-ms must be present in --time-limits-ms")
    if args.primary_exact_threshold not in args.exact_thresholds:
        parser.error("--primary-exact-threshold must be present in --exact-thresholds")
    if any(value > 64 for value in args.exact_thresholds):
        parser.error("--exact-thresholds values must be <= 64")
    return args


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise CampaignError(f"missing {label}: {path}")


def file_identity(path: Path, role: str, cache: dict[Path, str]) -> dict[str, Any]:
    require_file(path, role)
    return {
        "role": role,
        "path": display_path(path, role),
        "size_bytes": path.stat().st_size,
        "sha256": sha256_file(path, cache),
    }


def artifact_identity(
    manifest_path: Path, weights_path: Path, role: str, cache: dict[Path, str]
) -> dict[str, Any]:
    manifest = load_json(manifest_path)
    for field in ("pattern_set_id", "weights_checksum"):
        if not isinstance(manifest.get(field), str) or not manifest[field]:
            raise CampaignError(f"{role} manifest missing string {field}: {manifest_path}")
    require_file(weights_path, f"{role} weights")
    return {
        "role": role,
        "manifest_path": display_path(manifest_path, f"{role}-manifest"),
        "weights_path": display_path(weights_path, f"{role}-weights"),
        "artifact_id": manifest.get("artifact_id"),
        "pattern_set_id": manifest["pattern_set_id"],
        "manifest_weights_checksum": manifest["weights_checksum"],
        "manifest_sha256": sha256_file(manifest_path, cache),
        "weights_sha256": sha256_file(weights_path, cache),
    }


def git_output(arguments: list[str]) -> str:
    completed = subprocess.run(
        ["git", *arguments], check=False, capture_output=True, text=True, cwd=Path(__file__).parents[2]
    )
    if completed.returncode != 0:
        raise CampaignError(f"git {' '.join(arguments)} failed: {completed.stderr.strip()}")
    return completed.stdout.strip()


def repository_identity() -> dict[str, Any]:
    return {
        "sha": git_output(["rev-parse", "HEAD"]),
        "dirty": bool(git_output(["status", "--porcelain"])),
    }


def first_mismatch(expected: Any, actual: Any, path: str = "$") -> str | None:
    if expected == actual:
        return None
    if isinstance(expected, dict) and isinstance(actual, dict):
        for key in sorted(set(expected) | set(actual)):
            if key not in expected:
                return f"{path}.{key} unexpected"
            if key not in actual:
                return f"{path}.{key} missing"
            mismatch = first_mismatch(expected[key], actual[key], f"{path}.{key}")
            if mismatch is not None:
                return mismatch
    elif isinstance(expected, list) and isinstance(actual, list):
        if len(expected) != len(actual):
            return f"{path} length mismatch"
        for index, (expected_item, actual_item) in enumerate(zip(expected, actual, strict=True)):
            mismatch = first_mismatch(expected_item, actual_item, f"{path}[{index}]")
            if mismatch is not None:
                return mismatch
    return f"{path} mismatch"


def stage_paths(
    output_dir: Path, opening_set: str, time_ms: int, exact: int, variant: str
) -> tuple[Path, Path]:
    directory = output_dir / "runs" / opening_set / f"time-{time_ms}ms-exact-{exact}"
    return directory / f"{variant}.json", directory / f"{variant}.resume.json"


def arena_command(
    args: argparse.Namespace,
    corpus: Path,
    opening_count: int,
    opening_seed: int,
    time_ms: int,
    exact: int,
    variant: str,
    report_path: Path,
) -> list[str]:
    artifacts = {
        "candidate": (args.candidate_manifest, args.candidate_weights),
        "baseline": (args.baseline_manifest, args.baseline_weights),
    }
    role_pairs = {
        "candidate_vs_baseline": ("candidate", "baseline"),
        "baseline_vs_candidate": ("baseline", "candidate"),
        "same_candidate": ("candidate", "candidate"),
        "same_baseline": ("baseline", "baseline"),
    }
    candidate_role, baseline_role = role_pairs[variant]
    candidate_manifest, candidate_weights = artifacts[candidate_role]
    baseline_manifest, baseline_weights = artifacts[baseline_role]
    command = [
        str(args.arena_executable),
        "--candidate-manifest",
        str(candidate_manifest),
        "--candidate-weights",
        str(candidate_weights),
        "--candidate-name",
        candidate_role,
        "--baseline-manifest",
        str(baseline_manifest),
        "--baseline-weights",
        str(baseline_weights),
        "--baseline-name",
        baseline_role,
        "--openings",
        str(corpus),
        "--report-out",
        str(report_path),
        "--search-preset",
        args.search_preset,
        "--limit-mode",
        "time",
        "--time-ms",
        str(time_ms),
        "--exact-endgame-empties",
        str(exact),
        "--seed",
        str(opening_seed),
        "--opening-limit",
        str(opening_count),
        "--minimum-opening-pairs",
        str(opening_count),
        "--bootstrap-seed",
        str(args.campaign_seed),
        "--bootstrap-samples",
        str(args.bootstrap_iterations),
        "--tt-bytes",
        str(args.tt_bytes),
    ]
    if args.persistent_session:
        command.append("--persistent-session")
    return command


def require_mapping(mapping: dict[str, Any], field: str, context: str) -> dict[str, Any]:
    value = mapping.get(field)
    if not isinstance(value, dict):
        raise CampaignError(f"arena report missing object {context}.{field}")
    return value


def validate_arena_report(report: dict[str, Any], path: Path) -> None:
    required = {
        "candidate",
        "baseline",
        "inputs",
        "search_config",
        "opening_source_checksum",
        "selected_openings_checksum",
        "results",
        "game_records",
        "telemetry",
        "failed_games",
        "illegal_games",
        "paired_sanity",
        "strength_gate",
        "report_checksum",
    }
    missing = sorted(required - report.keys())
    if missing:
        raise CampaignError(f"arena report missing fields {missing}: {path}")
    results = require_mapping(report, "results", str(path))
    for field in ("overall", "by_side_assignment", "by_opening", "paired_score"):
        require_mapping(results, field, f"{path}.results")
    telemetry = require_mapping(report, "telemetry", str(path))
    for role in ("candidate", "baseline"):
        role_data = require_mapping(telemetry, role, f"{path}.telemetry")
        for field in ("overall", "by_phase", "by_side_to_move"):
            require_mapping(role_data, field, f"{path}.telemetry.{role}")
    search = require_mapping(report, "search_config", str(path))
    if search.get("limit_mode") != "fixed_wall_time":
        raise CampaignError(f"arena report is not fixed-wall-time: {path}")
    paired = require_mapping(report, "paired_sanity", str(path))
    if paired.get("paired_color_swap_complete") is not True:
        raise CampaignError(f"arena report lacks complete candidate Black/White pairs: {path}")


def expected_resume(
    command: list[str],
    inputs: list[dict[str, Any]],
    repo: dict[str, Any],
    runner_identity: dict[str, Any],
    executable_identity: dict[str, Any],
    artifacts: dict[str, Any],
    config: dict[str, Any],
) -> dict[str, Any]:
    return {
        "schema_version": SCHEMA_VERSION,
        "runner_version": RUNNER_VERSION,
        "command": sanitize_command(command),
        "command_identity_sha256": sha256_text(stable_json(command)),
        "inputs": inputs,
        "repository": repo,
        "tool_identity": {
            "runner": runner_identity,
            "arena_executable": executable_identity,
        },
        "artifacts": artifacts,
        "campaign_config": config,
    }


def run_stage(
    args: argparse.Namespace,
    opening_set: str,
    corpus: Path,
    opening_count: int,
    opening_seed: int,
    time_ms: int,
    exact: int,
    variant: str,
    shared: dict[str, Any],
    cache: dict[Path, str],
) -> tuple[dict[str, Any], str, Path]:
    report_path, sidecar_path = stage_paths(args.output_dir, opening_set, time_ms, exact, variant)
    command = arena_command(
        args, corpus, opening_count, opening_seed, time_ms, exact, variant, report_path
    )
    inputs = [
        file_identity(corpus, f"{opening_set}-opening-corpus", cache),
        file_identity(args.candidate_manifest, "candidate-manifest", cache),
        file_identity(args.candidate_weights, "candidate-weights", cache),
        file_identity(args.baseline_manifest, "baseline-manifest", cache),
        file_identity(args.baseline_weights, "baseline-weights", cache),
    ]
    expected = expected_resume(
        command,
        inputs,
        shared["repository"],
        shared["runner_identity"],
        shared["executable_identity"],
        shared["artifacts"],
        shared["campaign_config"],
    )
    if args.resume:
        if report_path.exists() and sidecar_path.exists():
            report = load_json(report_path)
            validate_arena_report(report, report_path)
            current = dict(expected)
            current["outputs"] = [file_identity(report_path, "arena-report", cache)]
            mismatch = first_mismatch(current, load_json(sidecar_path))
            if mismatch is not None:
                raise CampaignError(
                    f"resume metadata mismatch for {opening_set}/{time_ms}/{exact}/{variant} at {mismatch}"
                )
            return report, "skipped-resume-validated", report_path
        if report_path.exists() or sidecar_path.exists():
            raise CampaignError(
                f"incomplete stale outputs for {opening_set}/{time_ms}/{exact}/{variant}"
            )
    elif report_path.exists() or sidecar_path.exists():
        raise CampaignError(
            f"stale outputs exist for {opening_set}/{time_ms}/{exact}/{variant}; use --resume"
        )

    report_path.parent.mkdir(parents=True, exist_ok=True)
    print(f"running {opening_set} {time_ms}ms exact={exact} {variant}", flush=True)
    completed = subprocess.run(command, check=False, capture_output=True, text=True)
    if completed.returncode != 0:
        raise CampaignError(
            f"arena stage failed ({completed.returncode})\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    if not report_path.is_file():
        raise CampaignError(f"arena stage did not write report: {report_path}")
    report = load_json(report_path)
    validate_arena_report(report, report_path)
    complete = dict(expected)
    complete["outputs"] = [file_identity(report_path, "arena-report", cache)]
    write_json(sidecar_path, complete)
    return report, "completed", report_path


def numeric(mapping: dict[str, Any], field: str, context: str) -> float:
    value = mapping.get(field)
    if not isinstance(value, (int, float)):
        raise CampaignError(f"missing numeric field {context}.{field}")
    return float(value)


def candidate_score(result: Any) -> float:
    if result == "win":
        return 1.0
    if result == "draw":
        return 0.5
    if result == "loss":
        return 0.0
    raise CampaignError(f"unexpected candidate_result: {result!r}")


def opening_pair_scores(report: dict[str, Any]) -> list[float]:
    by_opening = require_mapping(require_mapping(report, "results", "report"), "by_opening", "results")
    scores: list[float] = []
    for opening, bucket_value in sorted(by_opening.items()):
        if not isinstance(bucket_value, dict):
            raise CampaignError(f"opening bucket is not an object: {opening}")
        if bucket_value.get("games") != 2:
            raise CampaignError(f"opening pair is incomplete: {opening}")
        scores.append(numeric(bucket_value, "candidate_score_rate", f"opening {opening}"))
    if not scores:
        raise CampaignError("arena report contains no opening pair scores")
    return scores


def percentile(values: list[float], probability: float) -> float:
    if not values:
        raise CampaignError("cannot take percentile of empty values")
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, int(probability * (len(ordered) - 1))))
    return ordered[index]


def bootstrap_interval(
    observations: list[float], seed: int, iterations: int, confidence: float
) -> dict[str, Any]:
    generator = random.Random(seed)
    count = len(observations)
    samples = [
        statistics.fmean(observations[generator.randrange(count)] for _ in range(count))
        for _ in range(iterations)
    ]
    tail = (1.0 - confidence) / 2.0
    return {
        "method": "deterministic-cluster-bootstrap-opening-pair",
        "confidence_level": confidence,
        "point_estimate": statistics.fmean(observations),
        "lower": percentile(samples, tail),
        "upper": percentile(samples, 1.0 - tail),
        "opening_pair_observations": count,
        "seed": seed,
        "iterations": iterations,
    }


def overall_result(reports: Iterable[dict[str, Any]]) -> dict[str, Any]:
    wins = losses = draws = failed = illegal = 0
    disc_diffs: list[int] = []
    games = 0
    for report in reports:
        bucket = require_mapping(require_mapping(report, "results", "report"), "overall", "results")
        wins += int(numeric(bucket, "candidate_wins", "results.overall"))
        losses += int(numeric(bucket, "candidate_losses", "results.overall"))
        draws += int(numeric(bucket, "draws", "results.overall"))
        failed += int(numeric(report, "failed_games", "report"))
        illegal += int(numeric(report, "illegal_games", "report"))
        records = report.get("game_records")
        if not isinstance(records, list):
            raise CampaignError("arena report game_records must be an array")
        for record in records:
            if not isinstance(record, dict) or not isinstance(record.get("candidate_disc_diff"), int):
                raise CampaignError("arena game record missing integer candidate_disc_diff")
            disc_diffs.append(record["candidate_disc_diff"])
        games += int(numeric(bucket, "games", "results.overall"))
    return {
        "games": games,
        "candidate_wins": wins,
        "candidate_losses": losses,
        "draws": draws,
        "candidate_score_rate": (wins + 0.5 * draws) / games if games else None,
        "average_disc_difference": statistics.fmean(disc_diffs) if disc_diffs else None,
        "median_disc_difference": statistics.median(disc_diffs) if disc_diffs else None,
        "failed_games": failed,
        "illegal_games": illegal,
    }


def combine_telemetry_summaries(summaries: Iterable[dict[str, Any]]) -> dict[str, Any]:
    totals = defaultdict(int)
    depths: list[int] = []
    counter_fields = (
        "search_calls",
        "elapsed_ns",
        "incremental_eval_enabled_searches",
        "incremental_state_initializations",
        "incremental_eval_calls",
        "stateless_eval_calls",
        "incremental_updates",
        "incremental_touched_instances",
        "exact_handoff_uses",
        "exact_root_searches",
        "exact_searches",
    )
    for summary in summaries:
        for field in counter_fields:
            totals[field] += int(numeric(summary, field, "telemetry summary"))
        for compound, output_name in (("nodes", "nodes"), ("eval_calls", "eval_calls")):
            data = require_mapping(summary, compound, "telemetry summary")
            totals[output_name] += int(numeric(data, "total", f"telemetry.{compound}"))
        histogram = require_mapping(summary, "completed_depth_histogram", "telemetry summary")
        for depth, count in histogram.items():
            depths.extend([int(depth)] * int(count))
    elapsed_ns = totals["elapsed_ns"]
    search_calls = totals["search_calls"]
    return {
        "search_calls": search_calls,
        "completed_depth_percentiles": {
            "p10": percentile([float(value) for value in depths], 0.10) if depths else None,
            "p50": percentile([float(value) for value in depths], 0.50) if depths else None,
            "p90": percentile([float(value) for value in depths], 0.90) if depths else None,
        },
        "nodes_per_sec": totals["nodes"] * 1_000_000_000 / elapsed_ns if elapsed_ns else None,
        "evals_per_sec": totals["eval_calls"] * 1_000_000_000 / elapsed_ns if elapsed_ns else None,
        "exact_completion_rate": totals["exact_searches"] / search_calls if search_calls else None,
        "exact_handoff_rate": totals["exact_handoff_uses"] / search_calls if search_calls else None,
        "incremental_evaluation_enabled_search_count": totals[
            "incremental_eval_enabled_searches"
        ],
        "incremental_eval_calls": totals["incremental_eval_calls"],
        "stateless_eval_calls": totals["stateless_eval_calls"],
        "incremental_updates": totals["incremental_updates"],
        "touched_pattern_instances": totals["incremental_touched_instances"],
        "counters": dict(totals),
    }


def combined_telemetry(reports: Iterable[dict[str, Any]]) -> dict[str, Any]:
    report_list = list(reports)
    output: dict[str, Any] = {}
    for role in ("candidate", "baseline"):
        summaries = [
            require_mapping(
                require_mapping(require_mapping(report, "telemetry", "report"), role, "telemetry"),
                "overall",
                f"telemetry.{role}",
            )
            for report in report_list
        ]
        output[role] = combine_telemetry_summaries(summaries)
    return output


def exposure_score_rates(reports: Iterable[dict[str, Any]]) -> tuple[dict[str, Any], dict[str, Any]]:
    phases: dict[str, list[float]] = defaultdict(list)
    sides: dict[str, list[float]] = defaultdict(list)
    for report in reports:
        for game in report["game_records"]:
            score = candidate_score(game.get("candidate_result"))
            calls = game.get("search_calls")
            if not isinstance(calls, list):
                raise CampaignError("arena game record search_calls must be an array")
            game_phases = {str(call["phase"]) for call in calls if isinstance(call, dict) and "phase" in call}
            game_sides = {
                str(call["side_to_move"])
                for call in calls
                if isinstance(call, dict) and "side_to_move" in call
            }
            for phase in game_phases:
                phases[phase].append(score)
            for side in game_sides:
                sides[side].append(score)

    def summarize(groups: dict[str, list[float]]) -> dict[str, Any]:
        return {
            key: {"game_exposures": len(values), "candidate_score_rate": statistics.fmean(values)}
            for key, values in sorted(groups.items())
        }

    return summarize(phases), summarize(sides)


def same_artifact_passes(report: dict[str, Any]) -> bool:
    paired = require_mapping(report, "paired_sanity", "report")
    overall = require_mapping(require_mapping(report, "results", "report"), "overall", "results")
    return (
        report.get("failed_games") == 0
        and report.get("illegal_games") == 0
        and paired.get("same_artifact_neutral") is True
        and abs(numeric(overall, "candidate_score_rate", "results.overall") - 0.5) <= 1.0e-12
        and abs(numeric(overall, "average_disc_diff_candidate_perspective", "results.overall"))
        <= 1.0e-12
    )


def swap_passes(forward: dict[str, Any], reverse: dict[str, Any]) -> bool:
    forward_overall = require_mapping(require_mapping(forward, "results", "forward"), "overall", "results")
    reverse_overall = require_mapping(require_mapping(reverse, "results", "reverse"), "overall", "results")
    return (
        forward.get("selected_openings_checksum") == reverse.get("selected_openings_checksum")
        and forward.get("failed_games") == 0
        and forward.get("illegal_games") == 0
        and reverse.get("failed_games") == 0
        and reverse.get("illegal_games") == 0
        and abs(
            numeric(forward_overall, "candidate_score_rate", "forward.overall")
            + numeric(reverse_overall, "candidate_score_rate", "reverse.overall")
            - 1.0
        )
        <= 1.0e-9
        and abs(
            numeric(
                forward_overall,
                "average_disc_diff_candidate_perspective",
                "forward.overall",
            )
            + numeric(
                reverse_overall,
                "average_disc_diff_candidate_perspective",
                "reverse.overall",
            )
        )
        <= 1.0e-9
    )


def depth_ratio(report: dict[str, Any]) -> float | None:
    telemetry = require_mapping(report, "telemetry", "report")
    values: dict[str, float] = {}
    for role in ("candidate", "baseline"):
        overall = require_mapping(require_mapping(telemetry, role, "telemetry"), "overall", role)
        percentiles = require_mapping(overall, "completed_depth_percentiles", f"telemetry.{role}")
        value = percentiles.get("p50")
        if not isinstance(value, (int, float)):
            return None
        values[role] = float(value)
    if values["baseline"] == 0.0:
        return 1.0 if values["candidate"] >= 0.0 else None
    return values["candidate"] / values["baseline"]


def suggest_decision(checks: dict[str, Any]) -> dict[str, Any]:
    reasons: list[str] = []
    if checks["failed_games"] or checks["illegal_games"]:
        reasons.append("failed or illegal games are present")
    if not checks["same_artifact_neutral"]:
        reasons.append("same-artifact sanity is not neutral")
    if not checks["swap_passed"]:
        reasons.append("candidate/baseline argument-order swap sanity failed")
    if reasons:
        return {"category": "reject_correctness", "reasons": reasons}
    if not checks["primary_eligible"]:
        return {"category": "inconclusive", "reasons": ["primary fixed-time cell is not eligible"]}
    if checks["primary_ci_upper"] < checks["score_threshold"]:
        return {
            "category": "reject_strength",
            "reasons": ["primary fixed-time confidence interval is wholly below the score threshold"],
        }
    if checks["eligible_cell_count"] < checks["minimum_promotion_cells"]:
        reasons.append("too few eligible matrix cells for promotion")
    if checks["primary_score_rate"] <= checks["score_threshold"]:
        reasons.append("primary candidate score rate does not exceed the configured threshold")
    if checks["primary_ci_lower"] <= checks["ci_lower_threshold"]:
        reasons.append("primary confidence interval lower bound does not exceed the configured threshold")
    if not checks["depth_regression_passed"]:
        reasons.append("primary completed-depth ratio indicates a material regression")
    if reasons:
        return {"category": "continue_validation", "reasons": reasons}
    return {
        "category": "promote",
        "reasons": ["all configured correctness, strength, matrix-size, and depth gates passed"],
    }


def build_decision(
    args: argparse.Namespace,
    shared: dict[str, Any],
    reports: dict[tuple[str, int, int, str], dict[str, Any]],
) -> dict[str, Any]:
    decision_set = "holdout" if args.holdout_opening_corpus is not None else "primary"
    forward_keys = sorted(
        key for key in reports if key[3] == "candidate_vs_baseline"
    )
    forward_reports = [reports[key] for key in forward_keys]
    primary_key = (
        decision_set,
        args.primary_time_ms,
        args.primary_exact_threshold,
        "candidate_vs_baseline",
    )
    primary = reports[primary_key]
    overall = overall_result(forward_reports)
    observations = [score for report in forward_reports for score in opening_pair_scores(report)]
    configured_ci = bootstrap_interval(
        observations, args.campaign_seed, args.bootstrap_iterations, args.confidence_level
    )
    ci_95 = bootstrap_interval(observations, args.campaign_seed, args.bootstrap_iterations, 0.95)
    primary_observations = opening_pair_scores(primary)
    primary_ci = bootstrap_interval(
        primary_observations,
        args.campaign_seed,
        args.bootstrap_iterations,
        args.confidence_level,
    )
    primary_ci_95 = bootstrap_interval(
        primary_observations, args.campaign_seed, args.bootstrap_iterations, 0.95
    )
    phase_scores, side_scores = exposure_score_rates(forward_reports)
    telemetry = combined_telemetry(forward_reports)

    cells: list[dict[str, Any]] = []
    same_passed = True
    swaps_passed = True
    all_failed = all_illegal = 0
    for opening_set in sorted({key[0] for key in reports}):
        for time_ms in args.time_limits_ms:
            for exact in args.exact_thresholds:
                cell_reports = {
                    variant: reports[(opening_set, time_ms, exact, variant)] for variant in VARIANTS
                }
                forward = cell_reports["candidate_vs_baseline"]
                reverse = cell_reports["baseline_vs_candidate"]
                same = same_artifact_passes(cell_reports["same_candidate"]) and same_artifact_passes(
                    cell_reports["same_baseline"]
                )
                swap = swap_passes(forward, reverse)
                same_passed = same_passed and same
                swaps_passed = swaps_passed and swap
                for report in cell_reports.values():
                    all_failed += int(report["failed_games"])
                    all_illegal += int(report["illegal_games"])
                cell_overall = overall_result([forward])
                cell_ci = bootstrap_interval(
                    opening_pair_scores(forward),
                    args.campaign_seed,
                    args.bootstrap_iterations,
                    args.confidence_level,
                )
                cells.append(
                    {
                        "opening_set": opening_set,
                        "time_limit_ms": time_ms,
                        "exact_threshold": exact,
                        "selected_opening_checksum": forward["selected_openings_checksum"],
                        "overall": cell_overall,
                        "bootstrap_confidence_interval": cell_ci,
                        "completed_depth_ratio_p50": depth_ratio(forward),
                        "same_artifact_neutral": same,
                        "swap_consistency_passed": swap,
                        "strength_gate_eligible": forward["strength_gate"].get("eligible") is True,
                    }
                )

    primary_overall = overall_result([primary])
    primary_ratio = depth_ratio(primary)
    eligible_cell_count = sum(
        1
        for cell in cells
        if cell["opening_set"] == decision_set and cell["strength_gate_eligible"]
    )
    checks = {
        "failed_games": all_failed,
        "illegal_games": all_illegal,
        "same_artifact_neutral": same_passed,
        "swap_passed": swaps_passed,
        "primary_eligible": primary["strength_gate"].get("eligible") is True,
        "primary_score_rate": primary_overall["candidate_score_rate"],
        "primary_ci_lower": primary_ci["lower"],
        "primary_ci_upper": primary_ci["upper"],
        "score_threshold": args.promotion_score_threshold,
        "ci_lower_threshold": args.promotion_ci_lower_threshold,
        "depth_regression_passed": primary_ratio is not None
        and primary_ratio >= args.minimum_completed_depth_ratio,
        "completed_depth_ratio_p50": primary_ratio,
        "minimum_completed_depth_ratio": args.minimum_completed_depth_ratio,
        "eligible_cell_count": eligible_cell_count,
        "minimum_promotion_cells": args.minimum_promotion_cells,
    }
    config = campaign_config(args, decision_set)
    return {
        "schema_version": SCHEMA_VERSION,
        "decision_version": DECISION_VERSION,
        "candidate_artifact": sanitize_json_paths(primary["candidate"]),
        "baseline_artifact": sanitize_json_paths(primary["baseline"]),
        "repo_sha": shared["repository"]["sha"],
        "repository": shared["repository"],
        "executable": shared["executable_identity"],
        "opening_corpora": shared["opening_corpora"],
        "selected_opening_checksum": primary["selected_openings_checksum"],
        "campaign_config": config,
        "overall": overall,
        "bootstrap_95_ci": ci_95,
        "bootstrap_confidence_interval": configured_ci,
        "phase_score_rate": phase_scores,
        "side_to_move_score_rate": side_scores,
        "telemetry": telemetry,
        "failed_games": all_failed,
        "illegal_games": all_illegal,
        "same_artifact_sanity": {"neutral": same_passed},
        "candidate_baseline_swap_consistency": {"passed": swaps_passed},
        "primary_fixed_time_cell": {
            "opening_set": decision_set,
            "time_limit_ms": args.primary_time_ms,
            "exact_threshold": args.primary_exact_threshold,
            "overall": primary_overall,
            "bootstrap_95_ci": primary_ci_95,
            "bootstrap_confidence_interval": primary_ci,
            "completed_depth_ratio_p50": primary_ratio,
            "strength_gate": primary["strength_gate"],
        },
        "promotion_checks": checks,
        "cells": cells,
        "suggested_decision": suggest_decision(checks),
        "non_claim_notes": [
            "local-only artifact-vs-artifact fixed-time campaign",
            "not an Elo or production-strength claim",
            "does not promote an artifact or change the default artifact pointer",
            "phase and side-to-move score rates are game-result exposure diagnostics",
            "generated arena reports and campaign output must not be committed",
        ],
    }


def campaign_config(args: argparse.Namespace, decision_set: str) -> dict[str, Any]:
    return {
        "campaign_seed": args.campaign_seed,
        "opening_count": args.opening_count,
        "holdout_opening_count": args.holdout_opening_count
        or (args.opening_count if args.holdout_opening_corpus is not None else None),
        "time_limits_ms": args.time_limits_ms,
        "exact_thresholds": args.exact_thresholds,
        "tt_bytes": args.tt_bytes,
        "persistent_session": args.persistent_session,
        "bootstrap_iterations": args.bootstrap_iterations,
        "confidence_level": args.confidence_level,
        "primary_time_ms": args.primary_time_ms,
        "primary_exact_threshold": args.primary_exact_threshold,
        "primary_opening_set": decision_set,
        "search_preset": args.search_preset,
        "promotion": {
            "score_threshold": args.promotion_score_threshold,
            "ci_lower_threshold": args.promotion_ci_lower_threshold,
            "minimum_completed_depth_ratio": args.minimum_completed_depth_ratio,
            "minimum_promotion_cells": args.minimum_promotion_cells,
        },
    }


def campaign_command(argv: list[str]) -> tuple[list[str], str]:
    raw = [sys.executable, str(Path(__file__)), *(value for value in argv if value != "--resume")]
    return sanitize_command(raw), sha256_text(stable_json(raw))


def write_summary(path: Path, decision: dict[str, Any]) -> None:
    overall = decision["overall"]
    primary = decision["primary_fixed_time_cell"]
    suggested = decision["suggested_decision"]
    lines = [
        "# Fixed-Time Artifact Strength Campaign",
        "",
        "Local-only artifact comparison. This is not Elo, a production-strength claim, "
        "or an artifact promotion.",
        "",
        "## Suggested decision",
        "",
        f"- category: `{suggested['category']}`",
        f"- reasons: {'; '.join(suggested['reasons'])}",
        "",
        "## Matrix aggregate",
        "",
        f"- games: {overall['games']}",
        f"- candidate W/L/D: {overall['candidate_wins']}/{overall['candidate_losses']}/{overall['draws']}",
        f"- candidate score rate: {overall['candidate_score_rate']:.6f}",
        "- average / median disc difference: "
        f"{overall['average_disc_difference']:.3f} / {overall['median_disc_difference']:.3f}",
        "- bootstrap 95% CI: "
        f"[{decision['bootstrap_95_ci']['lower']:.6f}, "
        f"{decision['bootstrap_95_ci']['upper']:.6f}]",
        f"- failed / illegal games: {decision['failed_games']} / {decision['illegal_games']}",
        "",
        "## Primary cell",
        "",
        f"- opening set: {primary['opening_set']}",
        f"- time / exact threshold: {primary['time_limit_ms']} ms / {primary['exact_threshold']}",
        f"- score rate: {primary['overall']['candidate_score_rate']:.6f}",
        "- bootstrap 95% CI: "
        f"[{primary['bootstrap_95_ci']['lower']:.6f}, "
        f"{primary['bootstrap_95_ci']['upper']:.6f}]",
        f"- candidate/baseline p50 completed-depth ratio: {primary['completed_depth_ratio_p50']}",
        "",
        "## Sanity",
        "",
        f"- same-artifact neutral: {decision['same_artifact_sanity']['neutral']}",
        f"- candidate/baseline swap consistency: {decision['candidate_baseline_swap_consistency']['passed']}",
        "",
        "Individual reports are listed in `arena-report-inventory.json`. Generated output "
        "remains local-only and must not be committed.",
    ]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main(argv: list[str]) -> int:
    try:
        args = parse_args(argv)
        cache: dict[Path, str] = {}
        args.output_dir.mkdir(parents=True, exist_ok=True)
        candidate = artifact_identity(
            args.candidate_manifest, args.candidate_weights, "candidate", cache
        )
        baseline = artifact_identity(
            args.baseline_manifest, args.baseline_weights, "baseline", cache
        )
        require_file(args.opening_corpus, "opening corpus")
        require_file(args.arena_executable, "arena executable")
        opening_sets = [
            ("primary", args.opening_corpus, args.opening_count, args.campaign_seed)
        ]
        if args.holdout_opening_corpus is not None:
            require_file(args.holdout_opening_corpus, "holdout opening corpus")
            opening_sets.append(
                (
                    "holdout",
                    args.holdout_opening_corpus,
                    args.holdout_opening_count or args.opening_count,
                    args.campaign_seed ^ 0x9E3779B97F4A7C15,
                )
            )
        shared = {
            "repository": repository_identity(),
            "runner_identity": file_identity(Path(__file__), "campaign-runner", cache),
            "executable_identity": file_identity(
                args.arena_executable, "arena-executable", cache
            ),
            "artifacts": {"candidate": candidate, "baseline": baseline},
            "opening_corpora": {
                name: file_identity(path, f"{name}-opening-corpus", cache)
                for name, path, _, _ in opening_sets
            },
            "campaign_config": campaign_config(
                args, "holdout" if args.holdout_opening_corpus is not None else "primary"
            ),
        }
        shared["campaign_command"], shared["campaign_command_identity_sha256"] = (
            campaign_command(argv)
        )
        reports: dict[tuple[str, int, int, str], dict[str, Any]] = {}
        inventory: list[dict[str, Any]] = []
        for opening_set, corpus, count, seed in opening_sets:
            for time_ms in args.time_limits_ms:
                for exact in args.exact_thresholds:
                    for variant in VARIANTS:
                        report, status, path = run_stage(
                            args,
                            opening_set,
                            corpus,
                            count,
                            seed,
                            time_ms,
                            exact,
                            variant,
                            shared,
                            cache,
                        )
                        key = (opening_set, time_ms, exact, variant)
                        reports[key] = report
                        inventory.append(
                            {
                                "opening_set": opening_set,
                                "time_limit_ms": time_ms,
                                "exact_threshold": exact,
                                "variant": variant,
                                "status": status,
                                "path": str(path.relative_to(args.output_dir)),
                                "sha256": sha256_file(path),
                                "arena_report_checksum": report["report_checksum"],
                                "selected_openings_checksum": report[
                                    "selected_openings_checksum"
                                ],
                            }
                        )

        decision = build_decision(args, shared, reports)
        decision_path = args.output_dir / "decision.json"
        inventory_path = args.output_dir / "arena-report-inventory.json"
        manifest_path = args.output_dir / "campaign-manifest.json"
        summary_path = args.output_dir / "summary.md"
        write_json(decision_path, decision)
        write_json(
            inventory_path,
            {
                "schema_version": SCHEMA_VERSION,
                "runner_version": RUNNER_VERSION,
                "reports": inventory,
            },
        )
        write_json(
            manifest_path,
            {
                "schema_version": SCHEMA_VERSION,
                "runner_version": RUNNER_VERSION,
                "repository": shared["repository"],
                "tool_identity": {
                    "runner": shared["runner_identity"],
                    "arena_executable": shared["executable_identity"],
                },
                "artifacts": shared["artifacts"],
                "opening_corpora": shared["opening_corpora"],
                "campaign_config": decision["campaign_config"],
                "command": shared["campaign_command"],
                "command_identity_sha256": shared["campaign_command_identity_sha256"],
                "report_inventory": "arena-report-inventory.json",
                "decision": "decision.json",
                "summary": "summary.md",
            },
        )
        write_summary(summary_path, decision)
        campaign_sidecar = {
            "schema_version": SCHEMA_VERSION,
            "runner_version": RUNNER_VERSION,
            "repository": shared["repository"],
            "tool_identity": {
                "runner": shared["runner_identity"],
                "arena_executable": shared["executable_identity"],
            },
            "artifacts": shared["artifacts"],
            "campaign_config": decision["campaign_config"],
            "command": shared["campaign_command"],
            "command_identity_sha256": shared["campaign_command_identity_sha256"],
            "outputs": [
                file_identity(path, role, cache)
                for role, path in (
                    ("decision", decision_path),
                    ("summary", summary_path),
                    ("report-inventory", inventory_path),
                    ("campaign-manifest", manifest_path),
                )
            ],
        }
        write_json(args.output_dir / "campaign.resume.json", campaign_sidecar)
        print(f"suggested_decision={decision['suggested_decision']['category']}")
        return 0
    except CampaignError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
