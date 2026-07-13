#!/usr/bin/env python3
"""Run a local same-artifact off/single/Multi-ProbCut strength matrix."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from typing import Any


RUNNER_VERSION = "multi-probcut-strength-campaign-v2"
COMPARISONS = {
    "same_config_off": ("off", "off"),
    "single_off_forward": ("single", "off"),
    "single_off_reverse": ("off", "single"),
    "multi_off_forward": ("multi", "off"),
    "multi_off_reverse": ("off", "multi"),
    "multi_forward": ("multi", "single"),
    "multi_reverse": ("single", "multi"),
    "shadow_multi_forward": ("shadow", "off"),
    "shadow_multi_reverse": ("off", "shadow"),
}


class CampaignError(RuntimeError):
    """Raised for invalid inputs or arena output."""


def parse_int_list(value: str, label: str, *, positive: bool = False) -> list[int]:
    try:
        values = [int(item) for item in value.split(",") if item]
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"{label} must be a comma-separated integer list") from error
    if not values or any(item < (1 if positive else 0) for item in values):
        raise argparse.ArgumentTypeError(f"{label} contains an invalid value")
    return values


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CampaignError(f"cannot read arena report {path}: {error}") from error
    if not isinstance(value, dict):
        raise CampaignError(f"arena report is not an object: {path}")
    return value


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as handle:
            while chunk := handle.read(1024 * 1024):
                digest.update(chunk)
    except OSError as error:
        raise CampaignError(f"cannot checksum {path}: {error}") from error
    return digest.hexdigest()


def fnv1a64_file(path: Path) -> str:
    value = 14_695_981_039_346_656_037
    try:
        data = path.read_bytes()
    except OSError as error:
        raise CampaignError(f"cannot checksum {path}: {error}") from error
    for byte in data:
        value ^= byte
        value = (value * 1_099_511_628_211) & ((1 << 64) - 1)
    return f"fnv1a64:{value:016x}"


def profile_identity(path: Path) -> dict[str, Any]:
    try:
        rows = [line for line in path.read_text(encoding="utf-8").splitlines() if line and not line.startswith("#")]
    except OSError as error:
        raise CampaignError(f"cannot read profile {path}: {error}") from error
    if len(rows) < 2:
        raise CampaignError("ProbCut profile has no data rows")
    header = rows[0].split("\t")
    values = rows[1].split("\t")
    if len(header) != len(values):
        raise CampaignError("ProbCut profile first row does not match its header")
    required = (
        "profile_id",
        "source_checksum_sha256",
        "joint_holdout_checksum_sha256",
        "evaluator_family",
        "artifact_family",
        "validated_maximum_probes_per_node",
        "joint_false_cut_count",
        "joint_cut_candidate_count",
        "joint_false_cut_rate_upper_bound",
        "scheduler_domain_evidence",
    )
    row = dict(zip(header, values, strict=True))
    if any(not row.get(field) for field in required):
        raise CampaignError("ProbCut profile identity is incomplete")
    identity = {field: row[field] for field in required}
    for field in ("source_checksum_sha256", "joint_holdout_checksum_sha256"):
        checksum = identity[field]
        if len(checksum) != 64 or any(character not in "0123456789abcdef" for character in checksum):
            raise CampaignError(f"ProbCut profile {field} is not lowercase SHA-256")
    pairs: list[dict[str, int]] = []
    try:
        for line in rows[1:]:
            fields = dict(zip(header, line.split("\t"), strict=True))
            if any(fields.get(field) != value for field, value in identity.items()):
                raise CampaignError("ProbCut profile mixes calibration identities")
            pair = {
                "deep_depth": int(fields["deep_depth"]),
                "shallow_depth": int(fields["shallow_depth"]),
            }
            if pair["deep_depth"] <= pair["shallow_depth"] or pair["shallow_depth"] <= 0:
                raise CampaignError("ProbCut profile contains an invalid depth pair")
            if pair not in pairs:
                pairs.append(pair)
    except (KeyError, ValueError) as error:
        raise CampaignError(f"ProbCut profile row is malformed: {error}") from error
    try:
        validated_maximum_probes = int(identity["validated_maximum_probes_per_node"])
        joint_false_cuts = int(identity["joint_false_cut_count"])
        joint_candidates = int(identity["joint_cut_candidate_count"])
        joint_upper = float(identity["joint_false_cut_rate_upper_bound"])
    except ValueError as error:
        raise CampaignError(f"ProbCut scheduler evidence is malformed: {error}") from error
    if (
        validated_maximum_probes <= 0
        or validated_maximum_probes > len(pairs)
        or joint_candidates <= 0
        or not 0 <= joint_false_cuts <= joint_candidates
        or not 0.0 <= joint_upper <= 1.0
    ):
        raise CampaignError("ProbCut scheduler evidence is invalid")
    scheduler_domain_evidence = identity["scheduler_domain_evidence"].split(";")
    if not scheduler_domain_evidence or any(record.count(":") != 14 for record in scheduler_domain_evidence):
        raise CampaignError("ProbCut scheduler/domain evidence is malformed")
    try:
        reviewed_scheduler_configurations = {
            (int(record.split(":", 2)[0]), int(record.split(":", 2)[1]))
            for record in scheduler_domain_evidence
        }
    except ValueError as error:
        raise CampaignError(f"ProbCut scheduler/domain evidence is malformed: {error}") from error
    required_scheduler_configurations = {
        (1, 1),
        (len(pairs), validated_maximum_probes),
    }
    if not required_scheduler_configurations.issubset(reviewed_scheduler_configurations):
        raise CampaignError("ProbCut profile does not authorize both single and multi campaign modes")
    return {
        **identity,
        "profile_file_sha256": sha256_file(path),
        "validated_pair_order": pairs,
        "validated_maximum_probes_per_node": validated_maximum_probes,
        "joint_false_cut_count": joint_false_cuts,
        "joint_cut_candidate_count": joint_candidates,
        "joint_false_cut_rate_upper_bound": joint_upper,
        "scheduler_domain_evidence_count": len(scheduler_domain_evidence),
        "scheduler_domain_evidence_sha256": hashlib.sha256(
            identity["scheduler_domain_evidence"].encode("utf-8")
        ).hexdigest(),
    }


def stage_limits(args: argparse.Namespace) -> list[tuple[str, list[str], int]]:
    stages = [
        (f"fixed-depth-{args.fixed_depth}", ["--limit-mode", "depth", "--depth", str(args.fixed_depth)], 0),
        (f"fixed-nodes-{args.fixed_nodes}", ["--limit-mode", "nodes", "--nodes", str(args.fixed_nodes)], args.exact_endgame_empties),
    ]
    stages.extend(
        (f"fixed-time-{time_ms}ms", ["--limit-mode", "time", "--time-ms", str(time_ms)], args.exact_endgame_empties)
        for time_ms in args.time_ms
    )
    return stages


def command_for(
    args: argparse.Namespace,
    openings: Path,
    seed: int,
    limit_args: list[str],
    candidate_mode: str,
    baseline_mode: str,
    exact_endgame_empties: int,
    output: Path,
) -> list[str]:
    command = [
        str(args.arena_executable),
        "--candidate-manifest", str(args.artifact_manifest),
        "--candidate-weights", str(args.artifact_weights),
        "--candidate-name", f"same-artifact-{candidate_mode}",
        "--baseline-manifest", str(args.artifact_manifest),
        "--baseline-weights", str(args.artifact_weights),
        "--baseline-name", f"same-artifact-{baseline_mode}",
        "--openings", str(openings),
        "--report-out", str(output),
        "--search-preset", args.search_preset,
        *limit_args,
        "--exact-endgame-empties", str(exact_endgame_empties),
        "--seed", str(seed),
        "--bootstrap-seed", str(args.bootstrap_seed),
        "--bootstrap-samples", str(args.bootstrap_samples),
        "--minimum-opening-pairs", str(args.minimum_opening_pairs),
        "--tt-bytes", str(args.tt_bytes),
        "--candidate-probcut", candidate_mode,
        "--baseline-probcut", baseline_mode,
        "--probcut-profile", str(args.probcut_profile),
        "--probcut-minimum-margin", str(args.minimum_margin),
        "--probcut-maximum-margin", str(args.maximum_margin),
        "--probcut-minimum-confidence", str(args.minimum_confidence),
        "--probcut-maximum-probes", str(args.maximum_probes),
        "--probcut-maximum-shallow-overhead-ratio", str(args.maximum_shallow_overhead_ratio),
    ]
    if args.opening_limit:
        command.extend(["--opening-limit", str(args.opening_limit)])
    if args.persistent_session:
        command.append("--persistent-session")
    return command


def validate_report(
    report: dict[str, Any],
    path: Path,
    args: argparse.Namespace,
    profile: dict[str, Any],
    candidate_mode: str,
    baseline_mode: str,
    limit_name: str,
    exact_endgame_empties: int,
    seed: int,
    openings: Path,
) -> None:
    if report.get("schema_version") != 4 or report.get("arena_version") != "full-game-artifact-arena-v4":
        raise CampaignError(f"unsupported arena report schema: {path}")
    config = report.get("search_config")
    if not isinstance(config, dict):
        raise CampaignError(f"arena report lacks search_config: {path}")
    if config.get("candidate_probcut_mode") != candidate_mode or config.get("baseline_probcut_mode") != baseline_mode:
        raise CampaignError(f"arena report ProbCut binding mismatch: {path}")
    expected_limit_mode = (
        "fixed_depth"
        if limit_name.startswith("fixed-depth-")
        else "fixed_nodes"
        if limit_name.startswith("fixed-nodes-")
        else "fixed_wall_time"
    )
    expected_depth = args.fixed_depth if expected_limit_mode == "fixed_depth" else 0
    expected_nodes = args.fixed_nodes if expected_limit_mode == "fixed_nodes" else 0
    expected_time_ms = (
        int(limit_name.removeprefix("fixed-time-").removesuffix("ms"))
        if expected_limit_mode == "fixed_wall_time"
        else 0
    )
    if (
        config.get("preset") != args.search_preset
        or config.get("limit_mode") != expected_limit_mode
        or config.get("depth") != expected_depth
        or config.get("nodes") != expected_nodes
        or config.get("time_ms") != expected_time_ms
        or config.get("exact_endgame_empties") != exact_endgame_empties
        or config.get("persistent_session") is not args.persistent_session
        or config.get("tt_requested_bytes") != args.tt_bytes
        or report.get("seed") != seed
    ):
        raise CampaignError(f"arena report search configuration mismatch: {path}")
    expected_pairs = profile["validated_pair_order"]
    for role, mode in (("candidate", candidate_mode), ("baseline", baseline_mode)):
        options = config.get(f"{role}_resolved_options")
        if not isinstance(options, dict):
            raise CampaignError(f"arena report lacks {role} options: {path}")
        multi = options.get("multi_probcut")
        if not isinstance(multi, dict):
            raise CampaignError(f"arena report lacks {role} MPC options: {path}")
        if mode == "off":
            if multi.get("enabled") is not False:
                raise CampaignError(f"arena report unexpectedly enables {role} MPC: {path}")
            continue
        selected_pairs = expected_pairs[:1] if mode == "single" else expected_pairs
        expected_probes = 1 if mode == "single" else args.maximum_probes
        if (
            multi.get("enabled") is not True
            or multi.get("profile_id") != profile["profile_id"]
            or multi.get("source_checksum_sha256") != profile["source_checksum_sha256"]
            or multi.get("joint_holdout_checksum_sha256")
            != profile["joint_holdout_checksum_sha256"]
            or multi.get("validated_maximum_probes_per_node")
            != profile["validated_maximum_probes_per_node"]
            or multi.get("joint_false_cut_count") != profile["joint_false_cut_count"]
            or multi.get("joint_cut_candidate_count") != profile["joint_cut_candidate_count"]
            or multi.get("joint_false_cut_rate_upper_bound")
            != profile["joint_false_cut_rate_upper_bound"]
            or multi.get("scheduler_domain_evidence_count")
            != profile["scheduler_domain_evidence_count"]
            or multi.get("evaluator_family") != profile["evaluator_family"]
            or multi.get("artifact_family") != profile["artifact_family"]
            or multi.get("ordered_depth_pairs") != selected_pairs
            or multi.get("maximum_probes_per_node") != expected_probes
            or multi.get("stop_after_first_success") is not True
            or multi.get("minimum_confidence") != args.minimum_confidence
            or multi.get("minimum_margin") != args.minimum_margin
            or multi.get("maximum_margin") != args.maximum_margin
            or multi.get("maximum_shallow_overhead_ratio")
            != args.maximum_shallow_overhead_ratio
            or multi.get("near_exact_disable_empties") != exact_endgame_empties
            or multi.get("shadow_verify") is not (mode == "shadow")
        ):
            raise CampaignError(f"arena report {role} MPC identity mismatch: {path}")
    if report.get("failed_games") != 0 or report.get("illegal_games") != 0:
        raise CampaignError(f"arena report contains failed or illegal games: {path}")
    paired = report.get("results", {}).get("paired_score", {})
    gate = report.get("strength_gate", {})
    paired_sanity = report.get("paired_sanity", {})
    if (
        paired.get("bootstrap_seed") != args.bootstrap_seed
        or paired.get("bootstrap_samples") != args.bootstrap_samples
        or paired.get("opening_pair_count") != report.get("opening_count")
        or paired.get("game_count") != report.get("games")
        or paired_sanity.get("incomplete_pairs") != 0
        or gate.get("eligible") is not True
        or gate.get("minimum_opening_pairs") != args.minimum_opening_pairs
        or gate.get("reasons") != []
    ):
        raise CampaignError(f"arena report strength-gate/pair completeness mismatch: {path}")
    expected_opening_limit = args.opening_limit or None
    if (
        report.get("opening_source_checksum") != fnv1a64_file(openings)
        or report.get("opening_limit") != expected_opening_limit
        or report.get("opening_count", 0) <= 0
        or report.get("opening_count", 0) > report.get("input_opening_count", -1)
        or (args.opening_limit and report.get("opening_count") != min(args.opening_limit, report.get("input_opening_count")))
    ):
        raise CampaignError(f"arena report opening binding mismatch: {path}")
    candidate = report.get("candidate")
    baseline = report.get("baseline")
    inputs = report.get("inputs")
    if not isinstance(candidate, dict) or not isinstance(baseline, dict) or not isinstance(inputs, dict):
        raise CampaignError(f"arena report artifact identity is missing: {path}")
    manifest_checksum = fnv1a64_file(args.artifact_manifest)
    weights_checksum = fnv1a64_file(args.artifact_weights)
    if (
        candidate.get("artifact_id") != profile["artifact_family"]
        or baseline.get("artifact_id") != profile["artifact_family"]
        or candidate.get("pattern_set_id") != profile["evaluator_family"]
        or baseline.get("pattern_set_id") != profile["evaluator_family"]
        or candidate.get("runtime_identity_checksum") != baseline.get("runtime_identity_checksum")
        or candidate.get("manifest_content_checksum") != manifest_checksum
        or baseline.get("manifest_content_checksum") != manifest_checksum
        or candidate.get("weights_file_checksum") != weights_checksum
        or baseline.get("weights_file_checksum") != weights_checksum
        or inputs.get("candidate_manifest_checksum") != manifest_checksum
        or inputs.get("baseline_manifest_checksum") != manifest_checksum
        or inputs.get("candidate_weights_checksum") != weights_checksum
        or inputs.get("baseline_weights_checksum") != weights_checksum
    ):
        raise CampaignError(f"arena report artifact runtime binding mismatch: {path}")


def stage_resume_identity(
    args: argparse.Namespace, openings: Path, command: list[str]
) -> dict[str, Any]:
    return {
        "runner_version": RUNNER_VERSION,
        "command": command,
        "inputs": {
            "arena_executable_sha256": sha256_file(args.arena_executable),
            "artifact_manifest_sha256": sha256_file(args.artifact_manifest),
            "artifact_weights_sha256": sha256_file(args.artifact_weights),
            "probcut_profile_sha256": sha256_file(args.probcut_profile),
            "openings_sha256": sha256_file(openings),
        },
    }


def stage_summary(report: dict[str, Any], comparison: str) -> dict[str, Any]:
    paired = report["results"]["paired_score"]
    candidate = report["telemetry"]["candidate"]["overall"]
    baseline = report["telemetry"]["baseline"]["overall"]
    reverse = comparison.endswith("_reverse")
    point = float(paired["point_estimate"])
    low = float(paired["lower_95"])
    high = float(paired["upper_95"])
    if reverse:
        point, low, high = 1.0 - point, 1.0 - high, 1.0 - low
        tested, control = baseline, candidate
    else:
        tested, control = candidate, baseline
    tested_depth = tested["completed_depth_percentiles"]["p50"]
    control_depth = control["completed_depth_percentiles"]["p50"]
    return {
        "tested_policy_score_rate": point,
        "tested_policy_ci95": {"lower": low, "upper": high},
        "opening_pairs": paired["opening_pair_count"],
        "games": paired["game_count"],
        "tested_completed_depth_p50": tested_depth,
        "control_completed_depth_p50": control_depth,
        "completed_depth_p50_delta": tested_depth - control_depth,
        "tested_nodes_per_sec": tested["nodes_per_sec"],
        "control_nodes_per_sec": control["nodes_per_sec"],
        "tested_probcut": tested["probcut"],
        "control_probcut": control["probcut"],
    }


def wilson_upper_bound(false_cuts: int, candidates: int) -> float:
    if candidates == 0:
        return 1.0
    z = 1.959963984540054
    rate = false_cuts / candidates
    denominator = 1.0 + z * z / candidates
    centre = rate + z * z / (2.0 * candidates)
    radius = z * ((rate * (1.0 - rate) + z * z / (4.0 * candidates)) / candidates) ** 0.5
    return min(1.0, (centre + radius) / denominator)


def pair_telemetry_total(stages: list[dict[str, Any]], pairs: list[dict[str, int]], field: str) -> int:
    wanted = {(pair["deep_depth"], pair["shallow_depth"]) for pair in pairs}
    return sum(
        int(row.get(field, 0))
        for stage in stages
        for row in stage["summary"]["tested_probcut"].get("by_phase_depth_pair", [])
        if (row.get("deep_depth"), row.get("shallow_depth")) in wanted
    )


def run(args: argparse.Namespace) -> dict[str, Any]:
    for path, label in (
        (args.arena_executable, "arena executable"),
        (args.artifact_manifest, "artifact manifest"),
        (args.artifact_weights, "artifact weights"),
        (args.probcut_profile, "ProbCut profile"),
        *((path, "opening corpus") for path in args.openings),
    ):
        if not path.is_file():
            raise CampaignError(f"missing {label}: {path}")
    profile = profile_identity(args.probcut_profile)
    if args.maximum_probes > profile["validated_maximum_probes_per_node"]:
        raise CampaignError("requested maximum probes exceeds reviewed scheduler evidence")
    artifact_manifest = load_json(args.artifact_manifest)
    if profile["evaluator_family"] != artifact_manifest.get("pattern_set_id"):
        raise CampaignError(
            "profile evaluator_family must exactly match artifact pattern_set_id"
        )
    artifact_family = artifact_manifest.get("artifact_id") or args.artifact_manifest.stem
    if profile["artifact_family"] != artifact_family:
        raise CampaignError("profile artifact_family must exactly match artifact artifact_id")
    stages: list[dict[str, Any]] = []
    for corpus_index, openings in enumerate(args.openings):
        for seed in args.seeds:
            for limit_name, limit_args, exact_endgame_empties in stage_limits(args):
                for comparison, (candidate_mode, baseline_mode) in COMPARISONS.items():
                    directory = args.output_dir / f"openings-{corpus_index}" / f"seed-{seed}" / limit_name
                    report_path = directory / f"{comparison}.json"
                    resume_path = directory / f"{comparison}.resume.json"
                    command = command_for(
                        args,
                        openings,
                        seed,
                        limit_args,
                        candidate_mode,
                        baseline_mode,
                        exact_endgame_empties,
                        report_path,
                    )
                    if args.dry_run:
                        stages.append({"comparison": comparison, "command": command, "status": "dry-run"})
                        continue
                    if report_path.exists() and not args.resume:
                        raise CampaignError(f"stale report exists (use --resume): {report_path}")
                    resume_identity = stage_resume_identity(args, openings, command)
                    if report_path.exists():
                        if not resume_path.is_file():
                            raise CampaignError(f"resume sidecar is missing: {resume_path}")
                        resume_data = load_json(resume_path)
                        if resume_data.get("identity") != resume_identity:
                            raise CampaignError(f"resume identity mismatch: {resume_path}")
                        if resume_data.get("report_sha256") != sha256_file(report_path):
                            raise CampaignError(f"resumed report checksum mismatch: {report_path}")
                        report = load_json(report_path)
                        status = "resumed"
                    else:
                        directory.mkdir(parents=True, exist_ok=True)
                        completed = subprocess.run(command, check=False, capture_output=True, text=True)
                        if completed.returncode != 0:
                            raise CampaignError(
                                f"arena failed ({completed.returncode})\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
                            )
                        report = load_json(report_path)
                        status = "completed"
                    validate_report(
                        report,
                        report_path,
                        args,
                        profile,
                        candidate_mode,
                        baseline_mode,
                        limit_name,
                        exact_endgame_empties,
                        seed,
                        openings,
                    )
                    if status == "completed":
                        resume_path.write_text(
                            json.dumps(
                                {
                                    "identity": resume_identity,
                                    "report_sha256": sha256_file(report_path),
                                },
                                indent=2,
                                sort_keys=True,
                            )
                            + "\n",
                            encoding="utf-8",
                        )
                    stages.append(
                        {
                            "opening_corpus_index": corpus_index,
                            "opening_corpus_sha256": sha256_file(openings),
                            "seed": seed,
                            "limit": limit_name,
                            "comparison": comparison,
                            "candidate_probcut": candidate_mode,
                            "baseline_probcut": baseline_mode,
                            "status": status,
                            "report": str(report_path),
                            "summary": stage_summary(report, comparison),
                            "same_config_sanity": report["same_artifact_sanity"],
                        }
                    )
    completed_stages = [stage for stage in stages if "summary" in stage]
    strict_sanity_stages = [
        stage
        for stage in completed_stages
        if stage["comparison"] == "same_config_off"
        and not stage["limit"].startswith("fixed-time-")
    ]
    sanity_passed = bool(strict_sanity_stages) and all(
        stage["same_config_sanity"].get("neutral") is True
        for stage in strict_sanity_stages
    )
    fixed_time_sanity = [
        {
            "opening_corpus_index": stage["opening_corpus_index"],
            "seed": stage["seed"],
            "limit": stage["limit"],
            "score_rate": stage["summary"]["tested_policy_score_rate"],
            "ci95": stage["summary"]["tested_policy_ci95"],
            "neutral": stage["same_config_sanity"].get("neutral"),
            "statistically_consistent_with_half": stage["summary"]["tested_policy_ci95"]["lower"]
            <= 0.5
            <= stage["summary"]["tested_policy_ci95"]["upper"],
        }
        for stage in completed_stages
        if stage["comparison"] == "same_config_off"
        and stage["limit"].startswith("fixed-time-")
    ]
    primary_multi_off = [
        stage
        for stage in completed_stages
        if stage["comparison"] in ("multi_off_forward", "multi_off_reverse")
        and stage["limit"] == "fixed-time-500ms"
    ]
    direction_consistent = bool(primary_multi_off) and all(
        stage["summary"]["tested_policy_score_rate"] > 0.5 for stage in primary_multi_off
    )
    ci_gate = bool(primary_multi_off) and all(
        stage["summary"]["tested_policy_ci95"]["lower"] > 0.5 for stage in primary_multi_off
    )
    depth_non_degraded = bool(primary_multi_off) and all(
        stage["summary"]["completed_depth_p50_delta"] >= 0 for stage in primary_multi_off
    )
    effective_efficiency_improved = bool(primary_multi_off) and all(
        stage["summary"]["completed_depth_p50_delta"] > 0
        or stage["summary"]["tested_nodes_per_sec"] > stage["summary"]["control_nodes_per_sec"]
        for stage in primary_multi_off
    )
    primary_pair_count_sufficient = bool(primary_multi_off) and all(
        stage["summary"]["opening_pairs"] >= 100 for stage in primary_multi_off
    )
    primary_multi_single = [
        stage
        for stage in completed_stages
        if stage["comparison"] in ("multi_forward", "multi_reverse")
        and stage["limit"] == "fixed-time-500ms"
    ]
    multi_single_non_degraded = bool(primary_multi_single) and all(
        stage["summary"]["tested_policy_ci95"]["lower"] >= 0.5 - args.multi_single_noninferiority_margin
        for stage in primary_multi_single
    )
    multi_single_point_superior = bool(primary_multi_single) and all(
        stage["summary"]["tested_policy_score_rate"] > 0.5 for stage in primary_multi_single
    )
    secondary_100ms = [
        stage
        for stage in completed_stages
        if stage["comparison"] in ("multi_off_forward", "multi_off_reverse")
        and stage["limit"] == "fixed-time-100ms"
    ]
    secondary_direction = bool(secondary_100ms) and all(
        stage["summary"]["tested_policy_score_rate"] >= args.minimum_100ms_score_rate
        for stage in secondary_100ms
    )
    active_multi = [
        stage
        for stage in completed_stages
        if stage["comparison"]
        in ("multi_off_forward", "multi_off_reverse", "multi_forward", "multi_reverse")
    ]
    later_pairs = profile["validated_pair_order"][1:]
    later_pair_attempts = pair_telemetry_total(active_multi, later_pairs, "attempts")
    later_pair_successes = pair_telemetry_total(active_multi, later_pairs, "successes")
    later_pair_exercised = (
        bool(later_pairs)
        and later_pair_attempts >= args.minimum_later_pair_attempts
        and later_pair_successes >= args.minimum_later_pair_successes
    )
    shadow_stages = [
        stage
        for stage in completed_stages
        if stage["comparison"] in ("shadow_multi_forward", "shadow_multi_reverse")
    ]
    shadow_candidates = pair_telemetry_total(shadow_stages, profile["validated_pair_order"], "shadow_candidates")
    shadow_verifications = pair_telemetry_total(shadow_stages, profile["validated_pair_order"], "shadow_verifications")
    shadow_false_cuts = pair_telemetry_total(shadow_stages, profile["validated_pair_order"], "shadow_false_cuts")
    shadow_upper = wilson_upper_bound(shadow_false_cuts, shadow_verifications)
    shadow_audit_passed = (
        shadow_candidates >= args.minimum_shadow_candidates
        and shadow_verifications == shadow_candidates
        and shadow_upper <= profile["joint_false_cut_rate_upper_bound"]
    )
    return {
        "schema_version": 1,
        "runner_version": RUNNER_VERSION,
        "profile": profile,
        "config": {
            "exact_endgame_empties": args.exact_endgame_empties,
            "fixed_depth_exact_endgame_empties": 0,
            "persistent_session": args.persistent_session,
            "tt_bytes": args.tt_bytes,
            "fixed_depth": args.fixed_depth,
            "fixed_nodes": args.fixed_nodes,
            "time_ms": args.time_ms,
            "seeds": args.seeds,
            "opening_corpora": len(args.openings),
        },
        "stages": stages,
        "automatic_checks": {
            "failed_games_zero": bool(completed_stages),
            "illegal_games_zero": bool(completed_stages),
            "same_config_sanity_passed": sanity_passed,
            "primary_500ms_direction_consistent": direction_consistent,
            "primary_500ms_ci_lower_above_half": ci_gate,
            "primary_500ms_completed_depth_non_degraded": depth_non_degraded,
            "primary_500ms_effective_efficiency_improved": effective_efficiency_improved,
            "primary_500ms_minimum_100_opening_pairs": primary_pair_count_sufficient,
            "multi_vs_single_non_degraded": multi_single_non_degraded,
            "multi_vs_single_point_superior": multi_single_point_superior,
            "secondary_100ms_not_reversed": secondary_direction,
            "later_pair_exercised": later_pair_exercised,
            "scheduler_shadow_audit_passed": shadow_audit_passed,
        },
        "fixed_time_same_config_diagnostics": fixed_time_sanity,
        "scheduler_runtime_evidence": {
            "later_pair_attempts": later_pair_attempts,
            "later_pair_successes": later_pair_successes,
            "shadow_candidates": shadow_candidates,
            "shadow_verifications": shadow_verifications,
            "shadow_false_cuts": shadow_false_cuts,
            "shadow_false_cut_rate_upper_bound": shadow_upper,
        },
        "manual_review_required": [
            "fixed-depth differential correctness holdout has no major error",
            "exact holdout is non-degraded",
        ],
        "production_enablement_authorized": False,
        "production_enablement_note": "A generated local report is evidence input, not preset authorization.",
    }


def main() -> int:
    root = Path(__file__).parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arena-executable", type=Path, default=root / "build/tools/arena/vibe-othello-full-game-artifact-arena")
    parser.add_argument("--artifact-manifest", type=Path, required=True)
    parser.add_argument("--artifact-weights", type=Path, required=True)
    parser.add_argument("--openings", type=Path, action="append", required=True)
    parser.add_argument("--probcut-profile", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--fixed-depth", type=int, default=8)
    parser.add_argument("--fixed-nodes", type=int, default=100_000)
    parser.add_argument("--time-ms", type=lambda value: parse_int_list(value, "--time-ms", positive=True), default=[50, 100, 500])
    parser.add_argument("--seeds", type=lambda value: parse_int_list(value, "--seeds"), default=[0, 1])
    parser.add_argument("--exact-endgame-empties", type=int, default=8)
    parser.add_argument("--opening-limit", type=int, default=0)
    parser.add_argument("--minimum-opening-pairs", type=int, default=100)
    parser.add_argument("--search-preset", choices=("basic", "full"), default="full")
    parser.add_argument("--tt-bytes", type=int, default=16 * 1024 * 1024)
    parser.add_argument("--minimum-margin", type=int, default=0)
    parser.add_argument("--maximum-margin", type=int, required=True)
    parser.add_argument("--minimum-confidence", type=float, default=0.0)
    parser.add_argument("--maximum-probes", type=int, default=2)
    parser.add_argument("--maximum-shallow-overhead-ratio", type=float, default=0.0)
    parser.add_argument("--multi-single-noninferiority-margin", type=float, default=0.02)
    parser.add_argument("--minimum-100ms-score-rate", type=float, default=0.48)
    parser.add_argument("--minimum-later-pair-attempts", type=int, default=30)
    parser.add_argument("--minimum-later-pair-successes", type=int, default=1)
    parser.add_argument("--minimum-shadow-candidates", type=int, default=30)
    parser.add_argument("--bootstrap-seed", type=int, default=0)
    parser.add_argument("--bootstrap-samples", type=int, default=10_000)
    parser.add_argument("--persistent-session", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if (
        args.fixed_depth <= 0
        or args.fixed_nodes <= 0
        or args.exact_endgame_empties < 0
        or args.exact_endgame_empties > 60
        or args.opening_limit < 0
        or args.minimum_opening_pairs <= 0
        or args.tt_bytes < 0
        or args.maximum_margin <= 0
        or args.minimum_margin < 0
        or args.maximum_margin < args.minimum_margin
        or args.minimum_confidence < 0.0
        or args.maximum_probes <= 0
        or args.maximum_shallow_overhead_ratio < 0.0
        or not 0.0 <= args.multi_single_noninferiority_margin < 0.5
        or not 0.0 <= args.minimum_100ms_score_rate <= 1.0
        or args.minimum_later_pair_attempts <= 0
        or args.minimum_later_pair_successes <= 0
        or args.minimum_shadow_candidates <= 0
        or args.bootstrap_samples <= 0
        or 100 not in args.time_ms
        or 500 not in args.time_ms
    ):
        parser.error("campaign numeric settings are invalid")
    try:
        report = run(args)
        if not args.dry_run:
            args.output_dir.mkdir(parents=True, exist_ok=True)
            (args.output_dir / "campaign-report.json").write_text(
                json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
    except CampaignError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
