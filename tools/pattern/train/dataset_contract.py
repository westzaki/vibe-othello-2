"""Dataset and trainer constants for pattern training."""

from __future__ import annotations

EXPECTED_HEADER = [
    "record_id",
    "ply",
    "split",
    "label_final_disc_diff",
    "phase",
    "pattern_id",
    "instance",
    "ternary_index",
]
COMPACT_HEADER = [
    "record_id",
    "ply",
    "split",
    "label_final_disc_diff",
    "phase",
    "pattern_features",
]
SPLITS = ("train", "validation", "test")
PHASES = tuple(range(13))
PHASE_COUNT = len(PHASES)
SCORE_UNIT = "disc-diff"
PHASE_MAPPING_ID = "disc-count-13-v1"
REPORT_SCHEMA_VERSION = 1
WEIGHTS_SCHEMA_VERSION_V1 = "pattern-eval-weights-v1"
WEIGHTS_SCHEMA_VERSION_V2 = "pattern-eval-weights-v2"
WEIGHTS_SCHEMA_VERSION = WEIGHTS_SCHEMA_VERSION_V2
TRAINER_ALGORITHM_V0A = "phase-bias-v0a"
TRAINER_ALGORITHM_V0B = "pattern-sgd-v0b"
TRAINER_ALGORITHM_V0C = "pattern-sgd-v0c"
TRAINER_ALGORITHM_V0D = "pattern-sgd-v0d"

def parse_int(text: str | None) -> int | None:
    if text is None:
        return None
    try:
        value = int(text)
    except ValueError:
        return None
    if str(value) != text:
        return None
    return value
