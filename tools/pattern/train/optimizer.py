"""Pattern SGD optimizers and phase balancing."""

from __future__ import annotations

import argparse
import math
import random
import sys
import time
from typing import Any

from dataset_contract import PHASES
from examples import Example
from metrics import metrics_for_examples, max_abs_weight, v0c_metrics_for_examples, weight_l2_norm
from objectives import PatternWeights, pattern_sgd_score, phase_bias_score
from weights_io import nonzero_pattern_weight_items, stable_float

def train_counts_by_phase(examples: list[Example]) -> dict[str, int]:
    counts = {str(phase): 0 for phase in PHASES}
    for example in examples:
        if example.split == "train":
            counts[str(example.phase)] += 1
    return counts


def weighted_average_phase_weight(
    phase_counts: dict[str, int], phase_weights: dict[str, float]
) -> float | None:
    total = sum(phase_counts.values())
    if total <= 0:
        return None
    return sum(phase_counts[str(phase)] * phase_weights[str(phase)] for phase in PHASES) / total


def normalize_phase_weights(
    phase_counts: dict[str, int],
    raw_weights: dict[str, float],
    min_weight: float,
    max_weight: float,
) -> dict[str, float]:
    weights = {
        str(phase): (
            max(min_weight, min(max_weight, raw_weights[str(phase)]))
            if phase_counts[str(phase)] > 0
            else 0.0
        )
        for phase in PHASES
    }
    for _iteration in range(32):
        average = weighted_average_phase_weight(phase_counts, weights)
        if average is None or average == 0.0:
            break
        next_weights = {
            str(phase): (
                max(min_weight, min(max_weight, weights[str(phase)] / average))
                if phase_counts[str(phase)] > 0
                else 0.0
            )
            for phase in PHASES
        }
        if all(abs(next_weights[str(phase)] - weights[str(phase)]) < 1e-12 for phase in PHASES):
            weights = next_weights
            break
        weights = next_weights
    return weights


def phase_balance_weights(
    examples: list[Example],
    scheme: str,
    min_weight: float,
    max_weight: float,
) -> tuple[dict[str, int], dict[str, float], list[str]]:
    phase_counts = train_counts_by_phase(examples)
    total_train_examples = sum(phase_counts.values())
    active_phase_count = sum(1 for count in phase_counts.values() if count > 0)
    if total_train_examples == 0 or active_phase_count == 0:
        raise RuntimeError("dataset has no accepted train examples")

    raw_weights: dict[str, float] = {}
    for phase in PHASES:
        key = str(phase)
        count = phase_counts[key]
        if count == 0:
            raw_weights[key] = 0.0
        elif scheme == "none":
            raw_weights[key] = 1.0
        elif scheme == "inverse-count":
            raw_weights[key] = total_train_examples / (count * active_phase_count)
        elif scheme == "sqrt-inverse-count":
            raw_weights[key] = math.sqrt(total_train_examples / (count * active_phase_count))
        else:
            raise RuntimeError(f"unsupported phase balance scheme: {scheme}")

    phase_weights = normalize_phase_weights(phase_counts, raw_weights, min_weight, max_weight)
    average = weighted_average_phase_weight(phase_counts, phase_weights)
    notes = [
        f"phase balance scheme: {scheme}",
        f"active train phases: {active_phase_count}",
        f"train example weighted average phase weight: {average:.12g}"
        if average is not None
        else "train example weighted average phase weight: unavailable",
    ]
    if any(phase_counts[str(phase)] == 0 for phase in PHASES):
        notes.append("phases without train examples receive weight 0.0 and are not updated")
    if any(
        phase_counts[str(phase)] > 0
        and (phase_weights[str(phase)] == min_weight or phase_weights[str(phase)] == max_weight)
        for phase in PHASES
    ):
        notes.append("one or more active phase weights reached the configured floor or cap")
    return phase_counts, phase_weights, notes


def weighted_train_residual_mae(
    examples: list[Example],
    phase_bias: dict[str, float],
    pattern_weights: PatternWeights,
    phase_weights: dict[str, float],
) -> float | None:
    weighted_absolute_error = 0.0
    total_weight = 0.0
    for example in examples:
        if example.split != "train":
            continue
        weight = phase_weights[str(example.phase)]
        label = float(example.label_final_disc_diff)
        phase_prediction = phase_bias_score(example, phase_bias)
        residual = label - phase_prediction
        prediction = pattern_sgd_score(example, phase_bias, pattern_weights)
        residual_prediction = prediction - phase_prediction
        weighted_absolute_error += weight * abs(residual_prediction - residual)
        total_weight += weight
    if total_weight == 0.0:
        return None
    return weighted_absolute_error / total_weight

def train_pattern_sgd(
    examples: list[Example],
    phase_bias: dict[str, float],
    epochs: int,
    learning_rate: float,
    l2: float,
    seed: int,
) -> tuple[PatternWeights, list[dict[str, Any]]]:
    train_examples = [example for example in examples if example.split == "train"]
    validation_examples = [example for example in examples if example.split == "validation"]
    pattern_weights: PatternWeights = {}
    metrics_by_epoch: list[dict[str, Any]] = []

    for epoch in range(epochs):
        epoch_examples = list(train_examples)
        random.Random(seed + epoch).shuffle(epoch_examples)
        for example in epoch_examples:
            feature_count = len(example.features)
            if feature_count == 0:
                continue
            error = (
                pattern_sgd_score(example, phase_bias, pattern_weights)
                - example.label_final_disc_diff
            )
            for feature in example.features:
                key = (example.phase, feature.pattern_id, feature.ternary_index)
                weight = pattern_weights.get(key, 0.0)
                weight -= learning_rate * ((error / feature_count) + (l2 * weight))
                if weight == 0.0:
                    pattern_weights.pop(key, None)
                else:
                    pattern_weights[key] = weight
        train_metrics = metrics_for_examples(train_examples, phase_bias, pattern_weights)
        validation_metrics = metrics_for_examples(validation_examples, phase_bias, pattern_weights)
        metrics_by_epoch.append(
            {
                "epoch": epoch + 1,
                "train": {
                    "MAE": train_metrics["MAE"],
                    "RMSE": train_metrics["RMSE"],
                },
                "validation": {
                    "MAE": validation_metrics["MAE"],
                    "RMSE": validation_metrics["RMSE"],
                },
            }
        )
    return pattern_weights, stable_float(metrics_by_epoch)


def v0c_weight_decay(args: argparse.Namespace) -> float:
    return args.weight_decay if args.weight_decay is not None else args.l2


def v0c_learning_rate(base_learning_rate: float, schedule: str, epoch: int) -> float:
    if schedule == "constant":
        return base_learning_rate
    if schedule == "inverse-sqrt":
        return base_learning_rate / math.sqrt(epoch)
    raise RuntimeError(f"unsupported lr schedule: {schedule}")


def train_pattern_sgd_v0c(
    examples: list[Example],
    phase_bias: dict[str, float],
    args: argparse.Namespace,
) -> tuple[PatternWeights, list[dict[str, Any]], dict[str, Any]]:
    train_examples = [example for example in examples if example.split == "train"]
    validation_examples = [example for example in examples if example.split == "validation"]
    pattern_weights: PatternWeights = {}
    best_weights: PatternWeights = {}
    metrics_by_epoch: list[dict[str, Any]] = []
    best_validation_mae: float | None = None
    best_epoch: int | None = None
    epochs_without_improvement = 0
    early_stop_triggered = False
    weight_decay = v0c_weight_decay(args)
    start_time = time.monotonic()
    train_count = len(train_examples)

    for epoch in range(1, args.epochs + 1):
        epoch_start_time = time.monotonic()
        epoch_examples = list(train_examples)
        random.Random(args.seed + epoch - 1).shuffle(epoch_examples)
        learning_rate = v0c_learning_rate(args.learning_rate, args.lr_schedule, epoch)
        gradient_clip_count = 0
        updated_feature_occurrence_count = 0
        for example_index, example in enumerate(epoch_examples, start=1):
            feature_count = len(example.features)
            if feature_count == 0:
                continue
            error = (
                pattern_sgd_score(example, phase_bias, pattern_weights)
                - example.label_final_disc_diff
            )
            for feature in example.features:
                key = (example.phase, feature.pattern_id, feature.ternary_index)
                weight = pattern_weights.get(key, 0.0)
                gradient = error / feature_count
                if args.gradient_clip is not None:
                    clipped = max(-args.gradient_clip, min(args.gradient_clip, gradient))
                    if clipped != gradient:
                        gradient_clip_count += 1
                    gradient = clipped
                weight -= learning_rate * (gradient + weight_decay * weight)
                updated_feature_occurrence_count += 1
                if weight == 0.0:
                    pattern_weights.pop(key, None)
                else:
                    pattern_weights[key] = weight
            if (
                args.progress_every_examples is not None
                and example_index % args.progress_every_examples == 0
            ):
                elapsed = max(time.monotonic() - start_time, 1e-9)
                print(
                    "trainer_progress "
                    f"epoch={epoch} "
                    f"examples={example_index}/{train_count} "
                    f"updated_feature_occurrences={updated_feature_occurrence_count} "
                    f"elapsed_sec={elapsed:.3f} "
                    f"examples_per_sec={example_index / max(time.monotonic() - epoch_start_time, 1e-9):.3f}",
                    file=sys.stderr,
                )

        needs_validation_metrics = (
            args.eval_every_epoch
            or epoch == args.epochs
            or args.early_stop_patience is not None
        )
        validation_metrics = (
            v0c_metrics_for_examples(validation_examples, phase_bias, pattern_weights)
            if needs_validation_metrics
            else None
        )
        validation_mae = validation_metrics["MAE"] if validation_metrics is not None else None
        if validation_mae is not None:
            if best_validation_mae is None or validation_mae < best_validation_mae:
                best_validation_mae = validation_mae
                best_epoch = epoch
                best_weights = dict(pattern_weights)
                epochs_without_improvement = 0
            else:
                epochs_without_improvement += 1

        if args.eval_every_epoch or epoch == args.epochs:
            train_metrics = v0c_metrics_for_examples(train_examples, phase_bias, pattern_weights)
            if validation_metrics is None:
                validation_metrics = v0c_metrics_for_examples(
                    validation_examples, phase_bias, pattern_weights
                )
            metrics_by_epoch.append(
                {
                    "epoch": epoch,
                    "learning_rate": learning_rate,
                    "train": {
                        "MAE": train_metrics["MAE"],
                        "RMSE": train_metrics["RMSE"],
                        "sign_accuracy": train_metrics["sign_accuracy"],
                        "residual_MAE": train_metrics["residual_MAE"],
                        "residual_RMSE": train_metrics["residual_RMSE"],
                    },
                    "validation": {
                        "MAE": validation_metrics["MAE"],
                        "RMSE": validation_metrics["RMSE"],
                        "sign_accuracy": validation_metrics["sign_accuracy"],
                        "residual_MAE": validation_metrics["residual_MAE"],
                        "residual_RMSE": validation_metrics["residual_RMSE"],
                    },
                    "nonzero_weight_count": len(nonzero_pattern_weight_items(pattern_weights)),
                    "weight_l2_norm": weight_l2_norm(pattern_weights),
                    "max_abs_weight": max_abs_weight(pattern_weights),
                    "gradient_clip_count": gradient_clip_count
                    if args.gradient_clip is not None
                    else None,
                    "updated_feature_occurrence_count": updated_feature_occurrence_count,
                }
            )
        if args.progress_every_examples is not None:
            elapsed = max(time.monotonic() - start_time, 1e-9)
            print(
                "trainer_epoch "
                f"epoch={epoch} "
                f"updated_feature_occurrences={updated_feature_occurrence_count} "
                f"nonzero_weight_count={len(pattern_weights)} "
                f"validation_MAE={validation_mae} "
                f"elapsed_sec={elapsed:.3f}",
                file=sys.stderr,
            )

        if (
            args.early_stop_patience is not None
            and validation_mae is not None
            and epochs_without_improvement >= args.early_stop_patience
        ):
            early_stop_triggered = True
            break

    if early_stop_triggered and best_epoch is not None:
        pattern_weights = best_weights
    epochs_completed = metrics_by_epoch[-1]["epoch"] if metrics_by_epoch else 0
    if args.epochs > 0 and not metrics_by_epoch:
        epochs_completed = epoch
    final_validation_metrics = v0c_metrics_for_examples(
        validation_examples, phase_bias, pattern_weights
    )
    state = {
        "epochs_requested": args.epochs,
        "epochs_completed": epochs_completed,
        "early_stop_triggered": early_stop_triggered,
        "best_epoch": best_epoch,
        "best_validation_MAE": best_validation_mae,
        "final_validation_MAE": final_validation_metrics["MAE"],
    }
    return pattern_weights, stable_float(metrics_by_epoch), stable_float(state)


def train_pattern_sgd_v0d(
    examples: list[Example],
    phase_bias: dict[str, float],
    args: argparse.Namespace,
    phase_weights: dict[str, float],
) -> tuple[PatternWeights, list[dict[str, Any]], dict[str, Any]]:
    train_examples = [example for example in examples if example.split == "train"]
    validation_examples = [example for example in examples if example.split == "validation"]
    pattern_weights: PatternWeights = {}
    best_weights: PatternWeights = {}
    metrics_by_epoch: list[dict[str, Any]] = []
    best_validation_mae: float | None = None
    best_epoch: int | None = None
    epochs_without_improvement = 0
    early_stop_triggered = False
    weight_decay = v0c_weight_decay(args)
    start_time = time.monotonic()
    train_count = len(train_examples)

    for epoch in range(1, args.epochs + 1):
        epoch_start_time = time.monotonic()
        epoch_examples = list(train_examples)
        random.Random(args.seed + epoch - 1).shuffle(epoch_examples)
        learning_rate = v0c_learning_rate(args.learning_rate, args.lr_schedule, epoch)
        gradient_clip_count = 0
        updated_feature_occurrence_count = 0
        for example_index, example in enumerate(epoch_examples, start=1):
            feature_count = len(example.features)
            if feature_count == 0:
                continue
            error = (
                pattern_sgd_score(example, phase_bias, pattern_weights)
                - example.label_final_disc_diff
            )
            weighted_error = error * phase_weights[str(example.phase)]
            for feature in example.features:
                key = (example.phase, feature.pattern_id, feature.ternary_index)
                weight = pattern_weights.get(key, 0.0)
                gradient = weighted_error / feature_count
                if args.gradient_clip is not None:
                    clipped = max(-args.gradient_clip, min(args.gradient_clip, gradient))
                    if clipped != gradient:
                        gradient_clip_count += 1
                    gradient = clipped
                weight -= learning_rate * (gradient + weight_decay * weight)
                updated_feature_occurrence_count += 1
                if weight == 0.0:
                    pattern_weights.pop(key, None)
                else:
                    pattern_weights[key] = weight
            if (
                args.progress_every_examples is not None
                and example_index % args.progress_every_examples == 0
            ):
                elapsed = max(time.monotonic() - start_time, 1e-9)
                print(
                    "trainer_progress "
                    f"epoch={epoch} "
                    f"examples={example_index}/{train_count} "
                    f"updated_feature_occurrences={updated_feature_occurrence_count} "
                    f"elapsed_sec={elapsed:.3f} "
                    f"examples_per_sec={example_index / max(time.monotonic() - epoch_start_time, 1e-9):.3f}",
                    file=sys.stderr,
                )

        needs_validation_metrics = (
            args.eval_every_epoch
            or epoch == args.epochs
            or args.early_stop_patience is not None
        )
        validation_metrics = (
            v0c_metrics_for_examples(validation_examples, phase_bias, pattern_weights)
            if needs_validation_metrics
            else None
        )
        validation_mae = validation_metrics["MAE"] if validation_metrics is not None else None
        if validation_mae is not None:
            if best_validation_mae is None or validation_mae < best_validation_mae:
                best_validation_mae = validation_mae
                best_epoch = epoch
                best_weights = dict(pattern_weights)
                epochs_without_improvement = 0
            else:
                epochs_without_improvement += 1

        if args.eval_every_epoch or epoch == args.epochs:
            train_metrics = v0c_metrics_for_examples(train_examples, phase_bias, pattern_weights)
            if validation_metrics is None:
                validation_metrics = v0c_metrics_for_examples(
                    validation_examples, phase_bias, pattern_weights
                )
            metrics_by_epoch.append(
                {
                    "epoch": epoch,
                    "learning_rate": learning_rate,
                    "train": {
                        "MAE": train_metrics["MAE"],
                        "RMSE": train_metrics["RMSE"],
                        "sign_accuracy": train_metrics["sign_accuracy"],
                        "residual_MAE": train_metrics["residual_MAE"],
                        "residual_RMSE": train_metrics["residual_RMSE"],
                        "weighted_residual_MAE": weighted_train_residual_mae(
                            train_examples, phase_bias, pattern_weights, phase_weights
                        ),
                    },
                    "validation": {
                        "MAE": validation_metrics["MAE"],
                        "RMSE": validation_metrics["RMSE"],
                        "sign_accuracy": validation_metrics["sign_accuracy"],
                        "residual_MAE": validation_metrics["residual_MAE"],
                        "residual_RMSE": validation_metrics["residual_RMSE"],
                    },
                    "nonzero_weight_count": len(nonzero_pattern_weight_items(pattern_weights)),
                    "weight_l2_norm": weight_l2_norm(pattern_weights),
                    "max_abs_weight": max_abs_weight(pattern_weights),
                    "gradient_clip_count": gradient_clip_count
                    if args.gradient_clip is not None
                    else None,
                    "updated_feature_occurrence_count": updated_feature_occurrence_count,
                }
            )
        if args.progress_every_examples is not None:
            elapsed = max(time.monotonic() - start_time, 1e-9)
            print(
                "trainer_epoch "
                f"epoch={epoch} "
                f"updated_feature_occurrences={updated_feature_occurrence_count} "
                f"nonzero_weight_count={len(pattern_weights)} "
                f"validation_MAE={validation_mae} "
                f"elapsed_sec={elapsed:.3f}",
                file=sys.stderr,
            )

        if (
            args.early_stop_patience is not None
            and validation_mae is not None
            and epochs_without_improvement >= args.early_stop_patience
        ):
            early_stop_triggered = True
            break

    if early_stop_triggered and best_epoch is not None:
        pattern_weights = best_weights
    epochs_completed = metrics_by_epoch[-1]["epoch"] if metrics_by_epoch else 0
    if args.epochs > 0 and not metrics_by_epoch:
        epochs_completed = epoch
    final_validation_metrics = v0c_metrics_for_examples(
        validation_examples, phase_bias, pattern_weights
    )
    state = {
        "epochs_requested": args.epochs,
        "epochs_completed": epochs_completed,
        "early_stop_triggered": early_stop_triggered,
        "best_epoch": best_epoch,
        "best_validation_MAE": best_validation_mae,
        "final_validation_MAE": final_validation_metrics["MAE"],
    }
    return pattern_weights, stable_float(metrics_by_epoch), stable_float(state)
