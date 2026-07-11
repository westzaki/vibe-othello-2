#!/usr/bin/env python3
"""Tiny real-tool smoke for the local-only full-phase growth cycle."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def run(command: list[str], *, expect_success: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, check=False, text=True, capture_output=True)
    if result.returncode != 0 and expect_success:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise RuntimeError("command failed: " + " ".join(command))
    return result


def make_normalized(args: argparse.Namespace, output: Path, report: Path) -> None:
    imported = run(
        [
            sys.executable,
            str(args.importer),
            "--input",
            str(args.sequence_fixture),
            "--manifest",
            str(args.sequence_manifest),
            "--replay-helper",
            str(args.replay_helper),
            "--min-ply",
            "0",
            "--emit-terminal",
            "--split-policy",
            "connected-board-game",
            "--report",
            str(report),
        ]
    )
    rows = list(csv.DictReader(imported.stdout.splitlines(), delimiter="\t"))
    if {int(row["phase"]) for row in rows} != set(range(13)):
        raise RuntimeError("tiny imported fixture did not cover every phase")
    for row in rows:
        row["split"] = "train"
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]), delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def make_full_coverage_artifact(args: argparse.Namespace, directory: Path) -> tuple[Path, Path]:
    catalog = json.loads(
        run([str(args.catalog_dump), "--pattern-set", "pattern-v2-endgame-lite", "--index-mode", "raw"]).stdout
    )
    contract = next(
        item for item in catalog["pattern_sets"] if item["pattern_set_id"] == "pattern-v2-endgame-lite"
    )
    weights_json = directory / "teacher.weights.json"
    weights_json.write_text(
        json.dumps(
            {
                "weights_schema_version": "pattern-eval-weights-v2",
                "phase_bias": {str(phase): 0.0 for phase in range(13)},
                "pattern_weights": [],
                "pattern_set_id": "pattern-v2-endgame-lite",
                "pattern_contract_digest": contract["pattern_contract_digest"],
                "index_mode": "raw",
                "phase_count": 13,
                "phase_mapping_id": "disc-count-13-v1",
                "score_unit": "disc-diff",
            },
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    weights = directory / "teacher.weights.bin"
    manifest = directory / "teacher.manifest.json"
    run(
        [
            sys.executable,
            str(args.exporter),
            "--weights-json",
            str(weights_json),
            "--weights-out",
            str(weights),
            "--manifest-out",
            str(manifest),
            "--pattern-set",
            "pattern-v2-endgame-lite",
            "--catalog-dump-exe",
            str(args.catalog_dump),
            "--trained-phases",
            *[str(phase) for phase in range(13)],
        ]
    )
    return weights, manifest


def make_validation_only_phase(source: Path, output: Path, phase: int) -> None:
    with source.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    for row in rows:
        if int(row["phase"]) == phase:
            row["split"] = "validation"
            row["game_group_id"] = f"{row['game_group_id']}-validation-only-{row['record_id']}"
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]), delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def runner_command(
    args: argparse.Namespace,
    normalized: Path,
    output_dir: Path,
    weights: Path,
    manifest: Path,
) -> list[str]:
    return [
        sys.executable,
        str(args.runner),
        "--normalized-tsv",
        str(normalized),
        "--output-dir",
        str(output_dir),
        "--scale",
        "smoke",
        "--roots-per-phase",
        "2",
        "--teacher-manifest",
        str(manifest),
        "--teacher-weights",
        str(weights),
        "--baseline-manifest",
        str(manifest),
        "--baseline-weights",
        str(weights),
        "--full-game-openings",
        str(args.openings),
        "--search-max-depth",
        "1",
        "--search-max-nodes",
        "200000",
        "--epochs",
        "1",
        "--arena-depths",
        "1",
        "--arena-seeds",
        "0",
        "--full-game-depth",
        "1",
        "--full-game-max-nodes",
        "200000",
        "--selector",
        str(args.selector),
        "--search-teacher-runner",
        str(args.search_teacher_runner),
        "--search-generator",
        str(args.search_generator),
        "--exact-generator",
        str(args.exact_generator),
        "--dataset-exe",
        str(args.dataset_exe),
        "--trainer",
        str(args.trainer),
        "--exporter",
        str(args.exporter),
        "--catalog-dump-exe",
        str(args.catalog_dump),
        "--ranking-evaluator",
        str(args.ranking_evaluator),
        "--late-arena-runner",
        str(args.late_arena_runner),
        "--late-arena-exe",
        str(args.late_arena_exe),
        "--full-arena-exe",
        str(args.full_arena_exe),
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--importer", required=True, type=Path)
    parser.add_argument("--replay-helper", required=True, type=Path)
    parser.add_argument("--sequence-fixture", required=True, type=Path)
    parser.add_argument("--sequence-manifest", required=True, type=Path)
    parser.add_argument("--openings", required=True, type=Path)
    parser.add_argument("--selector", required=True, type=Path)
    parser.add_argument("--search-teacher-runner", required=True, type=Path)
    parser.add_argument("--search-generator", required=True, type=Path)
    parser.add_argument("--exact-generator", required=True, type=Path)
    parser.add_argument("--dataset-exe", required=True, type=Path)
    parser.add_argument("--trainer", required=True, type=Path)
    parser.add_argument("--exporter", required=True, type=Path)
    parser.add_argument("--catalog-dump", required=True, type=Path)
    parser.add_argument("--ranking-evaluator", required=True, type=Path)
    parser.add_argument("--late-arena-runner", required=True, type=Path)
    parser.add_argument("--late-arena-exe", required=True, type=Path)
    parser.add_argument("--full-arena-exe", required=True, type=Path)
    args = parser.parse_args()
    try:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            normalized = root / "normalized.tsv"
            make_normalized(args, normalized, root / "import-report.json")
            weights, manifest = make_full_coverage_artifact(args, root)
            cycle = root / "cycle"
            command = runner_command(args, normalized, cycle, weights, manifest)
            run(command)
            report = json.loads((cycle / "full-phase-growth-cycle-report.json").read_text(encoding="utf-8"))
            run_data = report["runs"][0]
            expected_stages = {
                "phase_selection", "midgame_search_teacher", "late_game_exact_teacher",
                "teacher_merge_validation", "dataset_build", "rank_train", "export",
                "offline_ranking", "late_game_arena", "full_game_arena", "decision_summary",
            }
            if report.get("status") != "ok" or set(run_data["stages"]) != expected_stages:
                raise RuntimeError(f"full-phase cycle did not complete every stage: {report!r}")
            decision = json.loads(
                (cycle / "runs" / "seed-0" / "decision_summary" / "decision-report.json").read_text(
                    encoding="utf-8"
                )
            )
            full_game = decision.get("full_game_arena", {})
            if full_game.get("candidate_vs_baseline_score_rate") is None:
                raise RuntimeError(f"full-game score rate was not read from the arena report: {decision!r}")
            if decision.get("same_artifact_sanity") is not True or decision.get("swap_sanity") is not True:
                raise RuntimeError(f"full-game sanity checks were not retained: {decision!r}")
            if decision.get("suggested_decision", {}).get("category") == "invalid_run":
                raise RuntimeError(f"valid tiny fixture produced invalid decision: {decision!r}")
            run(command + ["--resume"])
            mismatch = run(command + ["--resume", "--search-max-depth", "2"], expect_success=False)
            if mismatch.returncode == 0 or "resume metadata mismatch" not in mismatch.stderr:
                raise RuntimeError(f"resume config mismatch was not rejected: {mismatch.stderr!r}")
            dry = root / "dry-run"
            run(runner_command(args, normalized, dry, weights, manifest) + ["--dry-run"])
            planned = json.loads((dry / "full-phase-growth-cycle-report.json").read_text(encoding="utf-8"))
            if planned.get("status") != "planned":
                raise RuntimeError(f"dry run did not report planned status: {planned!r}")
            undertrained = run(
                runner_command(args, normalized, root / "undertrained", weights, manifest)
                + ["--roots-per-phase", "1"],
                expect_success=False,
            )
            if undertrained.returncode == 0 or "full-phase candidate requires trainer trained_phases" not in undertrained.stderr:
                raise RuntimeError(f"partial trainer coverage was exported: {undertrained.stderr!r}")
            validation_only = root / "validation-only-phase.tsv"
            make_validation_only_phase(normalized, validation_only, 12)
            missing_train = run(
                runner_command(args, validation_only, root / "missing-train", weights, manifest),
                expect_success=False,
            )
            if missing_train.returncode == 0 or "phase selection has no train roots for phase(s): 12" not in missing_train.stderr:
                raise RuntimeError(f"validation-only phase was not rejected before export: {missing_train.stderr!r}")
    except (OSError, RuntimeError, StopIteration, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
