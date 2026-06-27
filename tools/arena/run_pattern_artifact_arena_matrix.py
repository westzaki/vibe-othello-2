#!/usr/bin/env python3
"""Run and aggregate local-only pattern artifact arena matrices."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import shlex
import statistics
import subprocess
import sys
import zlib
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


RUNNER_VERSION = "pattern-artifact-arena-matrix-v1"
REPORT_SCHEMA_VERSION = 1
LOCAL_ONLY_WARNINGS = (
    "local-only pattern artifact arena diagnostic",
    "generated reports, logs, weights, artifacts, datasets, and corpora must not be committed",
    "not an Elo result",
    "not a self-play strength claim",
    "not a production strength claim",
    "not an artifact promotion or default-pointer claim",
)
OVERALL_LOCAL_ASSESSMENTS = (
    "local_harness_passed",
    "local_harness_needs_more_runs",
    "local_harness_failed",
)
GROUP_LOCAL_ASSESSMENTS = (
    "same_artifact_sanity_passed",
    "same_artifact_sanity_failed",
    "check_swap_sanity_result",
    "local_harness_failed",
    "local_harness_needs_more_runs",
    "local_signal_supportive_or_non_negative",
    "local_signal_non_negative_or_supportive",
)


class MatrixError(RuntimeError):
    """Raised for expected local input, resume, and arena failures."""


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def stable_json(data: Any) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def load_json(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise MatrixError(f"invalid JSON: {path}: {error}") from error
    if not isinstance(data, dict):
        raise MatrixError(f"JSON root must be an object: {path}")
    return data


def write_json(path: Path, data: Any) -> None:
    path.write_text(stable_json(data), encoding="utf-8")


def parse_int_list(text: str, name: str, *, allow_zero: bool) -> list[int]:
    values: list[int] = []
    seen: set[int] = set()
    for raw in text.split(","):
        token = raw.strip()
        if not token:
            raise argparse.ArgumentTypeError(f"{name} contains an empty item")
        try:
            value = int(token)
        except ValueError as error:
            raise argparse.ArgumentTypeError(f"{name} item is not an integer: {token}") from error
        if value < 0 or (value == 0 and not allow_zero):
            expectation = "non-negative" if allow_zero else "positive"
            raise argparse.ArgumentTypeError(f"{name} values must be {expectation}: {value}")
        if value in seen:
            raise argparse.ArgumentTypeError(f"{name} contains duplicate value: {value}")
        seen.add(value)
        values.append(value)
    if not values:
        raise argparse.ArgumentTypeError(f"{name} must not be empty")
    return values


def parse_args(argv: list[str]) -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--positions-tsv", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--comparison-name")

    parser.add_argument("--candidate-weights", type=Path)
    parser.add_argument("--candidate-manifest", type=Path)
    parser.add_argument("--candidate-name")
    parser.add_argument("--candidate-pattern-set")
    parser.add_argument("--baseline-weights", type=Path)
    parser.add_argument("--baseline-manifest", type=Path)
    parser.add_argument("--baseline-name")
    parser.add_argument("--baseline-pattern-set")

    parser.add_argument(
        "--pair-json",
        action="append",
        default=[],
        help=(
            "Additional comparison object with comparison, candidate_weights, "
            "candidate_manifest, candidate_name, baseline_weights, "
            "baseline_manifest, and baseline_name fields."
        ),
    )
    parser.add_argument(
        "--depths",
        required=True,
        type=lambda text: parse_int_list(text, "--depths", allow_zero=False),
    )
    parser.add_argument(
        "--seeds",
        required=True,
        type=lambda text: parse_int_list(text, "--seeds", allow_zero=True),
    )
    parser.add_argument(
        "--max-positions",
        required=True,
        type=lambda text: parse_int_list(text, "--max-positions", allow_zero=False),
    )
    parser.add_argument("--max-empty", type=int, default=12)
    parser.add_argument("--side-swap", action="store_true")
    parser.add_argument("--progress-every", type=int, default=100)
    parser.add_argument("--same-artifact-sanity", choices=("none", "candidate", "baseline", "both"), default="none")
    parser.add_argument("--swap-sanity", choices=("none", "primary", "all"), default="none")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--keep-going", action="store_true")
    parser.add_argument("--created-at-utc")
    parser.add_argument(
        "--arena-exe",
        type=Path,
        default=root / "build/tools/arena/vibe-othello-pattern-artifact-arena",
    )
    args = parser.parse_args(argv)

    if args.max_empty < 0 or args.max_empty > 64:
        parser.error("--max-empty must be in [0, 64]")
    if args.progress_every < 0:
        parser.error("--progress-every must be non-negative")

    top_level = [
        args.candidate_weights,
        args.candidate_manifest,
        args.candidate_name,
        args.baseline_weights,
        args.baseline_manifest,
        args.baseline_name,
    ]
    if any(value is not None for value in top_level) and not all(value is not None for value in top_level):
        parser.error(
            "top-level comparison requires --candidate-weights, --candidate-manifest, "
            "--candidate-name, --baseline-weights, --baseline-manifest, and --baseline-name"
        )
    if all(value is None for value in top_level) and not args.pair_json:
        parser.error("at least one top-level comparison or --pair-json is required")
    return args


def sanitize_path(path: Path | None, role: str) -> str | None:
    if path is None:
        return None
    if path.is_absolute():
        return f"<{role}>/{path.name}"
    return str(path)


def sanitize_command_arg(arg: str) -> str:
    try:
        parsed = json.loads(arg)
    except json.JSONDecodeError:
        parsed = None
    if isinstance(parsed, (dict, list)):
        return json.dumps(sanitize_json_value(parsed), sort_keys=True)
    path = Path(arg)
    if path.is_absolute():
        return f"<local-path>/{path.name}"
    return arg


def sanitize_json_value(value: Any) -> Any:
    if isinstance(value, dict):
        return {key: sanitize_json_value(item) for key, item in value.items()}
    if isinstance(value, list):
        return [sanitize_json_value(item) for item in value]
    if isinstance(value, str):
        path = Path(value)
        if path.is_absolute():
            return f"<local-path>/{path.name}"
    return value


def command_for_report(command: list[str]) -> list[str]:
    return [sanitize_command_arg(part) for part in command]


def shell_join(command: list[str]) -> str:
    return " ".join(shlex.quote(part) for part in command)


def sha256_file(path: Path, cache: dict[Path, str]) -> str:
    resolved = path.resolve()
    cached = cache.get(resolved)
    if cached is not None:
        return cached
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            digest.update(chunk)
    value = f"sha256:{digest.hexdigest()}"
    cache[resolved] = value
    return value


def artifact_crc32(path: Path) -> str:
    data = path.read_bytes()
    if len(data) < 4:
        raise MatrixError(f"artifact is too small for checksum validation: {path}")
    return f"0x{zlib.crc32(data[:-4]) & 0xFFFFFFFF:08x}"


def manifest_json(path: Path) -> dict[str, Any]:
    data = load_json(path)
    pattern_set_id = data.get("pattern_set_id")
    weights_checksum = data.get("weights_checksum")
    if not isinstance(pattern_set_id, str) or not pattern_set_id:
        raise MatrixError(f"manifest missing string pattern_set_id: {path}")
    if not isinstance(weights_checksum, str) or not weights_checksum:
        raise MatrixError(f"manifest missing string weights_checksum: {path}")
    return data


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise MatrixError(f"missing {label}: {path}")


def artifact_spec(
    *,
    weights: Path,
    manifest: Path,
    name: str,
    pattern_set: str | None,
    role: str,
) -> dict[str, Any]:
    return {
        "weights": weights,
        "manifest": manifest,
        "name": name,
        "pattern_set": pattern_set,
        "role": role,
    }


def artifact_from_mapping(data: dict[str, Any], prefix: str) -> dict[str, Any]:
    required = [f"{prefix}_weights", f"{prefix}_manifest", f"{prefix}_name"]
    missing = [key for key in required if not isinstance(data.get(key), str) or not data.get(key)]
    if missing:
        raise MatrixError(f"--pair-json missing fields: {', '.join(missing)}")
    pattern_set = data.get(f"{prefix}_pattern_set")
    if pattern_set is not None and not isinstance(pattern_set, str):
        raise MatrixError(f"--pair-json field {prefix}_pattern_set must be a string")
    return artifact_spec(
        weights=Path(str(data[f"{prefix}_weights"])),
        manifest=Path(str(data[f"{prefix}_manifest"])),
        name=str(data[f"{prefix}_name"]),
        pattern_set=pattern_set,
        role=prefix,
    )


def comparison_from_mapping(data: dict[str, Any]) -> dict[str, Any]:
    name = data.get("comparison") or data.get("comparison_name")
    if not isinstance(name, str) or not name:
        raise MatrixError("--pair-json missing string comparison")
    return {
        "comparison": name,
        "candidate": artifact_from_mapping(data, "candidate"),
        "baseline": artifact_from_mapping(data, "baseline"),
        "kind": "comparison",
        "swap_of": None,
    }


def load_pair_json(raw: str) -> dict[str, Any]:
    try:
        data = json.loads(raw)
    except json.JSONDecodeError as error:
        raise MatrixError(f"invalid --pair-json: {error}") from error
    if not isinstance(data, dict):
        raise MatrixError("--pair-json root must be an object")
    return comparison_from_mapping(data)


def top_level_comparison(args: argparse.Namespace) -> dict[str, Any] | None:
    if args.candidate_weights is None:
        return None
    name = args.comparison_name or f"{args.candidate_name}_vs_{args.baseline_name}"
    return {
        "comparison": name,
        "candidate": artifact_spec(
            weights=args.candidate_weights,
            manifest=args.candidate_manifest,
            name=args.candidate_name,
            pattern_set=args.candidate_pattern_set,
            role="candidate",
        ),
        "baseline": artifact_spec(
            weights=args.baseline_weights,
            manifest=args.baseline_manifest,
            name=args.baseline_name,
            pattern_set=args.baseline_pattern_set,
            role="baseline",
        ),
        "kind": "comparison",
        "swap_of": None,
    }


def base_comparisons(args: argparse.Namespace) -> list[dict[str, Any]]:
    comparisons: list[dict[str, Any]] = []
    top_level = top_level_comparison(args)
    if top_level is not None:
        comparisons.append(top_level)
    comparisons.extend(load_pair_json(raw) for raw in args.pair_json)
    seen: set[str] = set()
    for comparison in comparisons:
        name = comparison["comparison"]
        if name in seen:
            raise MatrixError(f"duplicate comparison name: {name}")
        seen.add(name)
    return comparisons


def validate_artifact(artifact: dict[str, Any], cache: dict[Path, str]) -> dict[str, Any]:
    weights = artifact["weights"]
    manifest = artifact["manifest"]
    require_file(weights, f"{artifact['role']} weights")
    require_file(manifest, f"{artifact['role']} manifest")
    manifest_data = manifest_json(manifest)
    manifest_pattern_set = str(manifest_data["pattern_set_id"])
    pattern_set = artifact.get("pattern_set") or manifest_pattern_set
    if pattern_set != manifest_pattern_set:
        raise MatrixError(
            f"{artifact['role']} manifest pattern_set_id mismatch: "
            f"expected {pattern_set}, got {manifest_pattern_set}: {manifest}"
        )
    actual_crc = artifact_crc32(weights)
    expected_crc = str(manifest_data["weights_checksum"])
    if actual_crc.lower() != expected_crc.lower():
        raise MatrixError(
            f"{artifact['role']} artifact checksum mismatch: manifest {expected_crc}, "
            f"actual {actual_crc}: {weights}"
        )
    artifact["pattern_set"] = pattern_set
    return {
        "name": artifact["name"],
        "pattern_set": pattern_set,
        "weights": sanitize_path(weights, f"{artifact['role']}-weights"),
        "manifest": sanitize_path(manifest, f"{artifact['role']}-manifest"),
        "weights_size_bytes": weights.stat().st_size,
        "manifest_size_bytes": manifest.stat().st_size,
        "weights_sha256": sha256_file(weights, cache),
        "manifest_sha256": sha256_file(manifest, cache),
        "weights_checksum": actual_crc,
        "manifest_weights_checksum": expected_crc,
        "manifest_pattern_set_id": manifest_pattern_set,
    }


def positions_metadata(path: Path, cache: dict[Path, str]) -> dict[str, Any]:
    require_file(path, "positions TSV")
    return {
        "path": sanitize_path(path, "positions-tsv"),
        "size_bytes": path.stat().st_size,
        "sha256": sha256_file(path, cache),
    }


def comparison_inventory(comparison: dict[str, Any], cache: dict[Path, str]) -> dict[str, Any]:
    return {
        "comparison": comparison["comparison"],
        "kind": comparison["kind"],
        "swap_of": comparison.get("swap_of"),
        "candidate": validate_artifact(comparison["candidate"], cache),
        "baseline": validate_artifact(comparison["baseline"], cache),
    }


def sanity_comparisons(comparisons: list[dict[str, Any]], args: argparse.Namespace) -> list[dict[str, Any]]:
    expanded = list(comparisons)
    primary = comparisons[0]
    same_targets: list[tuple[str, dict[str, Any]]] = []
    if args.same_artifact_sanity in ("candidate", "both"):
        same_targets.append(("candidate", primary["candidate"]))
    if args.same_artifact_sanity in ("baseline", "both"):
        same_targets.append(("baseline", primary["baseline"]))
    for label, artifact in same_targets:
        expanded.append(
            {
                "comparison": f"same_artifact_sanity__{label}__{artifact['name']}",
                "candidate": dict(artifact, role="candidate"),
                "baseline": dict(artifact, role="baseline"),
                "kind": "same_artifact_sanity",
                "swap_of": None,
            }
        )

    if args.swap_sanity != "none":
        swap_targets = comparisons[:1] if args.swap_sanity == "primary" else comparisons
        for comparison in swap_targets:
            expanded.append(
                {
                    "comparison": f"swap_sanity__{comparison['comparison']}",
                    "candidate": dict(comparison["baseline"], role="candidate"),
                    "baseline": dict(comparison["candidate"], role="baseline"),
                    "kind": "swap_sanity",
                    "swap_of": comparison["comparison"],
                }
            )
    seen: set[str] = set()
    for comparison in expanded:
        name = comparison["comparison"]
        if name in seen:
            raise MatrixError(f"duplicate expanded comparison name: {name}")
        seen.add(name)
    return expanded


def slug(text: str) -> str:
    output = []
    for char in text.lower():
        if char.isalnum():
            output.append(char)
        elif output and output[-1] != "-":
            output.append("-")
    value = "".join(output).strip("-")
    return value[:80] or "run"


def run_key(run: dict[str, Any]) -> str:
    identity = {
        "comparison": run["comparison"]["comparison"],
        "candidate": run["comparison"]["candidate"]["name"],
        "baseline": run["comparison"]["baseline"]["name"],
        "depth": run["depth"],
        "seed": run["seed"],
        "max_positions": run["max_positions"],
    }
    digest = hashlib.sha1(stable_json(identity).encode("utf-8")).hexdigest()[:8]
    return (
        f"{slug(run['comparison']['comparison'])}"
        f"__depth-{run['depth']}__seed-{run['seed']}"
        f"__positions-{run['max_positions']}__{digest}"
    )


def planned_runs(comparisons: list[dict[str, Any]], args: argparse.Namespace) -> list[dict[str, Any]]:
    runs: list[dict[str, Any]] = []
    for comparison in comparisons:
        for depth in args.depths:
            for seed in args.seeds:
                for max_positions in args.max_positions:
                    runs.append(
                        {
                            "comparison": comparison,
                            "depth": depth,
                            "seed": seed,
                            "max_positions": max_positions,
                        }
                    )
    keys = [run_key(run) for run in runs]
    if len(keys) != len(set(keys)):
        raise MatrixError("arena matrix produced duplicate run keys")
    return runs


def arena_command(args: argparse.Namespace, run: dict[str, Any], report: Path, summary: Path) -> list[str]:
    comparison = run["comparison"]
    candidate = comparison["candidate"]
    baseline = comparison["baseline"]
    command = [
        str(args.arena_exe),
        "--positions-tsv",
        str(args.positions_tsv),
        "--candidate-weights",
        str(candidate["weights"]),
        "--candidate-manifest",
        str(candidate["manifest"]),
        "--candidate-name",
        str(candidate["pattern_set"]),
        "--baseline-weights",
        str(baseline["weights"]),
        "--baseline-manifest",
        str(baseline["manifest"]),
        "--baseline-name",
        str(baseline["pattern_set"]),
        "--max-empty",
        str(args.max_empty),
        "--max-positions",
        str(run["max_positions"]),
        "--seed",
        str(run["seed"]),
        "--depth",
        str(run["depth"]),
        "--report-out",
        str(report),
        "--summary-out",
        str(summary),
    ]
    if args.side_swap:
        command.append("--side-swap")
    if args.progress_every:
        command.extend(["--progress-every", str(args.progress_every)])
    return command


def input_fingerprint(path: Path, role: str, cache: dict[Path, str]) -> dict[str, Any]:
    return {
        "role": role,
        "path": sanitize_path(path, role),
        "size_bytes": path.stat().st_size,
        "sha256": sha256_file(path, cache),
    }


def resume_expected(command: list[str], run: dict[str, Any], inputs: list[tuple[str, Path]], cache: dict[Path, str]) -> dict[str, Any]:
    comparison = run["comparison"]
    return {
        "schema_version": 1,
        "runner_version": RUNNER_VERSION,
        "comparison": comparison["comparison"],
        "comparison_kind": comparison["kind"],
        "swap_of": comparison.get("swap_of"),
        "candidate_name": comparison["candidate"]["name"],
        "baseline_name": comparison["baseline"]["name"],
        "depth": run["depth"],
        "seed": run["seed"],
        "max_positions": run["max_positions"],
        "side_swap": True if "--side-swap" in command else False,
        "command": command_for_report(command),
        "inputs": [input_fingerprint(path, role, cache) for role, path in inputs],
    }


def resume_complete(expected: dict[str, Any], outputs: list[tuple[str, Path]], cache: dict[Path, str]) -> dict[str, Any]:
    data = dict(expected)
    data["outputs"] = [input_fingerprint(path, role, cache) for role, path in outputs]
    return data


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
    if isinstance(expected, list) and isinstance(actual, list):
        if len(expected) != len(actual):
            return f"{path} length {len(actual)} != {len(expected)}"
        for index, (expected_item, actual_item) in enumerate(zip(expected, actual, strict=True)):
            mismatch = first_mismatch(expected_item, actual_item, f"{path}[{index}]")
            if mismatch is not None:
                return mismatch
    return f"{path} mismatch"


def numeric(mapping: dict[str, Any] | None, key: str) -> float | int | None:
    if not isinstance(mapping, dict):
        return None
    value = mapping.get(key)
    return value if isinstance(value, (int, float)) else None


def arena_result_from_report(path: Path, run: dict[str, Any], status: str) -> dict[str, Any]:
    report = load_json(path)
    comparison = run["comparison"]
    return {
        "status": status,
        "comparison": comparison["comparison"],
        "comparison_kind": comparison["kind"],
        "swap_of": comparison.get("swap_of"),
        "candidate_name": comparison["candidate"]["name"],
        "candidate_pattern_set": comparison["candidate"]["pattern_set"],
        "baseline_name": comparison["baseline"]["name"],
        "baseline_pattern_set": comparison["baseline"]["pattern_set"],
        "depth": run["depth"],
        "seed": run["seed"],
        "max_positions": run["max_positions"],
        "selected_positions": report.get("selected_positions"),
        "games_played": report.get("games_played"),
        "candidate_wins": report.get("candidate_wins"),
        "baseline_wins": report.get("baseline_wins"),
        "draws": report.get("draws"),
        "candidate_score_rate": report.get("candidate_score_rate"),
        "average_disc_diff_candidate_perspective": report.get("average_disc_diff_candidate_perspective"),
        "illegal_or_failed_games": report.get("illegal_or_failed_games"),
        "report": sanitize_path(path, "arena-report"),
        "interpretation": interpretation_for_report(report),
    }


def interpretation_for_report(report: dict[str, Any]) -> str:
    failed = numeric(report, "illegal_or_failed_games") or 0
    rate = numeric(report, "candidate_score_rate")
    if failed:
        return "failed games present"
    if rate is None:
        return "missing score rate"
    if float(rate) > 0.52:
        return "supportive bounded arena direction"
    if float(rate) >= 0.5:
        return "non-negative bounded arena direction"
    if float(rate) >= 0.49:
        return "close to neutral bounded arena direction"
    return "negative bounded arena direction"


def output_exists(paths: list[Path]) -> bool:
    return any(path.exists() for path in paths)


def run_one_arena(args: argparse.Namespace, run: dict[str, Any], cache: dict[Path, str]) -> dict[str, Any]:
    key = run_key(run)
    run_dir = args.output_dir / "runs" / key
    report = run_dir / "arena-report.json"
    summary = run_dir / "arena-summary.md"
    resume = run_dir / "arena.resume.json"
    command = arena_command(args, run, report, summary)
    inputs = [
        ("positions-tsv", args.positions_tsv),
        ("candidate-weights", run["comparison"]["candidate"]["weights"]),
        ("candidate-manifest", run["comparison"]["candidate"]["manifest"]),
        ("baseline-weights", run["comparison"]["baseline"]["weights"]),
        ("baseline-manifest", run["comparison"]["baseline"]["manifest"]),
    ]
    expected = resume_expected(command, run, inputs, cache)
    outputs = [("arena-report", report), ("arena-summary", summary)]
    if args.resume:
        if report.exists() and summary.exists() and resume.exists():
            actual = load_json(resume)
            current = resume_complete(expected, outputs, cache)
            mismatch = first_mismatch(current, actual)
            if mismatch is not None:
                raise MatrixError(f"arena resume metadata mismatch for {key} at {mismatch}")
            return arena_result_from_report(report, run, "skipped-resume-validated")
        if output_exists([report, summary, resume]):
            raise MatrixError(f"incomplete stale arena outputs for {key}; remove them or rerun without --resume")
    elif output_exists([report, summary, resume]):
        raise MatrixError(f"stale arena outputs exist for {key}; use --resume or remove {run_dir}")

    run_dir.mkdir(parents=True, exist_ok=True)
    print(f"running arena: {shell_join(command)}", flush=True)
    completed = subprocess.run(command, check=False)
    if completed.returncode != 0:
        return {
            "status": "failed",
            "comparison": run["comparison"]["comparison"],
            "comparison_kind": run["comparison"]["kind"],
            "swap_of": run["comparison"].get("swap_of"),
            "depth": run["depth"],
            "seed": run["seed"],
            "max_positions": run["max_positions"],
            "returncode": completed.returncode,
            "command": command_for_report(command),
            "report": sanitize_path(report, "arena-report"),
        }
    if not report.is_file() or not summary.is_file():
        raise MatrixError(f"arena did not write expected report and summary for {key}")
    write_json(resume, resume_complete(expected, outputs, cache))
    return arena_result_from_report(report, run, "ok")


def mean_or_none(values: list[float]) -> float | None:
    return statistics.fmean(values) if values else None


def min_or_none(values: list[float]) -> float | None:
    return min(values) if values else None


def max_or_none(values: list[float]) -> float | None:
    return max(values) if values else None


def completed_results(results: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [result for result in results if result.get("status") in ("ok", "skipped-resume-validated")]


def basic_stats(group: list[dict[str, Any]]) -> dict[str, Any]:
    rates = [
        float(value)
        for result in group
        if (value := numeric(result, "candidate_score_rate")) is not None
    ]
    diffs = [
        float(value)
        for result in group
        if (value := numeric(result, "average_disc_diff_candidate_perspective")) is not None
    ]
    return {
        "run_count": len(group),
        "total_games": sum(int(numeric(result, "games_played") or 0) for result in group),
        "total_failed_games": sum(int(numeric(result, "illegal_or_failed_games") or 0) for result in group),
        "non_negative_run_count": sum(
            1
            for result in group
            if (numeric(result, "illegal_or_failed_games") or 0) == 0
            and (value := numeric(result, "candidate_score_rate")) is not None
            and float(value) >= 0.5
        ),
        "mean_score_rate": mean_or_none(rates),
        "min_score_rate": min_or_none(rates),
        "max_score_rate": max_or_none(rates),
        "mean_average_disc_diff": mean_or_none(diffs),
        "min_average_disc_diff": min_or_none(diffs),
        "max_average_disc_diff": max_or_none(diffs),
    }


def breakdown(group: list[dict[str, Any]], key: str) -> dict[str, Any]:
    values = sorted({result.get(key) for result in group})
    return {
        str(value): basic_stats([result for result in group if result.get(key) == value])
        for value in values
    }


def same_artifact_passes(group: list[dict[str, Any]]) -> bool:
    if not group:
        return False
    for result in group:
        rate = numeric(result, "candidate_score_rate")
        diff = numeric(result, "average_disc_diff_candidate_perspective")
        failed = numeric(result, "illegal_or_failed_games") or 0
        if failed != 0 or rate is None or diff is None:
            return False
        if abs(float(rate) - 0.5) > 1.0e-12 or abs(float(diff)) > 1.0e-12:
            return False
    return True


def group_local_assessment(comparison: str, kind: str, stats: dict[str, Any], same_passed: bool | None) -> str:
    if kind == "same_artifact_sanity":
        return "same_artifact_sanity_passed" if same_passed else "same_artifact_sanity_failed"
    if kind == "swap_sanity":
        return "check_swap_sanity_result"
    if stats.get("total_failed_games", 0) != 0:
        return "local_harness_failed"
    mean_rate = stats.get("mean_score_rate")
    mean_diff = stats.get("mean_average_disc_diff")
    run_count = int(stats.get("run_count", 0))
    non_negative = int(stats.get("non_negative_run_count", 0))
    if mean_rate is None or mean_diff is None or run_count == 0:
        return "local_harness_needs_more_runs"
    if mean_rate >= 0.5 and mean_diff >= 0.0 and non_negative >= math.ceil(run_count / 2):
        if "v1" in comparison.lower():
            return "local_signal_supportive_or_non_negative"
        return "local_signal_non_negative_or_supportive"
    if mean_rate < 0.49 or mean_diff < -0.5:
        return "local_harness_failed"
    return "local_harness_needs_more_runs"


def summarize_by_comparison(results: list[dict[str, Any]]) -> dict[str, Any]:
    completed = completed_results(results)
    comparisons = sorted({str(result.get("comparison")) for result in completed})
    output: dict[str, Any] = {}
    for comparison in comparisons:
        group = [result for result in completed if result.get("comparison") == comparison]
        kind = str(group[0].get("comparison_kind") or "comparison")
        stats = basic_stats(group)
        same_passed = same_artifact_passes(group) if kind == "same_artifact_sanity" else None
        output[comparison] = {
            **stats,
            "comparison_kind": kind,
            "depth_breakdown": breakdown(group, "depth"),
            "seed_breakdown": breakdown(group, "seed"),
            "max_positions_breakdown": breakdown(group, "max_positions"),
            "same_artifact_sanity_result": same_passed,
            "local_assessment": group_local_assessment(comparison, kind, stats, same_passed),
        }
    return output


def swap_sanity(results: list[dict[str, Any]]) -> dict[str, Any]:
    completed = completed_results(results)
    forward = {
        (
            result.get("comparison"),
            result.get("depth"),
            result.get("seed"),
            result.get("max_positions"),
        ): result
        for result in completed
        if result.get("comparison_kind") != "swap_sanity"
    }
    checks: list[dict[str, Any]] = []
    for reverse in [result for result in completed if result.get("comparison_kind") == "swap_sanity"]:
        key = (
            reverse.get("swap_of"),
            reverse.get("depth"),
            reverse.get("seed"),
            reverse.get("max_positions"),
        )
        original = forward.get(key)
        if original is None:
            checks.append(
                {
                    "swap_of": reverse.get("swap_of"),
                    "depth": reverse.get("depth"),
                    "seed": reverse.get("seed"),
                    "max_positions": reverse.get("max_positions"),
                    "passed": False,
                    "reason": "missing forward comparison",
                }
            )
            continue
        original_rate = numeric(original, "candidate_score_rate")
        reverse_rate = numeric(reverse, "candidate_score_rate")
        original_diff = numeric(original, "average_disc_diff_candidate_perspective")
        reverse_diff = numeric(reverse, "average_disc_diff_candidate_perspective")
        original_failed = numeric(original, "illegal_or_failed_games") or 0
        reverse_failed = numeric(reverse, "illegal_or_failed_games") or 0
        passed = (
            original_failed == 0
            and reverse_failed == 0
            and original_rate is not None
            and reverse_rate is not None
            and original_diff is not None
            and reverse_diff is not None
            and abs(float(original_rate) + float(reverse_rate) - 1.0) <= 1.0e-9
            and abs(float(original_diff) + float(reverse_diff)) <= 1.0e-9
        )
        checks.append(
            {
                "swap_of": reverse.get("swap_of"),
                "depth": reverse.get("depth"),
                "seed": reverse.get("seed"),
                "max_positions": reverse.get("max_positions"),
                "forward_score_rate": original_rate,
                "reverse_score_rate": reverse_rate,
                "forward_average_disc_diff": original_diff,
                "reverse_average_disc_diff": reverse_diff,
                "passed": passed,
            }
        )
    return {
        "run_count": len(checks),
        "passed_count": sum(1 for check in checks if check.get("passed") is True),
        "passed": bool(checks) and all(check.get("passed") is True for check in checks),
        "checks": checks,
    }


def same_artifact_sanity_summary(results: list[dict[str, Any]]) -> dict[str, Any]:
    groups: dict[str, list[dict[str, Any]]] = {}
    for result in completed_results(results):
        if result.get("comparison_kind") == "same_artifact_sanity":
            groups.setdefault(str(result.get("comparison")), []).append(result)
    checks = [
        {"comparison": comparison, "run_count": len(group), "passed": same_artifact_passes(group)}
        for comparison, group in sorted(groups.items())
    ]
    return {
        "run_count": sum(check["run_count"] for check in checks),
        "passed": bool(checks) and all(check["passed"] for check in checks),
        "checks": checks,
    }


def find_group(by_comparison: dict[str, Any], *needles: str) -> dict[str, Any] | None:
    lowered_needles = [needle.lower() for needle in needles]
    for name, data in by_comparison.items():
        lowered = name.lower()
        if all(needle in lowered for needle in lowered_needles):
            return data
    return None


def group_non_negative_most(data: dict[str, Any] | None, ratio: float) -> bool:
    if not data:
        return False
    run_count = int(data.get("run_count", 0) or 0)
    if run_count == 0:
        return False
    return int(data.get("non_negative_run_count", 0) or 0) >= math.ceil(run_count * ratio)


def group_mean_non_negative(data: dict[str, Any] | None) -> bool:
    if not data:
        return False
    rate = data.get("mean_score_rate")
    diff = data.get("mean_average_disc_diff")
    return isinstance(rate, (int, float)) and isinstance(diff, (int, float)) and rate >= 0.5 and diff >= 0.0


def deepest_depth_negative(data: dict[str, Any] | None) -> bool:
    if not data:
        return False
    depths = data.get("depth_breakdown")
    if not isinstance(depths, dict) or not depths:
        return False
    max_depth = max(int(depth) for depth in depths)
    stats = depths[str(max_depth)]
    return (
        int(stats.get("run_count", 0) or 0) > 0
        and int(stats.get("non_negative_run_count", 0) or 0) == 0
        and isinstance(stats.get("mean_score_rate"), (int, float))
        and stats["mean_score_rate"] < 0.5
    )


def overall_local_assessment(by_comparison: dict[str, Any], same: dict[str, Any], swap: dict[str, Any]) -> dict[str, Any]:
    failed_games = sum(int(data.get("total_failed_games", 0) or 0) for data in by_comparison.values())
    mt_exact = find_group(by_comparison, "move", "exact-root") or find_group(by_comparison, "move", "exact")
    mt_v1 = find_group(by_comparison, "move", "v1")
    exact_v1 = find_group(by_comparison, "exact-root", "v1") or find_group(by_comparison, "exact", "v1")

    reasons: list[str] = []
    if failed_games:
        reasons.append(f"failed games present: {failed_games}")
    if same["run_count"] == 0:
        reasons.append("same-artifact sanity was not run")
    elif not same["passed"]:
        reasons.append("same-artifact sanity failed")
    if swap["run_count"] == 0:
        reasons.append("swap sanity was not run")
    elif not swap["passed"]:
        reasons.append("swap sanity failed")
    if mt_exact is None:
        reasons.append("move-teacher vs exact-root comparison was not identified")
    if mt_v1 is None:
        reasons.append("move-teacher vs v1 comparison was not identified")
    if exact_v1 is None:
        reasons.append("exact-root vs v1 comparison was not identified")

    fatal = failed_games != 0 or (same["run_count"] > 0 and not same["passed"]) or (swap["run_count"] > 0 and not swap["passed"])
    if mt_exact is not None and not group_non_negative_most(mt_exact, 0.5):
        fatal = True
        reasons.append("move-teacher vs exact-root is negative in most runs")
    if mt_v1 is not None and not group_mean_non_negative(mt_v1):
        fatal = True
        reasons.append("move-teacher vs v1 aggregate is negative")
    if deepest_depth_negative(mt_exact):
        fatal = True
        reasons.append("deepest search depth is consistently negative for move-teacher vs exact-root")
    if fatal:
        return {"category": "local_harness_failed", "reasons": reasons}

    depth7_present = False
    if mt_exact is not None:
        depths = mt_exact.get("depth_breakdown")
        depth7_present = isinstance(depths, dict) and "7" in depths

    harness_passed = (
        mt_exact is not None
        and mt_v1 is not None
        and exact_v1 is not None
        and same["passed"]
        and swap["passed"]
        and group_non_negative_most(mt_exact, 0.8)
        and group_mean_non_negative(mt_exact)
        and group_mean_non_negative(mt_v1)
        and group_mean_non_negative(exact_v1)
        and depth7_present
    )
    if harness_passed:
        reasons.append("arena matrix completed with passing sanity checks and non-negative local directions")
        return {"category": "local_harness_passed", "reasons": reasons}

    if not depth7_present:
        reasons.append("depth 7 comparison is missing for move-teacher vs exact-root")
    if mt_exact is not None and not group_non_negative_most(mt_exact, 0.8):
        reasons.append("move-teacher vs exact-root is close or noisy across runs")
    if mt_exact is not None and not group_mean_non_negative(mt_exact):
        reasons.append("move-teacher vs exact-root aggregate is not positive enough")
    return {"category": "local_harness_needs_more_runs", "reasons": reasons}


def report_command(argv: list[str]) -> list[str]:
    command = [Path(sys.executable).name, "tools/arena/run_pattern_artifact_arena_matrix.py", *argv]
    if "--resume" not in command:
        command.append("--resume")
    return command_for_report(command)


def build_report(
    args: argparse.Namespace,
    argv: list[str],
    inventory: list[dict[str, Any]],
    planned: list[dict[str, Any]],
    results: list[dict[str, Any]],
    skipped: list[dict[str, Any]],
    cache: dict[Path, str],
) -> dict[str, Any]:
    by_comparison = summarize_by_comparison(results)
    same = same_artifact_sanity_summary(results)
    swap = swap_sanity(results)
    local_assessment = overall_local_assessment(by_comparison, same, swap)
    completed = completed_results(results)
    return {
        "schema_version": REPORT_SCHEMA_VERSION,
        "runner_version": RUNNER_VERSION,
        "created_at_utc": args.created_at_utc
        or datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "status": "planned" if args.dry_run else "ok",
        "local_only_warnings": list(LOCAL_ONLY_WARNINGS),
        "allowed_overall_local_assessments": list(OVERALL_LOCAL_ASSESSMENTS),
        "allowed_group_local_assessments": list(GROUP_LOCAL_ASSESSMENTS),
        "positions_tsv": positions_metadata(args.positions_tsv, cache),
        "matrix": {
            "depths": args.depths,
            "seeds": args.seeds,
            "max_positions": args.max_positions,
            "max_empty": args.max_empty,
            "side_swap": args.side_swap,
            "same_artifact_sanity": args.same_artifact_sanity,
            "swap_sanity": args.swap_sanity,
        },
        "comparisons": inventory,
        "planned_run_count": len(planned),
        "completed_run_count": len(completed),
        "skipped_run_count": len(skipped),
        "failed_run_count": sum(1 for result in results if result.get("status") == "failed"),
        "total_games": sum(int(numeric(result, "games_played") or 0) for result in completed),
        "total_failed_games": sum(int(numeric(result, "illegal_or_failed_games") or 0) for result in completed),
        "by_comparison": by_comparison,
        "same_artifact_sanity": same,
        "swap_sanity": swap,
        "local_assessment": local_assessment,
        "results": results,
        "skipped": skipped,
        "resume_command": report_command(argv),
    }


def format_float(value: Any, places: int = 6) -> str:
    return f"{float(value):.{places}f}" if isinstance(value, (int, float)) else "n/a"


def write_summary(path: Path, report: dict[str, Any]) -> None:
    lines = [
        "# Pattern Artifact Arena Matrix",
        "",
        "Local-only bounded artifact arena evaluation harness. This is not Elo, not a self-play strength claim, not production strength, and not an artifact promotion or default-pointer claim.",
        "",
        "## Matrix",
        "",
        f"- depths: {', '.join(str(value) for value in report['matrix']['depths'])}",
        f"- seeds: {', '.join(str(value) for value in report['matrix']['seeds'])}",
        f"- max positions: {', '.join(str(value) for value in report['matrix']['max_positions'])}",
        f"- side swap: {report['matrix']['side_swap']}",
        f"- planned runs: {report['planned_run_count']}",
        f"- completed runs: {report['completed_run_count']}",
        f"- total games: {report['total_games']}",
        f"- failed games: {report['total_failed_games']}",
        "",
        "## Local Assessment",
        "",
        f"- category: `{report['local_assessment']['category']}`",
        f"- reasons: {'; '.join(report['local_assessment'].get('reasons', [])) or 'none'}",
        "",
        "## Comparison Summary",
        "",
        "| Comparison | Runs | Games | Failed | Non-negative | Mean score rate | Min score rate | Max score rate | Mean avg disc diff | Local assessment |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for comparison, data in report["by_comparison"].items():
        lines.append(
            f"| `{comparison}` | {data['run_count']} | {data['total_games']} | "
            f"{data['total_failed_games']} | {data['non_negative_run_count']} | "
            f"{format_float(data['mean_score_rate'])} | {format_float(data['min_score_rate'])} | "
            f"{format_float(data['max_score_rate'])} | {format_float(data['mean_average_disc_diff'], 4)} | "
            f"`{data['local_assessment']}` |"
        )
    lines.extend(
        [
            "",
            "## Sanity",
            "",
            f"- same-artifact sanity passed: {report['same_artifact_sanity']['passed']}",
            f"- swap sanity passed: {report['swap_sanity']['passed']}",
            "",
            "## Resume Command",
            "",
            "```sh",
            shell_join(report["resume_command"]),
            "```",
            "",
            "Generated per-run reports remain local-only and must not be committed.",
        ]
    )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main(argv: list[str]) -> int:
    try:
        args = parse_args(argv)
        cache: dict[Path, str] = {}
        comparisons = base_comparisons(args)
        expanded = sanity_comparisons(comparisons, args)
        if not args.dry_run:
            require_file(args.arena_exe, "pattern artifact arena executable")
        positions_metadata(args.positions_tsv, cache)
        inventory = [comparison_inventory(comparison, cache) for comparison in expanded]
        planned = planned_runs(expanded, args)
        results: list[dict[str, Any]] = []
        skipped: list[dict[str, Any]] = []
        args.output_dir.mkdir(parents=True, exist_ok=True)
        if args.dry_run:
            for run in planned:
                command = arena_command(args, run, args.output_dir / "dry-run-report.json", args.output_dir / "dry-run-summary.md")
                results.append(
                    {
                        "status": "planned",
                        "comparison": run["comparison"]["comparison"],
                        "comparison_kind": run["comparison"]["kind"],
                        "swap_of": run["comparison"].get("swap_of"),
                        "candidate_name": run["comparison"]["candidate"]["name"],
                        "baseline_name": run["comparison"]["baseline"]["name"],
                        "depth": run["depth"],
                        "seed": run["seed"],
                        "max_positions": run["max_positions"],
                        "command": command_for_report(command),
                    }
                )
        else:
            for run in planned:
                result = run_one_arena(args, run, cache)
                results.append(result)
                if result.get("status") == "failed" and not args.keep_going:
                    skipped.extend(
                        {
                            "status": "skipped-after-failure",
                            "comparison": pending["comparison"]["comparison"],
                            "depth": pending["depth"],
                            "seed": pending["seed"],
                            "max_positions": pending["max_positions"],
                        }
                        for pending in planned[len(results) :]
                    )
                    break
        report = build_report(args, argv, inventory, planned, results, skipped, cache)
        write_json(args.output_dir / "arena-matrix-report.json", report)
        write_summary(args.output_dir / "arena-matrix-summary.md", report)
        print(f"wrote {args.output_dir / 'arena-matrix-report.json'}", flush=True)
        print(f"local_assessment={report['local_assessment']['category']}", flush=True)
        return 0
    except MatrixError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
