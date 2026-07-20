#!/usr/bin/env python3
"""CTest wrapper for the tiny pattern trainer smoke CLI."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path


EXPECTED_SUMMARY = {
    "model": "phase-bias-baseline",
    "input_rows": "48",
    "train_rows": "16",
    "validation_rows": "16",
    "test_rows": "16",
    "phases_seen": "1",
    "phase_bias[0]": "3",
    "checksum": "0x3b76cde99d45fa8f",
}
WEIGHTS_SCHEMA_VERSION_V1 = "pattern-eval-weights-v1"
WEIGHTS_SCHEMA_VERSION_V2 = "pattern-eval-weights-v2"


def run_capture(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, capture_output=True, text=True)


def parse_summary(text: str) -> dict[str, str]:
    summary: dict[str, str] = {}
    for line in text.splitlines():
        key, separator, value = line.partition("=")
        if not separator:
            raise ValueError(f"summary line is missing '=': {line}")
        if key in summary:
            raise ValueError(f"duplicate summary key: {key}")
        summary[key] = value
    return summary


def run_dataset(
    exe: Path, normalized_tsv: Path, report: Path, output_format: str = "expanded-tsv"
) -> str | None:
    result = run_capture(
        [
            str(exe),
            "--normalized-tsv",
            str(normalized_tsv),
            "--report",
            str(report),
            "--output-format",
            output_format,
        ]
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return None
    return result.stdout


def run_phase_bias_v0a(script: Path, dataset: Path, weights: Path, report: Path) -> bool:
    return run_trainer(script, "phase-bias-v0a", dataset, weights, report)


def run_trainer_v0b(
    script: Path,
    dataset: Path,
    weights: Path,
    report: Path,
    extra_args: list[str] | None = None,
) -> bool:
    return run_trainer(script, "pattern-sgd-v0b", dataset, weights, report, extra_args)


def run_trainer_v0c(
    script: Path,
    dataset: Path,
    weights: Path,
    report: Path,
    extra_args: list[str] | None = None,
) -> bool:
    return run_trainer(script, "pattern-sgd-v0c", dataset, weights, report, extra_args)


def run_trainer_v0d(
    script: Path,
    dataset: Path,
    weights: Path,
    report: Path,
    extra_args: list[str] | None = None,
) -> bool:
    return run_trainer(script, "pattern-sgd-v0d", dataset, weights, report, extra_args)


def run_trainer(
    script: Path,
    mode: str,
    dataset: Path,
    weights: Path,
    report: Path,
    extra_args: list[str] | None = None,
) -> bool:
    effective_extra_args = list(extra_args or [])
    metadata_markers = {
        "--dataset-report",
        "--pattern-set-id",
        "--weights-schema-version",
    }
    if mode != "phase-bias-v0a" and not any(arg in metadata_markers for arg in effective_extra_args):
        effective_extra_args.extend(["--weights-schema-version", WEIGHTS_SCHEMA_VERSION_V1])
    result = run_capture(
        [
            sys.executable,
            str(script),
            "--dataset",
            str(dataset),
            "--mode",
            mode,
            "--epochs",
            "8",
            "--learning-rate",
            "0.1",
            "--l2",
            "0.0",
            "--seed",
            "7",
            "--weights-out",
            str(weights),
            "--report-out",
            str(report),
            *effective_extra_args,
        ]
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return False
    return True


def run_phase_bias_v0a_expect_failure(script: Path, dataset: Path, expected_error: str) -> bool:
    return run_trainer_expect_failure(script, "phase-bias-v0a", dataset, expected_error)


def run_trainer_v0b_expect_failure(script: Path, dataset: Path, expected_error: str) -> bool:
    return run_trainer_expect_failure(script, "pattern-sgd-v0b", dataset, expected_error)


def run_trainer_expect_failure(script: Path, mode: str, dataset: Path, expected_error: str) -> bool:
    with tempfile.TemporaryDirectory() as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        result = run_capture(
            [
                sys.executable,
                str(script),
                "--dataset",
                str(dataset),
                "--mode",
                mode,
                "--weights-out",
                str(temp_dir / "weights.tsv"),
                "--report-out",
                str(temp_dir / "report.json"),
            ]
        )
    if result.returncode == 0:
        print(f"{mode} unexpectedly accepted invalid training dataset: {dataset}", file=sys.stderr)
        return False
    if expected_error not in result.stderr:
        print(f"{mode} did not report expected error {expected_error!r}", file=sys.stderr)
        sys.stderr.write(result.stderr)
        return False
    return True


def mutate_validation_and_test_labels(dataset_text: str) -> str:
    lines = dataset_text.splitlines()
    mutated = [lines[0]]
    for line in lines[1:]:
        fields = line.split("\t")
        if fields[2] == "validation":
            fields[3] = "64"
        elif fields[2] == "test":
            fields[3] = "-64"
        mutated.append("\t".join(fields))
    return "\n".join(mutated) + "\n"


def check_phase_bias_v0a_report(report: dict[str, object]) -> bool:
    expected_scalars = {
        "schema_version": 1,
        "trainer_algorithm": "phase-bias-v0a",
        "input_feature_rows": 56,
        "accepted_examples": 7,
        "rejected_examples": 0,
        "rejected_feature_rows": 0,
        "feature_rows_by_example_min": 8,
        "feature_rows_by_example_max": 8,
        "duplicate_feature_rows": 0,
    }
    for key, expected in expected_scalars.items():
        if report.get(key) != expected:
            print(f"v0a report field mismatch for {key}: {report.get(key)!r}", file=sys.stderr)
            return False
    if report.get("counts_by_split_examples") != {"train": 5, "validation": 1, "test": 1}:
        print(
            f"unexpected v0a split counts: {report.get('counts_by_split_examples')!r}",
            file=sys.stderr,
        )
        return False
    if report.get("counts_by_phase_examples") != {
        "0": 6,
        "1": 0,
        "2": 0,
        "3": 0,
        "4": 0,
        "5": 0,
        "6": 0,
        "7": 0,
        "8": 0,
        "9": 0,
        "10": 0,
        "11": 0,
        "12": 1,
    }:
        print(
            f"unexpected v0a phase counts: {report.get('counts_by_phase_examples')!r}",
            file=sys.stderr,
        )
        return False
    phase_bias = report.get("phase_bias")
    if not isinstance(phase_bias, dict):
        print("missing v0a phase_bias", file=sys.stderr)
        return False
    if phase_bias.get("0") != -0.25:
        print(
            f"phase 0 bias was not learned from train rows: {phase_bias.get('0')!r}",
            file=sys.stderr,
        )
        return False
    if phase_bias.get("12") != 4.0:
        print(
            f"phase 12 bias was not learned from train rows: {phase_bias.get('12')!r}",
            file=sys.stderr,
        )
        return False
    if phase_bias.get("1") != 0.0:
        print(
            f"phase 1 bias should be 0.0 without train rows: {phase_bias.get('1')!r}",
            file=sys.stderr,
        )
        return False
    metrics = report.get("metrics_by_split")
    if not isinstance(metrics, dict):
        print("missing v0a metrics", file=sys.stderr)
        return False
    for split in ("train", "validation", "test"):
        split_metrics = metrics.get(split)
        if not isinstance(split_metrics, dict) or split_metrics.get("rows", 0) <= 0:
            print(f"missing metrics for {split}: {split_metrics!r}", file=sys.stderr)
            return False
        for metric_name in ("MAE", "RMSE", "sign_accuracy"):
            if split_metrics.get(metric_name) is None:
                print(f"missing {metric_name} for {split}", file=sys.stderr)
                return False
    checksum = report.get("checksum")
    if not isinstance(checksum, str) or not checksum.startswith("sha256:"):
        print(f"invalid v0a checksum: {checksum!r}", file=sys.stderr)
        return False
    notes = report.get("notes")
    if not isinstance(notes, list) or not any(
        "example-level phase-bias" in str(note) for note in notes
    ):
        print(f"v0a report notes did not mention example-level training: {notes!r}", file=sys.stderr)
        return False
    return True


def first_data_line(dataset_text: str) -> str:
    lines = dataset_text.splitlines()
    if len(lines) < 2:
        raise ValueError("dataset has no data line")
    return lines[1]


def mutate_field(line: str, field_index: int, value: str) -> str:
    fields = line.split("\t")
    fields[field_index] = value
    return "\t".join(fields)


def check_partly_malformed_report(report: dict[str, object]) -> bool:
    expected = {
        "input_feature_rows": 58,
        "accepted_examples": 7,
        "rejected_examples": 2,
        "rejected_feature_rows": 2,
    }
    for key, expected_value in expected.items():
        if report.get(key) != expected_value:
            print(
                f"v0a rejected row field mismatch for {key}: {report.get(key)!r}",
                file=sys.stderr,
            )
            return False
    return True


def check_metadata_conflict_report(report: dict[str, object]) -> bool:
    expected = {
        "input_feature_rows": 57,
        "accepted_examples": 6,
        "rejected_examples": 1,
        "rejected_feature_rows": 9,
    }
    for key, expected_value in expected.items():
        if report.get(key) != expected_value:
            print(
                f"v0a metadata conflict field mismatch for {key}: {report.get(key)!r}",
                file=sys.stderr,
            )
            return False
    notes = report.get("notes")
    if not isinstance(notes, list) or not any("inconsistent metadata" in str(note) for note in notes):
        print(f"v0a metadata conflict was not reported in notes: {notes!r}", file=sys.stderr)
        return False
    return True


def check_duplicate_feature_report(report: dict[str, object]) -> bool:
    expected = {
        "input_feature_rows": 57,
        "accepted_examples": 7,
        "rejected_examples": 0,
        "rejected_feature_rows": 0,
        "duplicate_feature_rows": 1,
        "feature_rows_by_example_min": 8,
        "feature_rows_by_example_max": 9,
    }
    for key, expected_value in expected.items():
        if report.get(key) != expected_value:
            print(
                f"v0a duplicate feature field mismatch for {key}: {report.get(key)!r}",
                file=sys.stderr,
            )
            return False
    details = report.get("duplicate_feature_rows_by_record_id")
    if not isinstance(details, list) or len(details) != 1:
        print(f"v0a duplicate feature details are missing: {details!r}", file=sys.stderr)
        return False
    return True


def check_trainer_v0b_report(report: dict[str, object], weights: dict[str, object]) -> bool:
    expected_scalars = {
        "schema_version": 1,
        "trainer_algorithm": "pattern-sgd-v0b",
        "input_feature_rows": 56,
        "accepted_examples": 7,
        "rejected_examples": 0,
        "rejected_feature_rows": 0,
        "feature_rows_by_example_min": 8,
        "feature_rows_by_example_max": 8,
        "duplicate_feature_rows": 0,
        "epochs": 8,
        "learning_rate": 0.1,
        "l2": 0.0,
        "seed": 7,
    }
    for key, expected in expected_scalars.items():
        if report.get(key) != expected:
            print(f"v0b report field mismatch for {key}: {report.get(key)!r}", file=sys.stderr)
            return False
    if report.get("counts_by_split_examples") != {"train": 5, "validation": 1, "test": 1}:
        print(
            f"unexpected v0b split counts: {report.get('counts_by_split_examples')!r}",
            file=sys.stderr,
        )
        return False
    if weights.get("weights_schema_version") != WEIGHTS_SCHEMA_VERSION_V2:
        print(
            f"unexpected v0b weights schema: {weights.get('weights_schema_version')!r}",
            file=sys.stderr,
        )
        return False
    for key in (
        "pattern_set_id",
        "pattern_contract_digest",
        "index_mode",
        "phase_count",
        "phase_mapping_id",
        "score_unit",
    ):
        if key not in weights:
            print(f"v0b weights missing v2 field {key}", file=sys.stderr)
            return False
    if "trainer_algorithm" in weights:
        print("v0b weights must not contain trainer_algorithm", file=sys.stderr)
        return False
    if not isinstance(weights.get("phase_bias"), dict):
        print("missing v0b phase_bias weights", file=sys.stderr)
        return False
    pattern_weights = weights.get("pattern_weights")
    if not isinstance(pattern_weights, list) or not pattern_weights:
        print(f"missing learned v0b pattern weights: {pattern_weights!r}", file=sys.stderr)
        return False
    if report.get("nonzero_weight_count") != len(pattern_weights):
        print("v0b nonzero weight count does not match weights JSON", file=sys.stderr)
        return False
    if not isinstance(report.get("weight_l2_norm"), float) or report.get("weight_l2_norm") <= 0.0:
        print(f"invalid v0b weight_l2_norm: {report.get('weight_l2_norm')!r}", file=sys.stderr)
        return False
    for checksum_key in ("weights_checksum", "checksum"):
        checksum = report.get(checksum_key)
        if not isinstance(checksum, str) or not checksum.startswith("sha256:"):
            print(f"invalid v0b {checksum_key}: {checksum!r}", file=sys.stderr)
            return False
    if report.get("shuffle_policy") != "deterministic python random.Random(seed + epoch)":
        print(f"unexpected v0b shuffle policy: {report.get('shuffle_policy')!r}", file=sys.stderr)
        return False
    notes = report.get("notes")
    if not isinstance(notes, list) or not any(
        "not a production trainer" in str(note) for note in notes
    ):
        print(f"v0b report notes did not mark trainer as non-production: {notes!r}", file=sys.stderr)
        return False
    if not any("duplicate feature rows" in str(note) for note in notes):
        print(
            f"v0b report notes did not mention duplicate contribution policy: {notes!r}",
            file=sys.stderr,
        )
        return False
    epochs = report.get("metrics_by_epoch")
    if not isinstance(epochs, list) or len(epochs) != 8:
        print(f"unexpected v0b epoch metrics: {epochs!r}", file=sys.stderr)
        return False

    baseline = report.get("baseline_phase_bias_metrics")
    final = report.get("final_pattern_sgd_metrics")
    if not isinstance(baseline, dict) or not isinstance(final, dict):
        print("missing v0b baseline/final metrics", file=sys.stderr)
        return False
    baseline_split = baseline.get("metrics_by_split")
    final_split = final.get("metrics_by_split")
    if not isinstance(baseline_split, dict) or not isinstance(final_split, dict):
        print("missing v0b split metrics", file=sys.stderr)
        return False
    baseline_train = baseline_split.get("train")
    final_train = final_split.get("train")
    if not isinstance(baseline_train, dict) or not isinstance(final_train, dict):
        print("missing v0b train metrics", file=sys.stderr)
        return False
    if final_train.get("MAE") > baseline_train.get("MAE"):
        print(
            f"v0b train MAE regressed: {final_train.get('MAE')} > {baseline_train.get('MAE')}",
            file=sys.stderr,
        )
        return False
    if final_train.get("RMSE") > baseline_train.get("RMSE"):
        print(
            f"v0b train RMSE regressed: {final_train.get('RMSE')} > {baseline_train.get('RMSE')}",
            file=sys.stderr,
        )
        return False
    return True


def check_trainer_v0c_report(report: dict[str, object], weights: dict[str, object]) -> bool:
    expected_scalars = {
        "schema_version": 1,
        "trainer_algorithm": "pattern-sgd-v0c",
        "input_format": "expanded-tsv",
        "input_feature_rows": 56,
        "example_count": 7,
        "feature_occurrence_count": 56,
        "feature_rows_by_example_min": 8,
        "feature_rows_by_example_max": 8,
        "duplicate_feature_rows": 0,
        "epochs_requested": 8,
        "epochs_completed": 8,
        "learning_rate": 0.1,
        "lr_schedule": "constant",
        "weight_decay": 0.0,
        "deterministic_seed": 7,
    }
    for key, expected in expected_scalars.items():
        if report.get(key) != expected:
            print(f"v0c report field mismatch for {key}: {report.get(key)!r}", file=sys.stderr)
            return False
    if weights.get("weights_schema_version") != WEIGHTS_SCHEMA_VERSION_V2:
        print("v0c weights JSON must use the shared pattern-eval weights schema", file=sys.stderr)
        return False
    for key in (
        "pattern_set_id",
        "pattern_contract_digest",
        "index_mode",
        "phase_count",
        "phase_mapping_id",
        "score_unit",
    ):
        if key not in weights:
            print(f"v0c weights missing v2 field {key}", file=sys.stderr)
            return False
    if "trainer_algorithm" in weights:
        print("v0c weights must not contain trainer_algorithm", file=sys.stderr)
        return False
    if not isinstance(weights.get("pattern_weights"), list) or not weights.get("pattern_weights"):
        print(f"missing v0c learned pattern weights: {weights.get('pattern_weights')!r}", file=sys.stderr)
        return False
    if report.get("nonzero_weight_count") != len(weights["pattern_weights"]):
        print("v0c nonzero weight count does not match weights JSON", file=sys.stderr)
        return False
    for key in ("best_validation_MAE", "final_validation_MAE", "weight_l2_norm", "max_abs_weight"):
        if report.get(key) is None:
            print(f"v0c report missing {key}", file=sys.stderr)
            return False
    baseline = report.get("baseline_phase_bias_metrics")
    final = report.get("final_pattern_sgd_metrics")
    if not isinstance(baseline, dict) or not isinstance(final, dict):
        print("v0c report missing baseline/final metrics", file=sys.stderr)
        return False
    baseline_split = baseline.get("metrics_by_split")
    final_split = final.get("metrics_by_split")
    final_split_phase = final.get("metrics_by_split_phase")
    if (
        not isinstance(baseline_split, dict)
        or not isinstance(final_split, dict)
        or not isinstance(final_split_phase, dict)
    ):
        print("v0c report missing split or split-phase metrics", file=sys.stderr)
        return False
    for split in ("train", "validation", "test"):
        split_metrics = final_split.get(split)
        if not isinstance(split_metrics, dict):
            print(f"v0c missing split metrics for {split}", file=sys.stderr)
            return False
        for key in ("examples", "feature_occurrences", "MAE", "RMSE", "sign_accuracy", "residual_MAE", "residual_RMSE", "label_mean", "residual_mean"):
            if key not in split_metrics:
                print(f"v0c split metrics missing {key}: {split_metrics!r}", file=sys.stderr)
                return False
        phase_metrics = final_split_phase.get(split)
        if not isinstance(phase_metrics, dict) or set(phase_metrics) != {str(phase) for phase in range(13)}:
            print(f"v0c split-phase metrics missing phases for {split}", file=sys.stderr)
            return False
        empty_phase = phase_metrics.get("1")
        if not isinstance(empty_phase, dict) or empty_phase.get("examples") != 0 or empty_phase.get("MAE") is not None:
            print(f"v0c empty phase bucket is not stable: {empty_phase!r}", file=sys.stderr)
            return False
    train_baseline = baseline_split.get("train")
    train_final = final_split.get("train")
    if not isinstance(train_baseline, dict) or not isinstance(train_final, dict):
        print("v0c missing train metrics", file=sys.stderr)
        return False
    if train_final.get("MAE") > train_baseline.get("MAE"):
        print(
            f"v0c train MAE regressed: {train_final.get('MAE')} > {train_baseline.get('MAE')}",
            file=sys.stderr,
        )
        return False
    epochs = report.get("metrics_by_epoch")
    if not isinstance(epochs, list) or len(epochs) != 8:
        print(f"unexpected v0c epoch metrics: {epochs!r}", file=sys.stderr)
        return False
    if not all("updated_feature_occurrence_count" in epoch for epoch in epochs):
        print("v0c epoch metrics missing optimizer behavior", file=sys.stderr)
        return False
    notes = report.get("notes")
    if not isinstance(notes, list) or not any("not a production trainer" in str(note) for note in notes):
        print(f"v0c report notes did not mark trainer as non-production: {notes!r}", file=sys.stderr)
        return False
    return True


def check_trainer_v0c_optimizer_controls(
    trainer_script: Path, dataset_path: Path, temp_dir: Path
) -> bool:
    inverse_weights = temp_dir / "inverse-v0c-weights.json"
    inverse_report_path = temp_dir / "inverse-v0c-report.json"
    if not run_trainer_v0c(
        trainer_script,
        dataset_path,
        inverse_weights,
        inverse_report_path,
        ["--lr-schedule", "inverse-sqrt"],
    ):
        return False
    inverse_report = json.loads(inverse_report_path.read_text(encoding="utf-8"))
    inverse_epochs = inverse_report.get("metrics_by_epoch")
    if not isinstance(inverse_epochs, list) or len(inverse_epochs) < 2:
        print("inverse-sqrt v0c report missing epoch metrics", file=sys.stderr)
        return False
    if inverse_epochs[1].get("learning_rate") >= inverse_epochs[0].get("learning_rate"):
        print(f"inverse-sqrt learning rate did not decay: {inverse_epochs!r}", file=sys.stderr)
        return False

    clipped_weights = temp_dir / "clipped-v0c-weights.json"
    clipped_report_path = temp_dir / "clipped-v0c-report.json"
    if not run_trainer_v0c(
        trainer_script,
        dataset_path,
        clipped_weights,
        clipped_report_path,
        ["--gradient-clip", "0.000000001"],
    ):
        return False
    clipped_report = json.loads(clipped_report_path.read_text(encoding="utf-8"))
    clipped_epochs = clipped_report.get("metrics_by_epoch")
    if not isinstance(clipped_epochs, list) or not any(
        epoch.get("gradient_clip_count", 0) > 0 for epoch in clipped_epochs
    ):
        print(f"v0c gradient clip counts were not recorded: {clipped_epochs!r}", file=sys.stderr)
        return False

    stopped_weights = temp_dir / "stopped-v0c-weights.json"
    stopped_report_path = temp_dir / "stopped-v0c-report.json"
    if not run_trainer_v0c(
        trainer_script,
        dataset_path,
        stopped_weights,
        stopped_report_path,
        ["--early-stop-patience", "0"],
    ):
        return False
    stopped_report = json.loads(stopped_report_path.read_text(encoding="utf-8"))
    if stopped_report.get("early_stop_triggered") is not True:
        print(f"v0c early stop was not triggered: {stopped_report!r}", file=sys.stderr)
        return False
    if stopped_report.get("epochs_completed") >= stopped_report.get("epochs_requested"):
        print(f"v0c early stop did not stop before max epochs: {stopped_report!r}", file=sys.stderr)
        return False
    return True


def write_v0d_phase_balance_dataset(path: Path) -> None:
    path.write_text(
        "record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_features\n"
        "train-common-a\t8\ttrain\t10\t0\tedge-8:0:1,corner-3x3:0:2\n"
        "train-common-b\t10\ttrain\t8\t0\tedge-8:0:1,corner-3x3:0:3\n"
        "train-common-c\t12\ttrain\t6\t0\tedge-8:0:2,corner-3x3:0:4\n"
        "train-common-d\t14\ttrain\t4\t0\tedge-8:0:2,corner-3x3:0:5\n"
        "train-rare-a\t16\ttrain\t-10\t2\tedge-8:0:3,corner-3x3:0:6\n"
        "validation-a\t18\tvalidation\t5\t0\tedge-8:0:1,corner-3x3:0:2\n"
        "validation-b\t20\tvalidation\t-8\t2\tedge-8:0:3,corner-3x3:0:6\n"
        "test-a\t22\ttest\t3\t0\tedge-8:0:2,corner-3x3:0:4\n"
        "test-b\t24\ttest\t-6\t2\tedge-8:0:3,corner-3x3:0:6\n",
        encoding="utf-8",
    )


def check_trainer_v0d_phase_balance(trainer_script: Path, temp_dir: Path) -> bool:
    dataset = temp_dir / "v0d-phase-balance.tsv"
    write_v0d_phase_balance_dataset(dataset)
    first_weights = temp_dir / "first-v0d-weights.json"
    first_report_path = temp_dir / "first-v0d-report.json"
    second_weights = temp_dir / "second-v0d-weights.json"
    second_report_path = temp_dir / "second-v0d-report.json"
    if not run_trainer_v0d(trainer_script, dataset, first_weights, first_report_path):
        return False
    if not run_trainer_v0d(trainer_script, dataset, second_weights, second_report_path):
        return False
    if first_weights.read_text(encoding="utf-8") != second_weights.read_text(encoding="utf-8"):
        print("v0d weights are not deterministic across repeated runs", file=sys.stderr)
        return False
    if first_report_path.read_text(encoding="utf-8") != second_report_path.read_text(encoding="utf-8"):
        print("v0d report is not deterministic across repeated runs", file=sys.stderr)
        return False
    report = json.loads(first_report_path.read_text(encoding="utf-8"))
    weights = json.loads(first_weights.read_text(encoding="utf-8"))
    if report.get("trainer_algorithm") != "pattern-sgd-v0d":
        print(f"unexpected v0d trainer algorithm: {report.get('trainer_algorithm')!r}", file=sys.stderr)
        return False
    if weights.get("weights_schema_version") != WEIGHTS_SCHEMA_VERSION_V1:
        print("v0d weights JSON must use the shared pattern-eval weights schema", file=sys.stderr)
        return False
    if report.get("phase_balance") != "sqrt-inverse-count":
        print(f"unexpected default phase balance: {report.get('phase_balance')!r}", file=sys.stderr)
        return False
    phase_counts = report.get("phase_train_counts")
    if phase_counts != {"0": 4, "1": 0, "2": 1, "3": 0, "4": 0, "5": 0, "6": 0, "7": 0, "8": 0, "9": 0, "10": 0, "11": 0, "12": 0}:
        print(f"unexpected v0d phase train counts: {phase_counts!r}", file=sys.stderr)
        return False
    phase_weights = report.get("phase_weights")
    if not isinstance(phase_weights, dict):
        print(f"missing v0d phase weights: {phase_weights!r}", file=sys.stderr)
        return False
    if not phase_weights["2"] > phase_weights["0"]:
        print(f"rare phase was not weighted above common phase: {phase_weights!r}", file=sys.stderr)
        return False
    weighted_average = (4 * float(phase_weights["0"]) + float(phase_weights["2"])) / 5
    if abs(weighted_average - 1.0) > 1e-9:
        print(f"v0d phase weights were not normalized: {phase_weights!r}", file=sys.stderr)
        return False
    if report.get("weighted_train_residual_MAE") is None:
        print("v0d report missing weighted train residual MAE", file=sys.stderr)
        return False
    final = report.get("final_pattern_sgd_metrics")
    baseline = report.get("baseline_phase_bias_metrics")
    if not isinstance(final, dict) or not isinstance(baseline, dict):
        print("v0d report missing compatible metrics blocks", file=sys.stderr)
        return False
    if "metrics_by_split_phase" not in final or "metrics_by_split_phase" not in baseline:
        print("v0d report missing split-phase metrics", file=sys.stderr)
        return False

    none_weights = temp_dir / "none-v0d-weights.json"
    none_report_path = temp_dir / "none-v0d-report.json"
    if not run_trainer_v0d(
        trainer_script,
        dataset,
        none_weights,
        none_report_path,
        ["--phase-balance", "none"],
    ):
        return False
    none_report = json.loads(none_report_path.read_text(encoding="utf-8"))
    none_phase_weights = none_report.get("phase_weights")
    if none_phase_weights.get("0") != 1.0 or none_phase_weights.get("2") != 1.0:
        print(f"phase-balance none did not use unit active weights: {none_phase_weights!r}", file=sys.stderr)
        return False

    capped_report_path = temp_dir / "capped-v0d-report.json"
    if not run_trainer_v0d(
        trainer_script,
        dataset,
        temp_dir / "capped-v0d-weights.json",
        capped_report_path,
        ["--phase-balance", "inverse-count", "--max-phase-weight", "1.1", "--min-phase-weight", "0.9"],
    ):
        return False
    capped_weights = json.loads(capped_report_path.read_text(encoding="utf-8")).get("phase_weights")
    if not isinstance(capped_weights, dict) or capped_weights.get("2") != 1.1:
        print(f"v0d phase weight cap was not applied: {capped_weights!r}", file=sys.stderr)
        return False
    if capped_weights.get("0") < 0.9:
        print(f"v0d phase weight floor was not respected: {capped_weights!r}", file=sys.stderr)
        return False

    result = run_capture(
        [
            sys.executable,
            str(trainer_script),
            "--dataset",
            str(dataset),
            "--mode",
            "pattern-sgd-v0c",
            "--phase-balance",
            "sqrt-inverse-count",
            "--weights-out",
            str(temp_dir / "invalid-v0c-weights.json"),
            "--report-out",
            str(temp_dir / "invalid-v0c-report.json"),
        ]
    )
    if result.returncode == 0 or "require --mode pattern-sgd-v0d" not in result.stderr:
        print("v0c accepted phase-balance args reserved for pattern-sgd-v0d", file=sys.stderr)
        sys.stderr.write(result.stderr)
        return False
    invalid_bounds = run_capture(
        [
            sys.executable,
            str(trainer_script),
            "--dataset",
            str(dataset),
            "--mode",
            "pattern-sgd-v0d",
            "--max-phase-weight",
            "0.5",
            "--min-phase-weight",
            "2.0",
            "--weights-out",
            str(temp_dir / "invalid-bounds-weights.json"),
            "--report-out",
            str(temp_dir / "invalid-bounds-report.json"),
        ]
    )
    if invalid_bounds.returncode == 0 or "--min-phase-weight must be <= --max-phase-weight" not in invalid_bounds.stderr:
        print("v0d accepted invalid phase weight bounds", file=sys.stderr)
        sys.stderr.write(invalid_bounds.stderr)
        return False
    return True


def check_compact_v0b_equivalence(
    trainer_script: Path,
    dataset_exe: Path,
    normalized_tsv: Path,
    expanded_dataset_text: str,
    expanded_weights_text: str,
    expanded_report: dict[str, object],
    temp_dir: Path,
) -> bool:
    compact_report_path = temp_dir / "compact-dataset-report.json"
    compact_dataset_text = run_dataset(
        dataset_exe, normalized_tsv, compact_report_path, output_format="compact-tsv"
    )
    if compact_dataset_text is None:
        return False
    compact_dataset = temp_dir / "compact-pattern-dataset.tsv"
    compact_dataset.write_text(compact_dataset_text, encoding="utf-8")
    compact_weights = temp_dir / "compact-v0b-weights.json"
    compact_trainer_report = temp_dir / "compact-v0b-report.json"
    if not run_trainer_v0b(
        trainer_script,
        compact_dataset,
        compact_weights,
        compact_trainer_report,
        ["--dataset-report", str(compact_report_path)],
    ):
        return False
    if compact_weights.read_text(encoding="utf-8") != expanded_weights_text:
        print("compact v0b weights differ from expanded v0b weights", file=sys.stderr)
        return False
    compact_report = json.loads(compact_trainer_report.read_text(encoding="utf-8"))
    if compact_report.get("input_format") != "compact-tsv":
        print(f"unexpected compact trainer input format: {compact_report.get('input_format')!r}", file=sys.stderr)
        return False
    if compact_report.get("feature_occurrence_count") != len(expanded_dataset_text.splitlines()) - 1:
        print("compact trainer feature occurrence count does not match expanded rows", file=sys.stderr)
        return False
    for key in ("baseline_phase_bias_metrics", "final_pattern_sgd_metrics", "metrics_by_epoch"):
        if compact_report.get(key) != expanded_report.get(key):
            print(f"compact trainer metrics differ for {key}", file=sys.stderr)
            return False
    malformed = temp_dir / "malformed-compact.tsv"
    malformed.write_text(
        "record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_features\n"
        "bad-compact\t0\ttrain\t1\t0\tedge-8:0\n",
        encoding="utf-8",
    )
    if not run_trainer_v0b_expect_failure(
        trainer_script,
        malformed,
        "pattern_features token must be pattern_id:instance:ternary_index",
    ):
        return False
    empty = temp_dir / "empty-compact.tsv"
    empty.write_text(
        "record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_features\n"
        "empty-compact\t0\ttrain\t1\t0\t\n",
        encoding="utf-8",
    )
    if not run_trainer_v0b_expect_failure(
        trainer_script, empty, "pattern_features must be non-empty"
    ):
        return False
    return True


def check_compact_v0c_equivalence(
    trainer_script: Path,
    dataset_exe: Path,
    normalized_tsv: Path,
    expanded_dataset_text: str,
    expanded_weights_text: str,
    expanded_report: dict[str, object],
    temp_dir: Path,
) -> bool:
    compact_report_path = temp_dir / "compact-v0c-dataset-report.json"
    compact_dataset_text = run_dataset(
        dataset_exe, normalized_tsv, compact_report_path, output_format="compact-tsv"
    )
    if compact_dataset_text is None:
        return False
    compact_dataset = temp_dir / "compact-v0c-pattern-dataset.tsv"
    compact_dataset.write_text(compact_dataset_text, encoding="utf-8")
    compact_weights = temp_dir / "compact-v0c-weights.json"
    compact_trainer_report = temp_dir / "compact-v0c-report.json"
    if not run_trainer_v0c(
        trainer_script,
        compact_dataset,
        compact_weights,
        compact_trainer_report,
        ["--dataset-report", str(compact_report_path)],
    ):
        return False
    if compact_weights.read_text(encoding="utf-8") != expanded_weights_text:
        print("compact v0c weights differ from expanded v0c weights", file=sys.stderr)
        return False
    compact_report = json.loads(compact_trainer_report.read_text(encoding="utf-8"))
    if compact_report.get("input_format") != "compact-tsv":
        print(f"unexpected compact v0c input format: {compact_report.get('input_format')!r}", file=sys.stderr)
        return False
    if compact_report.get("feature_occurrence_count") != len(expanded_dataset_text.splitlines()) - 1:
        print("compact v0c feature occurrence count does not match expanded rows", file=sys.stderr)
        return False
    for key in ("baseline_phase_bias_metrics", "final_pattern_sgd_metrics", "metrics_by_epoch"):
        if compact_report.get(key) != expanded_report.get(key):
            print(f"compact v0c metrics differ for {key}", file=sys.stderr)
            return False
    return True


def check_phase_bias_v0a(trainer_script: Path, dataset_exe: Path, normalized_tsv: Path) -> bool:
    with tempfile.TemporaryDirectory() as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        dataset_report = temp_dir / "dataset-report.json"
        dataset_path = temp_dir / "pattern-dataset.tsv"

        dataset_text = run_dataset(dataset_exe, normalized_tsv, dataset_report)
        if dataset_text is None:
            return False
        dataset_path.write_text(dataset_text, encoding="utf-8")

        first_weights = temp_dir / "first-weights.tsv"
        first_report = temp_dir / "first-report.json"
        second_weights = temp_dir / "second-weights.tsv"
        second_report = temp_dir / "second-report.json"
        if not run_phase_bias_v0a(trainer_script, dataset_path, first_weights, first_report):
            return False
        if not run_phase_bias_v0a(trainer_script, dataset_path, second_weights, second_report):
            return False
        if first_weights.read_text(encoding="utf-8") != second_weights.read_text(encoding="utf-8"):
            print("v0a weights are not deterministic across repeated runs", file=sys.stderr)
            return False
        if first_report.read_text(encoding="utf-8") != second_report.read_text(encoding="utf-8"):
            print("v0a report is not deterministic across repeated runs", file=sys.stderr)
            return False

        report = json.loads(first_report.read_text(encoding="utf-8"))
        if not check_phase_bias_v0a_report(report):
            return False

        mutated_dataset = temp_dir / "mutated-validation-test.tsv"
        mutated_dataset.write_text(mutate_validation_and_test_labels(dataset_text), encoding="utf-8")
        mutated_weights = temp_dir / "mutated-weights.tsv"
        mutated_report = temp_dir / "mutated-report.json"
        if not run_phase_bias_v0a(trainer_script, mutated_dataset, mutated_weights, mutated_report):
            return False
        if first_weights.read_text(encoding="utf-8") != mutated_weights.read_text(encoding="utf-8"):
            print("v0a weights changed after mutating validation/test labels", file=sys.stderr)
            return False
        mutated_metrics = json.loads(mutated_report.read_text(encoding="utf-8")).get(
            "metrics_by_split", {}
        )
        if mutated_metrics == report.get("metrics_by_split"):
            print(
                "v0a validation/test metrics did not reflect held-out label changes",
                file=sys.stderr,
            )
            return False

        malformed_path = temp_dir / "partly-malformed.tsv"
        malformed_weights = temp_dir / "partly-malformed-weights.tsv"
        malformed_report = temp_dir / "partly-malformed-report.json"
        malformed_path.write_text(
            dataset_text
            + "bad-split\t0\toops\t0\t0\tedge-8\t0\t0\n"
            + "bad-label\t0\ttrain\t65\t0\tedge-8\t0\t0\n",
            encoding="utf-8",
        )
        if not run_phase_bias_v0a(
            trainer_script, malformed_path, malformed_weights, malformed_report
        ):
            return False
        malformed = json.loads(malformed_report.read_text(encoding="utf-8"))
        if not check_partly_malformed_report(malformed):
            return False

        conflict_path = temp_dir / "metadata-conflict.tsv"
        conflict_weights = temp_dir / "metadata-conflict-weights.tsv"
        conflict_report = temp_dir / "metadata-conflict-report.json"
        conflict_line = mutate_field(first_data_line(dataset_text), 2, "validation")
        conflict_path.write_text(dataset_text + conflict_line + "\n", encoding="utf-8")
        if not run_phase_bias_v0a(
            trainer_script, conflict_path, conflict_weights, conflict_report
        ):
            return False
        conflict = json.loads(conflict_report.read_text(encoding="utf-8"))
        if not check_metadata_conflict_report(conflict):
            return False

        duplicate_path = temp_dir / "duplicate-feature.tsv"
        duplicate_weights = temp_dir / "duplicate-feature-weights.tsv"
        duplicate_report = temp_dir / "duplicate-feature-report.json"
        duplicate_path.write_text(
            dataset_text + first_data_line(dataset_text) + "\n", encoding="utf-8"
        )
        if not run_phase_bias_v0a(
            trainer_script, duplicate_path, duplicate_weights, duplicate_report
        ):
            return False
        duplicate = json.loads(duplicate_report.read_text(encoding="utf-8"))
        if not check_duplicate_feature_report(duplicate):
            return False

        no_accepted_path = temp_dir / "no-accepted.tsv"
        no_accepted_path.write_text(
            "record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_id\tinstance\tternary_index\n"
            "bad-label\t0\ttrain\t65\t0\tedge-8\t0\t0\n",
            encoding="utf-8",
        )
        if not run_phase_bias_v0a_expect_failure(
            trainer_script, no_accepted_path, "dataset has no accepted examples"
        ):
            return False

        no_train_path = temp_dir / "no-train.tsv"
        no_train_path.write_text(
            "record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_id\tinstance\tternary_index\n"
            "validation-row\t0\tvalidation\t1\t0\tedge-8\t0\t0\n"
            "test-row\t0\ttest\t-1\t0\tedge-8\t0\t0\n",
            encoding="utf-8",
        )
        if not run_phase_bias_v0a_expect_failure(
            trainer_script, no_train_path, "dataset has no accepted train examples"
        ):
            return False

    return True


def check_trainer_v0b(trainer_script: Path, dataset_exe: Path, normalized_tsv: Path) -> bool:
    with tempfile.TemporaryDirectory() as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        dataset_report = temp_dir / "dataset-report.json"
        dataset_path = temp_dir / "pattern-dataset.tsv"

        dataset_text = run_dataset(dataset_exe, normalized_tsv, dataset_report)
        if dataset_text is None:
            return False
        dataset_path.write_text(dataset_text, encoding="utf-8")

        first_weights = temp_dir / "first-v0b-weights.json"
        first_report = temp_dir / "first-v0b-report.json"
        second_weights = temp_dir / "second-v0b-weights.json"
        second_report = temp_dir / "second-v0b-report.json"
        v2_metadata_args = ["--dataset-report", str(dataset_report)]
        if not run_trainer_v0b(
            trainer_script, dataset_path, first_weights, first_report, v2_metadata_args
        ):
            return False
        if not run_trainer_v0b(
            trainer_script, dataset_path, second_weights, second_report, v2_metadata_args
        ):
            return False
        if first_weights.read_text(encoding="utf-8") != second_weights.read_text(encoding="utf-8"):
            print("v0b weights are not deterministic across repeated runs", file=sys.stderr)
            return False
        if first_report.read_text(encoding="utf-8") != second_report.read_text(encoding="utf-8"):
            print("v0b report is not deterministic across repeated runs", file=sys.stderr)
            return False

        report = json.loads(first_report.read_text(encoding="utf-8"))
        weights = json.loads(first_weights.read_text(encoding="utf-8"))
        if not check_trainer_v0b_report(report, weights):
            return False
        if not check_compact_v0b_equivalence(
            trainer_script,
            dataset_exe,
            normalized_tsv,
            dataset_text,
            first_weights.read_text(encoding="utf-8"),
            report,
            temp_dir,
        ):
            return False

        first_v0c_weights = temp_dir / "first-v0c-weights.json"
        first_v0c_report = temp_dir / "first-v0c-report.json"
        second_v0c_weights = temp_dir / "second-v0c-weights.json"
        second_v0c_report = temp_dir / "second-v0c-report.json"
        if not run_trainer_v0c(
            trainer_script, dataset_path, first_v0c_weights, first_v0c_report, v2_metadata_args
        ):
            return False
        if not run_trainer_v0c(
            trainer_script, dataset_path, second_v0c_weights, second_v0c_report, v2_metadata_args
        ):
            return False
        if first_v0c_weights.read_text(encoding="utf-8") != second_v0c_weights.read_text(encoding="utf-8"):
            print("v0c weights are not deterministic across repeated runs", file=sys.stderr)
            return False
        if first_v0c_report.read_text(encoding="utf-8") != second_v0c_report.read_text(encoding="utf-8"):
            print("v0c report is not deterministic across repeated runs", file=sys.stderr)
            return False
        v0c_report = json.loads(first_v0c_report.read_text(encoding="utf-8"))
        v0c_weights = json.loads(first_v0c_weights.read_text(encoding="utf-8"))
        if not check_trainer_v0c_report(v0c_report, v0c_weights):
            return False
        if not check_compact_v0c_equivalence(
            trainer_script,
            dataset_exe,
            normalized_tsv,
            dataset_text,
            first_v0c_weights.read_text(encoding="utf-8"),
            v0c_report,
            temp_dir,
        ):
            return False
        if not check_trainer_v0c_optimizer_controls(trainer_script, dataset_path, temp_dir):
            return False

        mutated_dataset = temp_dir / "mutated-validation-test.tsv"
        mutated_dataset.write_text(mutate_validation_and_test_labels(dataset_text), encoding="utf-8")
        mutated_weights = temp_dir / "mutated-v0b-weights.json"
        mutated_report = temp_dir / "mutated-v0b-report.json"
        if not run_trainer_v0b(
            trainer_script, mutated_dataset, mutated_weights, mutated_report, v2_metadata_args
        ):
            return False
        if first_weights.read_text(encoding="utf-8") != mutated_weights.read_text(encoding="utf-8"):
            print("v0b weights changed after mutating validation/test labels", file=sys.stderr)
            return False
        mutated_metrics = json.loads(mutated_report.read_text(encoding="utf-8")).get(
            "final_pattern_sgd_metrics", {}
        )
        if mutated_metrics == report.get("final_pattern_sgd_metrics"):
            print(
                "v0b held-out metrics did not reflect validation/test label changes",
                file=sys.stderr,
            )
            return False

        malformed_path = temp_dir / "partly-malformed.tsv"
        malformed_weights = temp_dir / "partly-malformed-v0b-weights.json"
        malformed_report = temp_dir / "partly-malformed-v0b-report.json"
        malformed_path.write_text(
            dataset_text
            + "bad-split\t0\toops\t0\t0\tedge-8\t0\t0\n"
            + "bad-label\t0\ttrain\t65\t0\tedge-8\t0\t0\n",
            encoding="utf-8",
        )
        if not run_trainer_v0b(
            trainer_script, malformed_path, malformed_weights, malformed_report, v2_metadata_args
        ):
            return False
        malformed = json.loads(malformed_report.read_text(encoding="utf-8"))
        if not check_partly_malformed_report(malformed):
            return False

        conflict_path = temp_dir / "metadata-conflict.tsv"
        conflict_weights = temp_dir / "metadata-conflict-v0b-weights.json"
        conflict_report = temp_dir / "metadata-conflict-v0b-report.json"
        conflict_line = mutate_field(first_data_line(dataset_text), 2, "validation")
        conflict_path.write_text(dataset_text + conflict_line + "\n", encoding="utf-8")
        if not run_trainer_v0b(
            trainer_script, conflict_path, conflict_weights, conflict_report, v2_metadata_args
        ):
            return False
        conflict = json.loads(conflict_report.read_text(encoding="utf-8"))
        if not check_metadata_conflict_report(conflict):
            return False

        duplicate_path = temp_dir / "duplicate-feature.tsv"
        duplicate_weights = temp_dir / "duplicate-feature-v0b-weights.json"
        duplicate_report = temp_dir / "duplicate-feature-v0b-report.json"
        duplicate_path.write_text(
            dataset_text + first_data_line(dataset_text) + "\n", encoding="utf-8"
        )
        if not run_trainer_v0b(
            trainer_script, duplicate_path, duplicate_weights, duplicate_report, v2_metadata_args
        ):
            return False
        duplicate = json.loads(duplicate_report.read_text(encoding="utf-8"))
        if not check_duplicate_feature_report(duplicate):
            return False

        no_accepted_path = temp_dir / "no-accepted.tsv"
        no_accepted_path.write_text(
            "record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_id\tinstance\tternary_index\n"
            "bad-label\t0\ttrain\t65\t0\tedge-8\t0\t0\n",
            encoding="utf-8",
        )
        if not run_trainer_v0b_expect_failure(
            trainer_script, no_accepted_path, "dataset has no accepted examples"
        ):
            return False

        no_train_path = temp_dir / "no-train.tsv"
        no_train_path.write_text(
            "record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_id\tinstance\tternary_index\n"
            "validation-row\t0\tvalidation\t1\t0\tedge-8\t0\t0\n"
            "test-row\t0\ttest\t-1\t0\tedge-8\t0\t0\n",
            encoding="utf-8",
        )
        if not run_trainer_v0b_expect_failure(
            trainer_script, no_train_path, "dataset has no accepted train examples"
        ):
            return False

    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trainer-exe", required=True, type=Path)
    parser.add_argument("--trainer", required=True, type=Path)
    parser.add_argument("--dataset-exe", required=True, type=Path)
    parser.add_argument("--normalized-tsv", required=True, type=Path)
    parser.add_argument("--records", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        dataset_path = temp_dir / "pattern-dataset.tsv"

        dataset_result = run_capture(
            [
                str(args.dataset_exe),
                "--records",
                str(args.records),
                "--manifest",
                str(args.manifest),
                "--split-policy",
                "tiny-cycle",
            ]
        )
        if dataset_result.returncode != 0:
            sys.stderr.write(dataset_result.stderr)
            sys.stderr.write(dataset_result.stdout)
            return dataset_result.returncode
        dataset_path.write_text(dataset_result.stdout, encoding="utf-8")

        trainer_result = run_capture(
            [str(args.trainer_exe), "--dataset", str(dataset_path)],
        )
        if trainer_result.returncode != 0:
            sys.stderr.write(trainer_result.stderr)
            sys.stderr.write(trainer_result.stdout)
            return trainer_result.returncode

        try:
            summary = parse_summary(trainer_result.stdout)
        except ValueError as error:
            print(error, file=sys.stderr)
            return 1

        if summary != EXPECTED_SUMMARY:
            print(f"unexpected trainer summary: {summary}", file=sys.stderr)
            return 1

        malformed_path = temp_dir / "malformed.tsv"
        malformed_path.write_text(
            "record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_id\tinstance\tternary_index\n"
            "tiny-bad\t1\ttrain\t3\n",
            encoding="utf-8",
        )
        malformed_result = run_capture(
            [str(args.trainer_exe), "--dataset", str(malformed_path)],
        )
        if malformed_result.returncode == 0:
            print("malformed dataset unexpectedly succeeded", file=sys.stderr)
            return 1
        if "line 2: expected 8 TSV fields" not in malformed_result.stderr:
            print("malformed dataset did not report the expected error", file=sys.stderr)
            sys.stderr.write(malformed_result.stderr)
            return 1

    if not check_phase_bias_v0a(
        args.trainer,
        args.dataset_exe,
        args.normalized_tsv,
    ):
        return 1
    if not check_trainer_v0b(
        args.trainer,
        args.dataset_exe,
        args.normalized_tsv,
    ):
        return 1
    with tempfile.TemporaryDirectory() as temp_dir_name:
        if not check_trainer_v0d_phase_balance(args.trainer, Path(temp_dir_name)):
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
