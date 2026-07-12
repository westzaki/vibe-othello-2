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
ARTIFACT_ROLE_PAIRS = {
    "candidate_vs_baseline": ("candidate", "baseline"),
    "baseline_vs_candidate": ("baseline", "candidate"),
    "same_candidate": ("candidate", "candidate"),
    "same_baseline": ("baseline", "baseline"),
}
FNV1A64_OFFSET_BASIS = 14695981039346656037
FNV1A64_PRIME = 1099511628211
MINIMUM_ALLOWED_PROMOTION_OPENING_PAIRS = 100
ChecksumCache = dict[tuple[str, Path], str]


class CampaignError(RuntimeError):
    """Raised for invalid campaign inputs, reports, or resume state."""


def stable_json(value: Any) -> str:
    return json.dumps(value, indent=2, sort_keys=True) + "\n"


def sha256_text(value: str) -> str:
    return f"sha256:{hashlib.sha256(value.encode('utf-8')).hexdigest()}"


def fnv1a64_bytes(value: bytes) -> str:
    checksum = FNV1A64_OFFSET_BASIS
    for byte in value:
        checksum ^= byte
        checksum = (checksum * FNV1A64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return f"fnv1a64:{checksum:016x}"


def fnv1a64_file(path: Path, cache: ChecksumCache | None = None) -> str:
    cache_key = ("fnv1a64", path.resolve())
    if cache is not None and cache_key in cache:
        return cache[cache_key]
    try:
        value = fnv1a64_bytes(path.read_bytes())
    except OSError as error:
        raise CampaignError(f"cannot checksum {path}: {error}") from error
    if cache is not None:
        cache[cache_key] = value
    return value


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


def sha256_file(path: Path, cache: ChecksumCache | None = None) -> str:
    resolved = path.resolve()
    cache_key = ("sha256", resolved)
    if cache is not None and cache_key in cache:
        return cache[cache_key]
    digest = hashlib.sha256()
    try:
        with path.open("rb") as handle:
            while chunk := handle.read(1024 * 1024):
                digest.update(chunk)
    except OSError as error:
        raise CampaignError(f"cannot checksum {path}: {error}") from error
    value = f"sha256:{digest.hexdigest()}"
    if cache is not None:
        cache[cache_key] = value
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
    parser.add_argument("--minimum-promotion-opening-pairs", type=int, default=100)
    parser.add_argument(
        "--minimum-promotion-time-limits",
        "--minimum-promotion-cells",
        dest="minimum_promotion_time_limits",
        type=int,
        default=2,
    )
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
    if args.minimum_promotion_opening_pairs < MINIMUM_ALLOWED_PROMOTION_OPENING_PAIRS:
        parser.error(
            "--minimum-promotion-opening-pairs must be at least "
            f"{MINIMUM_ALLOWED_PROMOTION_OPENING_PAIRS}"
        )
    if args.minimum_promotion_time_limits < 2:
        parser.error("--minimum-promotion-time-limits must be at least 2")
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


def file_identity(path: Path, role: str, cache: ChecksumCache) -> dict[str, Any]:
    require_file(path, role)
    return {
        "role": role,
        "path": display_path(path, role),
        "size_bytes": path.stat().st_size,
        "sha256": sha256_file(path, cache),
        "fnv1a64": fnv1a64_file(path, cache),
    }


def artifact_identity(
    manifest_path: Path, weights_path: Path, role: str, cache: ChecksumCache
) -> dict[str, Any]:
    manifest = load_json(manifest_path)
    for field in ("pattern_set_id", "weights_checksum"):
        if not isinstance(manifest.get(field), str) or not manifest[field]:
            raise CampaignError(f"{role} manifest missing string {field}: {manifest_path}")
    require_file(weights_path, f"{role} weights")
    trained_phases = manifest.get("trained_phases")
    if "trained_phases" in manifest and not isinstance(trained_phases, list):
        raise CampaignError(f"{role} manifest trained_phases must be an array: {manifest_path}")
    trained_phases_reported = isinstance(trained_phases, list)
    trained_phase_values = trained_phases if trained_phases_reported else []
    if any(type(phase) is not int or phase < 0 or phase > 12 for phase in trained_phase_values):
        raise CampaignError(
            f"{role} manifest trained_phases must contain integers in [0, 12]: {manifest_path}"
        )
    fallback_phase = manifest.get("fallback_additive_through_phase")
    if fallback_phase is not None and (
        type(fallback_phase) is not int or fallback_phase < 0 or fallback_phase > 12
    ):
        raise CampaignError(
            f"{role} manifest fallback_additive_through_phase must be in [0, 12]: "
            f"{manifest_path}"
        )
    evaluator_policy = (
        "phase-aware-covered-phases-with-fallback-residual"
        if type(fallback_phase) is int
        else "phase-aware-covered-phases"
        if trained_phases_reported
        else "phase-aware-legacy-all-phase-learned"
    )
    runtime_payload = (
        f"{manifest['pattern_set_id']}\n"
        f"{manifest['weights_checksum']}\n"
        f"{evaluator_policy}\n"
        f"{'1' if trained_phases_reported else '0'}\n"
        f"{fallback_phase if type(fallback_phase) is int else 'none'}"
        + "".join(f",{int(phase)}" for phase in trained_phase_values)
    )
    return {
        "role": role,
        "manifest_path": display_path(manifest_path, f"{role}-manifest"),
        "weights_path": display_path(weights_path, f"{role}-weights"),
        "artifact_id": manifest.get("artifact_id"),
        "pattern_set_id": manifest["pattern_set_id"],
        "manifest_weights_checksum": manifest["weights_checksum"],
        "manifest_sha256": sha256_file(manifest_path, cache),
        "weights_sha256": sha256_file(weights_path, cache),
        "manifest_content_checksum": fnv1a64_file(manifest_path, cache),
        "weights_file_checksum": fnv1a64_file(weights_path, cache),
        "runtime_identity_checksum": fnv1a64_bytes(runtime_payload.encode("utf-8")),
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
    candidate_role, baseline_role = ARTIFACT_ROLE_PAIRS[variant]
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


def expected_report_binding(
    args: argparse.Namespace,
    opening_set: str,
    opening_count: int,
    opening_seed: int,
    time_ms: int,
    exact: int,
    variant: str,
    shared: dict[str, Any],
) -> dict[str, Any]:
    candidate_role, baseline_role = ARTIFACT_ROLE_PAIRS[variant]
    candidate = shared["artifacts"][candidate_role]
    baseline = shared["artifacts"][baseline_role]
    return {
        "search_config": {
            "preset": args.search_preset,
            "limit_mode": "fixed_wall_time",
            "time_ms": time_ms,
            "exact_endgame_empties": exact,
            "persistent_session": args.persistent_session,
            "tt_requested_bytes": args.tt_bytes,
        },
        "candidate_name": candidate_role,
        "baseline_name": baseline_role,
        "candidate_runtime_identity_checksum": candidate["runtime_identity_checksum"],
        "baseline_runtime_identity_checksum": baseline["runtime_identity_checksum"],
        "candidate_manifest_checksum": candidate["manifest_content_checksum"],
        "candidate_weights_checksum": candidate["weights_file_checksum"],
        "baseline_manifest_checksum": baseline["manifest_content_checksum"],
        "baseline_weights_checksum": baseline["weights_file_checksum"],
        "executable_checksum": shared["executable_identity"]["fnv1a64"],
        "opening_source_checksum": shared["opening_corpora"][opening_set]["fnv1a64"],
        "opening_count": opening_count,
        "opening_limit": opening_count,
        "seed": opening_seed,
        "bootstrap_seed": args.campaign_seed,
        "bootstrap_samples": args.bootstrap_iterations,
        "minimum_opening_pairs": opening_count,
    }


def require_equal(actual: Any, expected: Any, field: str, path: Path) -> None:
    if actual != expected:
        raise CampaignError(
            f"arena report binding mismatch for {field}: expected {expected!r}, "
            f"got {actual!r}: {path}"
        )


def validate_arena_report(
    report: dict[str, Any], path: Path, expected: dict[str, Any]
) -> None:
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
    for field, expected_value in expected["search_config"].items():
        require_equal(search.get(field), expected_value, f"search_config.{field}", path)
    paired = require_mapping(report, "paired_sanity", str(path))
    if paired.get("paired_color_swap_complete") is not True:
        raise CampaignError(f"arena report lacks complete candidate Black/White pairs: {path}")
    candidate = require_mapping(report, "candidate", str(path))
    baseline = require_mapping(report, "baseline", str(path))
    require_equal(candidate.get("name"), expected["candidate_name"], "candidate.name", path)
    require_equal(baseline.get("name"), expected["baseline_name"], "baseline.name", path)
    require_equal(
        candidate.get("runtime_identity_checksum"),
        expected["candidate_runtime_identity_checksum"],
        "candidate.runtime_identity_checksum",
        path,
    )
    require_equal(
        baseline.get("runtime_identity_checksum"),
        expected["baseline_runtime_identity_checksum"],
        "baseline.runtime_identity_checksum",
        path,
    )
    inputs = require_mapping(report, "inputs", str(path))
    for field in (
        "candidate_manifest_checksum",
        "candidate_weights_checksum",
        "baseline_manifest_checksum",
        "baseline_weights_checksum",
    ):
        require_equal(inputs.get(field), expected[field], f"inputs.{field}", path)
    executable = require_mapping(inputs, "executable", f"{path}.inputs")
    require_equal(
        executable.get("checksum"),
        expected["executable_checksum"],
        "inputs.executable.checksum",
        path,
    )
    for field in ("opening_source_checksum", "opening_count", "opening_limit", "seed"):
        require_equal(report.get(field), expected[field], field, path)
    paired_score = require_mapping(results, "paired_score", f"{path}.results")
    require_equal(
        paired_score.get("bootstrap_seed"), expected["bootstrap_seed"], "bootstrap_seed", path
    )
    require_equal(
        paired_score.get("bootstrap_samples"),
        expected["bootstrap_samples"],
        "bootstrap_samples",
        path,
    )
    gate = require_mapping(report, "strength_gate", str(path))
    require_equal(
        gate.get("minimum_opening_pairs"),
        expected["minimum_opening_pairs"],
        "strength_gate.minimum_opening_pairs",
        path,
    )


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
    cache: ChecksumCache,
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
    report_binding = expected_report_binding(
        args,
        opening_set,
        opening_count,
        opening_seed,
        time_ms,
        exact,
        variant,
        shared,
    )
    if args.resume:
        if report_path.exists() and sidecar_path.exists():
            report = load_json(report_path)
            current = dict(expected)
            current["outputs"] = [file_identity(report_path, "arena-report", cache)]
            mismatch = first_mismatch(current, load_json(sidecar_path))
            if mismatch is not None:
                raise CampaignError(
                    f"resume metadata mismatch for {opening_set}/{time_ms}/{exact}/{variant} at {mismatch}"
                )
            validate_arena_report(report, report_path, report_binding)
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
    validate_arena_report(report, report_path, report_binding)
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
    return list(opening_pair_score_map(report).values())


def opening_pair_score_map(report: dict[str, Any]) -> dict[str, float]:
    by_opening = require_mapping(require_mapping(report, "results", "report"), "by_opening", "results")
    scores: dict[str, float] = {}
    for opening, bucket_value in sorted(by_opening.items()):
        if not isinstance(bucket_value, dict):
            raise CampaignError(f"opening bucket is not an object: {opening}")
        if bucket_value.get("games") != 2:
            raise CampaignError(f"opening pair is incomplete: {opening}")
        scores[opening] = numeric(bucket_value, "candidate_score_rate", f"opening {opening}")
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


def argument_order_observations(
    forward: dict[str, Any], reverse: dict[str, Any]
) -> list[float]:
    forward_scores = opening_pair_score_map(forward)
    reverse_scores = opening_pair_score_map(reverse)
    if forward_scores.keys() != reverse_scores.keys():
        raise CampaignError("candidate/baseline argument-order reports used different openings")
    return [
        (forward_scores[key] + (1.0 - reverse_scores[key])) / 2.0
        for key in sorted(forward_scores)
    ]


def argument_order_overall(
    forward: dict[str, Any], reverse: dict[str, Any]
) -> dict[str, Any]:
    result = oriented_overall(((forward, False), (reverse, True)))
    result["descriptive_only"] = False
    return result


def oriented_overall(
    reports: Iterable[tuple[dict[str, Any], bool]]
) -> dict[str, Any]:
    wins = losses = draws = failed = illegal = 0
    disc_diffs: list[int] = []
    for report, reverse_perspective in reports:
        failed += int(numeric(report, "failed_games", "report"))
        illegal += int(numeric(report, "illegal_games", "report"))
        records = report.get("game_records")
        if not isinstance(records, list):
            raise CampaignError("arena report game_records must be an array")
        for record in records:
            if not isinstance(record, dict) or not isinstance(
                record.get("candidate_disc_diff"), int
            ):
                raise CampaignError("arena game record missing integer candidate_disc_diff")
            score = candidate_score(record.get("candidate_result"))
            if reverse_perspective:
                score = 1.0 - score
            if score == 1.0:
                wins += 1
            elif score == 0.5:
                draws += 1
            else:
                losses += 1
            disc_diffs.append(
                -record["candidate_disc_diff"]
                if reverse_perspective
                else record["candidate_disc_diff"]
            )
    games = wins + losses + draws
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
        "descriptive_only": True,
        "argument_orders_combined": True,
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


def same_artifact_diagnostic(
    report: dict[str, Any], seed: int, iterations: int
) -> dict[str, Any]:
    paired = require_mapping(report, "paired_sanity", "report")
    overall = require_mapping(require_mapping(report, "results", "report"), "overall", "results")
    exact_neutral = (
        report.get("failed_games") == 0
        and report.get("illegal_games") == 0
        and paired.get("same_artifact_neutral") is True
        and abs(numeric(overall, "candidate_score_rate", "results.overall") - 0.5) <= 1.0e-12
        and abs(numeric(overall, "average_disc_diff_candidate_perspective", "results.overall"))
        <= 1.0e-12
    )
    interval = bootstrap_interval(opening_pair_scores(report), seed, iterations, 0.95)
    return {
        "diagnostic_only": True,
        "clean": report.get("failed_games") == 0 and report.get("illegal_games") == 0,
        "exactly_neutral": exact_neutral,
        "bootstrap_95_ci": interval,
        "ci95_includes_neutral": interval["lower"] <= 0.5 <= interval["upper"],
    }


def swap_diagnostic(forward: dict[str, Any], reverse: dict[str, Any]) -> dict[str, Any]:
    forward_overall = require_mapping(require_mapping(forward, "results", "forward"), "overall", "results")
    reverse_overall = require_mapping(require_mapping(reverse, "results", "reverse"), "overall", "results")
    selected_openings_match = (
        forward.get("selected_openings_checksum") == reverse.get("selected_openings_checksum")
    )
    exactly_complementary = (
        selected_openings_match
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
    return {
        "diagnostic_only": True,
        "selected_openings_match": selected_openings_match,
        "exactly_complementary": exactly_complementary,
        "forward_score_rate": numeric(
            forward_overall, "candidate_score_rate", "forward.overall"
        ),
        "reverse_score_rate": numeric(
            reverse_overall, "candidate_score_rate", "reverse.overall"
        ),
    }


def completed_depth_p50(report: dict[str, Any], role: str) -> float | None:
    telemetry = require_mapping(report, "telemetry", "report")
    overall = require_mapping(require_mapping(telemetry, role, "telemetry"), "overall", role)
    percentiles = require_mapping(overall, "completed_depth_percentiles", f"telemetry.{role}")
    value = percentiles.get("p50")
    return float(value) if isinstance(value, (int, float)) else None


def ratio_or_none(numerator: float | None, denominator: float | None) -> float | None:
    if numerator is None or denominator is None:
        return None
    if denominator == 0.0:
        return 1.0 if numerator >= 0.0 else None
    return numerator / denominator


def argument_order_depth_ratio(
    forward: dict[str, Any], reverse: dict[str, Any]
) -> float | None:
    forward_ratio = ratio_or_none(
        completed_depth_p50(forward, "candidate"),
        completed_depth_p50(forward, "baseline"),
    )
    reverse_ratio = ratio_or_none(
        completed_depth_p50(reverse, "baseline"),
        completed_depth_p50(reverse, "candidate"),
    )
    if forward_ratio is None or reverse_ratio is None:
        return None
    return min(forward_ratio, reverse_ratio)


def promotion_cell_assessment(
    *,
    strength_gate_eligible: bool,
    score_rate: float,
    ci95: dict[str, Any],
    completed_depth_ratio: float | None,
    opening_pair_count: int,
    score_threshold: float,
    ci_lower_threshold: float,
    minimum_completed_depth_ratio: float,
    minimum_opening_pairs: int,
) -> dict[str, Any]:
    checks = {
        "strength_gate_eligible": strength_gate_eligible,
        "score_rate_passed": score_rate > score_threshold,
        "ci95_lower_bound_passed": ci95["lower"] > ci_lower_threshold,
        "completed_depth_passed": completed_depth_ratio is not None
        and completed_depth_ratio >= minimum_completed_depth_ratio,
        "minimum_opening_pairs_passed": opening_pair_count >= minimum_opening_pairs,
    }
    return {**checks, "promotion_passed": all(checks.values())}


def suggest_decision(checks: dict[str, Any]) -> dict[str, Any]:
    reasons: list[str] = []
    if checks["failed_games"] or checks["illegal_games"]:
        return {
            "category": "reject_correctness",
            "reasons": ["failed or illegal games are present"],
        }
    if not checks["primary_strength_gate_eligible"]:
        return {"category": "inconclusive", "reasons": ["primary fixed-time cell is not eligible"]}
    if not checks["primary_minimum_opening_pairs_passed"]:
        return {
            "category": "continue_validation",
            "reasons": ["primary fixed-time cell has too few opening pairs for promotion"],
        }
    if checks["primary_ci95_upper"] < checks["ci_lower_threshold"]:
        return {
            "category": "reject_strength",
            "reasons": [
                "primary fixed-time 95% confidence interval is wholly below the score threshold"
            ],
        }
    if not checks["primary_promotion_passed"]:
        reasons.append("primary cell did not pass score, fixed-95%-CI, depth, and sample gates")
    if checks["promotion_time_limit_count"] < checks["minimum_promotion_time_limits"]:
        reasons.append("too few distinct fixed-time limits passed every promotion gate")
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
    forward_keys = sorted(key for key in reports if key[3] == "candidate_vs_baseline")
    forward_reports = [reports[key] for key in forward_keys]
    primary_key = (
        decision_set,
        args.primary_time_ms,
        args.primary_exact_threshold,
        "candidate_vs_baseline",
    )
    primary = reports[primary_key]
    phase_scores, side_scores = exposure_score_rates(forward_reports)
    telemetry = combined_telemetry(forward_reports)

    cells: list[dict[str, Any]] = []
    same_diagnostics: list[dict[str, Any]] = []
    swap_diagnostics: list[dict[str, Any]] = []
    oriented_reports: list[tuple[dict[str, Any], bool]] = []
    all_failed = all_illegal = 0
    for opening_set in sorted({key[0] for key in reports}):
        for time_ms in args.time_limits_ms:
            for exact in args.exact_thresholds:
                cell_reports = {
                    variant: reports[(opening_set, time_ms, exact, variant)] for variant in VARIANTS
                }
                forward = cell_reports["candidate_vs_baseline"]
                reverse = cell_reports["baseline_vs_candidate"]
                oriented_reports.extend(((forward, False), (reverse, True)))
                same_candidate = {
                    "opening_set": opening_set,
                    "time_limit_ms": time_ms,
                    "exact_threshold": exact,
                    "artifact": "candidate",
                    **same_artifact_diagnostic(
                        cell_reports["same_candidate"],
                        args.campaign_seed,
                        args.bootstrap_iterations,
                    ),
                }
                same_baseline = {
                    "opening_set": opening_set,
                    "time_limit_ms": time_ms,
                    "exact_threshold": exact,
                    "artifact": "baseline",
                    **same_artifact_diagnostic(
                        cell_reports["same_baseline"],
                        args.campaign_seed,
                        args.bootstrap_iterations,
                    ),
                }
                swap = {
                    "opening_set": opening_set,
                    "time_limit_ms": time_ms,
                    "exact_threshold": exact,
                    **swap_diagnostic(forward, reverse),
                }
                same_diagnostics.extend((same_candidate, same_baseline))
                swap_diagnostics.append(swap)
                for report in cell_reports.values():
                    all_failed += int(report["failed_games"])
                    all_illegal += int(report["illegal_games"])
                cell_overall = argument_order_overall(forward, reverse)
                observations = argument_order_observations(forward, reverse)
                configured_ci = bootstrap_interval(
                    observations,
                    args.campaign_seed,
                    args.bootstrap_iterations,
                    args.confidence_level,
                )
                ci95 = bootstrap_interval(
                    observations, args.campaign_seed, args.bootstrap_iterations, 0.95
                )
                completed_depth_ratio = argument_order_depth_ratio(forward, reverse)
                strength_gate_eligible = (
                    forward["strength_gate"].get("eligible") is True
                    and reverse["strength_gate"].get("eligible") is True
                )
                promotion = promotion_cell_assessment(
                    strength_gate_eligible=strength_gate_eligible,
                    score_rate=cell_overall["candidate_score_rate"],
                    ci95=ci95,
                    completed_depth_ratio=completed_depth_ratio,
                    opening_pair_count=len(observations),
                    score_threshold=args.promotion_score_threshold,
                    ci_lower_threshold=args.promotion_ci_lower_threshold,
                    minimum_completed_depth_ratio=args.minimum_completed_depth_ratio,
                    minimum_opening_pairs=args.minimum_promotion_opening_pairs,
                )
                cells.append(
                    {
                        "opening_set": opening_set,
                        "time_limit_ms": time_ms,
                        "exact_threshold": exact,
                        "selected_opening_checksum": forward["selected_openings_checksum"],
                        "opening_pair_count": len(observations),
                        "overall": cell_overall,
                        "bootstrap_95_ci": ci95,
                        "bootstrap_confidence_interval": configured_ci,
                        "completed_depth_ratio_p50": completed_depth_ratio,
                        "same_artifact_diagnostics": {
                            "candidate": same_candidate,
                            "baseline": same_baseline,
                        },
                        "argument_order_diagnostic": swap,
                        "strength_gate": {
                            "eligible": strength_gate_eligible,
                            "forward": forward["strength_gate"],
                            "reverse": reverse["strength_gate"],
                        },
                        "promotion_checks": promotion,
                        "promotion_passed": promotion["promotion_passed"],
                    }
                )

    overall = oriented_overall(oriented_reports)
    overall["heterogeneous_matrix_aggregate"] = True
    overall["confidence_interval"] = None
    overall["confidence_interval_reason"] = (
        "not reported because repeated openings across heterogeneous matrix cells are not "
        "independent observations"
    )
    primary_cell = next(
        cell
        for cell in cells
        if cell["opening_set"] == decision_set
        and cell["time_limit_ms"] == args.primary_time_ms
        and cell["exact_threshold"] == args.primary_exact_threshold
    )
    promotion_cells = [
        cell
        for cell in cells
        if cell["opening_set"] == decision_set and cell["promotion_passed"]
    ]
    promotion_time_limits = sorted({cell["time_limit_ms"] for cell in promotion_cells})
    primary_promotion = primary_cell["promotion_checks"]
    checks = {
        "failed_games": all_failed,
        "illegal_games": all_illegal,
        "primary_strength_gate_eligible": primary_cell["strength_gate"]["eligible"],
        "primary_opening_pair_count": primary_cell["opening_pair_count"],
        "minimum_promotion_opening_pairs": args.minimum_promotion_opening_pairs,
        "primary_minimum_opening_pairs_passed": primary_promotion[
            "minimum_opening_pairs_passed"
        ],
        "primary_score_rate": primary_cell["overall"]["candidate_score_rate"],
        "primary_ci95_lower": primary_cell["bootstrap_95_ci"]["lower"],
        "primary_ci95_upper": primary_cell["bootstrap_95_ci"]["upper"],
        "score_threshold": args.promotion_score_threshold,
        "ci_lower_threshold": args.promotion_ci_lower_threshold,
        "primary_completed_depth_ratio_p50": primary_cell["completed_depth_ratio_p50"],
        "primary_completed_depth_passed": primary_promotion["completed_depth_passed"],
        "minimum_completed_depth_ratio": args.minimum_completed_depth_ratio,
        "primary_promotion_passed": primary_cell["promotion_passed"],
        "promotion_passed_cell_count": len(promotion_cells),
        "promotion_passed_time_limits": promotion_time_limits,
        "promotion_time_limit_count": len(promotion_time_limits),
        "minimum_promotion_time_limits": args.minimum_promotion_time_limits,
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
        "phase_score_rate": phase_scores,
        "side_to_move_score_rate": side_scores,
        "telemetry": telemetry,
        "failed_games": all_failed,
        "illegal_games": all_illegal,
        "same_artifact_sanity": {
            "diagnostic_only": True,
            "all_exactly_neutral": all(
                diagnostic["exactly_neutral"] for diagnostic in same_diagnostics
            ),
            "all_ci95_include_neutral": all(
                diagnostic["ci95_includes_neutral"] for diagnostic in same_diagnostics
            ),
            "checks": same_diagnostics,
        },
        "candidate_baseline_swap_consistency": {
            "diagnostic_only": True,
            "all_selected_openings_match": all(
                diagnostic["selected_openings_match"] for diagnostic in swap_diagnostics
            ),
            "all_exactly_complementary": all(
                diagnostic["exactly_complementary"] for diagnostic in swap_diagnostics
            ),
            "checks": swap_diagnostics,
        },
        "primary_fixed_time_cell": primary_cell,
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
            "confidence_level": 0.95,
            "minimum_completed_depth_ratio": args.minimum_completed_depth_ratio,
            "minimum_opening_pairs": args.minimum_promotion_opening_pairs,
            "minimum_distinct_time_limits": args.minimum_promotion_time_limits,
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
        "- confidence interval: not reported for the heterogeneous matrix aggregate",
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
        f"- opening pairs: {primary['opening_pair_count']}",
        f"- promotion passed: {primary['promotion_passed']}",
        "- passing distinct time limits: "
        f"{decision['promotion_checks']['promotion_passed_time_limits']}",
        "",
        "## Fixed-time diagnostics",
        "",
        "These timing-sensitive checks are diagnostics and are not correctness rejection gates.",
        "",
        "- all same-artifact 95% CIs include neutral: "
        f"{decision['same_artifact_sanity']['all_ci95_include_neutral']}",
        "- all argument-order selected openings match: "
        f"{decision['candidate_baseline_swap_consistency']['all_selected_openings_match']}",
        "",
        "Individual reports are listed in `arena-report-inventory.json`. Generated output "
        "remains local-only and must not be committed.",
    ]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main(argv: list[str]) -> int:
    try:
        args = parse_args(argv)
        cache: ChecksumCache = {}
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
