"""Scoring helpers and trainer objective primitives."""

from __future__ import annotations

import hashlib
import math
import random
from dataclasses import dataclass

from dataset_contract import PHASES
from examples import Example, MoveTeacherMove, MoveTeacherRoot, PlayedMovePolicyTarget

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


def pattern_rank_child_value(
    move: MoveTeacherMove,
    phase_bias: dict[str, float],
    pattern_weights: PatternWeights,
    residual_baseline_through_phase: int | None,
) -> float:
    value = pattern_sgd_score(move.example, phase_bias, pattern_weights)
    if (
        residual_baseline_through_phase is not None
        and move.example.phase <= residual_baseline_through_phase
    ):
        if move.child_baseline_score is None:
            raise RuntimeError(
                "residual training requires move-teacher v3 baseline scores "
                f"through phase {residual_baseline_through_phase}"
            )
        value += move.child_baseline_score
    return value


@dataclass(frozen=True)
class RankPair:
    better_index: int
    worse_index: int


def stable_seed(*parts: object) -> int:
    payload = "\x1f".join(str(part) for part in parts).encode("utf-8")
    return int.from_bytes(hashlib.sha256(payload).digest()[:8], "big")


def pairwise_logistic_loss_and_child_gradients(
    better_child_value: float, worse_child_value: float, temperature: float
) -> tuple[float, float, float]:
    """Return loss and d(loss)/d(V(child)) for a teacher-better/worse pair.

    Root score is -V(child). Therefore a better move must lower the child
    value and a worse move must raise it.
    """
    margin = (worse_child_value - better_child_value) / temperature
    if margin >= 0.0:
        loss = math.log1p(math.exp(-margin))
    else:
        loss = -margin + math.log1p(math.exp(margin))
    inverse_margin_sigmoid = 1.0 / (1.0 + math.exp(max(-700.0, min(700.0, margin))))
    gradient = inverse_margin_sigmoid / temperature
    return loss, gradient, -gradient


def huber_loss_and_gradient(error: float, delta: float = 1.0) -> tuple[float, float]:
    absolute_error = abs(error)
    if absolute_error <= delta:
        return 0.5 * error * error, error
    return delta * (absolute_error - 0.5 * delta), delta if error > 0.0 else -delta


def played_move_policy_loss_and_child_gradients(
    root: MoveTeacherRoot,
    target: PlayedMovePolicyTarget,
    child_values: list[float],
    temperature: float,
) -> tuple[float, list[float]]:
    """Cross-entropy of aggregate played-move frequency against root move scores."""
    if len(child_values) != len(root.moves):
        raise RuntimeError("played-move policy child value count does not match root moves")
    total_occurrences = target.occurrence_count
    if total_occurrences <= 0:
        raise RuntimeError("played-move policy target has no occurrences")
    logits = [-value / temperature for value in child_values]
    maximum = max(logits)
    exponentials = [math.exp(logit - maximum) for logit in logits]
    normalizer = sum(exponentials)
    probabilities = [value / normalizer for value in exponentials]
    target_probabilities = [
        target.move_counts.get(move.move, 0) / total_occurrences for move in root.moves
    ]
    loss = -sum(
        target_probability * math.log(max(probability, 1e-300))
        for probability, target_probability in zip(
            probabilities, target_probabilities, strict=True
        )
        if target_probability > 0.0
    )
    gradients = [
        (target_probability - probability) / temperature
        for probability, target_probability in zip(
            probabilities, target_probabilities, strict=True
        )
    ]
    return loss, gradients


def sampled_rank_pairs(
    root: MoveTeacherRoot,
    tie_margin: float,
    pair_sampling_cap: int,
    seed: int,
) -> tuple[list[RankPair], int]:
    pairs: list[RankPair] = []
    for left_index, left in enumerate(root.moves):
        for right_index in range(left_index + 1, len(root.moves)):
            right = root.moves[right_index]
            difference = left.teacher_root_score - right.teacher_root_score
            if abs(difference) <= tie_margin:
                continue
            if difference > 0:
                pairs.append(RankPair(left_index, right_index))
            else:
                pairs.append(RankPair(right_index, left_index))
    pairs.sort(
        key=lambda pair: (
            root.moves[pair.better_index].move,
            root.moves[pair.worse_index].move,
        )
    )
    eligible_pair_count = len(pairs)
    if pair_sampling_cap > 0 and len(pairs) > pair_sampling_cap:
        sampled_indices = random.Random(
            stable_seed(seed, "pair-sampling", root.root_board_id)
        ).sample(range(len(pairs)), pair_sampling_cap)
        pairs = [pairs[index] for index in sorted(sampled_indices)]
    return pairs, eligible_pair_count
