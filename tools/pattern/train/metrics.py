"""Pattern trainer metrics and summaries."""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Any

from dataset_contract import PHASES, SPLITS
from examples import Example, MoveTeacherRoot, split_examples
from objectives import PatternWeights, pairwise_logistic_loss_and_child_gradients, pattern_sgd_score, phase_bias_score, sign

def metrics_for_examples(
    examples: list[Example],
    phase_bias: dict[str, float],
    pattern_weights: PatternWeights | None = None,
    include_sign_accuracy: bool = True,
    include_examples: bool = False,
    rows_are_feature_rows: bool = False,
) -> dict[str, Any]:
    feature_rows = sum(len(example.features) for example in examples)
    if not examples:
        metrics: dict[str, Any] = {
            "rows": 0,
            "MAE": None,
            "RMSE": None,
        }
        if include_examples:
            metrics["examples"] = 0
        if include_sign_accuracy:
            metrics["sign_accuracy"] = None
        return metrics

    absolute_error = 0.0
    squared_error = 0.0
    sign_matches = 0
    for example in examples:
        if pattern_weights is None:
            prediction = phase_bias_score(example, phase_bias)
        else:
            prediction = pattern_sgd_score(example, phase_bias, pattern_weights)
        error = prediction - example.label_final_disc_diff
        absolute_error += abs(error)
        squared_error += error * error
        if sign(prediction) == sign(example.label_final_disc_diff):
            sign_matches += 1
    example_count = len(examples)
    metrics = {
        "rows": feature_rows if rows_are_feature_rows else example_count,
        "MAE": absolute_error / example_count,
        "RMSE": math.sqrt(squared_error / example_count),
    }
    if include_examples:
        metrics["examples"] = example_count
    if include_sign_accuracy:
        metrics["sign_accuracy"] = sign_matches / example_count
    return metrics


def mean_float(values: list[float]) -> float | None:
    return sum(values) / len(values) if values else None


def stddev_float(values: list[float]) -> float | None:
    if not values:
        return None
    value_mean = sum(values) / len(values)
    return math.sqrt(sum((value - value_mean) ** 2 for value in values) / len(values))


def weight_l2_norm(pattern_weights: PatternWeights) -> float:
    return math.sqrt(sum(weight * weight for weight in pattern_weights.values()))


def max_abs_weight(pattern_weights: PatternWeights) -> float:
    return max((abs(weight) for weight in pattern_weights.values()), default=0.0)


def v0c_metrics_for_examples(
    examples: list[Example], phase_bias: dict[str, float], pattern_weights: PatternWeights
) -> dict[str, Any]:
    feature_occurrences = sum(len(example.features) for example in examples)
    if not examples:
        return {
            "examples": 0,
            "feature_occurrences": feature_occurrences,
            "MAE": None,
            "RMSE": None,
            "sign_accuracy": None,
            "residual_MAE": None,
            "residual_RMSE": None,
            "label_mean": None,
            "label_stddev": None,
            "residual_mean": None,
            "residual_stddev": None,
        }

    labels: list[float] = []
    residuals: list[float] = []
    absolute_error = 0.0
    squared_error = 0.0
    residual_absolute_error = 0.0
    residual_squared_error = 0.0
    sign_matches = 0
    for example in examples:
        label = float(example.label_final_disc_diff)
        phase_prediction = phase_bias_score(example, phase_bias)
        residual = label - phase_prediction
        prediction = pattern_sgd_score(example, phase_bias, pattern_weights)
        error = prediction - label
        residual_prediction = prediction - phase_prediction
        residual_error = residual_prediction - residual
        labels.append(label)
        residuals.append(residual)
        absolute_error += abs(error)
        squared_error += error * error
        residual_absolute_error += abs(residual_error)
        residual_squared_error += residual_error * residual_error
        if sign(prediction) == sign(label):
            sign_matches += 1
    example_count = len(examples)
    return {
        "examples": example_count,
        "feature_occurrences": feature_occurrences,
        "MAE": absolute_error / example_count,
        "RMSE": math.sqrt(squared_error / example_count),
        "sign_accuracy": sign_matches / example_count,
        "residual_MAE": residual_absolute_error / example_count,
        "residual_RMSE": math.sqrt(residual_squared_error / example_count),
        "label_mean": mean_float(labels),
        "label_stddev": stddev_float(labels),
        "residual_mean": mean_float(residuals),
        "residual_stddev": stddev_float(residuals),
    }


def v0c_metrics_by_split(
    examples: list[Example], phase_bias: dict[str, float], pattern_weights: PatternWeights
) -> dict[str, Any]:
    examples_by_split = split_examples(examples)
    return {
        split: v0c_metrics_for_examples(examples_by_split[split], phase_bias, pattern_weights)
        for split in SPLITS
    }


def v0c_metrics_by_split_phase(
    examples: list[Example], phase_bias: dict[str, float], pattern_weights: PatternWeights
) -> dict[str, Any]:
    result: dict[str, Any] = {}
    examples_by_split = split_examples(examples)
    for split in SPLITS:
        result[split] = {}
        for phase in PHASES:
            phase_bucket = [
                example for example in examples_by_split[split] if example.phase == phase
            ]
            result[split][str(phase)] = v0c_metrics_for_examples(
                phase_bucket, phase_bias, pattern_weights
            )
    return result


_PREDICTED_TIE_EPSILON = 1e-12


@dataclass
class RankingAccumulator:
    root_count: int = 0
    legal_move_count: int = 0
    top1_correct: float = 0.0
    top1_tie_aware_correct: float = 0.0
    best_in_top2: float = 0.0
    pairwise_correct: float = 0.0
    pairwise_count: int = 0
    pairwise_loss_sum: float = 0.0
    roots_with_all_moves_same_predicted_score: int = 0
    regrets: list[float] = field(default_factory=list)
    value_absolute_error: float = 0.0
    value_squared_error: float = 0.0
    value_count: int = 0
    predicted_score_min: float | None = None
    predicted_score_max: float | None = None

    def add_root(
        self,
        root: MoveTeacherRoot,
        phase_bias: dict[str, float],
        pattern_weights: PatternWeights,
        tie_margin: float,
        rank_temperature: float,
    ) -> None:
        predictions = [
            -pattern_sgd_score(move.example, phase_bias, pattern_weights)
            for move in root.moves
        ]
        child_values = [-prediction for prediction in predictions]
        teacher_scores = [move.teacher_root_score for move in root.moves]
        ordered_indices = sorted(
            range(len(root.moves)), key=lambda index: (-predictions[index], root.moves[index].move)
        )
        best_teacher_score = max(teacher_scores)
        teacher_best_indices = {
            index
            for index, teacher_score in enumerate(teacher_scores)
            if best_teacher_score - teacher_score <= tie_margin
        }
        chosen_index = ordered_indices[0]
        top_prediction = predictions[chosen_index]
        predicted_top_indices = {
            index
            for index, prediction in enumerate(predictions)
            if abs(prediction - top_prediction) <= _PREDICTED_TIE_EPSILON
        }

        self.root_count += 1
        self.legal_move_count += len(root.moves)
        self.top1_correct += 1.0 if chosen_index in teacher_best_indices else 0.0
        self.top1_tie_aware_correct += (
            1.0 if predicted_top_indices & teacher_best_indices else 0.0
        )
        self.best_in_top2 += (
            1.0 if any(index in teacher_best_indices for index in ordered_indices[:2]) else 0.0
        )
        self.regrets.append(best_teacher_score - teacher_scores[chosen_index])
        if max(predictions) - min(predictions) <= _PREDICTED_TIE_EPSILON:
            self.roots_with_all_moves_same_predicted_score += 1
        self.predicted_score_min = (
            min(predictions)
            if self.predicted_score_min is None
            else min(self.predicted_score_min, min(predictions))
        )
        self.predicted_score_max = (
            max(predictions)
            if self.predicted_score_max is None
            else max(self.predicted_score_max, max(predictions))
        )

        for move, value in zip(root.moves, child_values, strict=True):
            error = value - move.child_label_score
            self.value_absolute_error += abs(error)
            self.value_squared_error += error * error
            self.value_count += 1
        for left in range(len(root.moves)):
            for right in range(left + 1, len(root.moves)):
                teacher_difference = teacher_scores[left] - teacher_scores[right]
                if abs(teacher_difference) <= tie_margin:
                    continue
                if teacher_difference > 0.0:
                    loss, _better_gradient, _worse_gradient = pairwise_logistic_loss_and_child_gradients(
                        child_values[left], child_values[right], rank_temperature
                    )
                else:
                    loss, _better_gradient, _worse_gradient = pairwise_logistic_loss_and_child_gradients(
                        child_values[right], child_values[left], rank_temperature
                    )
                predicted_difference = predictions[left] - predictions[right]
                self.pairwise_count += 1
                self.pairwise_loss_sum += loss
                if abs(predicted_difference) <= _PREDICTED_TIE_EPSILON:
                    self.pairwise_correct += 0.5
                elif sign(teacher_difference) == sign(predicted_difference):
                    self.pairwise_correct += 1.0

    def as_dict(self) -> dict[str, Any]:
        if self.root_count == 0:
            return {
                "root_count": 0,
                "legal_move_count": 0,
                "top1_accuracy": None,
                "top1_tie_aware_accuracy": None,
                "best_move_in_top2_rate": None,
                "pairwise_accuracy": None,
                "pairwise_count": 0,
                "pairwise_logistic_loss": None,
                "mean_teacher_regret": None,
                "median_teacher_regret": None,
                "roots_with_all_moves_same_predicted_score": 0,
                "predicted_score_range": None,
                "value_MAE": None,
                "value_RMSE": None,
            }
        return {
            "root_count": self.root_count,
            "legal_move_count": self.legal_move_count,
            "top1_accuracy": self.top1_correct / self.root_count,
            "top1_tie_aware_accuracy": self.top1_tie_aware_correct / self.root_count,
            "best_move_in_top2_rate": self.best_in_top2 / self.root_count,
            "pairwise_accuracy": (
                self.pairwise_correct / self.pairwise_count if self.pairwise_count else None
            ),
            "pairwise_count": self.pairwise_count,
            "pairwise_logistic_loss": (
                self.pairwise_loss_sum / self.pairwise_count if self.pairwise_count else None
            ),
            "mean_teacher_regret": sum(self.regrets) / len(self.regrets),
            "median_teacher_regret": median_float(self.regrets),
            "roots_with_all_moves_same_predicted_score": self.roots_with_all_moves_same_predicted_score,
            "predicted_score_range": {
                "min": self.predicted_score_min,
                "max": self.predicted_score_max,
            },
            "value_MAE": self.value_absolute_error / self.value_count,
            "value_RMSE": math.sqrt(self.value_squared_error / self.value_count),
        }


def median_float(values: list[float]) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    middle = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[middle]
    return (ordered[middle - 1] + ordered[middle]) / 2.0


def ranking_metrics_for_roots(
    roots: list[MoveTeacherRoot],
    phase_bias: dict[str, float],
    pattern_weights: PatternWeights,
    tie_margin: float,
    rank_temperature: float,
) -> dict[str, Any]:
    overall = RankingAccumulator()
    by_split = {split: RankingAccumulator() for split in SPLITS}
    by_phase = {str(phase): RankingAccumulator() for phase in PHASES}
    for root in roots:
        overall.add_root(root, phase_bias, pattern_weights, tie_margin, rank_temperature)
        by_split[root.split].add_root(
            root, phase_bias, pattern_weights, tie_margin, rank_temperature
        )
        by_phase[str(root.phase)].add_root(
            root, phase_bias, pattern_weights, tie_margin, rank_temperature
        )
    return {
        "overall": overall.as_dict(),
        "by_split": {split: by_split[split].as_dict() for split in SPLITS},
        "by_phase": {str(phase): by_phase[str(phase)].as_dict() for phase in PHASES},
    }
