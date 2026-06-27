#!/usr/bin/env python3
"""Smoke checks for the pattern artifact arena matrix helper."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
import textwrap
import zlib
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper", required=True, type=Path)
    return parser.parse_args()


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, capture_output=True, text=True)


def require_success(completed: subprocess.CompletedProcess[str]) -> None:
    if completed.returncode != 0:
        raise AssertionError(
            f"command failed with {completed.returncode}: {completed.args}\n"
            f"stdout:\n{completed.stdout}\n"
            f"stderr:\n{completed.stderr}"
        )


def require_failure(completed: subprocess.CompletedProcess[str], expected: str) -> None:
    if completed.returncode == 0:
        raise AssertionError(f"command unexpectedly succeeded: {completed.args}")
    if expected not in completed.stderr:
        raise AssertionError(
            f"stderr did not contain {expected!r}: {completed.args}\n{completed.stderr}"
        )


def load_json(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise AssertionError(f"JSON root is not an object: {path}")
    return data


def make_positions(path: Path, suffix: str = "") -> None:
    path.write_text(f"board_id\tempty_count\nboard-a{suffix}\t1\n", encoding="utf-8")


def make_artifact(root: Path, name: str, pattern_set: str) -> tuple[Path, Path]:
    weights = root / f"{name}.weights.bin"
    manifest = root / f"{name}.manifest.json"
    payload_without_footer = f"{name}:{pattern_set}:local-smoke".encode("utf-8")
    checksum = f"0x{zlib.crc32(payload_without_footer) & 0xFFFFFFFF:08x}"
    weights.write_bytes(payload_without_footer + b"\0\0\0\0")
    manifest.write_text(
        json.dumps(
            {
                "format_version": 1,
                "bit_order": "a1-lsb",
                "score_unit": "disc-diff",
                "score_scale": 1,
                "phase_count": 13,
                "pattern_set_id": pattern_set,
                "weights_file": weights.name,
                "weights_checksum": checksum,
                "notes": "local matrix smoke artifact; not production",
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    return weights, manifest


def fake_rate(candidate_weights: str, baseline_weights: str) -> tuple[float, float]:
    candidate = Path(candidate_weights).name
    baseline = Path(baseline_weights).name
    if candidate == baseline:
        return 0.5, 0.0
    if candidate.startswith("move") and baseline.startswith("exact"):
        return 0.51, 0.4
    if candidate.startswith("exact") and baseline.startswith("move"):
        return 0.49, -0.4
    if candidate.startswith("move") and baseline.startswith("v1"):
        return 0.54, 2.0
    if candidate.startswith("v1") and baseline.startswith("move"):
        return 0.46, -2.0
    if candidate.startswith("exact") and baseline.startswith("v1"):
        return 0.53, 1.5
    if candidate.startswith("v1") and baseline.startswith("exact"):
        return 0.47, -1.5
    return 0.5, 0.0


def write_fake_arena(path: Path) -> None:
    path.write_text(
        textwrap.dedent(
            """\
            #!/usr/bin/env python3
            import argparse
            import json
            from pathlib import Path

            parser = argparse.ArgumentParser()
            parser.add_argument("--positions-tsv", required=True)
            parser.add_argument("--candidate-weights", required=True)
            parser.add_argument("--candidate-manifest", required=True)
            parser.add_argument("--candidate-name", required=True)
            parser.add_argument("--baseline-weights", required=True)
            parser.add_argument("--baseline-manifest", required=True)
            parser.add_argument("--baseline-name", required=True)
            parser.add_argument("--max-empty")
            parser.add_argument("--max-positions", type=int, required=True)
            parser.add_argument("--seed", type=int, required=True)
            parser.add_argument("--side-swap", action="store_true")
            parser.add_argument("--depth", type=int, required=True)
            parser.add_argument("--report-out", required=True)
            parser.add_argument("--summary-out", required=True)
            parser.add_argument("--progress-every")
            args = parser.parse_args()

            def fake_rate(candidate_weights, baseline_weights):
                candidate = Path(candidate_weights).name
                baseline = Path(baseline_weights).name
                if candidate == baseline:
                    return 0.5, 0.0
                if candidate.startswith("move") and baseline.startswith("exact"):
                    return 0.51, 0.4
                if candidate.startswith("exact") and baseline.startswith("move"):
                    return 0.49, -0.4
                if candidate.startswith("move") and baseline.startswith("v1"):
                    return 0.54, 2.0
                if candidate.startswith("v1") and baseline.startswith("move"):
                    return 0.46, -2.0
                if candidate.startswith("exact") and baseline.startswith("v1"):
                    return 0.53, 1.5
                if candidate.startswith("v1") and baseline.startswith("exact"):
                    return 0.47, -1.5
                return 0.5, 0.0

            rate, diff = fake_rate(args.candidate_weights, args.baseline_weights)
            games = args.max_positions * (2 if args.side_swap else 1)
            candidate_score = round(games * rate)
            candidate_wins = candidate_score
            baseline_wins = games - candidate_score
            report = {
                "selected_positions": args.max_positions,
                "games_played": games,
                "candidate_wins": candidate_wins,
                "baseline_wins": baseline_wins,
                "draws": 0,
                "candidate_score": candidate_score,
                "candidate_score_rate": candidate_score / games,
                "average_disc_diff_candidate_perspective": diff,
                "illegal_or_failed_games": 0,
            }
            Path(args.report_out).parent.mkdir(parents=True, exist_ok=True)
            Path(args.report_out).write_text(json.dumps(report, indent=2, sort_keys=True) + "\\n", encoding="utf-8")
            Path(args.summary_out).write_text("# fake arena\\n", encoding="utf-8")
            """
        ),
        encoding="utf-8",
    )
    path.chmod(0o755)


def base_command(
    helper: Path,
    fake_arena: Path,
    positions: Path,
    output_dir: Path,
    move: tuple[Path, Path],
    exact: tuple[Path, Path],
    v1: tuple[Path, Path],
    *extra: str,
) -> list[str]:
    move_pair = {
        "comparison": "move_teacher_v2_vs_v1",
        "candidate_weights": str(move[0]),
        "candidate_manifest": str(move[1]),
        "candidate_name": "move-teacher-v2-100k-seed0",
        "baseline_weights": str(v1[0]),
        "baseline_manifest": str(v1[1]),
        "baseline_name": "v1-exact-teacher",
    }
    exact_pair = {
        "comparison": "exact_root_v2_vs_v1",
        "candidate_weights": str(exact[0]),
        "candidate_manifest": str(exact[1]),
        "candidate_name": "exact-root-v2-100k",
        "baseline_weights": str(v1[0]),
        "baseline_manifest": str(v1[1]),
        "baseline_name": "v1-exact-teacher",
    }
    return [
        sys.executable,
        str(helper),
        "--positions-tsv",
        str(positions),
        "--output-dir",
        str(output_dir),
        "--comparison-name",
        "move_teacher_v2_vs_exact_root_v2",
        "--candidate-weights",
        str(move[0]),
        "--candidate-manifest",
        str(move[1]),
        "--candidate-name",
        "move-teacher-v2-100k-seed0",
        "--baseline-weights",
        str(exact[0]),
        "--baseline-manifest",
        str(exact[1]),
        "--baseline-name",
        "exact-root-v2-100k",
        "--pair-json",
        json.dumps(move_pair, sort_keys=True),
        "--pair-json",
        json.dumps(exact_pair, sort_keys=True),
        "--depths",
        "3,7",
        "--seeds",
        "0,10",
        "--max-positions",
        "100",
        "--side-swap",
        "--same-artifact-sanity",
        "candidate",
        "--swap-sanity",
        "primary",
        "--arena-exe",
        str(fake_arena),
        *extra,
    ]


def check_matrix_report(report: dict[str, Any]) -> None:
    if report.get("completed_run_count") != 20:
        raise AssertionError(f"unexpected completed count: {report.get('completed_run_count')}")
    if report.get("total_games") != 4000:
        raise AssertionError(f"unexpected total games: {report.get('total_games')}")
    if report.get("total_failed_games") != 0:
        raise AssertionError(f"unexpected failed games: {report.get('total_failed_games')}")
    if report.get("local_assessment", {}).get("category") != "local_harness_passed":
        raise AssertionError(f"unexpected local assessment: {report.get('local_assessment')}")
    if "recommendation" in report or "allowed_recommendations" in report or "allowed_local_assessments" in report:
        raise AssertionError(f"matrix report should not use ambiguous recommendation keys: {report}")
    if report.get("allowed_overall_local_assessments") != [
        "local_harness_passed",
        "local_harness_needs_more_runs",
        "local_harness_failed",
    ]:
        raise AssertionError(
            f"overall assessment enum mismatch: {report.get('allowed_overall_local_assessments')}"
        )
    allowed_group = set(report.get("allowed_group_local_assessments", []))
    if report.get("same_artifact_sanity", {}).get("passed") is not True:
        raise AssertionError(f"same-artifact sanity failed: {report.get('same_artifact_sanity')}")
    if report.get("swap_sanity", {}).get("passed") is not True:
        raise AssertionError(f"swap sanity failed: {report.get('swap_sanity')}")
    by_comparison = report.get("by_comparison", {})
    move_exact = by_comparison.get("move_teacher_v2_vs_exact_root_v2", {})
    if move_exact.get("run_count") != 4 or move_exact.get("non_negative_run_count") != 4:
        raise AssertionError(f"move vs exact aggregate mismatch: {move_exact}")
    if move_exact.get("total_games") != 800:
        raise AssertionError(f"move vs exact games mismatch: {move_exact}")
    if abs(float(move_exact.get("mean_score_rate")) - 0.51) > 1.0e-12:
        raise AssertionError(f"move vs exact mean score mismatch: {move_exact}")
    if move_exact.get("local_assessment") != "local_signal_non_negative_or_supportive":
        raise AssertionError(f"move vs exact local assessment mismatch: {move_exact}")
    for data in by_comparison.values():
        if data.get("local_assessment") not in allowed_group:
            raise AssertionError(f"group assessment is not declared in enum: {data}")


def check_success_and_resume(args: argparse.Namespace, root: Path) -> None:
    positions = root / "positions.tsv"
    make_positions(positions)
    fake_arena = root / "fake_arena.py"
    write_fake_arena(fake_arena)
    move = make_artifact(root, "move", "pattern-v2-endgame-lite")
    exact = make_artifact(root, "exact", "pattern-v2-endgame-lite")
    v1 = make_artifact(root, "v1", "pattern-v1-buro-lite")
    output_dir = root / "matrix"

    require_success(run(base_command(args.helper, fake_arena, positions, output_dir, move, exact, v1)))
    report = load_json(output_dir / "arena-matrix-report.json")
    check_matrix_report(report)

    require_success(run(base_command(args.helper, fake_arena, positions, output_dir, move, exact, v1, "--resume")))
    resume_report = load_json(output_dir / "arena-matrix-report.json")
    statuses = {result.get("status") for result in resume_report.get("results", [])}
    if statuses != {"skipped-resume-validated"}:
        raise AssertionError(f"resume did not validate-skip all runs: {statuses}")

    make_positions(positions, "-changed")
    require_failure(
        run(base_command(args.helper, fake_arena, positions, output_dir, move, exact, v1, "--resume")),
        "arena resume metadata mismatch",
    )


def check_dry_run(args: argparse.Namespace, root: Path) -> None:
    positions = root / "dry-positions.tsv"
    make_positions(positions)
    fake_arena = root / "missing-fake-arena.py"
    move = make_artifact(root, "dry-move", "pattern-v2-endgame-lite")
    exact = make_artifact(root, "dry-exact", "pattern-v2-endgame-lite")
    v1 = make_artifact(root, "dry-v1", "pattern-v1-buro-lite")
    output_dir = root / "dry-run"
    require_success(
        run(base_command(args.helper, fake_arena, positions, output_dir, move, exact, v1, "--dry-run"))
    )
    report = load_json(output_dir / "arena-matrix-report.json")
    if report.get("status") != "planned":
        raise AssertionError(f"dry run report status mismatch: {report.get('status')}")
    if {result.get("status") for result in report.get("results", [])} != {"planned"}:
        raise AssertionError(f"dry run did not produce planned results: {report.get('results')}")


def check_missing_inputs(args: argparse.Namespace, root: Path) -> None:
    positions = root / "missing-positions.tsv"
    make_positions(positions)
    fake_arena = root / "fake_arena.py"
    write_fake_arena(fake_arena)
    move = make_artifact(root, "missing-move", "pattern-v2-endgame-lite")
    exact = make_artifact(root, "missing-exact", "pattern-v2-endgame-lite")
    v1 = make_artifact(root, "missing-v1", "pattern-v1-buro-lite")

    missing_candidate = (root / "does-not-exist.weights.bin", move[1])
    require_failure(
        run(base_command(args.helper, fake_arena, positions, root / "missing-candidate", missing_candidate, exact, v1)),
        "missing candidate weights",
    )

    missing_baseline = (root / "missing-baseline.weights.bin", exact[1])
    require_failure(
        run(base_command(args.helper, fake_arena, positions, root / "missing-baseline", move, missing_baseline, v1)),
        "missing baseline weights",
    )

    bad_manifest = root / "bad.manifest.json"
    bad_manifest.write_text(
        json.dumps(
            {
                "pattern_set_id": "pattern-v2-endgame-lite",
                "weights_checksum": "0x00000000",
            }
        )
        + "\n",
        encoding="utf-8",
    )
    require_failure(
        run(base_command(args.helper, fake_arena, positions, root / "bad-manifest", (move[0], bad_manifest), exact, v1)),
        "artifact checksum mismatch",
    )


def main() -> int:
    args = parse_args()
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        check_dry_run(args, root)
        check_success_and_resume(args, root)
        check_missing_inputs(args, root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
