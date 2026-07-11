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


def make_collision_reporting_selector(selector: Path, output: Path) -> None:
    selector_literal = json.dumps(str(selector))
    output.write_text(
        "\n".join(
            [
                "#!/usr/bin/env python3",
                "import json",
                "import subprocess",
                "import sys",
                f"selector = {selector_literal}",
                "returncode = subprocess.call([sys.executable, selector, *sys.argv[1:]])",
                "if returncode != 0:",
                "    raise SystemExit(returncode)",
                "report_path = sys.argv[sys.argv.index('--report-out') + 1]",
                "with open(report_path, encoding='utf-8') as handle:",
                "    report = json.load(handle)",
                "report['selected_cross_split_game_group_collision_count'] = 1",
                "with open(report_path, 'w', encoding='utf-8') as handle:",
                "    json.dump(report, handle, indent=2, sort_keys=True)",
                "    handle.write('\\n')",
            ]
        )
        + "\n",
        encoding="utf-8",
    )


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
        "--phase-quota",
        "0=1",
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
            preflight = report.get("preflight", {})
            if (
                preflight.get("requested_roots") != 25
                or preflight.get("phase_quota_overrides") != {"0": 1}
                or preflight.get("phase_quotas", {}).get("0") != 1
                or preflight.get("phase_quotas", {}).get("1") != 2
            ):
                raise RuntimeError(f"full-phase quota override was not retained: {report!r}")
            selection = json.loads(
                (cycle / "runs" / "seed-0" / "phase_selection" / "selection-report.json").read_text(
                    encoding="utf-8"
                )
            )
            if (
                selection.get("requested_rows") != 25
                or selection.get("selected_rows") != 25
                or selection.get("phase_coverage", {}).get("0", {}).get("requested_roots") != 1
                or selection.get("selected_cross_split_board_collision_count") != 0
                or selection.get("selected_cross_split_game_group_collision_count") != 0
            ):
                raise RuntimeError(f"selector did not apply the phase quota override: {selection!r}")
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
            if decision.get("requested_roots") != 25 or decision.get("phase_quota_overrides") != {"0": 1}:
                raise RuntimeError(f"decision report lost phase quota provenance: {decision!r}")
            run(command + ["--resume"])
            mismatch = run(command + ["--resume", "--search-max-depth", "2"], expect_success=False)
            if mismatch.returncode == 0 or "resume metadata mismatch" not in mismatch.stderr:
                raise RuntimeError(f"resume config mismatch was not rejected: {mismatch.stderr!r}")
            dry = root / "dry-run"
            run(runner_command(args, normalized, dry, weights, manifest) + ["--dry-run"])
            planned = json.loads((dry / "full-phase-growth-cycle-report.json").read_text(encoding="utf-8"))
            if planned.get("status") != "planned":
                raise RuntimeError(f"dry run did not report planned status: {planned!r}")
            resplit_dry = root / "dry-run-selected-resplit"
            run(
                runner_command(args, normalized, resplit_dry, weights, manifest)
                + [
                    "--dry-run",
                    "--max-roots-per-game-group",
                    "1",
                    "--selected-split-policy",
                    "game-group-hash",
                    "--train-only-phase",
                    "0",
                ]
            )
            resplit_planned = json.loads(
                (resplit_dry / "full-phase-growth-cycle-report.json").read_text(encoding="utf-8")
            )
            selection_command = resplit_planned["runs"][0]["stages"]["phase_selection"]["config"]["command"]
            if (
                resplit_planned.get("preflight", {}).get("selected_split_policy") != "game-group-hash"
                or resplit_planned.get("preflight", {}).get("train_only_phases") != [0]
                or selection_command[-4:] != [
                    "--selected-split-policy",
                    "game-group-hash",
                    "--train-only-phase",
                    "0",
                ]
            ):
                raise RuntimeError(f"selected split policy was not forwarded: {resplit_planned!r}")
            warm_dry = root / "dry-run-warm-residual"
            initial_weights = root / "teacher.weights.json"
            run(
                runner_command(args, normalized, warm_dry, weights, manifest)
                + [
                    "--dry-run",
                    "--artifact-score-scale",
                    "100",
                    "--fallback-additive-through-phase",
                    "9",
                    "--initial-weights",
                    str(initial_weights),
                    "--initial-trained-phases",
                    "10,11,12",
                    "--freeze-phases",
                    "10,11,12",
                ]
            )
            warm_planned = json.loads(
                (warm_dry / "full-phase-growth-cycle-report.json").read_text(encoding="utf-8")
            )
            warm_preflight = warm_planned.get("preflight", {})
            warm_stages = warm_planned["runs"][0]["stages"]
            train_command = warm_stages["rank_train"]["config"]["command"]
            export_command = warm_stages["export"]["config"]["command"]
            if (
                warm_preflight.get("artifact_score_scale") != 100
                or warm_preflight.get("fallback_additive_through_phase") != 9
                or warm_preflight.get("initial_trained_phases") != [10, 11, 12]
                or warm_preflight.get("freeze_phases") != [10, 11, 12]
                or "--initial-weights" not in train_command
                or train_command.count("--freeze-phase") != 3
                or export_command[export_command.index("--score-scale") + 1] != "100"
                or export_command[
                    export_command.index("--fallback-additive-through-phase") + 1
                ]
                != "9"
            ):
                raise RuntimeError(f"warm residual settings were not forwarded: {warm_planned!r}")

            warm_resume = root / "warm-resume"
            warm_options = [
                "--initial-weights",
                str(initial_weights),
                "--initial-trained-phases",
                "10,11,12",
                "--freeze-phases",
                "10,11,12",
            ]
            warm_resume_command = runner_command(
                args, normalized, warm_resume, weights, manifest
            ) + warm_options
            run(warm_resume_command)
            rank_sidecar = json.loads(
                (
                    warm_resume
                    / "runs"
                    / "seed-0"
                    / "rank_train"
                    / "stage.resume.json"
                ).read_text(encoding="utf-8")
            )
            rank_inputs = rank_sidecar.get("inputs", [])
            initial_fingerprints = [
                item for item in rank_inputs if item.get("role") == "initial_weights"
            ]
            if len(initial_fingerprints) != 1 or not initial_fingerprints[0].get("sha256"):
                raise RuntimeError(f"rank resume metadata omitted initial weights: {rank_sidecar!r}")
            changed_initial = json.loads(initial_weights.read_text(encoding="utf-8"))
            changed_initial["phase_bias"]["0"] = 0.25
            initial_weights.write_text(
                json.dumps(changed_initial, sort_keys=True) + "\n", encoding="utf-8"
            )
            changed_resume = run(warm_resume_command + ["--resume"], expect_success=False)
            if (
                changed_resume.returncode == 0
                or "rank_train resume metadata mismatch" not in changed_resume.stderr
            ):
                raise RuntimeError(
                    "changed warm-start weights did not invalidate resume: "
                    f"{changed_resume.stderr!r}"
                )

            collision_selector = root / "collision-reporting-selector.py"
            make_collision_reporting_selector(args.selector, collision_selector)
            collision_command = runner_command(
                args, normalized, root / "collision-report", weights, manifest
            )
            collision_command[collision_command.index("--selector") + 1] = str(
                collision_selector
            )
            collision_result = run(collision_command, expect_success=False)
            if (
                collision_result.returncode == 0
                or "phase selection report has selected cross-split leakage"
                not in collision_result.stderr
            ):
                raise RuntimeError(
                    "full-phase cycle accepted selected split leakage: "
                    f"{collision_result.stderr!r}"
                )
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
