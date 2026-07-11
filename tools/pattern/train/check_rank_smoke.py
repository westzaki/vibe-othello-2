#!/usr/bin/env python3
"""Synthetic integration smoke for the deterministic pairwise rank trainer."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path


HEADER = (
    "root_board_id\troot_record_id\troot_split\troot_phase\troot_empty_count\tmove\t"
    "child_board_id\tchild_board_a1_to_h8\tchild_empty_count\tchild_phase\t"
    "root_move_score_side_to_move\tchild_label_score_side_to_move\tis_best_move\t"
    "best_move_tie_count\tmove_rank\tbest_score_margin\tteacher_source\tteacher_depth\tteacher_nodes\n"
)
HEADER_V2 = (
    "root_board_id\troot_record_id\troot_split\troot_phase\troot_empty_count\tmove\t"
    "child_board_id\tchild_board_a1_to_h8\tchild_empty_count\tchild_phase\t"
    "root_move_score_side_to_move\tchild_label_score_side_to_move\tis_best_move\t"
    "best_move_tie_count\tmove_rank\tbest_score_margin\tteacher_kind\tteacher_source\t"
    "teacher_artifact_id\tteacher_artifact_checksum\tteacher_depth\tteacher_nodes\t"
    "teacher_search_config_id\n"
)


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, check=False, text=True, capture_output=True)
    if result.returncode:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise RuntimeError("command failed: " + " ".join(command))
    return result


def write_fixture(dataset: Path, move_teacher: Path) -> None:
    dataset.write_text(
        "record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_features\n"
        "a\t0\ttrain\t-4\t0\tedge-8:0:1,edge-8:0:1\n"
        "b\t0\ttrain\t4\t0\tedge-8:0:2\n"
        "three-a\t0\ttrain\t-6\t0\tedge-8:0:3\n"
        "three-b\t0\ttrain\t0\t0\tedge-8:0:4\n"
        "three-c\t0\ttrain\t6\t0\tedge-8:0:5\n"
        "phase-a\t0\ttrain\t5\t1\tedge-8:0:6\n"
        "phase-b\t0\ttrain\t-5\t1\tedge-8:0:7\n"
        "tie-a\t0\ttrain\t-2\t0\tedge-8:0:8\n"
        "tie-b\t0\ttrain\t-2\t0\tedge-8:0:9\n"
        "validation-a\t0\tvalidation\t-3\t0\tedge-8:0:10\n"
        "validation-b\t0\tvalidation\t3\t0\tedge-8:0:11\n",
        encoding="utf-8",
    )
    rows = [
        ("root-two", "record-two", "train", 0, "a", "a", 0, 4, -4, 1, 1, 1, 8),
        ("root-two", "record-two", "train", 0, "b", "b", 0, -4, 4, 0, 1, 2, 8),
        ("root-three", "record-three", "train", 0, "a", "three-a", 0, 6, -6, 1, 1, 1, 12),
        ("root-three", "record-three", "train", 0, "b", "three-b", 0, 0, 0, 0, 1, 2, 12),
        ("root-three", "record-three", "train", 0, "c", "three-c", 0, -6, 6, 0, 1, 3, 12),
        ("root-phase", "record-phase", "train", 1, "a", "phase-a", 1, -5, 5, 0, 1, 2, 10),
        ("root-phase", "record-phase", "train", 1, "b", "phase-b", 1, 5, -5, 1, 1, 1, 10),
        ("root-tie", "record-tie", "train", 0, "a", "tie-a", 0, 2, -2, 1, 2, 1, 0),
        ("root-tie", "record-tie", "train", 0, "b", "tie-b", 0, 2, -2, 1, 2, 2, 0),
        ("root-validation", "record-validation", "validation", 0, "a", "validation-a", 0, 3, -3, 1, 1, 1, 6),
        ("root-validation", "record-validation", "validation", 0, "b", "validation-b", 0, -3, 3, 0, 1, 2, 6),
    ]
    lines = [HEADER]
    for root, record, split, root_phase, move, child, child_phase, root_score, child_score, best, ties, rank, margin in rows:
        lines.append(
            f"{root}\t{record}\t{split}\t{root_phase}\t0\t{move}\t{child}\t-\t0\t{child_phase}\t"
            f"{root_score}\t{child_score}\t{best}\t{ties}\t{rank}\t{margin}\tsynthetic\t0\t0\n"
        )
    move_teacher.write_text("".join(lines), encoding="utf-8")


def write_v2_fixture(source: Path, destination: Path, *, mismatch: bool) -> None:
    rows = source.read_text(encoding="utf-8").splitlines()
    converted = [HEADER_V2]
    for row_index, line in enumerate(rows[1:]):
        fields = line.split("\t")
        artifact_checksum = "sha256:artifact-b" if mismatch and row_index == 1 else "sha256:artifact-a"
        converted.append(
            "\t".join(
                fields[:16]
                + [
                    "search",
                    fields[16],
                    "teacher-artifact-a",
                    artifact_checksum,
                    fields[17],
                    fields[18],
                    "depth-8",
                ]
            )
            + "\n"
        )
    destination.write_text("".join(converted), encoding="utf-8")


def dataset_report(catalog_dump: Path, path: Path) -> None:
    catalog = json.loads(run([str(catalog_dump), "--pattern-set", "fixed-pattern-fixture-v1", "--index-mode", "raw"]).stdout)
    contract = next(item for item in catalog["pattern_sets"] if item["pattern_set_id"] == "fixed-pattern-fixture-v1")
    path.write_text(
        json.dumps(
            {
                "pattern_set_id": "fixed-pattern-fixture-v1",
                "pattern_contract_digest": contract["pattern_contract_digest"],
                "index_mode": "raw",
                "phase_count": 13,
                "phase_mapping_id": "disc-count-13-v1",
                "score_unit": "disc-diff",
            }
        ),
        encoding="utf-8",
    )


def trainer_command(
    trainer: Path, dataset: Path, move_teachers: Path | list[Path], report_input: Path, weights: Path, report: Path,
) -> list[str]:
    sidecars = [move_teachers] if isinstance(move_teachers, Path) else move_teachers
    command = [sys.executable, str(trainer), "--dataset", str(dataset)]
    for move_teacher in sidecars:
        command.extend(["--move-teacher", str(move_teacher)])
    return command + [
        "--mode", "pattern-rank-v0e", "--epochs", "12", "--learning-rate", "0.2",
        "--weight-decay", "0", "--rank-temperature", "1", "--value-loss-weight", "0",
        "--pair-sampling-cap", "0", "--tie-margin", "0", "--seed", "17",
        "--dataset-report", str(report_input), "--weights-out", str(weights), "--report-out", str(report),
    ]


def train(
    trainer: Path, dataset: Path, move_teacher: Path | list[Path], report_input: Path, weights: Path, report: Path,
    extra: list[str] | None = None,
) -> None:
    command = trainer_command(trainer, dataset, move_teacher, report_input, weights, report)
    run(command + (extra or []))


def split_sidecar(source: Path, first: Path, second: Path) -> None:
    lines = source.read_text(encoding="utf-8").splitlines(keepends=True)
    first_rows = [line for line in lines[1:] if line.startswith(("root-two\t", "root-three\t", "root-tie\t"))]
    second_rows = [line for line in lines[1:] if line not in first_rows]
    first.write_text(lines[0] + "".join(first_rows), encoding="utf-8")
    second.write_text(lines[0] + "".join(second_rows), encoding="utf-8")


def weight_map(payload: dict[str, object]) -> dict[tuple[int, str, int], float]:
    rows = payload.get("pattern_weights")
    assert isinstance(rows, list)
    return {
        (int(row["phase"]), str(row["pattern_id"]), int(row["ternary_index"])): float(row["weight"])
        for row in rows
        if isinstance(row, dict)
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trainer", required=True, type=Path)
    parser.add_argument("--exporter", required=True, type=Path)
    parser.add_argument("--catalog-dump", required=True, type=Path)
    parser.add_argument("--roundtrip", required=True, type=Path)
    args = parser.parse_args()
    try:
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            dataset = temp / "dataset.tsv"
            move_teacher = temp / "move-teacher.tsv"
            report_input = temp / "dataset-report.json"
            first_weights = temp / "first.weights.json"
            first_report = temp / "first.report.json"
            second_weights = temp / "second.weights.json"
            second_report = temp / "second.report.json"
            write_fixture(dataset, move_teacher)
            dataset_report(args.catalog_dump, report_input)
            train(args.trainer, dataset, move_teacher, report_input, first_weights, first_report)
            train(args.trainer, dataset, move_teacher, report_input, second_weights, second_report)
            if first_weights.read_bytes() != second_weights.read_bytes() or first_report.read_bytes() != second_report.read_bytes():
                raise RuntimeError("pattern-rank-v0e output is not deterministic")
            weights = json.loads(first_weights.read_text(encoding="utf-8"))
            report = json.loads(first_report.read_text(encoding="utf-8"))
            values = weight_map(weights)
            if not values[(0, "edge-8", 1)] < 0.0 or not values[(0, "edge-8", 2)] > 0.0:
                raise RuntimeError("two-move sign direction did not lower the better child value")
            if abs(values[(0, "edge-8", 1)]) <= abs(values[(0, "edge-8", 2)]):
                raise RuntimeError("repeated feature occurrence did not receive repeated gradient contribution")
            if not values[(1, "edge-8", 6)] > 0.0 or not values[(1, "edge-8", 7)] < 0.0:
                raise RuntimeError("phase-separated root did not update its independent phase weights")
            ranking = report.get("ranking_metrics", {})
            overall = ranking.get("overall", {}) if isinstance(ranking, dict) else {}
            if overall.get("pairwise_count", 0) < 5 or overall.get("top1_accuracy") != 1.0:
                raise RuntimeError(f"three-move or two-move ranking was not learned: {overall!r}")
            if report.get("pair_sampling_totals", {}).get("teacher_tie_pair_count", 0) <= 0:
                raise RuntimeError("teacher tie pairs were not recorded as excluded")
            if not {"value_MAE", "value_RMSE", "predicted_score_range"} <= set(overall):
                raise RuntimeError("ranking report is missing value and score-range diagnostics")
            if report.get("trained_phases") != [0, 1]:
                raise RuntimeError(f"rank trainer did not report updated child phases: {report!r}")

            warm_weights = temp / "warm.weights.json"
            warm_report = temp / "warm.report.json"
            train(
                args.trainer,
                dataset,
                move_teacher,
                report_input,
                warm_weights,
                warm_report,
                [
                    "--initial-weights",
                    str(first_weights),
                    "--initial-trained-phases",
                    "0",
                    "1",
                    "--freeze-phase",
                    "1",
                ],
            )
            warm_payload = json.loads(warm_weights.read_text(encoding="utf-8"))
            warm_data = json.loads(warm_report.read_text(encoding="utf-8"))
            warm_values = weight_map(warm_payload)
            initial_phase_one = {key: value for key, value in values.items() if key[0] == 1}
            warm_phase_one = {key: value for key, value in warm_values.items() if key[0] == 1}
            if warm_phase_one != initial_phase_one:
                raise RuntimeError("warm-start training changed frozen phase-1 pattern weights")
            if warm_payload.get("phase_bias", {}).get("1") != weights.get("phase_bias", {}).get("1"):
                raise RuntimeError("warm-start training changed frozen phase-1 bias")
            if {key: value for key, value in warm_values.items() if key[0] == 0} == {
                key: value for key, value in values.items() if key[0] == 0
            }:
                raise RuntimeError("warm-start training did not update unfrozen phase 0")
            warm_start = warm_data.get("warm_start", {})
            if (
                warm_data.get("trained_phases") != [0, 1]
                or warm_start.get("initial_trained_phases") != [0, 1]
                or warm_start.get("frozen_phases") != [1]
                or not str(warm_start.get("checksum", "")).startswith("sha256:")
            ):
                raise RuntimeError(f"warm-start provenance was not retained: {warm_data!r}")

            first_sidecar = temp / "first-move-teacher.tsv"
            second_sidecar = temp / "second-move-teacher.tsv"
            split_sidecar(move_teacher, first_sidecar, second_sidecar)
            multi_weights = temp / "multi.weights.json"
            multi_report = temp / "multi.report.json"
            train(
                args.trainer,
                dataset,
                [first_sidecar, second_sidecar],
                report_input,
                multi_weights,
                multi_report,
            )
            multi = json.loads(multi_report.read_text(encoding="utf-8"))
            if len(multi.get("move_teacher_inputs", [])) != 2:
                raise RuntimeError(f"multiple move-teacher inputs were not retained: {multi!r}")
            if multi.get("trained_phases") != [0, 1]:
                raise RuntimeError(f"multiple sidecars changed phase coverage: {multi!r}")

            valid_v2 = temp / "move-teacher-v2.tsv"
            write_v2_fixture(move_teacher, valid_v2, mismatch=False)
            v2_weights = temp / "v2.weights.json"
            v2_report = temp / "v2.report.json"
            train(args.trainer, dataset, valid_v2, report_input, v2_weights, v2_report)
            provenance = json.loads(v2_report.read_text(encoding="utf-8")).get("move_teacher_provenance")
            if provenance != {
                "teacher_kind": "search",
                "teacher_source": "synthetic",
                "teacher_artifact_id": "teacher-artifact-a",
                "teacher_artifact_checksum": "sha256:artifact-a",
                "teacher_search_config_id": "depth-8",
            }:
                raise RuntimeError(f"v2 provenance was not retained in trainer report: {provenance!r}")
            mixed_v2 = temp / "mixed-v2.tsv"
            write_v2_fixture(move_teacher, mixed_v2, mismatch=True)
            rejected = subprocess.run(
                trainer_command(
                    args.trainer, dataset, mixed_v2, report_input,
                    temp / "mixed.weights.json", temp / "mixed.report.json",
                ),
                check=False,
                text=True,
                capture_output=True,
            )
            if rejected.returncode == 0 or "v2 provenance must be identical" not in rejected.stderr:
                raise RuntimeError(f"mixed v2 provenance was accepted: {rejected.stderr!r}")

            stopped_weights = temp / "stopped.weights.json"
            stopped_report = temp / "stopped.report.json"
            train(
                args.trainer, dataset, move_teacher, report_input, stopped_weights, stopped_report,
                ["--learning-rate", "0", "--early-stop-patience", "0"],
            )
            stopped = json.loads(stopped_report.read_text(encoding="utf-8"))
            if stopped.get("early_stop_triggered") is not True or stopped.get("best_epoch") != 1:
                raise RuntimeError(f"early stopping is not deterministic: {stopped!r}")

            artifact = temp / "rank.weights.bin"
            manifest = temp / "rank.manifest.json"
            run([
                sys.executable, str(args.exporter), "--weights-json", str(first_weights),
                "--weights-out", str(artifact), "--manifest-out", str(manifest),
                "--pattern-set", "fixed-pattern-fixture-v1", "--catalog-dump-exe", str(args.catalog_dump),
                "--trained-phases", "0", "1",
            ])
            run([str(args.roundtrip), "--weights", str(artifact), "--phase-count", "13"])
    except (AssertionError, RuntimeError, OSError, json.JSONDecodeError, StopIteration) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
