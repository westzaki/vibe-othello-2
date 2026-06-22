#!/usr/bin/env python3
"""Smoke checks for the pattern-learning growth-cycle runner."""

from __future__ import annotations

import argparse
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--growth-runner", required=True, type=Path)
    return parser.parse_args()


def load_runner(path: Path) -> Any:
    spec = importlib.util.spec_from_file_location("pattern_growth_cycle", path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"cannot import runner: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def synthetic_run(
    root_count: int,
    seed: int,
    *,
    top1_delta: float,
    top2_delta: float,
    pairwise_delta: float,
    regret_delta: float,
    heldout_top2_delta: float,
    heldout_pairwise_delta: float,
    heldout_regret_delta: float,
    all_same_ratio: float = 0.0,
    static_score_width: float = 20.0,
) -> dict[str, Any]:
    return {
        "run_id": f"roots-{root_count}-seed-{seed}",
        "root_count": root_count,
        "seed": seed,
        "status": "ok",
        "trained": {
            "root_count": root_count,
            "roots_with_all_moves_same_predicted_score": int(root_count * all_same_ratio),
            "static_score_range": {"min": 0.0, "max": static_score_width},
        },
        "deltas": {
            "top1_accuracy": top1_delta,
            "best_move_in_top2_rate": top2_delta,
            "pairwise_accuracy": pairwise_delta,
            "mean_teacher_regret": regret_delta,
            "roots_with_all_moves_same_predicted_score": -1,
        },
        "heldout_validation_test": {
            "deltas": {
                "best_move_in_top2_rate": heldout_top2_delta,
                "pairwise_accuracy": heldout_pairwise_delta,
                "mean_teacher_regret": heldout_regret_delta,
            }
        },
    }


def synthetic_matrix(runs: list[dict[str, Any]]) -> dict[str, Any]:
    return {"schema_version": 1, "runs": runs}


def arena_result(comparison: str, rate: float, *, diff: float = 1.0, failed: int = 0) -> dict[str, Any]:
    return {
        "status": "ok",
        "comparison": comparison,
        "candidate_score_rate": rate,
        "average_disc_diff_candidate_perspective": diff,
        "illegal_or_failed_games": failed,
        "games_played": 2000,
    }


def decide(module: Any, matrix: dict[str, Any], arenas: list[dict[str, Any]]) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
    decision = module.summarize_decision_leverage(matrix)
    arena = module.summarize_arena_validation(arenas, [])
    scorecard = module.promotion_decision(decision, arena)
    return decision, arena, scorecard


def robust_positive_runs() -> list[dict[str, Any]]:
    return [
        synthetic_run(
            root_count,
            seed,
            top1_delta=0.04 + root_count / 1_000_000,
            top2_delta=0.05,
            pairwise_delta=0.07,
            regret_delta=-0.8,
            heldout_top2_delta=0.03,
            heldout_pairwise_delta=0.04,
            heldout_regret_delta=-0.4,
        )
        for root_count in (5000, 10000, 20000)
        for seed in (0, 1, 2)
    ]


def supportive_arenas() -> list[dict[str, Any]]:
    return [
        arena_result("same_artifact_sanity", 0.5, diff=0.0),
        arena_result("move_teacher_v2_vs_v1", 0.523),
        arena_result("move_teacher_v2_vs_v1", 0.528),
        arena_result("move_teacher_v2_vs_exact_root_v2", 0.501),
    ]


def check_robust_positive(module: Any) -> bool:
    decision, arena, scorecard = decide(module, synthetic_matrix(robust_positive_runs()), supportive_arenas())
    if not decision.get("gate_passed") or not arena.get("gate_passed"):
        print(f"positive gates did not pass: {decision} {arena}", file=sys.stderr)
        return False
    if scorecard.get("category") != "promote_to_larger_local_validation":
        print(f"positive scorecard mismatch: {scorecard}", file=sys.stderr)
        return False
    return True


def check_neutral_arena_hold(module: Any) -> bool:
    arenas = [
        arena_result("same_artifact_sanity", 0.5, diff=0.0),
        arena_result("move_teacher_v2_vs_v1", 0.5, diff=0.0),
        arena_result("move_teacher_v2_vs_exact_root_v2", 0.5, diff=0.0),
    ]
    _decision, _arena, scorecard = decide(module, synthetic_matrix(robust_positive_runs()), arenas)
    if scorecard.get("category") != "hold_for_more_data":
        print(f"neutral arena should hold: {scorecard}", file=sys.stderr)
        return False
    return True


def check_single_seed_hold(module: Any) -> bool:
    runs = [
        synthetic_run(
            50000,
            0,
            top1_delta=0.04,
            top2_delta=0.03,
            pairwise_delta=0.04,
            regret_delta=-0.5,
            heldout_top2_delta=0.02,
            heldout_pairwise_delta=0.02,
            heldout_regret_delta=-0.3,
        )
    ]
    decision, arena, scorecard = decide(module, synthetic_matrix(runs), supportive_arenas())
    if not decision.get("gate_passed") or not arena.get("gate_passed"):
        print(f"single seed should still pass gates: {decision} {arena}", file=sys.stderr)
        return False
    if decision.get("stable_across_root_counts_and_seeds"):
        print(f"single seed should not count as stable: {decision}", file=sys.stderr)
        return False
    if scorecard.get("category") != "hold_for_more_data":
        print(f"single seed should hold for more data: {scorecard}", file=sys.stderr)
        return False
    return True


def check_negative_pairwise(module: Any) -> bool:
    runs = [
        synthetic_run(
            5000,
            seed,
            top1_delta=-0.01,
            top2_delta=-0.01,
            pairwise_delta=-0.02,
            regret_delta=0.5,
            heldout_top2_delta=-0.01,
            heldout_pairwise_delta=-0.02,
            heldout_regret_delta=0.5,
        )
        for seed in (0, 1, 2)
    ]
    _decision, _arena, scorecard = decide(module, synthetic_matrix(runs), supportive_arenas())
    if scorecard.get("category") not in {"needs_rank_objective", "negative"}:
        print(f"negative pairwise category mismatch: {scorecard}", file=sys.stderr)
        return False
    return True


def check_all_same_rank_need(module: Any) -> bool:
    runs = [
        synthetic_run(
            5000,
            seed,
            top1_delta=0.0,
            top2_delta=0.0,
            pairwise_delta=0.0,
            regret_delta=0.2,
            heldout_top2_delta=0.0,
            heldout_pairwise_delta=0.0,
            heldout_regret_delta=0.2,
            all_same_ratio=0.25,
            static_score_width=1.0,
        )
        for seed in (0, 1, 2)
    ]
    _decision, _arena, scorecard = decide(module, synthetic_matrix(runs), supportive_arenas())
    if scorecard.get("category") != "needs_rank_objective":
        print(f"high all-same category mismatch: {scorecard}", file=sys.stderr)
        return False
    return True


def check_swap_sanity_variant_identity(module: Any) -> bool:
    seed0 = {
        "comparison": "candidate_baseline_swap_sanity",
        "candidate": None,
        "candidate_name": "pattern-v1-buro-lite",
        "swap_of": "roots-100000-seed-0",
        "depth": 3,
        "arena_seed": 0,
    }
    seed1 = dict(seed0, swap_of="roots-100000-seed-1")
    seed0_key = module.variant_key(
        seed0["comparison"],
        module.arena_variant_identity(seed0),
        "depth",
        seed0["depth"],
        "arena-seed",
        seed0["arena_seed"],
    )
    seed1_key = module.variant_key(
        seed1["comparison"],
        module.arena_variant_identity(seed1),
        "depth",
        seed1["depth"],
        "arena-seed",
        seed1["arena_seed"],
    )
    if seed0_key == seed1_key:
        print(f"swap sanity keys should differ: {seed0_key}", file=sys.stderr)
        return False
    if "roots-100000-seed-0" not in seed0_key or "roots-100000-seed-1" not in seed1_key:
        print(f"swap sanity keys should include swap_of run ids: {seed0_key} {seed1_key}", file=sys.stderr)
        return False
    return True


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_minimal_normalized(path: Path) -> None:
    path.write_text("board_id\tempty_count\nsynthetic-board\t1\n", encoding="utf-8")


def write_fake_matrix_helper(path: Path) -> None:
    path.write_text(
        "\n".join(
            [
                "#!/usr/bin/env python3",
                "from __future__ import annotations",
                "import argparse",
                "import json",
                "from pathlib import Path",
                "parser = argparse.ArgumentParser()",
                "parser.add_argument('--output-dir', required=True, type=Path)",
                "parser.add_argument('--root-counts')",
                "parser.add_argument('--seeds')",
                "parser.add_argument('--resume', action='store_true')",
                "parser.add_argument('rest', nargs='*')",
                "args, _unknown = parser.parse_known_args()",
                "if not args.resume:",
                "    raise SystemExit('expected --resume')",
                "args.output_dir.mkdir(parents=True, exist_ok=True)",
                "(args.output_dir / 'helper-called.txt').write_text('called\\n', encoding='utf-8')",
                "report = {",
                "    'schema_version': 1,",
                "    'root_counts': [int(item) for item in args.root_counts.split(',') if item],",
                "    'seeds': [int(item) for item in args.seeds.split(',') if item],",
                "    'runs': [],",
                "    'helper_resume_marker': True,",
                "}",
                "(args.output_dir / 'matrix-report.json').write_text(",
                "    json.dumps(report, indent=2, sort_keys=True) + '\\n', encoding='utf-8'",
                ")",
                "(args.output_dir / 'matrix-summary.md').write_text('# fake matrix\\n', encoding='utf-8')",
            ]
        )
        + "\n",
        encoding="utf-8",
    )


def run_capture(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, capture_output=True, text=True)


def check_missing_input(args: argparse.Namespace, root: Path) -> bool:
    output_dir = root / "missing-output"
    result = run_capture(
        [
            sys.executable,
            str(args.growth_runner),
            "--normalized-tsv",
            str(root / "missing-normalized.tsv"),
            "--output-dir",
            str(output_dir),
            "--root-counts",
            "1",
            "--seeds",
            "0",
            "--skip-arenas",
            "--created-at-utc",
            "2026-01-02T03:04:05Z",
        ]
    )
    if result.returncode == 0:
        print("missing input unexpectedly succeeded", file=sys.stderr)
        return False
    report = load_json(output_dir / "growth-cycle-report.json")
    missing = report.get("stages", {}).get("preflight", {}).get("required_inputs_missing")
    if not isinstance(missing, list) or not any(item.get("input") == "normalized-tsv" for item in missing):
        print(f"missing input report did not name normalized TSV: {report}", file=sys.stderr)
        return False
    if "python3 tools/pattern/train/run_pattern_growth_cycle.py" not in report["stages"]["preflight"].get(
        "command_template", ""
    ):
        print("missing input report lacks resume command template", file=sys.stderr)
        return False
    if not (output_dir / "growth-cycle-summary.md").exists():
        print("missing input did not write summary", file=sys.stderr)
        return False
    return True


def check_dry_run(args: argparse.Namespace, root: Path) -> bool:
    normalized = root / "normalized.tsv"
    write_minimal_normalized(normalized)
    output_dir = root / "dry-run-output"
    result = run_capture(
        [
            sys.executable,
            str(args.growth_runner),
            "--normalized-tsv",
            str(normalized),
            "--output-dir",
            str(output_dir),
            "--root-counts",
            "1",
            "--seeds",
            "0",
            "--skip-arenas",
            "--dry-run",
            "--created-at-utc",
            "2026-01-02T03:04:05Z",
        ]
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return False
    report = load_json(output_dir / "growth-cycle-report.json")
    required = {
        "schema_version",
        "stages",
        "baseline_inventory",
        "decision_leverage",
        "arena_validation",
        "promotion_scorecard",
        "next_action",
        "warnings",
    }
    missing = sorted(required - set(report))
    if missing:
        print(f"dry-run report missing fields {missing}: {report}", file=sys.stderr)
        return False
    if report.get("status") != "planned":
        print(f"dry-run status mismatch: {report.get('status')}", file=sys.stderr)
        return False
    if report["stages"]["decision_leverage_matrix"].get("status") != "planned":
        print(f"dry-run matrix stage not planned: {report['stages']}", file=sys.stderr)
        return False
    if not (output_dir / "growth-cycle-summary.md").exists():
        print("dry-run did not write summary", file=sys.stderr)
        return False
    return True


def check_resume_invokes_matrix_helper(args: argparse.Namespace, root: Path) -> bool:
    normalized = root / "resume-normalized.tsv"
    write_minimal_normalized(normalized)
    fake_helper = root / "fake-matrix-helper.py"
    write_fake_matrix_helper(fake_helper)
    fake_files = {}
    for name in (
        "generator",
        "dataset",
        "trainer",
        "exporter",
        "ranking",
        "baseline-root.weights.bin",
        "baseline-root.manifest.json",
    ):
        fake_files[name] = root / name
        fake_files[name].write_text("fake\n", encoding="utf-8")

    output_dir = root / "resume-output"
    matrix_dir = output_dir / "decision-leverage-matrix"
    matrix_dir.mkdir(parents=True)
    (matrix_dir / "matrix-report.json").write_text(
        json.dumps({"schema_version": 1, "runs": [], "stale": True}, indent=2) + "\n",
        encoding="utf-8",
    )

    result = run_capture(
        [
            sys.executable,
            str(args.growth_runner),
            "--normalized-tsv",
            str(normalized),
            "--output-dir",
            str(output_dir),
            "--root-counts",
            "1",
            "--seeds",
            "0",
            "--matrix-helper",
            str(fake_helper),
            "--generator",
            str(fake_files["generator"]),
            "--dataset-exe",
            str(fake_files["dataset"]),
            "--trainer",
            str(fake_files["trainer"]),
            "--exporter",
            str(fake_files["exporter"]),
            "--ranking-evaluator",
            str(fake_files["ranking"]),
            "--baseline-root-label-weights",
            str(fake_files["baseline-root.weights.bin"]),
            "--baseline-root-label-manifest",
            str(fake_files["baseline-root.manifest.json"]),
            "--skip-arenas",
            "--resume",
            "--created-at-utc",
            "2026-01-02T03:04:05Z",
        ]
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return False
    if not (matrix_dir / "helper-called.txt").exists():
        print("resume with existing matrix report did not invoke matrix helper", file=sys.stderr)
        return False
    report = load_json(output_dir / "growth-cycle-report.json")
    stage = report.get("stages", {}).get("decision_leverage_matrix", {})
    if stage.get("status") != "resume-validated-by-matrix-helper":
        print(f"resume matrix stage status mismatch: {stage}", file=sys.stderr)
        return False
    matrix = report.get("decision_leverage", {}).get("matrix_report", {})
    if matrix.get("helper_resume_marker") is not True or matrix.get("stale") is not None:
        print(f"growth cycle reused stale matrix report instead of helper output: {matrix}", file=sys.stderr)
        return False
    return True


def main() -> int:
    args = parse_args()
    module = load_runner(args.growth_runner)
    checks = (
        check_robust_positive(module),
        check_neutral_arena_hold(module),
        check_single_seed_hold(module),
        check_negative_pairwise(module),
        check_all_same_rank_need(module),
        check_swap_sanity_variant_identity(module),
    )
    if not all(checks):
        return 1
    with tempfile.TemporaryDirectory(prefix="pattern-growth-cycle-smoke-") as temp:
        root = Path(temp)
        if not check_missing_input(args, root):
            return 1
        if not check_dry_run(args, root):
            return 1
        if not check_resume_invokes_matrix_helper(args, root):
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
