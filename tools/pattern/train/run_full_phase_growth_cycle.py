#!/usr/bin/env python3
"""Run a local-only full-phase search/exact/rank/artifact validation cycle.

All generated data belongs in an ignored local measurement directory.  This
runner never promotes an artifact or resolves the repository default pointer.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import subprocess
import sys
from collections.abc import Callable
from pathlib import Path
from typing import Any


NORMALIZED_HEADER = [
    "record_id", "position_id", "game_group_id", "board_id", "source_occurrence_id", "source_dataset_id",
    "split", "board_a1_to_h8", "label_kind", "label_unit", "label_perspective", "label_score_side_to_move",
    "occupied_count", "phase", "player_disc_count", "opponent_disc_count", "empty_count",
]
MOVE_TEACHER_HEADER_V1 = [
    "root_board_id", "root_record_id", "root_split", "root_phase", "root_empty_count", "move",
    "child_board_id", "child_board_a1_to_h8", "child_empty_count", "child_phase",
    "root_move_score_side_to_move", "child_label_score_side_to_move", "is_best_move", "best_move_tie_count",
    "move_rank", "best_score_margin", "teacher_source", "teacher_depth", "teacher_nodes",
]
MOVE_TEACHER_HEADER_V2 = [
    "root_board_id", "root_record_id", "root_split", "root_phase", "root_empty_count", "move",
    "child_board_id", "child_board_a1_to_h8", "child_empty_count", "child_phase",
    "root_move_score_side_to_move", "child_label_score_side_to_move", "is_best_move", "best_move_tie_count",
    "move_rank", "best_score_margin", "teacher_kind", "teacher_source", "teacher_artifact_id",
    "teacher_artifact_checksum", "teacher_depth", "teacher_nodes", "teacher_search_config_id",
]
PHASES = tuple(range(13))
SCALE_ROOTS = {"smoke": 1, "diagnostic": 5_000, "candidate": 20_000, "large": 50_000}
SCALE_ARENA_POSITIONS = {"smoke": 1, "diagnostic": 500, "candidate": 1_000, "large": 2_000}
SCALE_OPENING_LIMIT = {"smoke": 1, "diagnostic": 32, "candidate": 128, "large": 256}
LOCAL_ONLY_WARNINGS = [
    "local-only run; do not commit generated TSVs, labels, caches, weights, manifests, reports, or logs",
    "not an artifact promotion, default-pointer update, production-strength, Elo, or self-play claim",
    "teacher and baseline artifacts are explicit inputs; the default artifact pointer is never resolved",
]


class CycleError(RuntimeError):
    """Raised for deterministic preflight, stage, or resume failures."""


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def stable_json(data: Any) -> str:
    return json.dumps(data, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CycleError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise CycleError(f"JSON root must be an object: {path}")
    return value


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(stable_json(value), encoding="utf-8")


def parse_int_list(text: str, option: str, *, minimum: int = 0) -> list[int]:
    try:
        values = [int(value) for value in text.split(",") if value]
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"{option} must be a comma-separated integer list") from error
    if not values or any(value < minimum for value in values) or len(set(values)) != len(values):
        raise argparse.ArgumentTypeError(f"{option} must contain unique integers >= {minimum}")
    return values


def parse_phase_quota(text: str) -> tuple[int, int]:
    try:
        phase_text, count_text = text.split("=", 1)
        phase = int(phase_text)
        count = int(count_text)
    except ValueError as error:
        raise argparse.ArgumentTypeError("phase quota must use PHASE=COUNT") from error
    if phase not in PHASES:
        raise argparse.ArgumentTypeError("phase quota phase must be in [0, 12]")
    if count <= 0:
        raise argparse.ArgumentTypeError("phase quota count must be positive")
    return phase, count


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        raise CycleError(f"cannot checksum {path}: {error}") from error
    return f"sha256:{digest.hexdigest()}"


def report_path(path: Path) -> str:
    return path.name if path.is_absolute() else str(path)


def fingerprint(path: Path, role: str) -> dict[str, Any]:
    if not path.is_file():
        raise CycleError(f"missing {role}: {path}")
    return {"role": role, "path": report_path(path), "size_bytes": path.stat().st_size, "sha256": sha256_file(path)}


def repo_commit_sha() -> str:
    result = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo_root(), check=False, text=True, capture_output=True)
    if result.returncode != 0:
        raise CycleError("cannot determine repository commit SHA")
    return result.stdout.strip()


def first_mismatch(expected: Any, actual: Any, path: str = "$") -> str | None:
    if type(expected) is not type(actual):
        return path
    if isinstance(expected, dict):
        if set(expected) != set(actual):
            return path
        for key in sorted(expected):
            mismatch = first_mismatch(expected[key], actual[key], f"{path}.{key}")
            if mismatch is not None:
                return mismatch
        return None
    if isinstance(expected, list):
        if len(expected) != len(actual):
            return path
        for index, (left, right) in enumerate(zip(expected, actual, strict=True)):
            mismatch = first_mismatch(left, right, f"{path}[{index}]")
            if mismatch is not None:
                return mismatch
        return None
    return None if expected == actual else path


def run_command(command: list[str], *, stdout_path: Path | None = None) -> None:
    result = subprocess.run(command, check=False, text=True, capture_output=stdout_path is not None)
    if result.returncode != 0:
        if stdout_path is not None:
            sys.stderr.write(result.stdout)
            sys.stderr.write(result.stderr)
        raise CycleError(f"command failed ({result.returncode}): {' '.join(command)}")
    if stdout_path is not None:
        stdout_path.write_text(result.stdout, encoding="utf-8")


def tool_identities(paths: list[Path]) -> list[dict[str, Any]]:
    return [fingerprint(path, "tool") for path in paths]


def stage_expected(
    name: str,
    repo_sha: str,
    config: dict[str, Any],
    inputs: list[tuple[str, Path]],
    tools: list[Path],
) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "stage": name,
        "repo_commit_sha": repo_sha,
        "config": config,
        "inputs": [fingerprint(path, role) for role, path in inputs],
        "tool_identity": tool_identities(tools),
    }


def complete_stage(expected: dict[str, Any], outputs: list[Path]) -> dict[str, Any]:
    return expected | {"outputs": [fingerprint(path, "output") for path in outputs]}


def execute_stage(
    *,
    args: argparse.Namespace,
    name: str,
    run_dir: Path,
    repo_sha: str,
    config: dict[str, Any],
    inputs: list[tuple[str, Path]],
    tools: list[Path],
    outputs: list[Path],
    action: Callable[[], None],
) -> dict[str, Any]:
    sidecar = run_dir / name / "stage.resume.json"
    if args.dry_run:
        return {"status": "planned", "config": config, "outputs": [report_path(path) for path in outputs]}
    expected = stage_expected(name, repo_sha, config, inputs, tools)
    existing = [path.exists() for path in [*outputs, sidecar]]
    if args.resume and all(existing):
        mismatch = first_mismatch(complete_stage(expected, outputs), load_json(sidecar))
        if mismatch is not None:
            raise CycleError(f"{name} resume metadata mismatch at {mismatch}")
        return {"status": "skipped-resume-validated", "sidecar": report_path(sidecar)}
    if any(existing):
        hint = "remove the stale local output or rerun with a matching --resume configuration"
        raise CycleError(f"{name} has partial or stale output; {hint}")
    for output in outputs:
        output.parent.mkdir(parents=True, exist_ok=True)
    action()
    missing = [path for path in outputs if not path.is_file()]
    if missing:
        raise CycleError(f"{name} completed without output(s): {', '.join(str(path) for path in missing)}")
    write_json(sidecar, complete_stage(expected, outputs))
    return {"status": "ok", "sidecar": report_path(sidecar)}


def read_tsv(path: Path, expected_header: list[str]) -> list[dict[str, str]]:
    try:
        with path.open(newline="", encoding="utf-8") as handle:
            reader = csv.DictReader(handle, delimiter="\t")
            if reader.fieldnames != expected_header:
                raise CycleError(f"unexpected TSV header in {path}")
            rows = list(reader)
    except OSError as error:
        raise CycleError(f"cannot read TSV {path}: {error}") from error
    if any(row is None or None in row for row in rows):
        raise CycleError(f"malformed TSV row in {path}")
    return rows


def write_tsv(path: Path, header: list[str], rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=header, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def phase_of(row: dict[str, str]) -> int:
    try:
        value = int(row["phase"])
    except (KeyError, ValueError) as error:
        raise CycleError("normalized row has invalid phase") from error
    if value not in range(13):
        raise CycleError("normalized row phase must be in [0, 12]")
    return value


def partition_selected(selected: Path, early: Path, late: Path) -> None:
    rows = read_tsv(selected, NORMALIZED_HEADER)
    early_rows = [row for row in rows if phase_of(row) <= 9]
    late_rows = [row for row in rows if phase_of(row) >= 10]
    if not early_rows or not late_rows:
        raise CycleError("phase selection did not produce both search and exact roots")
    write_tsv(early, NORMALIZED_HEADER, early_rows)
    write_tsv(late, NORMALIZED_HEADER, late_rows)


def load_manifest(path: Path) -> dict[str, Any]:
    return load_json(path)


def check_full_teacher_coverage(path: Path) -> None:
    values = load_manifest(path).get("trained_phases")
    if values != list(range(13)):
        raise CycleError("--teacher-manifest must declare trained_phases [0, ..., 12] for search teaching")


def merge_teacher_outputs(
    *,
    early_move_teacher: Path,
    early_children: Path,
    late_move_teacher: Path,
    late_children: Path,
    early_selected: Path,
    late_selected: Path,
    search_report: Path,
    exact_report: Path,
    output_children: Path,
    bundle_out: Path,
) -> None:
    early_moves = read_tsv(early_move_teacher, MOVE_TEACHER_HEADER_V2)
    late_moves = read_tsv(late_move_teacher, MOVE_TEACHER_HEADER_V1)
    early_rows = read_tsv(early_children, NORMALIZED_HEADER)
    late_rows = read_tsv(late_children, NORMALIZED_HEADER)
    if not early_moves or not late_moves:
        raise CycleError("teacher output has no move rows")
    for row in early_moves:
        if int(row["root_phase"]) not in range(10):
            raise CycleError("search teacher contains a non-midgame root")
    for row in late_moves:
        if int(row["root_phase"]) not in range(10, 13):
            raise CycleError("exact teacher contains a non-late root")
    expected_early_roots = {row["board_id"] for row in read_tsv(early_selected, NORMALIZED_HEADER)}
    expected_late_roots = {row["board_id"] for row in read_tsv(late_selected, NORMALIZED_HEADER)}
    missing_early = sorted(expected_early_roots - {row["root_board_id"] for row in early_moves})
    missing_late = sorted(expected_late_roots - {row["root_board_id"] for row in late_moves})
    if missing_early or missing_late:
        raise CycleError(
            "teacher output is incomplete for selected roots"
            + (f"; midgame={missing_early[:3]}" if missing_early else "")
            + (f"; late-game={missing_late[:3]}" if missing_late else "")
        )

    by_record: dict[str, dict[str, str]] = {}
    split_by_board: dict[str, str] = {}
    for row in [*early_rows, *late_rows]:
        record_id = row["record_id"]
        board_id = row["board_id"]
        prior = by_record.get(record_id)
        if prior is not None and prior != row:
            raise CycleError(f"teacher children conflict for record_id {record_id!r}")
        by_record[record_id] = row
        prior_split = split_by_board.setdefault(board_id, row["split"])
        if prior_split != row["split"]:
            raise CycleError(f"teacher child board crosses splits: {board_id!r}")
    child_ids = set(by_record)
    required_child_ids = {row["child_board_id"] for row in [*early_moves, *late_moves]}
    missing = sorted(required_child_ids - child_ids)
    extra = sorted(child_ids - required_child_ids)
    if missing or extra:
        raise CycleError(
            "teacher child merge is not an exact move-teacher join"
            + (f"; missing={missing[:3]}" if missing else "")
            + (f"; extra={extra[:3]}" if extra else "")
        )
    write_tsv(output_children, NORMALIZED_HEADER, [by_record[key] for key in sorted(by_record)])
    search_provenance = {
        key: early_moves[0][key]
        for key in ("teacher_kind", "teacher_source", "teacher_artifact_id", "teacher_artifact_checksum", "teacher_search_config_id")
    }
    if any({key: row[key] for key in search_provenance} != search_provenance for row in early_moves):
        raise CycleError("search move-teacher v2 provenance differs within its sidecar")
    write_json(
        bundle_out,
        {
            "schema_version": 1,
            "teacher_inputs": [
                {"kind": "artifact_search", "schema": "move-teacher-tsv-v2", "move_rows": len(early_moves), "provenance": search_provenance},
                {"kind": "exact", "schema": "move-teacher-tsv-v1", "move_rows": len(late_moves), "teacher_source": late_moves[0]["teacher_source"]},
            ],
            "child_rows": len(by_record),
            "selected_roots": {"midgame": len(expected_early_roots), "late_game": len(expected_late_roots)},
            "teacher_root_summary": {
                "midgame": {
                    "selected": len(expected_early_roots),
                    "completed": len({row["root_board_id"] for row in early_moves}),
                    "rejected": load_json(search_report).get("rejected_roots", 0),
                    "incomplete": not bool(load_json(search_report).get("complete", False)),
                },
                "late_game": {
                    "selected": len(expected_late_roots),
                    "completed": len({row["root_board_id"] for row in late_moves}),
                    "terminal_skipped": load_json(exact_report).get("terminal_roots_skipped", 0),
                    "solve_failures": load_json(exact_report).get("solve_failures", 0),
                    "too_many_empty": load_json(exact_report).get("skipped_too_many_empty", 0),
                },
            },
            "root_phase_coverage": sorted({int(row["root_phase"]) for row in [*early_moves, *late_moves]}),
            "warnings": LOCAL_ONLY_WARNINGS,
        },
    )


def ranking_summary(report_paths: dict[str, Path]) -> dict[str, Any]:
    reports = {name: load_json(path) for name, path in report_paths.items()}
    pairs: list[dict[str, Any]] = []
    for teacher in ("search", "exact"):
        candidate = reports[f"candidate_{teacher}"]
        baseline = reports[f"baseline_{teacher}"]
        candidate_pairwise = float(candidate.get("pairwise_accuracy", 0.0))
        baseline_pairwise = float(baseline.get("pairwise_accuracy", 0.0))
        candidate_regret = float(candidate.get("mean_root_regret", 0.0))
        baseline_regret = float(baseline.get("mean_root_regret", 0.0))
        roots = int(candidate.get("root_count", 0))
        candidate_by_phase = candidate.get("results_by_phase", {})
        baseline_by_phase = baseline.get("results_by_phase", {})
        phase_deltas: dict[str, Any] = {}
        if isinstance(candidate_by_phase, dict) and isinstance(baseline_by_phase, dict):
            for phase in sorted(set(candidate_by_phase) | set(baseline_by_phase), key=int):
                candidate_phase = candidate_by_phase.get(phase, {})
                baseline_phase = baseline_by_phase.get(phase, {})
                if not isinstance(candidate_phase, dict) or not isinstance(baseline_phase, dict):
                    continue
                phase_roots = int(candidate_phase.get("root_count", 0))
                phase_deltas[phase] = {
                    "root_count": phase_roots,
                    "pairwise_accuracy_delta": float(candidate_phase.get("pairwise_accuracy", 0.0)) - float(baseline_phase.get("pairwise_accuracy", 0.0)),
                    "mean_root_regret_delta": float(baseline_phase.get("mean_root_regret", 0.0)) - float(candidate_phase.get("mean_root_regret", 0.0)),
                    "top1_accuracy_delta": float(candidate_phase.get("top1_accuracy", 0.0)) - float(baseline_phase.get("top1_accuracy", 0.0)),
                    "all_same_score_rate": 0.0 if phase_roots == 0 else float(candidate_phase.get("roots_with_all_moves_same_predicted_score", 0)) / phase_roots,
                }
        pairs.append(
            {
                "teacher": teacher,
                "root_count": roots,
                "pairwise_accuracy_delta": candidate_pairwise - baseline_pairwise,
                "mean_root_regret_delta": baseline_regret - candidate_regret,
                "top1_accuracy_delta": float(candidate.get("top1_accuracy", 0.0)) - float(baseline.get("top1_accuracy", 0.0)),
                "all_same_score_rate": 0.0 if roots == 0 else float(candidate.get("roots_with_all_moves_same_predicted_score", 0)) / roots,
                "phase_deltas": phase_deltas,
            }
        )
    total_roots = sum(item["root_count"] for item in pairs)
    weighted = lambda key: (sum(item[key] * item["root_count"] for item in pairs) / total_roots if total_roots else 0.0)
    return {"by_teacher": pairs, "root_count": total_roots, "pairwise_accuracy_delta": weighted("pairwise_accuracy_delta"), "mean_root_regret_delta": weighted("mean_root_regret_delta"), "top1_accuracy_delta": weighted("top1_accuracy_delta"), "all_same_score_rate": weighted("all_same_score_rate")}


def arena_overall(report: dict[str, Any]) -> dict[str, Any]:
    results = report.get("results")
    if not isinstance(results, dict):
        raise CycleError("full-game arena report has no results")
    overall = results.get("overall")
    if not isinstance(overall, dict):
        raise CycleError("full-game arena report has no overall results")
    return overall


def arena_score_rate(report: dict[str, Any]) -> float:
    value = arena_overall(report).get("candidate_score_rate")
    if not isinstance(value, (int, float)):
        raise CycleError("full-game arena report has no candidate_score_rate")
    return float(value)


def clean_arena(report: dict[str, Any]) -> bool:
    return report.get("failed_games") == 0 and report.get("illegal_games") == 0


def full_game_summary(paths: dict[str, Path]) -> dict[str, Any]:
    reports = {name: load_json(path) for name, path in paths.items()}
    same = reports["same_artifact"]
    same_sanity = same.get("same_artifact_sanity")
    same_ok = (
        clean_arena(same)
        and isinstance(same_sanity, dict)
        and same_sanity.get("neutral") is True
    )
    forward = arena_score_rate(reports["candidate_vs_baseline"])
    reverse = arena_score_rate(reports["baseline_vs_candidate"])
    swap_ok = (
        clean_arena(reports["candidate_vs_baseline"])
        and clean_arena(reports["baseline_vs_candidate"])
        and abs((forward + reverse) - 1.0) <= 0.04
    )
    return {
        "same_artifact_sanity": same_ok,
        "swap_sanity": swap_ok,
        "candidate_vs_baseline_score_rate": forward,
        "baseline_vs_candidate_score_rate": reverse,
        "reports": {name: report_path(path) for name, path in paths.items()},
    }


def validate_selection_train_coverage(path: Path) -> None:
    report = load_json(path)
    collision_counts: dict[str, int] = {}
    for field in (
        "selected_cross_split_board_collision_count",
        "selected_cross_split_game_group_collision_count",
    ):
        value = report.get(field)
        if isinstance(value, bool) or not isinstance(value, int):
            raise CycleError(f"phase selection report has invalid {field}")
        collision_counts[field] = value
    if any(collision_counts.values()):
        raise CycleError(
            "phase selection report has selected cross-split leakage: "
            f"board_id={collision_counts['selected_cross_split_board_collision_count']}, "
            "game_group_id="
            f"{collision_counts['selected_cross_split_game_group_collision_count']}"
        )
    phase_split_counts = report.get("selected_phase_split_counts")
    if not isinstance(phase_split_counts, dict):
        raise CycleError("phase selection report has no selected phase/split coverage")
    missing = [
        phase
        for phase in range(13)
        if not isinstance(phase_split_counts.get(str(phase)), dict)
        or int(phase_split_counts[str(phase)].get("train", 0)) <= 0
    ]
    if missing:
        raise CycleError(
            "phase selection has no train roots for phase(s): " + ", ".join(str(phase) for phase in missing)
        )


def trainer_trained_phases(path: Path) -> list[int]:
    value = load_json(path).get("trained_phases")
    if value != list(range(13)):
        raise CycleError(
            "full-phase candidate requires trainer trained_phases [0, ..., 12]; "
            f"got {value!r}"
        )
    return list(range(13))


def decision_for_run(
    *,
    args: argparse.Namespace,
    seed: int,
    selection: dict[str, Any],
    teacher_bundle: dict[str, Any],
    trainer: dict[str, Any],
    candidate_weights: Path,
    candidate_manifest: Path,
    ranking: dict[str, Any],
    late_matrix: dict[str, Any],
    full_game: dict[str, Any],
) -> dict[str, Any]:
    coverage = teacher_bundle.get("root_phase_coverage") == list(range(13))
    late_assessment = late_matrix.get("overall_local_assessment", {})
    late_ok = not bool(late_assessment.get("fatal", False))
    sanity_ok = bool(full_game["same_artifact_sanity"] and full_game["swap_sanity"] and late_ok)
    pairwise = float(ranking["pairwise_accuracy_delta"])
    regret = float(ranking["mean_root_regret_delta"])
    all_same = float(ranking["all_same_score_rate"])
    full_rate = full_game["candidate_vs_baseline_score_rate"]
    if not coverage or trainer.get("trained_phases") != list(range(13)) or not sanity_ok:
        category, reasons = "invalid_run", ["phase coverage, trainer coverage, or arena sanity validation failed"]
    elif pairwise < 0.0 and regret < 0.0:
        category, reasons = "negative", ["offline ranking pairwise accuracy and regret both regressed"]
    elif pairwise <= 0.0 or regret <= 0.0:
        category, reasons = "needs_rank_objective", ["pairwise ranking or root regret did not improve"]
    elif all_same >= 0.25:
        category, reasons = "needs_feature_capacity", ["predicted root scores remain all-same for too many roots"]
    elif full_rate is not None and full_rate >= 0.5:
        category, reasons = "promote_to_larger_validation", ["offline ranking and bounded late/full-game arenas are non-negative"]
    else:
        category, reasons = "hold_for_more_data", ["ranking improved but full-game evidence is neutral or incomplete"]
    return {
        "schema_version": 1,
        "seed": seed,
        "scale": args.scale,
        "selected_roots": selection.get("selected_rows"),
        "requested_roots": selection.get("requested_rows"),
        "phase_quotas": selection.get("selection_policy", {}).get("phase_quotas"),
        "phase_quota_overrides": selection.get("selection_policy", {}).get("phase_quota_overrides"),
        "selected_split_policy": selection.get("selection_policy", {}).get("selected_split_policy"),
        "train_only_phases": selection.get("selection_policy", {}).get("train_only_phases"),
        "artifact_score_scale": args.artifact_score_scale,
        "fallback_additive_through_phase": args.fallback_additive_through_phase,
        "phase_coverage": teacher_bundle.get("root_phase_coverage"),
        "child_rows": teacher_bundle.get("child_rows"),
        "rejected_or_incomplete_teacher_roots": teacher_bundle.get("teacher_root_summary"),
        "ranking_metrics_delta": ranking,
        "phase_ranking": {item["teacher"]: item["phase_deltas"] for item in ranking["by_teacher"]},
        "all_same_score_rate": all_same,
        "late_game_arena": late_matrix.get("overall_local_assessment"),
        "full_game_arena": full_game,
        "same_artifact_sanity": bool(full_game["same_artifact_sanity"]),
        "swap_sanity": bool(full_game["swap_sanity"]),
        "candidate_artifact_identity": {
            "manifest": report_path(candidate_manifest),
            "manifest_sha256": sha256_file(candidate_manifest),
            "weights": report_path(candidate_weights),
            "weights_sha256": sha256_file(candidate_weights),
        },
        "warnings": LOCAL_ONLY_WARNINGS,
        "suggested_decision": {"category": category, "reasons": reasons},
    }


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--normalized-tsv", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--scale", choices=tuple(SCALE_ROOTS), default="smoke")
    parser.add_argument("--roots-per-phase", type=int)
    parser.add_argument(
        "--phase-quota",
        action="append",
        default=[],
        type=parse_phase_quota,
        metavar="PHASE=COUNT",
        help="Override the base root quota for one phase; repeat for multiple phases.",
    )
    parser.add_argument("--max-roots-per-game-group", type=int)
    parser.add_argument(
        "--selected-split-policy",
        choices=("preserve", "game-group-hash"),
        default="preserve",
        help="Preserve input splits or deterministically resplit cap-1 selected roots by game group.",
    )
    parser.add_argument(
        "--train-only-phase",
        action="append",
        default=[],
        type=int,
        metavar="PHASE",
        help="Force selected roots in one phase to train after split assignment; repeat as needed.",
    )
    parser.add_argument("--seeds", default="0", type=lambda value: parse_int_list(value, "--seeds"))
    parser.add_argument("--pattern-set", default="pattern-v2-endgame-lite")
    parser.add_argument("--teacher-manifest", required=True, type=Path)
    parser.add_argument("--teacher-weights", required=True, type=Path)
    parser.add_argument("--baseline-manifest", required=True, type=Path)
    parser.add_argument("--baseline-weights", required=True, type=Path)
    parser.add_argument("--full-game-openings", required=True, type=Path)
    parser.add_argument("--search-max-depth", type=int, default=4)
    parser.add_argument("--search-max-nodes", type=int, default=200_000)
    parser.add_argument("--search-max-time-ms", type=int, default=0)
    parser.add_argument("--search-preset", choices=("basic", "full"), default="full")
    parser.add_argument("--search-exact-endgame-empties", type=int, default=8)
    parser.add_argument("--exact-max-empty", type=int, default=14)
    parser.add_argument("--epochs", type=int, default=8)
    parser.add_argument("--learning-rate", type=float, default=0.05)
    parser.add_argument("--weight-decay", type=float, default=0.0001)
    parser.add_argument("--rank-temperature", type=float, default=1.0)
    parser.add_argument("--value-loss-weight", type=float, default=0.05)
    parser.add_argument("--pair-sampling-cap", type=int, default=64)
    parser.add_argument("--tie-margin", type=float, default=0.0)
    parser.add_argument("--artifact-score-scale", type=int, default=1)
    parser.add_argument("--fallback-additive-through-phase", type=int)
    parser.add_argument("--initial-weights", type=Path)
    parser.add_argument(
        "--initial-trained-phases",
        type=lambda value: parse_int_list(value, "--initial-trained-phases"),
    )
    parser.add_argument(
        "--freeze-phases",
        default=[],
        type=lambda value: parse_int_list(value, "--freeze-phases"),
    )
    parser.add_argument("--gradient-clip", type=float)
    parser.add_argument("--early-stop-patience", type=int)
    parser.add_argument("--arena-depths", default="3", type=lambda value: parse_int_list(value, "--arena-depths", minimum=1))
    parser.add_argument("--arena-seeds", default="0", type=lambda value: parse_int_list(value, "--arena-seeds"))
    parser.add_argument("--arena-max-positions", type=int)
    parser.add_argument("--full-game-depth", type=int, default=3)
    parser.add_argument("--full-game-max-nodes", type=int, default=200_000)
    parser.add_argument("--full-game-opening-limit", type=int)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--created-at-utc")
    parser.add_argument("--selector", type=Path, default=root / "tools/pattern/labels/select_phase_stratified_roots.py")
    parser.add_argument("--search-teacher-runner", type=Path, default=root / "tools/pattern/labels/run_search_move_teacher_generation.py")
    parser.add_argument("--search-generator", type=Path, default=root / "build/tools/pattern/labels/vibe-othello-generate-search-move-teacher-dataset")
    parser.add_argument("--exact-generator", type=Path, default=root / "build/tools/pattern/labels/vibe-othello-generate-exact-move-teacher-dataset")
    parser.add_argument("--dataset-exe", type=Path, default=root / "build/tools/pattern/dataset/vibe-othello-pattern-dataset-smoke")
    parser.add_argument("--trainer", type=Path, default=root / "tools/pattern/train/train_pattern.py")
    parser.add_argument("--exporter", type=Path, default=root / "tools/pattern/export/export_v0b.py")
    parser.add_argument("--catalog-dump-exe", type=Path, default=root / "build/tools/pattern/export/vibe-othello-pattern-catalog-dump")
    parser.add_argument("--ranking-evaluator", type=Path, default=root / "build/tools/pattern/labels/vibe-othello-evaluate-move-teacher-ranking")
    parser.add_argument("--late-arena-runner", type=Path, default=root / "tools/arena/run_pattern_artifact_arena_matrix.py")
    parser.add_argument("--late-arena-exe", type=Path, default=root / "build/tools/arena/vibe-othello-pattern-artifact-arena")
    parser.add_argument("--full-arena-exe", type=Path, default=root / "build/tools/arena/vibe-othello-full-game-artifact-arena")
    args = parser.parse_args()
    args.roots_per_phase = args.roots_per_phase or SCALE_ROOTS[args.scale]
    override_phases = [phase for phase, _ in args.phase_quota]
    if len(set(override_phases)) != len(override_phases):
        parser.error("--phase-quota must not repeat a phase")
    args.phase_quota_overrides = dict(args.phase_quota)
    args.phase_quotas = {
        phase: args.phase_quota_overrides.get(phase, args.roots_per_phase) for phase in PHASES
    }
    if args.max_roots_per_game_group is None and args.scale in ("candidate", "large"):
        args.max_roots_per_game_group = 1
    args.arena_max_positions = args.arena_max_positions or SCALE_ARENA_POSITIONS[args.scale]
    args.full_game_opening_limit = args.full_game_opening_limit or SCALE_OPENING_LIMIT[args.scale]
    if args.roots_per_phase <= 0 or args.search_max_depth <= 0 or args.full_game_depth <= 0:
        parser.error("root quota and search depths must be positive")
    if args.max_roots_per_game_group is not None and args.max_roots_per_game_group <= 0:
        parser.error("--max-roots-per-game-group must be positive")
    if args.selected_split_policy == "game-group-hash" and args.max_roots_per_game_group != 1:
        parser.error("--selected-split-policy game-group-hash requires --max-roots-per-game-group 1")
    if any(phase not in PHASES for phase in args.train_only_phase):
        parser.error("--train-only-phase must be in [0, 12]")
    if len(set(args.train_only_phase)) != len(args.train_only_phase):
        parser.error("--train-only-phase must not repeat a phase")
    args.train_only_phases = frozenset(args.train_only_phase)
    if args.search_max_nodes < 0 or args.full_game_max_nodes < 0 or args.search_max_time_ms < 0 or args.exact_max_empty < 0 or args.exact_max_empty > 64:
        parser.error("search and exact bounds are invalid")
    if args.search_max_nodes == 0 and args.search_max_time_ms != 0:
        parser.error("wall-clock-only search teacher runs are not supported")
    if args.epochs < 0 or args.learning_rate < 0 or args.weight_decay < 0 or args.value_loss_weight < 0 or args.tie_margin < 0:
        parser.error("trainer values must be non-negative")
    if args.rank_temperature <= 0 or args.pair_sampling_cap < 0 or args.arena_max_positions <= 0 or args.full_game_opening_limit <= 0:
        parser.error("rank/arena values are invalid")
    if args.artifact_score_scale <= 0 or args.artifact_score_scale > 65535:
        parser.error("--artifact-score-scale must be in [1, 65535]")
    if args.fallback_additive_through_phase is not None and not (
        0 <= args.fallback_additive_through_phase < len(PHASES)
    ):
        parser.error("--fallback-additive-through-phase must be in [0, 12]")
    if args.initial_weights is None and (args.initial_trained_phases is not None or args.freeze_phases):
        parser.error("warm-start phase arguments require --initial-weights")
    if args.initial_weights is not None and args.initial_trained_phases is None:
        parser.error("--initial-weights requires --initial-trained-phases")
    initial_phases = [] if args.initial_trained_phases is None else args.initial_trained_phases
    if any(phase not in PHASES for phase in [*initial_phases, *args.freeze_phases]):
        parser.error("warm-start phases must be in [0, 12]")
    if not set(args.freeze_phases).issubset(initial_phases):
        parser.error("--freeze-phases must be a subset of --initial-trained-phases")
    return args


def required_tools(args: argparse.Namespace) -> list[Path]:
    return [args.selector, args.search_teacher_runner, args.search_generator, args.exact_generator, args.dataset_exe, args.trainer, args.exporter, args.catalog_dump_exe, args.ranking_evaluator, args.late_arena_runner, args.late_arena_exe, args.full_arena_exe]


def preflight(args: argparse.Namespace) -> dict[str, Any]:
    paths = [args.normalized_tsv, args.teacher_manifest, args.teacher_weights, args.baseline_manifest, args.baseline_weights, args.full_game_openings]
    if args.initial_weights is not None:
        paths.append(args.initial_weights)
    if not args.dry_run:
        for path in [*paths, *required_tools(args)]:
            if not path.is_file():
                raise CycleError(f"required local input/tool is missing: {path}")
        check_full_teacher_coverage(args.teacher_manifest)
    return {
        "status": "planned" if args.dry_run else "ok",
        "scale": args.scale,
        "roots_per_phase": args.roots_per_phase,
        "phase_quotas": {str(phase): args.phase_quotas[phase] for phase in PHASES},
        "phase_quota_overrides": {
            str(phase): count for phase, count in sorted(args.phase_quota_overrides.items())
        },
        "requested_roots": sum(args.phase_quotas.values()),
        "max_roots_per_game_group": args.max_roots_per_game_group,
        "selected_split_policy": args.selected_split_policy,
        "train_only_phases": sorted(args.train_only_phases),
        "artifact_score_scale": args.artifact_score_scale,
        "fallback_additive_through_phase": args.fallback_additive_through_phase,
        "initial_weights": None if args.initial_weights is None else report_path(args.initial_weights),
        "initial_trained_phases": args.initial_trained_phases,
        "freeze_phases": args.freeze_phases,
        "seeds": args.seeds,
        "warnings": LOCAL_ONLY_WARNINGS,
    }


def main() -> int:
    args = parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    report_out = args.output_dir / "full-phase-growth-cycle-report.json"
    summary_out = args.output_dir / "full-phase-growth-cycle-summary.md"
    try:
        preflight_report = preflight(args)
        commit_sha = repo_commit_sha() if not args.dry_run else "planned"
        run_reports: list[dict[str, Any]] = []
        for seed in args.seeds:
            run_dir = args.output_dir / "runs" / f"seed-{seed}"
            selection_dir = run_dir / "phase_selection"
            selected = selection_dir / "selected.tsv"
            selection_report = selection_dir / "selection-report.json"
            early_selected = selection_dir / "selected-midgame.tsv"
            late_selected = selection_dir / "selected-late-game.tsv"
            selection_command = [sys.executable, str(args.selector), "--normalized-tsv", str(args.normalized_tsv), "--output-tsv", str(selected), "--report-out", str(selection_report), "--roots-per-phase", str(args.roots_per_phase), "--seed", str(seed), "--require-all-phases"]
            for phase, count in sorted(args.phase_quota_overrides.items()):
                selection_command.extend(["--phase-quota", f"{phase}={count}"])
            if args.max_roots_per_game_group is not None:
                selection_command.extend(["--max-roots-per-game-group", str(args.max_roots_per_game_group)])
            if args.selected_split_policy != "preserve":
                selection_command.extend(["--selected-split-policy", args.selected_split_policy])
            for phase in sorted(args.train_only_phases):
                selection_command.extend(["--train-only-phase", str(phase)])
            stages: dict[str, Any] = {}
            def run_selection() -> None:
                run_command(selection_command)
                validate_selection_train_coverage(selection_report)
                partition_selected(selected, early_selected, late_selected)
            stages["phase_selection"] = execute_stage(args=args, name="phase_selection", run_dir=run_dir, repo_sha=commit_sha, config={"command": selection_command}, inputs=[("normalized_tsv", args.normalized_tsv)], tools=[args.selector], outputs=[selected, selection_report, early_selected, late_selected], action=run_selection)

            search_dir = run_dir / "midgame_search_teacher"
            search_move = search_dir / "search-move-teacher.tsv"
            search_children = search_dir / "search-move-teacher-child-normalized.tsv"
            search_report = search_dir / "search-move-teacher-report.json"
            search_command = [sys.executable, str(args.search_teacher_runner), "--generator", str(args.search_generator), "--normalized-tsv", str(early_selected), "--teacher-manifest", str(args.teacher_manifest), "--teacher-weights", str(args.teacher_weights), "--output-dir", str(search_dir), "--max-depth", str(args.search_max_depth), "--max-nodes", str(args.search_max_nodes), "--max-time-ms", str(args.search_max_time_ms), "--search-preset", args.search_preset, "--exact-endgame-empties", str(args.search_exact_endgame_empties), "--min-phase", "0", "--max-phase", "9"]
            stages["midgame_search_teacher"] = execute_stage(args=args, name="midgame_search_teacher", run_dir=run_dir, repo_sha=commit_sha, config={"command": search_command}, inputs=[("selected_midgame", early_selected), ("teacher_manifest", args.teacher_manifest), ("teacher_weights", args.teacher_weights)], tools=[args.search_teacher_runner, args.search_generator], outputs=[search_move, search_children, search_report], action=lambda: run_command(search_command))

            exact_dir = run_dir / "late_game_exact_teacher"
            exact_move = exact_dir / "exact-move-teacher.tsv"
            exact_children = exact_dir / "exact-move-teacher-child-normalized.tsv"
            exact_report = exact_dir / "exact-move-teacher-report.json"
            exact_command = [str(args.exact_generator), "--normalized-tsv", str(late_selected), "--move-teacher-out", str(exact_move), "--child-normalized-out", str(exact_children), "--report-out", str(exact_report), "--max-empty", str(args.exact_max_empty), "--seed", str(seed)]
            stages["late_game_exact_teacher"] = execute_stage(args=args, name="late_game_exact_teacher", run_dir=run_dir, repo_sha=commit_sha, config={"command": exact_command}, inputs=[("selected_late_game", late_selected)], tools=[args.exact_generator], outputs=[exact_move, exact_children, exact_report], action=lambda: run_command(exact_command))

            merge_dir = run_dir / "teacher_merge_validation"
            merged_children = merge_dir / "child-normalized.tsv"
            bundle = merge_dir / "teacher-bundle.json"
            stages["teacher_merge_validation"] = execute_stage(args=args, name="teacher_merge_validation", run_dir=run_dir, repo_sha=commit_sha, config={"sources": ["search-v2", "exact-v1"]}, inputs=[("selected_midgame", early_selected), ("selected_late_game", late_selected), ("search_move_teacher", search_move), ("search_children", search_children), ("search_report", search_report), ("exact_move_teacher", exact_move), ("exact_children", exact_children), ("exact_report", exact_report)], tools=[Path(__file__)], outputs=[merged_children, bundle], action=lambda: merge_teacher_outputs(early_move_teacher=search_move, early_children=search_children, late_move_teacher=exact_move, late_children=exact_children, early_selected=early_selected, late_selected=late_selected, search_report=search_report, exact_report=exact_report, output_children=merged_children, bundle_out=bundle))

            dataset_dir = run_dir / "dataset_build"
            dataset = dataset_dir / "pattern-dataset.tsv"
            dataset_report = dataset_dir / "pattern-dataset-report.json"
            dataset_command = [str(args.dataset_exe), "--normalized-tsv", str(merged_children), "--report", str(dataset_report), "--output-format", "compact-tsv", "--pattern-set", args.pattern_set]
            stages["dataset_build"] = execute_stage(args=args, name="dataset_build", run_dir=run_dir, repo_sha=commit_sha, config={"command": dataset_command}, inputs=[("merged_children", merged_children)], tools=[args.dataset_exe], outputs=[dataset, dataset_report], action=lambda: run_command(dataset_command, stdout_path=dataset))

            train_dir = run_dir / "rank_train"
            weights_json = train_dir / "weights.json"
            trainer_report = train_dir / "trainer-report.json"
            train_command = [sys.executable, str(args.trainer), "--dataset", str(dataset), "--mode", "pattern-rank-v0e", "--move-teacher", str(search_move), "--move-teacher", str(exact_move), "--epochs", str(args.epochs), "--learning-rate", str(args.learning_rate), "--weight-decay", str(args.weight_decay), "--rank-temperature", str(args.rank_temperature), "--value-loss-weight", str(args.value_loss_weight), "--pair-sampling-cap", str(args.pair_sampling_cap), "--tie-margin", str(args.tie_margin), "--seed", str(seed), "--dataset-report", str(dataset_report), "--weights-out", str(weights_json), "--report-out", str(trainer_report)]
            if args.gradient_clip is not None:
                train_command.extend(["--gradient-clip", str(args.gradient_clip)])
            if args.early_stop_patience is not None:
                train_command.extend(["--early-stop-patience", str(args.early_stop_patience)])
            if args.initial_weights is not None:
                train_command.extend(["--initial-weights", str(args.initial_weights)])
                train_command.extend(
                    ["--initial-trained-phases", *[str(value) for value in args.initial_trained_phases]]
                )
                for phase in args.freeze_phases:
                    train_command.extend(["--freeze-phase", str(phase)])
            train_inputs = [
                ("dataset", dataset),
                ("dataset_report", dataset_report),
                ("search_move_teacher", search_move),
                ("exact_move_teacher", exact_move),
            ]
            if args.initial_weights is not None:
                train_inputs.append(("initial_weights", args.initial_weights))
            stages["rank_train"] = execute_stage(args=args, name="rank_train", run_dir=run_dir, repo_sha=commit_sha, config={"command": train_command}, inputs=train_inputs, tools=[args.trainer], outputs=[weights_json, trainer_report], action=lambda: run_command(train_command))

            export_dir = run_dir / "export"
            candidate_weights = export_dir / "candidate.weights.bin"
            candidate_manifest = export_dir / "candidate.manifest.json"
            trained_phases = list(range(13)) if args.dry_run else trainer_trained_phases(trainer_report)
            export_command = [sys.executable, str(args.exporter), "--weights-json", str(weights_json), "--weights-out", str(candidate_weights), "--manifest-out", str(candidate_manifest), "--pattern-set", args.pattern_set, "--score-scale", str(args.artifact_score_scale), "--catalog-dump-exe", str(args.catalog_dump_exe), "--trained-phases", *[str(value) for value in trained_phases]]
            if args.fallback_additive_through_phase is not None:
                export_command.extend(
                    [
                        "--fallback-additive-through-phase",
                        str(args.fallback_additive_through_phase),
                    ]
                )
            stages["export"] = execute_stage(args=args, name="export", run_dir=run_dir, repo_sha=commit_sha, config={"command": export_command, "trained_phases_source": "trainer-report"}, inputs=[("weights_json", weights_json), ("trainer_report", trainer_report)], tools=[args.exporter, args.catalog_dump_exe], outputs=[candidate_weights, candidate_manifest], action=lambda: run_command(export_command))

            ranking_dir = run_dir / "offline_ranking"
            ranking_paths = {name: ranking_dir / f"{name}.json" for name in ("candidate_search", "baseline_search", "candidate_exact", "baseline_exact")}
            ranking_summaries = {name: ranking_dir / f"{name}.md" for name in ranking_paths}
            ranking_aggregate = ranking_dir / "ranking-summary.json"
            ranking_commands: list[list[str]] = []
            for teacher, teacher_path in (("search", search_move), ("exact", exact_move)):
                for role, weights, manifest in (("candidate", candidate_weights, candidate_manifest), ("baseline", args.baseline_weights, args.baseline_manifest)):
                    name = f"{role}_{teacher}"
                    ranking_commands.append([str(args.ranking_evaluator), "--move-teacher", str(teacher_path), "--weights", str(weights), "--manifest", str(manifest), "--pattern-set", args.pattern_set, "--tie-margin", str(args.tie_margin), "--report-out", str(ranking_paths[name]), "--summary-out", str(ranking_summaries[name])])
            def run_ranking() -> None:
                for command in ranking_commands:
                    run_command(command)
                write_json(ranking_aggregate, ranking_summary(ranking_paths))
            stages["offline_ranking"] = execute_stage(args=args, name="offline_ranking", run_dir=run_dir, repo_sha=commit_sha, config={"commands": ranking_commands}, inputs=[("search_move_teacher", search_move), ("exact_move_teacher", exact_move), ("candidate_weights", candidate_weights), ("candidate_manifest", candidate_manifest), ("baseline_weights", args.baseline_weights), ("baseline_manifest", args.baseline_manifest)], tools=[args.ranking_evaluator], outputs=[*ranking_paths.values(), *ranking_summaries.values(), ranking_aggregate], action=run_ranking)

            late_dir = run_dir / "late_game_arena"
            late_report = late_dir / "arena-matrix-report.json"
            late_summary = late_dir / "arena-matrix-summary.md"
            late_command = [sys.executable, str(args.late_arena_runner), "--positions-tsv", str(late_selected), "--output-dir", str(late_dir), "--comparison-name", "full_phase_candidate_vs_baseline", "--candidate-weights", str(candidate_weights), "--candidate-manifest", str(candidate_manifest), "--candidate-name", "full-phase-candidate", "--baseline-weights", str(args.baseline_weights), "--baseline-manifest", str(args.baseline_manifest), "--baseline-name", "explicit-baseline", "--depths", ",".join(str(value) for value in args.arena_depths), "--seeds", ",".join(str(value) for value in args.arena_seeds), "--max-empty", str(args.exact_max_empty), "--max-positions", str(args.arena_max_positions), "--side-swap", "--same-artifact-sanity", "candidate", "--swap-sanity", "primary", "--arena-exe", str(args.late_arena_exe)]
            stages["late_game_arena"] = execute_stage(args=args, name="late_game_arena", run_dir=run_dir, repo_sha=commit_sha, config={"command": late_command}, inputs=[("late_selected", late_selected), ("candidate_weights", candidate_weights), ("candidate_manifest", candidate_manifest), ("baseline_weights", args.baseline_weights), ("baseline_manifest", args.baseline_manifest)], tools=[args.late_arena_runner, args.late_arena_exe], outputs=[late_report, late_summary], action=lambda: run_command(late_command))

            full_dir = run_dir / "full_game_arena"
            full_paths = {name: full_dir / f"{name}.json" for name in ("candidate_vs_baseline", "same_artifact", "baseline_vs_candidate")}
            full_aggregate = full_dir / "full-game-summary.json"
            def full_command(candidate_weights_arg: Path, candidate_manifest_arg: Path, baseline_weights_arg: Path, baseline_manifest_arg: Path, report: Path) -> list[str]:
                return [str(args.full_arena_exe), "--candidate-manifest", str(candidate_manifest_arg), "--candidate-weights", str(candidate_weights_arg), "--candidate-name", "candidate", "--baseline-manifest", str(baseline_manifest_arg), "--baseline-weights", str(baseline_weights_arg), "--baseline-name", "baseline", "--openings", str(args.full_game_openings), "--report-out", str(report), "--depth", str(args.full_game_depth), "--nodes", str(args.full_game_max_nodes), "--time-ms", "0", "--search-preset", args.search_preset, "--exact-endgame-empties", str(args.search_exact_endgame_empties), "--seed", str(seed), "--opening-limit", str(args.full_game_opening_limit)]
            full_commands = [full_command(candidate_weights, candidate_manifest, args.baseline_weights, args.baseline_manifest, full_paths["candidate_vs_baseline"]), full_command(candidate_weights, candidate_manifest, candidate_weights, candidate_manifest, full_paths["same_artifact"]), full_command(args.baseline_weights, args.baseline_manifest, candidate_weights, candidate_manifest, full_paths["baseline_vs_candidate"])]
            def run_full_arena() -> None:
                for command in full_commands:
                    run_command(command)
                write_json(full_aggregate, full_game_summary(full_paths))
            stages["full_game_arena"] = execute_stage(args=args, name="full_game_arena", run_dir=run_dir, repo_sha=commit_sha, config={"commands": full_commands}, inputs=[("openings", args.full_game_openings), ("candidate_weights", candidate_weights), ("candidate_manifest", candidate_manifest), ("baseline_weights", args.baseline_weights), ("baseline_manifest", args.baseline_manifest)], tools=[args.full_arena_exe], outputs=[*full_paths.values(), full_aggregate], action=run_full_arena)

            decision_dir = run_dir / "decision_summary"
            decision_path = decision_dir / "decision-report.json"
            def write_decision() -> None:
                write_json(decision_path, decision_for_run(args=args, seed=seed, selection=load_json(selection_report), teacher_bundle=load_json(bundle), trainer=load_json(trainer_report), candidate_weights=candidate_weights, candidate_manifest=candidate_manifest, ranking=load_json(ranking_aggregate), late_matrix=load_json(late_report), full_game=load_json(full_aggregate)))
            stages["decision_summary"] = execute_stage(args=args, name="decision_summary", run_dir=run_dir, repo_sha=commit_sha, config={"decision_categories": ["promote_to_larger_validation", "hold_for_more_data", "needs_rank_objective", "needs_feature_capacity", "negative", "invalid_run"]}, inputs=[("selection", selection_report), ("teacher_bundle", bundle), ("trainer_report", trainer_report), ("candidate_manifest", candidate_manifest), ("ranking", ranking_aggregate), ("late_arena", late_report), ("full_arena", full_aggregate)], tools=[Path(__file__)], outputs=[decision_path], action=write_decision)
            run_reports.append({"seed": seed, "status": "planned" if args.dry_run else "ok", "stages": stages, "decision_report": report_path(decision_path)})

        report = {"schema_version": 1, "status": "planned" if args.dry_run else "ok", "repo_commit_sha": commit_sha, "preflight": preflight_report, "runs": run_reports, "warnings": LOCAL_ONLY_WARNINGS}
        write_json(report_out, report)
        summary_out.write_text("# Full-Phase Pattern Growth Cycle\n\nLocal-only orchestration report. See `full-phase-growth-cycle-report.json`.\n", encoding="utf-8")
        print(f"report={report_out}")
        print(f"summary={summary_out}")
    except (CycleError, OSError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
