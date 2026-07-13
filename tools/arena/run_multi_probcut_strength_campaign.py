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


RUNNER_VERSION = "multi-probcut-strength-campaign-v1"
COMPARISONS = {
    "same_config_off": ("off", "off"),
    "single_forward": ("single", "off"),
    "single_reverse": ("off", "single"),
    "multi_forward": ("multi", "off"),
    "multi_reverse": ("off", "multi"),
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
    required = ("profile_id", "source_checksum_sha256", "evaluator_family", "artifact_family")
    row = dict(zip(header, values, strict=True))
    if any(not row.get(field) for field in required):
        raise CampaignError("ProbCut profile identity is incomplete")
    identity = {field: row[field] for field in required}
    checksum = identity["source_checksum_sha256"]
    if len(checksum) != 64 or any(character not in "0123456789abcdef" for character in checksum):
        raise CampaignError("ProbCut profile source checksum is not lowercase SHA-256")
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
    return {
        **identity,
        "profile_file_sha256": sha256_file(path),
        "ordered_depth_pairs": pairs,
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
    if (
        config.get("preset") != args.search_preset
        or config.get("limit_mode") != expected_limit_mode
        or config.get("exact_endgame_empties") != exact_endgame_empties
        or config.get("persistent_session") is not args.persistent_session
        or config.get("tt_requested_bytes") != args.tt_bytes
        or report.get("seed") != seed
    ):
        raise CampaignError(f"arena report search configuration mismatch: {path}")
    expected_pairs = profile["ordered_depth_pairs"]
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
            or multi.get("shadow_verify") is not False
        ):
            raise CampaignError(f"arena report {role} MPC identity mismatch: {path}")
    if report.get("failed_games") != 0 or report.get("illegal_games") != 0:
        raise CampaignError(f"arena report contains failed or illegal games: {path}")


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
    artifact_manifest = load_json(args.artifact_manifest)
    if profile["evaluator_family"] != artifact_manifest.get("pattern_set_id"):
        raise CampaignError(
            "profile evaluator_family must exactly match artifact pattern_set_id"
        )
    if profile["artifact_family"] != artifact_manifest.get("artifact_id"):
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
    sanity_passed = all(
        stage["same_config_sanity"].get("neutral") is True
        for stage in completed_stages
        if stage["comparison"] == "same_config_off"
    ) and bool(completed_stages)
    primary_multi = [
        stage
        for stage in completed_stages
        if stage["comparison"] in ("multi_forward", "multi_reverse")
        and stage["limit"] == "fixed-time-500ms"
    ]
    direction_consistent = bool(primary_multi) and all(
        stage["summary"]["tested_policy_score_rate"] > 0.5 for stage in primary_multi
    )
    ci_gate = bool(primary_multi) and all(
        stage["summary"]["tested_policy_ci95"]["lower"] > 0.5 for stage in primary_multi
    )
    depth_non_degraded = bool(primary_multi) and all(
        stage["summary"]["completed_depth_p50_delta"] >= 0 for stage in primary_multi
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
        },
        "manual_review_required": [
            "fixed-depth correctness holdout has no major error",
            "exact holdout is non-degraded",
            "false-cut audit accepts every enabled profile domain",
            "nodes/sec or effective depth improves after shallow overhead",
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
    parser.add_argument("--minimum-opening-pairs", type=int, default=1)
    parser.add_argument("--search-preset", choices=("basic", "full"), default="full")
    parser.add_argument("--tt-bytes", type=int, default=16 * 1024 * 1024)
    parser.add_argument("--minimum-margin", type=int, default=0)
    parser.add_argument("--maximum-margin", type=int, required=True)
    parser.add_argument("--minimum-confidence", type=float, default=0.0)
    parser.add_argument("--maximum-probes", type=int, default=2)
    parser.add_argument("--maximum-shallow-overhead-ratio", type=float, default=0.0)
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
        or args.bootstrap_samples <= 0
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
