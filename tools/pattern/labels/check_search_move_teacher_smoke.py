#!/usr/bin/env python3
"""CTest smoke coverage for deterministic artifact-search move-teacher generation."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path


NORMALIZED_HEADER = [
    "record_id", "position_id", "game_group_id", "board_id", "source_occurrence_id",
    "source_dataset_id", "split", "board_a1_to_h8", "label_kind", "label_unit",
    "label_perspective", "label_score_side_to_move", "occupied_count", "phase",
    "player_disc_count", "opponent_disc_count", "empty_count",
]
MOVE_HEADER_V3 = [
    "root_board_id", "root_record_id", "root_split", "root_phase", "root_empty_count",
    "move", "child_board_id", "child_board_a1_to_h8", "child_empty_count", "child_phase",
    "root_move_score_side_to_move", "child_label_score_side_to_move",
    "child_baseline_score_side_to_move", "is_best_move", "best_move_tie_count",
    "move_rank", "best_score_margin", "teacher_kind",
    "teacher_source", "teacher_artifact_id", "teacher_artifact_checksum", "teacher_depth",
    "teacher_nodes", "teacher_search_config_id",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generator", required=True, type=Path)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--ranking-evaluator", required=True, type=Path)
    return parser.parse_args()


def phase_for_occupied(occupied: int) -> int:
    return min(12, ((occupied - 4) * 13) // 60)


def relative_board(serialized: str) -> str:
    board_text, side = serialized.split()
    player_disc = "B" if side == "b" else "W"
    opponent_disc = "W" if side == "b" else "B"
    board = ["-"] * 64
    for rank_from_top, row in enumerate(board_text.split("/")):
        rank = 7 - rank_from_top
        for file, value in enumerate(row):
            index = rank * 8 + file
            board[index] = "X" if value == player_disc else "O" if value == opponent_disc else "-"
    return "".join(board)


def normalized_row(board_id: str, serialized: str, split: str = "train") -> dict[str, str]:
    board = relative_board(serialized)
    player = board.count("X")
    opponent = board.count("O")
    occupied = player + opponent
    return {
        "record_id": f"record-{board_id}", "position_id": f"position-{board_id}",
        "game_group_id": f"game-{board_id}", "board_id": board_id,
        "source_occurrence_id": f"source-{board_id}", "source_dataset_id": "synthetic-search-teacher",
        "split": split, "board_a1_to_h8": board, "label_kind": "observed_final_disc_diff",
        "label_unit": "final_disc_diff", "label_perspective": "side_to_move",
        "label_score_side_to_move": "0", "occupied_count": str(occupied),
        "phase": str(phase_for_occupied(occupied)), "player_disc_count": str(player),
        "opponent_disc_count": str(opponent), "empty_count": str(64 - occupied),
    }


def write_tsv(path: Path, rows: list[dict[str, str]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=NORMALIZED_HEADER, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if reader.fieldnames != MOVE_HEADER_V3:
            raise RuntimeError(f"unexpected move-teacher header: {reader.fieldnames}")
        return list(reader)


def run(command: list[str], expected: int = 0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if result.returncode != expected:
        raise RuntimeError(
            f"command returned {result.returncode}, expected {expected}: {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def make_full_coverage_artifact(source_manifest: Path, source_weights: Path, output_root: Path,
                                name: str, phase_zero_bias: int | None = None) -> tuple[Path, Path]:
    manifest = json.loads(source_manifest.read_text(encoding="utf-8"))
    payload = bytearray(source_weights.read_bytes())
    if payload[:8] != b"VOPWGT\0\0":
        raise RuntimeError("unexpected source artifact magic")
    _format, _bit_order, _unit, _scale, phase_count, _patterns, name_length, _reserved, weight_count = (
        struct.unpack_from("<HHHHHHHHI", payload, 8)
    )
    if phase_count != 13 or weight_count % phase_count != 0:
        raise RuntimeError("unexpected source artifact phase layout")
    if phase_zero_bias is not None:
        weights_offset = 8 + struct.calcsize("<HHHHHHHHI") + name_length
        struct.pack_into("<i", payload, weights_offset, phase_zero_bias)
    checksum = zlib.crc32(payload[:-4]) & 0xFFFFFFFF
    struct.pack_into("<I", payload, len(payload) - 4, checksum)
    weights = output_root / f"{name}.weights.bin"
    weights.write_bytes(payload)
    manifest["weights_file"] = weights.name
    manifest["weights_checksum"] = f"0x{checksum:08x}"
    manifest["trained_phases"] = list(range(13))
    manifest_path = output_root / f"{name}.manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return weights, manifest_path


def core_command(generator: Path, normalized: Path, manifest: Path, weights: Path, output: Path,
                 *, depth: int = 1, nodes: int = 0, time_ms: int = 0, exact: int = 0,
                 min_phase: int = 0, max_phase: int = 9,
                 coverage_policy: str = "require-all") -> list[str]:
    return [
        str(generator), "--normalized-tsv", str(normalized), "--teacher-manifest", str(manifest),
        "--teacher-weights", str(weights), "--move-teacher-out", str(output / "moves.tsv"),
        "--child-normalized-out", str(output / "children.tsv"), "--report-out", str(output / "report.json"),
        "--max-depth", str(depth), "--max-nodes", str(nodes), "--max-time-ms", str(time_ms),
        "--search-preset", "basic", "--teacher-coverage-policy", coverage_policy,
        "--exact-endgame-empties", str(exact), "--min-phase",
        str(min_phase), "--max-phase", str(max_phase),
    ]


def main() -> int:
    args = parse_args()
    repo = Path(__file__).resolve().parents[3]
    default_manifest = repo / "data/eval/artifacts/pattern-v2-endgame-lite-100k-mt-v0/manifest.json"
    default_weights = repo / "data/eval/artifacts/pattern-v2-endgame-lite-100k-mt-v0/weights.bin"
    initial = "......../......../......../...WB.../...BW.../......../......../........ b"
    asymmetric_early = "......../......../..W...../...WBB../.WBWWB../.BW...B./......../........ b"
    one_empty_move = "BBBBBBW./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b"
    one_empty_pass = "BBBBBWB./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b"
    try:
        with tempfile.TemporaryDirectory(prefix="vibe-othello-search-teacher-") as temp:
            root = Path(temp)
            weights, manifest = make_full_coverage_artifact(
                default_manifest, default_weights, root, "teacher"
            )

            early = root / "early.tsv"
            write_tsv(early, [normalized_row("initial", initial)])
            first = root / "first"
            second = root / "second"
            first.mkdir()
            second.mkdir()
            run(core_command(args.generator, early, manifest, weights, first))
            run(core_command(args.generator, early, manifest, weights, second))
            if (first / "moves.tsv").read_bytes() != (second / "moves.tsv").read_bytes():
                raise RuntimeError("same input/config did not produce deterministic teacher TSV")
            rows = read_tsv(first / "moves.tsv")
            if len(rows) != 4 or {row["move_rank"] for row in rows} != {"1", "2", "3", "4"}:
                raise RuntimeError(f"initial root legal-move completeness/rank failed: {rows}")
            for row in rows:
                if row["teacher_kind"] != "artifact_search" or row["teacher_source"] != "search-move-teacher-v1":
                    raise RuntimeError(f"teacher provenance missing: {row}")
                if int(row["root_move_score_side_to_move"]) != -int(row["child_label_score_side_to_move"]):
                    raise RuntimeError(f"root/child perspective sign failed: {row}")
                expected_child_id = "board-" + hashlib.sha256(
                    f"board-v1\t{row['child_board_a1_to_h8']}".encode("ascii")
                ).hexdigest()[:16]
                if row["child_board_id"] != expected_child_id:
                    raise RuntimeError(f"child board identity is not canonical: {row}")
                if row["best_move_tie_count"] != "4" or row["best_score_margin"] != "0":
                    raise RuntimeError(f"tie/margin contract failed: {row}")

            ranking = root / "ranking.json"
            summary = root / "ranking.md"
            run([
                str(args.ranking_evaluator), "--move-teacher", str(first / "moves.tsv"), "--weights",
                str(weights), "--manifest", str(manifest), "--pattern-set", "pattern-v2-endgame-lite",
                "--report-out", str(ranking), "--summary-out", str(summary),
            ])
            if json.loads(ranking.read_text(encoding="utf-8"))["root_count"] != 1:
                raise RuntimeError("ranking evaluator did not accept move-teacher schema v3")

            asymmetric = root / "asymmetric.tsv"
            write_tsv(asymmetric, [normalized_row("asymmetric-early", asymmetric_early)])
            asymmetric_teacher = root / "asymmetric-teacher"
            asymmetric_teacher.mkdir()
            run(core_command(args.generator, asymmetric, manifest, weights, asymmetric_teacher))
            phase_aware_ranking = root / "phase-aware-ranking.json"
            run([
                str(args.ranking_evaluator),
                "--move-teacher",
                str(asymmetric_teacher / "moves.tsv"),
                "--weights",
                str(default_weights),
                "--manifest",
                str(default_manifest),
                "--pattern-set",
                "pattern-v2-endgame-lite",
                "--report-out",
                str(phase_aware_ranking),
                "--summary-out",
                str(root / "phase-aware-ranking.md"),
            ])
            phase_aware = json.loads(phase_aware_ranking.read_text(encoding="utf-8"))
            if phase_aware.get("roots_with_all_moves_same_predicted_score") != 0:
                raise RuntimeError(
                    "ranking evaluator ignored trained-phase fallback routing: "
                    f"{phase_aware!r}"
                )

            late = root / "late.tsv"
            write_tsv(late, [normalized_row("one-empty-move", one_empty_move),
                              normalized_row("one-empty-pass", one_empty_pass, "validation")])
            late_out = root / "late"
            late_out.mkdir()
            run(core_command(args.generator, late, manifest, weights, late_out,
                             nodes=100000, exact=1, min_phase=12, max_phase=12))
            late_rows = read_tsv(late_out / "moves.tsv")
            if {row["root_board_id"] for row in late_rows} != {"one-empty-move", "one-empty-pass"}:
                raise RuntimeError(f"terminal child or forced pass root missing: {late_rows}")
            terminal_rows = [row for row in late_rows if row["root_board_id"] == "one-empty-move"]
            if len(terminal_rows) != 1 or terminal_rows[0]["child_empty_count"] != "0":
                raise RuntimeError(f"terminal child was not materialized: {late_rows}")
            if [row["move"] for row in late_rows if row["root_board_id"] == "one-empty-pass"] != ["pass"]:
                raise RuntimeError(f"forced pass was not emitted: {late_rows}")

            interrupted = root / "interrupted"
            interrupted.mkdir()
            run(core_command(args.generator, early, manifest, weights, interrupted, depth=5, nodes=1), 1)
            report = json.loads((interrupted / "report.json").read_text(encoding="utf-8"))
            if report["complete"] or (interrupted / "moves.tsv").exists():
                raise RuntimeError("interrupted root produced a complete or partial teacher TSV")

            uncovered = root / "uncovered"
            uncovered.mkdir()
            run(core_command(args.generator, early, default_manifest, default_weights, uncovered), 1)
            explicit_fallback = root / "explicit-fallback"
            explicit_fallback.mkdir()
            run(
                core_command(
                    args.generator,
                    early,
                    default_manifest,
                    default_weights,
                    explicit_fallback,
                    coverage_policy="explicit-phase-aware",
                )
            )
            explicit_report = json.loads(
                (explicit_fallback / "report.json").read_text(encoding="utf-8")
            )
            if (
                explicit_report.get("teacher_coverage_policy") != "explicit-phase-aware"
                or explicit_report.get("teacher_trained_phases") != [10, 11, 12]
                or explicit_report.get("teacher_fallback_phases") != list(range(10))
                or explicit_report.get("teacher_source")
                != "search-move-teacher-v2-explicit-phase-aware"
            ):
                raise RuntimeError(
                    f"explicit phase-aware teacher provenance is incomplete: {explicit_report!r}"
                )
            run(core_command(args.generator, early, manifest, weights, root / "wall-clock", time_ms=1), 2)

            out_of_range_weights, out_of_range_manifest = make_full_coverage_artifact(
                default_manifest, default_weights, root, "out-of-range", phase_zero_bias=100
            )
            score_clamp = root / "score-clamp"
            score_clamp.mkdir()
            run(core_command(args.generator, early, out_of_range_manifest, out_of_range_weights, score_clamp))
            clamped_rows = read_tsv(score_clamp / "moves.tsv")
            child_scores = [int(row["child_label_score_side_to_move"]) for row in clamped_rows]
            root_scores = [int(row["root_move_score_side_to_move"]) for row in clamped_rows]
            if (
                not child_scores
                or any(abs(score) > 64 for score in [*child_scores, *root_scores])
                or not any(abs(score) == 64 for score in child_scores)
            ):
                raise RuntimeError(
                    f"pattern evaluator did not clamp teacher scores to disc-diff range: {clamped_rows}"
                )

            runner_out = root / "resume"
            runner_base = [
                sys.executable, str(args.runner), "--generator", str(args.generator), "--normalized-tsv", str(early),
                "--teacher-manifest", str(manifest), "--teacher-weights", str(weights),
                "--output-dir", str(runner_out), "--max-depth", "1", "--max-nodes", "0",
                "--max-time-ms", "0", "--search-preset", "basic", "--exact-endgame-empties", "0",
            ]
            run(runner_base)
            resume = run(runner_base + ["--resume"])
            if "skipped-resume-validated" not in resume.stdout:
                raise RuntimeError("resume did not validate matching sidecar")
            run(runner_base + ["--max-depth", "2", "--resume"], 1)
            run([str(args.generator), "--self-test-contract"])
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
