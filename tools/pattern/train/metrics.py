"""Pattern trainer metrics and summaries."""

from __future__ import annotations

import math
from typing import Any

from dataset_contract import PHASES, SPLITS
from examples import Example, split_examples
from objectives import PatternWeights, pattern_sgd_score, phase_bias_score, sign

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
