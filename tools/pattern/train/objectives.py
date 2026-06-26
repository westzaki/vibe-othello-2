"""Scoring helpers and trainer objective primitives."""

from __future__ import annotations

from dataset_contract import PHASES
from examples import Example

def mean(values: list[int]) -> float:
    return sum(values) / len(values) if values else 0.0


def train_phase_bias(examples: list[Example]) -> dict[str, float]:
    labels_by_phase: dict[int, list[int]] = {phase: [] for phase in PHASES}
    for example in examples:
        if example.split == "train":
            labels_by_phase[example.phase].append(example.label_final_disc_diff)
    return {str(phase): mean(labels_by_phase[phase]) for phase in PHASES}


PatternWeightKey = tuple[int, str, int]
PatternWeights = dict[PatternWeightKey, float]


def sign(value: float) -> int:
    if value > 0:
        return 1
    if value < 0:
        return -1
    return 0


def phase_bias_score(example: Example, phase_bias: dict[str, float]) -> float:
    return phase_bias[str(example.phase)]


def pattern_sgd_score(
    example: Example, phase_bias: dict[str, float], pattern_weights: PatternWeights
) -> float:
    score = phase_bias_score(example, phase_bias)
    for feature in example.features:
        score += pattern_weights.get(
            (example.phase, feature.pattern_id, feature.ternary_index), 0.0
        )
    return score
