"""Pattern SGD optimizers and phase balancing."""

from __future__ import annotations

import argparse
import math
import random
import sys
import time
from typing import Any

from dataset_contract import PHASES
from examples import Example, MoveTeacherRoot, PlayedMovePolicyTarget
from metrics import (
    max_abs_weight,
    metrics_for_examples,
    played_move_policy_metrics_for_roots,
    ranking_metrics_for_roots,
    v0c_metrics_for_examples,
    weight_l2_norm,
)
from objectives import (
    PatternWeights,
    huber_loss_and_gradient,
    pairwise_logistic_loss_and_child_gradients,
    played_move_policy_loss_and_child_gradients,
    pattern_rank_child_value,
    pattern_sgd_score,
    phase_bias_score,
    sampled_rank_pairs,
    stable_seed,
)
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


def _clip_gradient(gradient: float, gradient_clip: float | None) -> tuple[float, bool]:
    if gradient_clip is None:
        return gradient, False
    clipped = max(-gradient_clip, min(gradient_clip, gradient))
    return clipped, clipped != gradient


def _rank_epoch_metrics(
    roots: list[MoveTeacherRoot],
    phase_bias: dict[str, float],
    pattern_weights: PatternWeights,
    args: argparse.Namespace,
) -> dict[str, Any]:
    return ranking_metrics_for_roots(
        roots,
        phase_bias,
        pattern_weights,
        args.tie_margin,
        args.rank_temperature,
        args.residual_baseline_through_phase,
    )


def train_pattern_rank_v0e(
    roots: list[MoveTeacherRoot],
    policy_targets_by_root_id: dict[str, PlayedMovePolicyTarget],
    initial_phase_bias: dict[str, float],
    initial_pattern_weights: PatternWeights,
    frozen_phases: frozenset[int],
    args: argparse.Namespace,
) -> tuple[
    dict[str, float], PatternWeights, list[dict[str, Any]], dict[str, Any], dict[str, int], list[int]
]:
    """Train V(child) with root-level pairwise ranking and optional value calibration."""
    train_roots = [root for root in roots if root.split == "train"]
    phase_bias = dict(initial_phase_bias)
    pattern_weights: PatternWeights = dict(initial_pattern_weights)
    best_phase_bias = dict(phase_bias)
    best_weights: PatternWeights = dict(pattern_weights)
    best_validation_loss: float | None = None
    best_validation_objective: float | None = None
    best_epoch: int | None = None
    epochs_without_improvement = 0
    early_stop_triggered = False
    epochs_completed = 0
    metrics_by_epoch: list[dict[str, Any]] = []
    sampling_totals = {
        "eligible_pair_count": 0,
        "selected_pair_count": 0,
        "teacher_tie_pair_count": 0,
        "roots_with_no_selected_pairs": 0,
        "played_move_policy_roots": 0,
        "played_move_policy_occurrences": 0,
    }
    updated_child_phases: set[int] = set()
    trainable_pattern_ids = args.trainable_pattern_ids

    for epoch in range(1, args.epochs + 1):
        epochs_completed = epoch
        epoch_roots = list(train_roots)
        random.Random(args.seed + epoch - 1).shuffle(epoch_roots)
        learning_rate = v0c_learning_rate(args.learning_rate, args.lr_schedule, epoch)
        epoch_sampling = {
            "eligible_pair_count": 0,
            "selected_pair_count": 0,
            "teacher_tie_pair_count": 0,
            "roots_with_no_selected_pairs": 0,
            "played_move_policy_roots": 0,
            "played_move_policy_occurrences": 0,
        }
        gradient_clip_count = 0
        updated_feature_occurrence_count = 0

        for root_index, root in enumerate(epoch_roots, start=1):
            child_values = [
                pattern_rank_child_value(
                    move,
                    phase_bias,
                    pattern_weights,
                    args.residual_baseline_through_phase,
                )
                for move in root.moves
            ]
            pairs, eligible_pair_count = sampled_rank_pairs(
                root, args.tie_margin, args.pair_sampling_cap, args.seed
            )
            total_pair_count = len(root.moves) * (len(root.moves) - 1) // 2
            epoch_sampling["eligible_pair_count"] += eligible_pair_count
            epoch_sampling["selected_pair_count"] += len(pairs)
            epoch_sampling["teacher_tie_pair_count"] += total_pair_count - eligible_pair_count
            if not pairs:
                epoch_sampling["roots_with_no_selected_pairs"] += 1
            pair_order = list(pairs)
            random.Random(stable_seed(args.seed, "pair-order", epoch, root.root_board_id)).shuffle(
                pair_order
            )

            value_gradients = [0.0 for _move in root.moves]
            if pair_order:
                pair_scale = 1.0 / len(pair_order)
                for pair in pair_order:
                    if root.moves[pair.better_index].example.phase not in frozen_phases:
                        updated_child_phases.add(root.moves[pair.better_index].example.phase)
                    if root.moves[pair.worse_index].example.phase not in frozen_phases:
                        updated_child_phases.add(root.moves[pair.worse_index].example.phase)
                    _loss, better_gradient, worse_gradient = pairwise_logistic_loss_and_child_gradients(
                        child_values[pair.better_index],
                        child_values[pair.worse_index],
                        args.rank_temperature,
                    )
                    value_gradients[pair.better_index] += better_gradient * pair_scale
                    value_gradients[pair.worse_index] += worse_gradient * pair_scale
            policy_target = policy_targets_by_root_id.get(root.root_board_id)
            if policy_target is not None and args.policy_loss_weight > 0.0:
                _policy_loss, policy_gradients = (
                    played_move_policy_loss_and_child_gradients(
                        root,
                        policy_target,
                        child_values,
                        args.rank_temperature,
                    )
                )
                epoch_sampling["played_move_policy_roots"] += 1
                epoch_sampling[
                    "played_move_policy_occurrences"
                ] += policy_target.occurrence_count
                for move_index, gradient in enumerate(policy_gradients):
                    if root.moves[move_index].example.phase not in frozen_phases:
                        updated_child_phases.add(root.moves[move_index].example.phase)
                    value_gradients[move_index] += gradient * args.policy_loss_weight
            if args.value_loss_weight > 0.0:
                calibration_scale = args.value_loss_weight / len(root.moves)
                for move_index, move in enumerate(root.moves):
                    if move.example.phase not in frozen_phases:
                        updated_child_phases.add(move.example.phase)
                    _loss, gradient = huber_loss_and_gradient(
                        child_values[move_index] - move.child_label_score
                    )
                    value_gradients[move_index] += gradient * calibration_scale

            phase_bias_gradients: dict[str, float] = {}
            pattern_gradients: dict[tuple[int, str, int], float] = {}
            for move_index, move in enumerate(root.moves):
                if move.example.phase in frozen_phases:
                    continue
                value_gradient = value_gradients[move_index]
                phase_key = str(move.example.phase)
                phase_bias_gradients[phase_key] = (
                    phase_bias_gradients.get(phase_key, 0.0) + value_gradient
                )
                for feature in move.example.features:
                    if (
                        trainable_pattern_ids
                        and feature.pattern_id not in trainable_pattern_ids
                    ):
                        continue
                    key = (move.example.phase, feature.pattern_id, feature.ternary_index)
                    pattern_gradients[key] = pattern_gradients.get(key, 0.0) + value_gradient
                    updated_feature_occurrence_count += 1

            for phase_key in sorted(phase_bias_gradients):
                gradient, clipped = _clip_gradient(phase_bias_gradients[phase_key], args.gradient_clip)
                gradient_clip_count += int(clipped)
                phase_bias[phase_key] -= learning_rate * gradient
            weight_decay = v0c_weight_decay(args)
            for key in sorted(pattern_gradients):
                if key[0] in frozen_phases:
                    continue
                gradient, clipped = _clip_gradient(pattern_gradients[key], args.gradient_clip)
                gradient_clip_count += int(clipped)
                weight = pattern_weights.get(key, 0.0)
                weight -= learning_rate * (gradient + weight_decay * weight)
                if weight == 0.0:
                    pattern_weights.pop(key, None)
                else:
                    pattern_weights[key] = weight

            if (
                args.progress_every_examples is not None
                and root_index % args.progress_every_examples == 0
            ):
                print(
                    "trainer_progress "
                    f"epoch={epoch} roots={root_index}/{len(epoch_roots)} "
                    f"selected_pairs={epoch_sampling['selected_pair_count']}",
                    file=sys.stderr,
                )

        for key in sampling_totals:
            sampling_totals[key] += epoch_sampling[key]
        needs_metrics = args.eval_every_epoch or epoch == args.epochs or args.early_stop_patience is not None
        current_metrics = _rank_epoch_metrics(roots, phase_bias, pattern_weights, args) if needs_metrics else None
        current_policy_metrics = (
            played_move_policy_metrics_for_roots(
                roots,
                policy_targets_by_root_id,
                phase_bias,
                pattern_weights,
                args.rank_temperature,
                args.residual_baseline_through_phase,
            )
            if needs_metrics and policy_targets_by_root_id
            else None
        )
        validation = None if current_metrics is None else current_metrics["by_split"]["validation"]
        validation_loss = None if validation is None else validation["pairwise_logistic_loss"]
        validation_policy_loss = (
            None
            if current_policy_metrics is None
            else current_policy_metrics["by_split"]["validation"]["cross_entropy"]
        )
        validation_objective = validation_loss
        if validation_objective is not None and validation_policy_loss is not None:
            validation_objective += args.policy_loss_weight * validation_policy_loss
        if validation_objective is not None:
            if (
                best_validation_objective is None
                or validation_objective < best_validation_objective
            ):
                best_validation_objective = validation_objective
                best_validation_loss = validation_loss
                best_epoch = epoch
                best_phase_bias = dict(phase_bias)
                best_weights = dict(pattern_weights)
                epochs_without_improvement = 0
            else:
                epochs_without_improvement += 1
        if args.eval_every_epoch or epoch == args.epochs:
            assert current_metrics is not None
            metrics_by_epoch.append(
                {
                    "epoch": epoch,
                    "learning_rate": learning_rate,
                    "ranking_metrics": current_metrics,
                    "played_move_policy_metrics": current_policy_metrics,
                    "pair_sampling": epoch_sampling,
                    "nonzero_weight_count": len(nonzero_pattern_weight_items(pattern_weights)),
                    "weight_l2_norm": weight_l2_norm(pattern_weights),
                    "max_abs_weight": max_abs_weight(pattern_weights),
                    "gradient_clip_count": gradient_clip_count if args.gradient_clip is not None else None,
                    "updated_feature_occurrence_count": updated_feature_occurrence_count,
                }
            )
        if (
            args.early_stop_patience is not None
            and validation_loss is not None
            and epochs_without_improvement >= args.early_stop_patience
        ):
            early_stop_triggered = True
            break

    if early_stop_triggered and best_epoch is not None:
        phase_bias = best_phase_bias
        pattern_weights = best_weights
    final_metrics = _rank_epoch_metrics(roots, phase_bias, pattern_weights, args)
    final_policy_metrics = (
        played_move_policy_metrics_for_roots(
            roots,
            policy_targets_by_root_id,
            phase_bias,
            pattern_weights,
            args.rank_temperature,
            args.residual_baseline_through_phase,
        )
        if policy_targets_by_root_id
        else None
    )
    state = {
        "epochs_requested": args.epochs,
        "epochs_completed": epochs_completed,
        "early_stop_triggered": early_stop_triggered,
        "best_epoch": best_epoch,
        "best_validation_pairwise_logistic_loss": best_validation_loss,
        "best_validation_objective": best_validation_objective,
        "final_validation_pairwise_logistic_loss": final_metrics["by_split"]["validation"][
            "pairwise_logistic_loss"
        ],
        "final_validation_played_move_policy_cross_entropy": (
            None
            if final_policy_metrics is None
            else final_policy_metrics["by_split"]["validation"]["cross_entropy"]
        ),
        "trainable_pattern_ids": sorted(trainable_pattern_ids),
    }
    return (
        stable_float(phase_bias),
        pattern_weights,
        stable_float(metrics_by_epoch),
        stable_float(state),
        sampling_totals,
        sorted(updated_child_phases),
    )
