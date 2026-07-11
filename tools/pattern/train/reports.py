"""Pattern trainer report construction and mode runners."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any

from dataset_contract import (
    PHASES,
    REPORT_SCHEMA_VERSION,
    SPLITS,
    TRAINER_ALGORITHM_V0A,
    TRAINER_ALGORITHM_V0B,
    TRAINER_ALGORITHM_V0C,
    TRAINER_ALGORITHM_V0D,
    TRAINER_ALGORITHM_V0E,
)
from examples import Example, LoadResult, MoveTeacherLoadResult, phase_examples, split_examples
from metrics import (
    max_abs_weight,
    metrics_for_examples,
    v0c_metrics_by_split,
    v0c_metrics_by_split_phase,
    v0c_metrics_for_examples,
    weight_l2_norm,
)
from objectives import PatternWeights, train_phase_bias
from optimizer import (
    phase_balance_weights,
    train_pattern_sgd,
    train_pattern_sgd_v0c,
    train_pattern_sgd_v0d,
    train_pattern_rank_v0e,
    v0c_weight_decay,
    weighted_train_residual_mae,
)
from weights_io import (
    WeightsMetadata,
    checksum_for,
    metadata_json,
    nonzero_pattern_weight_items,
    pattern_weights_json,
    stable_float,
    top_abs_weights,
    weight_count_by_pattern,
    weight_count_by_phase,
    weights_checksum_for,
    weights_metadata_for,
    weights_tsv,
)

def report_without_checksum(
    load_result: LoadResult, phase_bias: dict[str, float]
) -> dict[str, Any]:
    accepted = load_result.accepted_examples
    examples_by_split = {
        split: [example for example in accepted if example.split == split] for split in SPLITS
    }
    examples_by_phase = {
        phase: [example for example in accepted if example.phase == phase] for phase in PHASES
    }
    feature_counts = [len(example.features) for example in accepted]

    report = {
        "schema_version": REPORT_SCHEMA_VERSION,
        "trainer_algorithm": TRAINER_ALGORITHM_V0A,
        "input_format": load_result.input_format,
        "input_rows": load_result.input_rows,
        "input_feature_rows": load_result.input_feature_rows,
        "example_count": len(accepted),
        "feature_occurrence_count": sum(feature_counts),
        "average_features_per_example": (
            sum(feature_counts) / len(feature_counts) if feature_counts else 0.0
        ),
        "accepted_examples": len(accepted),
        "rejected_examples": load_result.rejected_examples,
        "rejected_feature_rows": load_result.rejected_feature_rows,
        "counts_by_split_examples": {
            split: len(examples_by_split[split]) for split in SPLITS
        },
        "counts_by_phase_examples": {
            str(phase): len(examples_by_phase[phase]) for phase in PHASES
        },
        "feature_rows_by_example_min": min(feature_counts) if feature_counts else 0,
        "feature_rows_by_example_max": max(feature_counts) if feature_counts else 0,
        "duplicate_feature_rows": load_result.duplicate_feature_rows.total,
        "duplicate_feature_rows_by_record_id": load_result.duplicate_feature_rows.by_record_id,
        "metrics_by_split": {
            split: metrics_for_examples(examples_by_split[split], phase_bias) for split in SPLITS
        },
        "metrics_by_phase": {
            str(phase): metrics_for_examples(examples_by_phase[phase], phase_bias)
            for phase in PHASES
        },
        "phase_bias": phase_bias,
        "notes": [
            "example-level phase-bias baseline; pattern weights, SGD, ridge regression, "
            "artifact export, and self-play are out of scope",
            "phase bias is learned from train split examples only",
            "validation and test examples are used only for metrics",
            "metrics are example-weighted over record_id groups",
            *load_result.notes,
        ],
    }
    return stable_float(report)


def trained_phases_for_examples(examples: list[Example]) -> list[int]:
    return sorted({example.phase for example in examples if example.split == "train"})


def metrics_by_split(
    examples: list[Example], phase_bias: dict[str, float], pattern_weights: PatternWeights | None
) -> dict[str, Any]:
    examples_by_split = split_examples(examples)
    return {
        split: metrics_for_examples(
            examples_by_split[split],
            phase_bias,
            pattern_weights,
            include_examples=True,
            rows_are_feature_rows=True,
        )
        for split in SPLITS
    }


def metrics_by_phase(
    examples: list[Example], phase_bias: dict[str, float], pattern_weights: PatternWeights | None
) -> dict[str, Any]:
    examples_by_phase = phase_examples(examples)
    return {
        str(phase): metrics_for_examples(
            examples_by_phase[phase],
            phase_bias,
            pattern_weights,
            include_sign_accuracy=False,
            include_examples=True,
            rows_are_feature_rows=True,
        )
        for phase in PHASES
    }


def pattern_sgd_report_without_checksum(
    load_result: LoadResult,
    phase_bias: dict[str, float],
    pattern_weights: PatternWeights,
    metrics_by_epoch: list[dict[str, Any]],
    args: argparse.Namespace,
    weights_checksum: str,
    weights_metadata: WeightsMetadata | None,
) -> dict[str, Any]:
    accepted = load_result.accepted_examples
    examples_by_split = split_examples(accepted)
    examples_by_phase = phase_examples(accepted)
    feature_counts = [len(example.features) for example in accepted]
    nonzero_weights = nonzero_pattern_weight_items(pattern_weights)
    weight_l2_norm = math.sqrt(sum(weight * weight for weight in pattern_weights.values()))

    report = {
        "schema_version": REPORT_SCHEMA_VERSION,
        "trainer_algorithm": TRAINER_ALGORITHM_V0B,
        "weights_schema_version": args.weights_schema_version,
        "input_format": load_result.input_format,
        "input_rows": load_result.input_rows,
        "input_feature_rows": load_result.input_feature_rows,
        "example_count": len(accepted),
        "feature_occurrence_count": sum(feature_counts),
        "average_features_per_example": (
            sum(feature_counts) / len(feature_counts) if feature_counts else 0.0
        ),
        "accepted_examples": len(accepted),
        "rejected_examples": load_result.rejected_examples,
        "rejected_feature_rows": load_result.rejected_feature_rows,
        "counts_by_split_examples": {
            split: len(examples_by_split[split]) for split in SPLITS
        },
        "counts_by_phase_examples": {
            str(phase): len(examples_by_phase[phase]) for phase in PHASES
        },
        "feature_rows_by_example_min": min(feature_counts) if feature_counts else 0,
        "feature_rows_by_example_max": max(feature_counts) if feature_counts else 0,
        "duplicate_feature_rows": load_result.duplicate_feature_rows.total,
        "duplicate_feature_rows_by_record_id": load_result.duplicate_feature_rows.by_record_id,
        "epochs": args.epochs,
        "learning_rate": args.learning_rate,
        "l2": args.l2,
        "seed": args.seed,
        "shuffle_policy": "deterministic python random.Random(seed + epoch)",
        "baseline_phase_bias_metrics": {
            "phase_bias": phase_bias,
            "metrics_by_split": metrics_by_split(accepted, phase_bias, None),
            "metrics_by_phase": metrics_by_phase(accepted, phase_bias, None),
        },
        "final_pattern_sgd_metrics": {
            "metrics_by_split": metrics_by_split(accepted, phase_bias, pattern_weights),
            "metrics_by_phase": metrics_by_phase(accepted, phase_bias, pattern_weights),
        },
        "metrics_by_epoch": metrics_by_epoch,
        "nonzero_weight_count": len(nonzero_weights),
        "weight_l2_norm": weight_l2_norm,
        "weights_checksum": weights_checksum,
        "notes": [
            "pattern-sgd-v0b is an example-level pattern weight learning smoke trainer, "
            "not a production trainer",
            "phase bias is initialized from train split examples and held fixed during v0b SGD",
            "pattern weights are learned from train split examples only",
            "validation and test examples are used only for metrics",
            "feature occurrences are added once per feature row, matching runtime evaluator "
            "occurrence semantics",
            "duplicate feature rows are reported and intentionally kept as repeated contributions "
            "in v0b",
            f"weights JSON uses {args.weights_schema_version}; artifact export and runtime load are out of scope",
            *load_result.notes,
        ],
    }
    if weights_metadata is not None:
        report.update(metadata_json(weights_metadata))
    if args.emit_trained_phases:
        report["trained_phases"] = trained_phases_for_examples(accepted)
    return stable_float(report)


def pattern_sgd_v0c_report_without_checksum(
    load_result: LoadResult,
    phase_bias: dict[str, float],
    pattern_weights: PatternWeights,
    metrics_by_epoch: list[dict[str, Any]],
    training_state: dict[str, Any],
    args: argparse.Namespace,
    weights_checksum: str,
    weights_metadata: WeightsMetadata | None,
    trainer_algorithm: str = TRAINER_ALGORITHM_V0C,
    phase_train_counts: dict[str, int] | None = None,
    phase_weights: dict[str, float] | None = None,
    phase_balance_notes: list[str] | None = None,
) -> dict[str, Any]:
    accepted = load_result.accepted_examples
    examples_by_split = split_examples(accepted)
    examples_by_phase = phase_examples(accepted)
    feature_counts = [len(example.features) for example in accepted]
    nonzero_weights = nonzero_pattern_weight_items(pattern_weights)
    final_metrics_by_split = v0c_metrics_by_split(accepted, phase_bias, pattern_weights)
    baseline_metrics_by_split = v0c_metrics_by_split(accepted, phase_bias, {})
    is_v0d = trainer_algorithm == TRAINER_ALGORITHM_V0D

    report = {
        "schema_version": REPORT_SCHEMA_VERSION,
        "trainer_algorithm": trainer_algorithm,
        "weights_schema_version": args.weights_schema_version,
        "input_format": load_result.input_format,
        "input_rows": load_result.input_rows,
        "input_feature_rows": load_result.input_feature_rows,
        "example_count": len(accepted),
        "feature_occurrence_count": sum(feature_counts),
        "average_features_per_example": (
            sum(feature_counts) / len(feature_counts) if feature_counts else 0.0
        ),
        "accepted_examples": len(accepted),
        "rejected_examples": load_result.rejected_examples,
        "rejected_feature_rows": load_result.rejected_feature_rows,
        "counts_by_split_examples": {
            split: len(examples_by_split[split]) for split in SPLITS
        },
        "counts_by_phase_examples": {
            str(phase): len(examples_by_phase[phase]) for phase in PHASES
        },
        "feature_rows_by_example_min": min(feature_counts) if feature_counts else 0,
        "feature_rows_by_example_max": max(feature_counts) if feature_counts else 0,
        "duplicate_feature_rows": load_result.duplicate_feature_rows.total,
        "duplicate_feature_rows_by_record_id": load_result.duplicate_feature_rows.by_record_id,
        "epochs_requested": training_state["epochs_requested"],
        "epochs_completed": training_state["epochs_completed"],
        "early_stop_triggered": training_state["early_stop_triggered"],
        "best_epoch": training_state["best_epoch"],
        "best_validation_MAE": training_state["best_validation_MAE"],
        "final_validation_MAE": training_state["final_validation_MAE"],
        "deterministic_seed": args.seed,
        "learning_rate": args.learning_rate,
        "lr_schedule": args.lr_schedule,
        "l2": args.l2,
        "weight_decay": v0c_weight_decay(args),
        "gradient_clip": args.gradient_clip,
        "early_stop_patience": args.early_stop_patience,
        "eval_every_epoch": args.eval_every_epoch,
        "shuffle_policy": "deterministic",
        "update_rule": {
            "loss": "squared-error SGD",
            "prediction": "phase_bias[phase] + sum(pattern_weight[phase, pattern_id, ternary_index])",
            "error": "prediction - label",
            "per_occurrence_gradient": (
                "phase_weight[phase] * error / feature_count"
                if is_v0d
                else "error / feature_count"
            ),
            "gradient_clip": "optional absolute clip applied to per-feature error gradient before weight decay",
            "weight_decay": "applied only to pattern weights",
            "phase_bias": "learned from train split and fixed during pattern SGD",
            "weight_key": "phase + pattern_id + ternary_index; instance is excluded",
        },
        "residual_definition": {
            "label": "label_final_disc_diff",
            "phase_bias_prediction": "phase_bias[phase]",
            "residual": "label - phase_bias_prediction",
        },
        "baseline_phase_bias_metrics": {
            "phase_bias": phase_bias,
            "metrics_by_split": baseline_metrics_by_split,
            "metrics_by_split_phase": v0c_metrics_by_split_phase(accepted, phase_bias, {}),
        },
        "final_pattern_sgd_metrics": {
            "metrics_by_split": final_metrics_by_split,
            "metrics_by_split_phase": v0c_metrics_by_split_phase(
                accepted, phase_bias, pattern_weights
            ),
        },
        "metrics_by_epoch": metrics_by_epoch,
        "nonzero_weight_count": len(nonzero_weights),
        "weight_l2_norm": weight_l2_norm(pattern_weights),
        "max_abs_weight": max_abs_weight(pattern_weights),
        "weights_by_phase": weight_count_by_phase(pattern_weights),
        "weights_by_pattern": weight_count_by_pattern(pattern_weights),
        "top_abs_weights": top_abs_weights(pattern_weights),
        "weights_checksum": weights_checksum,
        "notes": [
            (
                "pattern-sgd-v0d is a local research trainer, not a production trainer"
                if is_v0d
                else "pattern-sgd-v0c is a local research trainer, not a production trainer"
            ),
            (
                "v0d report metrics are fitting diagnostics, not strength, Elo, match bench, or self-play claims"
                if is_v0d
                else "v0c report metrics are fitting diagnostics, not strength, Elo, match bench, or self-play claims"
            ),
            (
                "phase bias is learned from train split examples and held fixed during v0d SGD"
                if is_v0d
                else "phase bias is learned from train split examples and held fixed during v0c SGD"
            ),
            "pattern weights are learned from train split examples only",
            "validation and test examples are used only for metrics and early stopping",
            "feature occurrences are added once per feature row, matching runtime evaluator occurrence semantics",
            (
                "duplicate feature rows are reported and intentionally kept as repeated contributions in v0d"
                if is_v0d
                else "duplicate feature rows are reported and intentionally kept as repeated contributions in v0c"
            ),
            f"weights JSON uses {args.weights_schema_version}; pattern-sgd-v0b/v0c/v0d share this exporter schema",
            *((phase_balance_notes or []) if is_v0d else []),
            *load_result.notes,
        ],
    }
    if weights_metadata is not None:
        report.update(metadata_json(weights_metadata))
    if is_v0d:
        assert phase_train_counts is not None
        assert phase_weights is not None
        final_metrics_by_phase = {
            str(phase): v0c_metrics_for_examples(
                examples_by_phase[phase], phase_bias, pattern_weights
            )
            for phase in PHASES
        }
        baseline_metrics_by_phase = {
            str(phase): v0c_metrics_for_examples(examples_by_phase[phase], phase_bias, {})
            for phase in PHASES
        }
        report["baseline_phase_bias_metrics"]["metrics_by_phase"] = baseline_metrics_by_phase
        report["final_pattern_sgd_metrics"]["metrics_by_phase"] = final_metrics_by_phase
        report.update(
            {
                "phase_balance": args.phase_balance,
                "phase_weight_floor": args.min_phase_weight,
                "phase_weight_cap": args.max_phase_weight,
                "phase_weights": phase_weights,
                "phase_train_counts": phase_train_counts,
                "weighted_train_residual_MAE": weighted_train_residual_mae(
                    accepted, phase_bias, pattern_weights, phase_weights
                ),
                "phase_balance_notes": phase_balance_notes or [],
            }
        )
    if args.emit_trained_phases:
        report["trained_phases"] = trained_phases_for_examples(accepted)
    return stable_float(report)


def write_text(path: Path, text: str) -> None:
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    except OSError as error:
        raise RuntimeError(f"cannot write {path}: {error}") from error


def run_phase_bias_v0a(load_result: LoadResult, args: argparse.Namespace) -> None:
    phase_bias = train_phase_bias(load_result.accepted_examples)
    weights_text = weights_tsv(phase_bias)
    report = report_without_checksum(load_result, phase_bias)
    report["checksum"] = checksum_for(report, weights_text)
    report_text = json.dumps(report, indent=2, sort_keys=False) + "\n"
    write_text(args.weights_out, weights_text)
    write_text(args.report_out, report_text)


def run_pattern_sgd_v0b(load_result: LoadResult, args: argparse.Namespace) -> None:
    phase_bias = train_phase_bias(load_result.accepted_examples)
    pattern_weights, metrics_by_epoch = train_pattern_sgd(
        load_result.accepted_examples,
        phase_bias,
        args.epochs,
        args.learning_rate,
        args.l2,
        args.seed,
    )
    weights_metadata = weights_metadata_for(args)
    weights_text = pattern_weights_json(phase_bias, pattern_weights, args, weights_metadata)
    weights_checksum = weights_checksum_for(weights_text)
    report = pattern_sgd_report_without_checksum(
        load_result,
        phase_bias,
        pattern_weights,
        metrics_by_epoch,
        args,
        weights_checksum,
        weights_metadata,
    )
    report["checksum"] = checksum_for(report, weights_text)
    report_text = json.dumps(report, indent=2, sort_keys=False) + "\n"
    write_text(args.weights_out, weights_text)
    write_text(args.report_out, report_text)


def run_pattern_sgd_v0c(load_result: LoadResult, args: argparse.Namespace) -> None:
    phase_bias = train_phase_bias(load_result.accepted_examples)
    pattern_weights, metrics_by_epoch, training_state = train_pattern_sgd_v0c(
        load_result.accepted_examples,
        phase_bias,
        args,
    )
    weights_metadata = weights_metadata_for(args)
    weights_text = pattern_weights_json(phase_bias, pattern_weights, args, weights_metadata)
    weights_checksum = weights_checksum_for(weights_text)
    report = pattern_sgd_v0c_report_without_checksum(
        load_result,
        phase_bias,
        pattern_weights,
        metrics_by_epoch,
        training_state,
        args,
        weights_checksum,
        weights_metadata,
    )
    report["checksum"] = checksum_for(report, weights_text)
    report_text = json.dumps(report, indent=2, sort_keys=False) + "\n"
    write_text(args.weights_out, weights_text)
    write_text(args.report_out, report_text)


def run_pattern_sgd_v0d(load_result: LoadResult, args: argparse.Namespace) -> None:
    phase_bias = train_phase_bias(load_result.accepted_examples)
    phase_train_counts, phase_weights, phase_balance_notes = phase_balance_weights(
        load_result.accepted_examples,
        args.phase_balance,
        args.min_phase_weight,
        args.max_phase_weight,
    )
    pattern_weights, metrics_by_epoch, training_state = train_pattern_sgd_v0d(
        load_result.accepted_examples,
        phase_bias,
        args,
        phase_weights,
    )
    weights_metadata = weights_metadata_for(args)
    weights_text = pattern_weights_json(phase_bias, pattern_weights, args, weights_metadata)
    weights_checksum = weights_checksum_for(weights_text)
    report = pattern_sgd_v0c_report_without_checksum(
        load_result,
        phase_bias,
        pattern_weights,
        metrics_by_epoch,
        training_state,
        args,
        weights_checksum,
        weights_metadata,
        trainer_algorithm=TRAINER_ALGORITHM_V0D,
        phase_train_counts=phase_train_counts,
        phase_weights=phase_weights,
        phase_balance_notes=phase_balance_notes,
    )
    report["checksum"] = checksum_for(report, weights_text)
    report_text = json.dumps(report, indent=2, sort_keys=False) + "\n"
    write_text(args.weights_out, weights_text)
    write_text(args.report_out, report_text)


def run_pattern_rank_v0e(
    load_result: LoadResult,
    move_teacher_result: MoveTeacherLoadResult,
    args: argparse.Namespace,
) -> None:
    initial_phase_bias = train_phase_bias(load_result.accepted_examples)
    phase_bias, pattern_weights, metrics_by_epoch, training_state, sampling_totals, trained_phases = (
        train_pattern_rank_v0e(move_teacher_result.roots, initial_phase_bias, args)
    )
    weights_metadata = weights_metadata_for(args)
    weights_text = pattern_weights_json(phase_bias, pattern_weights, args, weights_metadata)
    weights_checksum = weights_checksum_for(weights_text)
    nonzero_weights = nonzero_pattern_weight_items(pattern_weights)
    final_ranking_metrics = (
        metrics_by_epoch[-1]["ranking_metrics"]
        if metrics_by_epoch and not training_state["early_stop_triggered"]
        else None
    )
    if final_ranking_metrics is None:
        from metrics import ranking_metrics_for_roots

        final_ranking_metrics = ranking_metrics_for_roots(
            move_teacher_result.roots,
            phase_bias,
            pattern_weights,
            args.tie_margin,
            args.rank_temperature,
        )
    report: dict[str, Any] = {
        "schema_version": REPORT_SCHEMA_VERSION,
        "trainer_algorithm": TRAINER_ALGORITHM_V0E,
        "weights_schema_version": args.weights_schema_version,
        "input_format": load_result.input_format,
        "input_rows": load_result.input_rows,
        "input_feature_rows": load_result.input_feature_rows,
        "accepted_examples": len(load_result.accepted_examples),
        "rejected_examples": load_result.rejected_examples,
        "rejected_feature_rows": load_result.rejected_feature_rows,
        "move_teacher_root_count": len(move_teacher_result.roots),
        "move_teacher_move_row_count": move_teacher_result.move_rows,
        "move_teacher_schema_version": move_teacher_result.schema_version,
        "move_teacher_provenance": (
            None
            if move_teacher_result.provenance is None
            else {
                "teacher_kind": move_teacher_result.provenance.teacher_kind,
                "teacher_source": move_teacher_result.provenance.teacher_source,
                "teacher_artifact_id": move_teacher_result.provenance.teacher_artifact_id,
                "teacher_artifact_checksum": move_teacher_result.provenance.teacher_artifact_checksum,
                "teacher_search_config_id": move_teacher_result.provenance.teacher_search_config_id,
            }
        ),
        "trained_phases": trained_phases,
        "counts_by_split_roots": {
            split: sum(root.split == split for root in move_teacher_result.roots) for split in SPLITS
        },
        "counts_by_phase_roots": {
            str(phase): sum(root.phase == phase for root in move_teacher_result.roots)
            for phase in PHASES
        },
        "duplicate_feature_rows": load_result.duplicate_feature_rows.total,
        "duplicate_feature_rows_by_record_id": load_result.duplicate_feature_rows.by_record_id,
        "deterministic_seed": args.seed,
        "ranking_config": {
            "rank_temperature": args.rank_temperature,
            "value_loss_weight": args.value_loss_weight,
            "value_loss": "Huber(delta=1.0)",
            "pair_sampling_cap": args.pair_sampling_cap,
            "tie_margin": args.tie_margin,
            "pair_sampling": "deterministic seed/root_board_id sampling; 0 means all pairs",
            "root_order": "deterministic seed + epoch shuffle",
            "pair_order": "deterministic seed + epoch + root_board_id shuffle",
        },
        "epochs_requested": training_state["epochs_requested"],
        "epochs_completed": training_state["epochs_completed"],
        "early_stop_triggered": training_state["early_stop_triggered"],
        "best_epoch": training_state["best_epoch"],
        "best_validation_pairwise_logistic_loss": training_state[
            "best_validation_pairwise_logistic_loss"
        ],
        "final_validation_pairwise_logistic_loss": training_state[
            "final_validation_pairwise_logistic_loss"
        ],
        "learning_rate": args.learning_rate,
        "lr_schedule": args.lr_schedule,
        "l2": args.l2,
        "weight_decay": v0c_weight_decay(args),
        "gradient_clip": args.gradient_clip,
        "early_stop_patience": args.early_stop_patience,
        "update_rule": {
            "model": "V(child) = phase_bias[child_phase] + sum(pattern_weight[child_phase, pattern_id, ternary_index])",
            "root_move_score": "-V(child)",
            "ranking_loss": "softplus(-(root_score_better - root_score_worse) / rank_temperature)",
            "tie_policy": "teacher pairs with abs(score difference) <= tie_margin are excluded from ranking loss",
            "value_calibration": "optional Huber child-value loss; value_loss_weight=0 disables it",
            "weight_key": "phase + pattern_id + ternary_index; instance is excluded",
            "feature_occurrences": "duplicate occurrences contribute repeatedly to the linear score and gradient",
        },
        "ranking_metrics": final_ranking_metrics,
        "metrics_by_epoch": metrics_by_epoch,
        "pair_sampling_totals": sampling_totals,
        "phase_bias_initial": initial_phase_bias,
        "phase_bias_final": phase_bias,
        "nonzero_weight_count": len(nonzero_weights),
        "weight_l2_norm": weight_l2_norm(pattern_weights),
        "max_abs_weight": max_abs_weight(pattern_weights),
        "weights_by_phase": weight_count_by_phase(pattern_weights),
        "weights_by_pattern": weight_count_by_pattern(pattern_weights),
        "top_abs_weights": top_abs_weights(pattern_weights),
        "weights_checksum": weights_checksum,
        "notes": [
            "pattern-rank-v0e is a local pairwise ranking trainer, not a production strength claim",
            "ranking metrics are the primary diagnostics; child value MAE/RMSE are calibration diagnostics",
            "teacher ties are excluded from pairwise loss but retained in tie-aware top1 metrics",
            "runtime-compatible weights JSON uses the existing pattern-eval-weights-v2 schema",
            "validation and test roots are never used for updates",
            *load_result.notes,
        ],
    }
    if weights_metadata is not None:
        report.update(metadata_json(weights_metadata))
    report = stable_float(report)
    report["checksum"] = checksum_for(report, weights_text)
    report_text = json.dumps(report, indent=2, sort_keys=False) + "\n"
    write_text(args.weights_out, weights_text)
    write_text(args.report_out, report_text)
