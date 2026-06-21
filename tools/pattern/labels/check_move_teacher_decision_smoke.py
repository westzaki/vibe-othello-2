#!/usr/bin/env python3
"""CTest wrapper for exact move-teacher generation and ranking diagnostics."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
import tempfile
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


NORMALIZED_HEADER_V2 = [
    "record_id",
    "position_id",
    "game_group_id",
    "board_id",
    "source_occurrence_id",
    "source_dataset_id",
    "split",
    "board_a1_to_h8",
    "label_kind",
    "label_unit",
    "label_perspective",
    "label_score_side_to_move",
    "occupied_count",
    "phase",
    "player_disc_count",
    "opponent_disc_count",
    "empty_count",
]
NORMALIZED_HEADER_V1 = [
    "record_id",
    "position_id",
    "source_dataset_id",
    "split",
    "board_a1_to_h8",
    "label_kind",
    "label_unit",
    "label_perspective",
    "label_score_side_to_move",
    "occupied_count",
    "phase",
    "player_disc_count",
    "opponent_disc_count",
    "empty_count",
]
MOVE_TEACHER_HEADER = [
    "root_board_id",
    "root_record_id",
    "root_split",
    "root_phase",
    "root_empty_count",
    "move",
    "child_board_id",
    "child_board_a1_to_h8",
    "child_empty_count",
    "child_phase",
    "root_move_score_side_to_move",
    "child_label_score_side_to_move",
    "is_best_move",
    "best_move_tie_count",
    "move_rank",
    "best_score_margin",
    "teacher_source",
    "teacher_depth",
    "teacher_nodes",
]

POSITIONS = [
    (
        "zero-empty-terminal",
        "train",
        "BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/WWWWWWWW/WWWWWWWW/WWWWWWWW b",
    ),
    (
        "one-empty-move",
        "train",
        "BBBBBBW./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b",
    ),
    (
        "one-empty-pass",
        "validation",
        "BBBBBWB./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b",
    ),
    (
        "two-empty",
        "test",
        "WWWWWWW./BBBBBBB./BBWBWBWW/BBBBBWWB/BWBBWWWB/BBBWWWWW/BBBBWWWW/BBBWWWWW b",
    ),
    (
        "fourteen-empty",
        "train",
        "WWWWWWW./.WWWBW../BBWBBB../BWWBB.B./BWBBBBBB/BWBBB.B./.WWWWB../BW.WWWWW b",
    ),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generator", required=True, type=Path)
    parser.add_argument("--ranking-evaluator", required=True, type=Path)
    parser.add_argument("--dataset-exe", required=True, type=Path)
    parser.add_argument("--trainer", required=True, type=Path)
    parser.add_argument("--exporter", required=True, type=Path)
    parser.add_argument("--campaign-helper", required=True, type=Path)
    parser.add_argument("--matrix-helper", required=True, type=Path)
    return parser.parse_args()


def phase_for_occupied(occupied: int) -> int:
    return min(12, ((occupied - 4) * 13) // 60)


def relative_board(serialized: str) -> str:
    board_text, side = serialized.split()
    player_disc = "B" if side == "b" else "W"
    opponent_disc = "W" if side == "b" else "B"
    board = ["-"] * 64
    ranks = board_text.split("/")
    if len(ranks) != 8:
        raise AssertionError(serialized)
    for rank_from_top, row in enumerate(ranks):
        rank = 7 - rank_from_top
        if len(row) != 8:
            raise AssertionError(serialized)
        for file, value in enumerate(row):
            index = rank * 8 + file
            if value == player_disc:
                board[index] = "X"
            elif value == opponent_disc:
                board[index] = "O"
            elif value == ".":
                board[index] = "-"
            else:
                raise AssertionError(serialized)
    return "".join(board)


def normalized_row(index: int, board_id: str, split: str, serialized: str) -> dict[str, str]:
    board = relative_board(serialized)
    player = board.count("X")
    opponent = board.count("O")
    empty = board.count("-")
    occupied = player + opponent
    return {
        "record_id": f"record-{index}",
        "position_id": f"position-{index}",
        "game_group_id": f"game-{index % 3}",
        "board_id": board_id,
        "source_occurrence_id": f"source-{index}",
        "source_dataset_id": "synthetic-move-teacher",
        "split": split,
        "board_a1_to_h8": board,
        "label_kind": "observed_final_disc_diff",
        "label_unit": "final_disc_diff",
        "label_perspective": "side_to_move",
        "label_score_side_to_move": "0",
        "occupied_count": str(occupied),
        "phase": str(phase_for_occupied(occupied)),
        "player_disc_count": str(player),
        "opponent_disc_count": str(opponent),
        "empty_count": str(empty),
    }


def fixture_rows(include_duplicate: bool = True) -> list[dict[str, str]]:
    rows = [
        normalized_row(index, f"board-{name}", split, serialized)
        for index, (name, split, serialized) in enumerate(POSITIONS, start=1)
    ]
    if include_duplicate:
        duplicate = dict(rows[1])
        duplicate["record_id"] = "record-duplicate"
        duplicate["position_id"] = "position-duplicate"
        duplicate["source_occurrence_id"] = "source-duplicate"
        rows.append(duplicate)
    return rows


def write_tsv(path: Path, header: list[str], rows: list[dict[str, str]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, delimiter="\t", lineterminator="\n", fieldnames=header)
        writer.writeheader()
        writer.writerows(rows)


def load_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def load_json(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise AssertionError(path)
    return data


def run(command: list[str], **kwargs: Any) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, capture_output=True, text=True, **kwargs)


def require_success(result: subprocess.CompletedProcess[str]) -> bool:
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return False
    return True


def require_failure(result: subprocess.CompletedProcess[str], expected: str) -> bool:
    if result.returncode == 0:
        print("command unexpectedly succeeded", file=sys.stderr)
        return False
    if expected not in result.stderr:
        print(f"missing expected error {expected!r}: {result.stderr!r}", file=sys.stderr)
        return False
    return True


def run_generator(
    generator: Path,
    normalized: Path,
    move_teacher: Path,
    child_normalized: Path,
    report: Path,
    *extra: str,
) -> subprocess.CompletedProcess[str]:
    return run(
        [
            str(generator),
            "--normalized-tsv",
            str(normalized),
            "--move-teacher-out",
            str(move_teacher),
            "--child-normalized-out",
            str(child_normalized),
            "--report-out",
            str(report),
            *extra,
        ]
    )


def run_dataset(dataset_exe: Path, normalized: Path, dataset: Path, report: Path, pattern_set: str) -> bool:
    result = run(
        [
            str(dataset_exe),
            "--normalized-tsv",
            str(normalized),
            "--report",
            str(report),
            "--output-format",
            "compact-tsv",
            "--pattern-set",
            pattern_set,
        ]
    )
    if not require_success(result):
        return False
    dataset.write_text(result.stdout, encoding="utf-8")
    return True


def run_trainer(trainer: Path, dataset: Path, weights_json: Path, report: Path) -> bool:
    return require_success(
        run(
            [
                sys.executable,
                str(trainer),
                "--dataset",
                str(dataset),
                "--mode",
                "pattern-sgd-v0c",
                "--epochs",
                "2",
                "--learning-rate",
                "0.1",
                "--lr-schedule",
                "inverse-sqrt",
                "--weight-decay",
                "0.0001",
                "--weights-out",
                str(weights_json),
                "--report-out",
                str(report),
                "--seed",
                "0",
            ]
        )
    )


def run_exporter(exporter: Path, weights_json: Path, weights_bin: Path, manifest: Path, pattern_set: str) -> bool:
    return require_success(
        run(
            [
                sys.executable,
                str(exporter),
                "--weights-json",
                str(weights_json),
                "--weights-out",
                str(weights_bin),
                "--manifest-out",
                str(manifest),
                "--pattern-set",
                pattern_set,
            ]
        )
    )


def run_ranking(
    evaluator: Path,
    move_teacher: Path,
    weights_bin: Path,
    manifest: Path,
    report: Path,
    summary: Path,
    pattern_set: str,
) -> bool:
    return require_success(
        run(
            [
                str(evaluator),
                "--move-teacher",
                str(move_teacher),
                "--weights",
                str(weights_bin),
                "--manifest",
                str(manifest),
                "--pattern-set",
                pattern_set,
                "--report-out",
                str(report),
                "--summary-out",
                str(summary),
            ]
        )
    )


def check_basic_generation(generator: Path, root: Path) -> tuple[bool, Path, Path]:
    normalized = root / "normalized.tsv"
    move_teacher = root / "move-teacher.tsv"
    child_normalized = root / "child-normalized.tsv"
    report_path = root / "move-teacher-report.json"
    write_tsv(normalized, NORMALIZED_HEADER_V2, fixture_rows())
    result = run_generator(
        generator,
        normalized,
        move_teacher,
        child_normalized,
        report_path,
        "--max-empty",
        "2",
        "--seed",
        "7",
        "--progress-every",
        "1",
    )
    if not require_success(result):
        return False, move_teacher, child_normalized

    move_rows = load_tsv(move_teacher)
    child_rows = load_tsv(child_normalized)
    if not move_rows or list(move_rows[0].keys()) != MOVE_TEACHER_HEADER:
        print("bad move-teacher header", file=sys.stderr)
        return False, move_teacher, child_normalized
    if not child_rows or list(child_rows[0].keys()) != NORMALIZED_HEADER_V2:
        print("bad child normalized header", file=sys.stderr)
        return False, move_teacher, child_normalized
    if len(move_rows) != len(child_rows):
        print("move and child row counts differ", file=sys.stderr)
        return False, move_teacher, child_normalized

    root_counts = Counter(row["root_board_id"] for row in move_rows)
    if root_counts["board-one-empty-move"] != 1:
        print(f"dedup or one-empty move count failed: {root_counts}", file=sys.stderr)
        return False, move_teacher, child_normalized
    if "board-zero-empty-terminal" in root_counts:
        print("terminal root should be skipped", file=sys.stderr)
        return False, move_teacher, child_normalized

    one_move = [row for row in move_rows if row["root_board_id"] == "board-one-empty-move"][0]
    if one_move["move"] != "h8":
        print(f"unexpected one-empty legal move: {one_move}", file=sys.stderr)
        return False, move_teacher, child_normalized
    if int(one_move["root_move_score_side_to_move"]) != 64:
        print(f"bad root score for one-empty move: {one_move}", file=sys.stderr)
        return False, move_teacher, child_normalized
    if int(one_move["child_label_score_side_to_move"]) != -64:
        print(f"bad child score sign for one-empty move: {one_move}", file=sys.stderr)
        return False, move_teacher, child_normalized

    pass_rows = [row for row in move_rows if row["root_board_id"] == "board-one-empty-pass"]
    if len(pass_rows) != 1 or pass_rows[0]["move"] != "pass":
        print(f"pass root was not emitted correctly: {pass_rows}", file=sys.stderr)
        return False, move_teacher, child_normalized
    if int(pass_rows[0]["root_move_score_side_to_move"]) != -int(pass_rows[0]["child_label_score_side_to_move"]):
        print(f"pass sign convention failed: {pass_rows[0]}", file=sys.stderr)
        return False, move_teacher, child_normalized
    if pass_rows[0]["child_empty_count"] != "1" or pass_rows[0]["teacher_depth"] != "1":
        print(f"pass child empty/depth failed: {pass_rows[0]}", file=sys.stderr)
        return False, move_teacher, child_normalized

    for move_row, child_row in zip(move_rows, child_rows, strict=True):
        if move_row["child_board_id"] != child_row["board_id"]:
            print("child board id mismatch", file=sys.stderr)
            return False, move_teacher, child_normalized
        if child_row["label_kind"] != "teacher_exact_move_child_final_disc_diff":
            print(f"child label kind not accepted: {child_row}", file=sys.stderr)
            return False, move_teacher, child_normalized
        if child_row["label_perspective"] != "side_to_move" or child_row["label_unit"] != "disc":
            print(f"child label unit/perspective mismatch: {child_row}", file=sys.stderr)
            return False, move_teacher, child_normalized
        if child_row["label_score_side_to_move"] != move_row["child_label_score_side_to_move"]:
            print("child label score mismatch", file=sys.stderr)
            return False, move_teacher, child_normalized
        if child_row["split"] != move_row["root_split"]:
            print("child split did not preserve root split", file=sys.stderr)
            return False, move_teacher, child_normalized

    report = load_json(report_path)
    expected = {
        "input_rows": 6,
        "eligible_rows": 5,
        "selected_roots": 4,
        "unique_roots_selected": 4,
        "skipped_too_many_empty": 1,
        "duplicate_board_rows": 1,
        "terminal_roots_skipped": 1,
        "solve_failures": 0,
        "move_rows": len(move_rows),
        "child_normalized_rows": len(child_rows),
    }
    for key, value in expected.items():
        if report.get(key) != value:
            print(f"report mismatch for {key}: {report.get(key)!r}", file=sys.stderr)
            return False, move_teacher, child_normalized
    for phrase in (
        "root_move_score_side_to_move is -child_label_score_side_to_move",
        "teacher_depth is the child empty count solved exactly",
    ):
        if phrase not in report.get("notes", []):
            print(f"missing report note {phrase!r}", file=sys.stderr)
            return False, move_teacher, child_normalized
    return True, move_teacher, child_normalized


def check_determinism_and_schema(generator: Path, root: Path) -> bool:
    normalized = root / "det-normalized.tsv"
    write_tsv(normalized, NORMALIZED_HEADER_V2, fixture_rows())
    first_move = root / "det-first-move.tsv"
    first_child = root / "det-first-child.tsv"
    second_move = root / "det-second-move.tsv"
    second_child = root / "det-second-child.tsv"
    if not require_success(
        run_generator(generator, normalized, first_move, first_child, root / "det-first.json", "--max-empty", "14", "--max-roots", "2", "--seed", "123")
    ):
        return False
    if not require_success(
        run_generator(generator, normalized, second_move, second_child, root / "det-second.json", "--max-empty", "14", "--max-roots", "2", "--seed", "123")
    ):
        return False
    if first_move.read_text(encoding="utf-8") != second_move.read_text(encoding="utf-8"):
        print("same seed did not produce deterministic move-teacher output", file=sys.stderr)
        return False
    if first_child.read_text(encoding="utf-8") != second_child.read_text(encoding="utf-8"):
        print("same seed did not produce deterministic child output", file=sys.stderr)
        return False
    other_move = root / "det-other-move.tsv"
    other_child = root / "det-other-child.tsv"
    if not require_success(
        run_generator(generator, normalized, other_move, other_child, root / "det-other.json", "--max-empty", "14", "--max-roots", "2", "--seed", "999")
    ):
        return False
    if other_move.read_text(encoding="utf-8") == first_move.read_text(encoding="utf-8"):
        print("different seed did not alter capped root selection in fixture", file=sys.stderr)
        return False

    v1_path = root / "schema-v1.tsv"
    v1_row = {
        "record_id": "v1-record",
        "position_id": "v1-position",
        "source_dataset_id": "synthetic",
        "split": "train",
        "board_a1_to_h8": fixture_rows(include_duplicate=False)[0]["board_a1_to_h8"],
        "label_kind": "engine_disc_estimate",
        "label_unit": "disc",
        "label_perspective": "side_to_move",
        "label_score_side_to_move": "0",
        "occupied_count": "64",
        "phase": "12",
        "player_disc_count": "40",
        "opponent_disc_count": "24",
        "empty_count": "0",
    }
    write_tsv(v1_path, NORMALIZED_HEADER_V1, [v1_row])
    return require_failure(
        run_generator(
            generator,
            v1_path,
            root / "schema-v1-move.tsv",
            root / "schema-v1-child.tsv",
            root / "schema-v1-report.json",
        ),
        "schema v1 is not supported",
    )


def feature_counts_by_record(dataset: Path) -> dict[str, Counter[tuple[str, int]]]:
    with dataset.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        result: dict[str, Counter[tuple[str, int]]] = {}
        for row in reader:
            counter: Counter[tuple[str, int]] = Counter()
            for token in row["pattern_features"].split(","):
                pattern_id, _instance, ternary_index = token.split(":")
                counter[(pattern_id, int(ternary_index))] += 1
            result[row["record_id"]] = counter
        return result


def write_weight_json(path: Path, phase: int, pattern_id: str, ternary_index: int, weight: int) -> None:
    payload = {
        "schema_version": 1,
        "trainer_version": "pattern-sgd-v0b",
        "phase_bias": {str(phase_id): 0 for phase_id in range(13)},
        "pattern_weights": [
            {
                "phase": phase,
                "pattern_id": pattern_id,
                "ternary_index": ternary_index,
                "weight": weight,
            }
        ],
    }
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_filtered_move_teacher(path: Path, rows: list[dict[str, str]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, delimiter="\t", lineterminator="\n", fieldnames=MOVE_TEACHER_HEADER)
        writer.writeheader()
        writer.writerows(rows)


def check_ranking_known_artifact(
    args: argparse.Namespace, root: Path, move_teacher: Path, child_normalized: Path
) -> bool:
    del move_teacher, child_normalized
    known_move_teacher = root / "known-move-teacher.tsv"
    known_child_normalized = root / "known-child-normalized.tsv"
    best_board = "X" * 64
    other_board = "O" * 64
    write_filtered_move_teacher(
        known_move_teacher,
        [
            {
                "root_board_id": "synthetic-known-root",
                "root_record_id": "synthetic-root-record",
                "root_split": "train",
                "root_phase": "12",
                "root_empty_count": "1",
                "move": "a1",
                "child_board_id": "synthetic-known-child-best",
                "child_board_a1_to_h8": best_board,
                "child_empty_count": "0",
                "child_phase": "12",
                "root_move_score_side_to_move": "12",
                "child_label_score_side_to_move": "-12",
                "is_best_move": "1",
                "best_move_tie_count": "1",
                "move_rank": "1",
                "best_score_margin": "0",
                "teacher_source": "exact-move-teacher-v1",
                "teacher_depth": "0",
                "teacher_nodes": "1",
            },
            {
                "root_board_id": "synthetic-known-root",
                "root_record_id": "synthetic-root-record",
                "root_split": "train",
                "root_phase": "12",
                "root_empty_count": "1",
                "move": "h8",
                "child_board_id": "synthetic-known-child-other",
                "child_board_a1_to_h8": other_board,
                "child_empty_count": "0",
                "child_phase": "12",
                "root_move_score_side_to_move": "-12",
                "child_label_score_side_to_move": "12",
                "is_best_move": "0",
                "best_move_tie_count": "1",
                "move_rank": "2",
                "best_score_margin": "24",
                "teacher_source": "exact-move-teacher-v1",
                "teacher_depth": "0",
                "teacher_nodes": "1",
            },
        ],
    )
    write_tsv(
        known_child_normalized,
        NORMALIZED_HEADER_V2,
        [
            {
                "record_id": "synthetic-known-child-best",
                "position_id": "synthetic-known-child-best",
                "game_group_id": "synthetic-known-game",
                "board_id": "synthetic-known-child-best",
                "source_occurrence_id": "synthetic-known-child-best:source",
                "source_dataset_id": "exact-move-teacher-v1",
                "split": "train",
                "board_a1_to_h8": best_board,
                "label_kind": "teacher_exact_move_child_final_disc_diff",
                "label_unit": "disc",
                "label_perspective": "side_to_move",
                "label_score_side_to_move": "-12",
                "occupied_count": "64",
                "phase": "12",
                "player_disc_count": "64",
                "opponent_disc_count": "0",
                "empty_count": "0",
            },
            {
                "record_id": "synthetic-known-child-other",
                "position_id": "synthetic-known-child-other",
                "game_group_id": "synthetic-known-game",
                "board_id": "synthetic-known-child-other",
                "source_occurrence_id": "synthetic-known-child-other:source",
                "source_dataset_id": "exact-move-teacher-v1",
                "split": "train",
                "board_a1_to_h8": other_board,
                "label_kind": "teacher_exact_move_child_final_disc_diff",
                "label_unit": "disc",
                "label_perspective": "side_to_move",
                "label_score_side_to_move": "12",
                "occupied_count": "64",
                "phase": "12",
                "player_disc_count": "0",
                "opponent_disc_count": "64",
                "empty_count": "0",
            },
        ],
    )
    dataset = root / "known-pattern-dataset.tsv"
    dataset_report = root / "known-pattern-dataset-report.json"
    if not run_dataset(args.dataset_exe, known_child_normalized, dataset, dataset_report, "pattern-v2-endgame-lite"):
        return False
    move_rows = load_tsv(known_move_teacher)
    by_root: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in move_rows:
        by_root[row["root_board_id"]].append(row)
    target_rows = sorted(next(iter(by_root.values())), key=lambda row: row["move"])

    best = max(target_rows, key=lambda row: (int(row["root_move_score_side_to_move"]), row["move"]))
    others = [row for row in target_rows if row["child_board_id"] != best["child_board_id"]]
    counts = feature_counts_by_record(dataset)
    best_counts = counts[best["child_board_id"]]
    other_counts = [counts[row["child_board_id"]] for row in others]
    chosen: tuple[str, int, int] | None = None
    for key, best_count in best_counts.items():
        other_max = max(counter.get(key, 0) for counter in other_counts)
        if best_count > other_max:
            chosen = (key[0], key[1], -24)
            break
        other_min = min(counter.get(key, 0) for counter in other_counts)
        if best_count < other_min:
            chosen = (key[0], key[1], 24)
            break
    if chosen is None:
        print("could not find a discriminating feature for known-artifact ranking test", file=sys.stderr)
        return False

    weights_json = root / "known-weights.json"
    weights_bin = root / "known.weights.bin"
    manifest = root / "known.manifest.json"
    write_weight_json(weights_json, int(best["child_phase"]), chosen[0], chosen[1], chosen[2])
    if not run_exporter(args.exporter, weights_json, weights_bin, manifest, "pattern-v2-endgame-lite"):
        return False
    report_path = root / "known-ranking-report.json"
    summary_path = root / "known-ranking-summary.md"
    if not run_ranking(
        args.ranking_evaluator,
        known_move_teacher,
        weights_bin,
        manifest,
        report_path,
        summary_path,
        "pattern-v2-endgame-lite",
    ):
        return False
    report = load_json(report_path)
    if report.get("root_count") != 1:
        print(f"known ranking root_count mismatch: {report}", file=sys.stderr)
        return False
    if report.get("top1_accuracy") != 1.0 or report.get("top1_tie_aware_accuracy") != 1.0:
        print(f"known artifact did not rank exact best first: {report}", file=sys.stderr)
        return False
    if report.get("mean_teacher_regret") != 0.0 or report.get("median_teacher_regret") != 0.0:
        print(f"known artifact regret should be zero: {report}", file=sys.stderr)
        return False
    if report.get("roots_with_all_moves_same_predicted_score") != 0:
        print(f"known artifact produced tied predicted scores: {report}", file=sys.stderr)
        return False
    if "Not Elo" not in summary_path.read_text(encoding="utf-8"):
        print("ranking summary missing non-claim", file=sys.stderr)
        return False
    return True


def check_training_integration(
    args: argparse.Namespace, root: Path, move_teacher: Path, child_normalized: Path
) -> bool:
    dataset = root / "trained-pattern-dataset.tsv"
    dataset_report = root / "trained-pattern-dataset-report.json"
    if not run_dataset(args.dataset_exe, child_normalized, dataset, dataset_report, "pattern-v2-endgame-lite"):
        return False
    dataset_data = load_json(dataset_report)
    if dataset_data.get("counts_by_label_kind") != {
        "teacher_exact_move_child_final_disc_diff": dataset_data.get("accepted_rows")
    }:
        print(f"child label kind did not flow through dataset builder: {dataset_data}", file=sys.stderr)
        return False

    weights_json = root / "trained-weights.json"
    trainer_report = root / "trained-trainer-report.json"
    if not run_trainer(args.trainer, dataset, weights_json, trainer_report):
        return False
    weights_bin = root / "trained.weights.bin"
    manifest = root / "trained.manifest.json"
    if not run_exporter(args.exporter, weights_json, weights_bin, manifest, "pattern-v2-endgame-lite"):
        return False
    ranking_report = root / "trained-ranking-report.json"
    ranking_summary = root / "trained-ranking-summary.md"
    if not run_ranking(
        args.ranking_evaluator,
        move_teacher,
        weights_bin,
        manifest,
        ranking_report,
        ranking_summary,
        "pattern-v2-endgame-lite",
    ):
        return False
    report = load_json(ranking_report)
    if report.get("root_count", 0) <= 0 or report.get("legal_move_count", 0) <= 0:
        print(f"trained ranking report missing counts: {report}", file=sys.stderr)
        return False
    if "static_score_range" not in report or "results_by_split" not in report:
        print(f"ranking report missing required sections: {report}", file=sys.stderr)
        return False
    return True


def campaign_command(args: argparse.Namespace, normalized: Path, output_dir: Path, seed: int) -> list[str]:
    return [
        sys.executable,
        str(args.campaign_helper),
        "--normalized-tsv",
        str(normalized),
        "--output-dir",
        str(output_dir),
        "--max-empty",
        "2",
        "--max-roots",
        "4",
        "--seed",
        str(seed),
        "--pattern-set",
        "pattern-v2-endgame-lite",
        "--trainer-mode",
        "pattern-sgd-v0c",
        "--epochs",
        "1",
        "--learning-rate",
        "0.1",
        "--lr-schedule",
        "inverse-sqrt",
        "--weight-decay",
        "0.0001",
        "--generator",
        str(args.generator),
        "--dataset-exe",
        str(args.dataset_exe),
        "--trainer",
        str(args.trainer),
        "--exporter",
        str(args.exporter),
        "--ranking-evaluator",
        str(args.ranking_evaluator),
    ]


def matrix_command(args: argparse.Namespace, normalized: Path, output_dir: Path, *extra: str) -> list[str]:
    return [
        sys.executable,
        str(args.matrix_helper),
        "--normalized-tsv",
        str(normalized),
        "--output-dir",
        str(output_dir),
        "--root-counts",
        "4",
        "--seeds",
        "5",
        "--max-empty",
        "2",
        "--pattern-set",
        "pattern-v2-endgame-lite",
        "--trainer-mode",
        "pattern-sgd-v0c",
        "--epochs",
        "1",
        "--learning-rate",
        "0.1",
        "--lr-schedule",
        "inverse-sqrt",
        "--weight-decay",
        "0.0001",
        "--campaign-helper",
        str(args.campaign_helper),
        "--generator",
        str(args.generator),
        "--dataset-exe",
        str(args.dataset_exe),
        "--trainer",
        str(args.trainer),
        "--exporter",
        str(args.exporter),
        "--ranking-evaluator",
        str(args.ranking_evaluator),
        *extra,
    ]


def check_campaign_resume_safety(args: argparse.Namespace, root: Path) -> bool:
    normalized = root / "campaign-normalized.tsv"
    output_dir = root / "campaign"
    write_tsv(normalized, NORMALIZED_HEADER_V2, fixture_rows())

    first_command = campaign_command(args, normalized, output_dir, seed=3)
    if not require_success(run(first_command)):
        return False
    first_report = load_json(output_dir / "campaign-report.json")
    if first_report.get("stages", {}).get("generate_exact_move_teacher_dataset", {}).get("status") != "ok":
        print(f"campaign first run did not execute generator: {first_report.get('stages')}", file=sys.stderr)
        return False

    if not require_success(run([*first_command, "--resume"])):
        return False
    resume_report = load_json(output_dir / "campaign-report.json")
    expected_skipped = [
        "generate_exact_move_teacher_dataset",
        "build_child_pattern_dataset",
        "train_child_value_artifact",
        "export_child_value_artifact",
        "evaluate_move_teacher_child_artifact_ranking",
    ]
    for stage in expected_skipped:
        status = resume_report.get("stages", {}).get(stage, {}).get("status")
        if status != "skipped-resume-validated":
            print(f"campaign resume did not validate-skip {stage}: {status}", file=sys.stderr)
            return False

    stale_command = campaign_command(args, normalized, output_dir, seed=4)
    return require_failure(
        run([*stale_command, "--resume"]),
        "resume metadata mismatch for stage generate_exact_move_teacher_dataset",
    )


def check_matrix_helper(args: argparse.Namespace, root: Path) -> bool:
    normalized = root / "matrix-normalized.tsv"
    output_dir = root / "matrix"
    write_tsv(normalized, NORMALIZED_HEADER_V2, fixture_rows())

    if not require_success(run(matrix_command(args, normalized, output_dir))):
        return False
    report_path = output_dir / "matrix-report.json"
    summary_path = output_dir / "matrix-summary.md"
    if not report_path.exists() or not summary_path.exists():
        print("matrix helper did not write report and summary", file=sys.stderr)
        return False
    report = load_json(report_path)
    runs = report.get("runs")
    if not isinstance(runs, list) or len(runs) != 1:
        print(f"matrix report run count mismatch: {report}", file=sys.stderr)
        return False
    if runs[0].get("status") != "ok" or runs[0].get("selected_roots", 0) <= 0:
        print(f"matrix run did not complete: {runs[0]}", file=sys.stderr)
        return False
    aggregate = report.get("aggregate", {}).get("overall", {})
    if aggregate.get("completed_run_count") != 1:
        print(f"matrix aggregate did not count completed run: {aggregate}", file=sys.stderr)
        return False
    if "Not Elo" not in summary_path.read_text(encoding="utf-8"):
        print("matrix summary missing non-claim", file=sys.stderr)
        return False

    if not require_success(run(matrix_command(args, normalized, output_dir, "--resume"))):
        return False
    resume_report = load_json(report_path)
    resume_runs = resume_report.get("runs")
    if not isinstance(resume_runs, list) or len(resume_runs) != 1:
        print(f"matrix resume report run count mismatch: {resume_report}", file=sys.stderr)
        return False
    stage_statuses = resume_runs[0].get("campaign_stage_statuses", {})
    if stage_statuses.get("generate_exact_move_teacher_dataset") != "skipped-resume-validated":
        print(f"matrix resume did not pass through campaign resume validation: {stage_statuses}", file=sys.stderr)
        return False
    return True


def main() -> int:
    args = parse_args()
    with tempfile.TemporaryDirectory() as temp_text:
        root = Path(temp_text)
        ok, move_teacher, child_normalized = check_basic_generation(args.generator, root)
        if not ok:
            return 1
        if not check_determinism_and_schema(args.generator, root):
            return 1
        if not check_ranking_known_artifact(args, root, move_teacher, child_normalized):
            return 1
        if not check_training_integration(args, root, move_teacher, child_normalized):
            return 1
        if not check_campaign_resume_safety(args, root):
            return 1
        if not check_matrix_helper(args, root):
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
