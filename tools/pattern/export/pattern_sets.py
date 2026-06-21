"""Pattern-set metadata shared by local artifact exporters."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class PatternSetSpec:
    pattern_set_id: str
    patterns: tuple[tuple[str, int], ...]
    note: str
    v0a_artifact_id: str
    v0b_artifact_id: str


PATTERN_SETS: dict[str, PatternSetSpec] = {
    "fixed-pattern-fixture-v1": PatternSetSpec(
        pattern_set_id="fixed-pattern-fixture-v1",
        patterns=(
            ("edge-8", 8),
            ("corner-3x3", 9),
        ),
        note="local smoke artifact; not production",
        v0a_artifact_id="tiny-smoke-phase-bias-v0a-artifact-v1",
        v0b_artifact_id="tiny-smoke-pattern-sgd-v0b-artifact-v1",
    ),
    "pattern-v1-buro-lite": PatternSetSpec(
        pattern_set_id="pattern-v1-buro-lite",
        patterns=(
            ("edge-8", 8),
            ("near-edge-8", 8),
            ("diagonal-8", 8),
            ("diagonal-7", 7),
            ("corner-2x5", 10),
            ("corner-3x3", 9),
        ),
        note="local pattern-v1-buro-lite artifact; not production",
        v0a_artifact_id="buro-lite-phase-bias-v0a-local-artifact-v1",
        v0b_artifact_id="buro-lite-pattern-sgd-v0b-local-artifact-v1",
    ),
    "pattern-v2-endgame-lite": PatternSetSpec(
        pattern_set_id="pattern-v2-endgame-lite",
        patterns=(
            ("edge-8", 8),
            ("near-edge-8", 8),
            ("diagonal-8", 8),
            ("diagonal-7", 7),
            ("corner-2x5", 10),
            ("corner-3x3", 9),
            ("corner-2x4-8", 8),
            ("edge-plus-x-10", 10),
            ("corner-wing-8", 8),
            ("near-edge-segment-8", 8),
            ("diagonal-corner-8", 8),
        ),
        note="local pattern-v2-endgame-lite artifact; not production",
        v0a_artifact_id="endgame-lite-phase-bias-v0a-local-artifact-v1",
        v0b_artifact_id="endgame-lite-pattern-sgd-v0b-local-artifact-v1",
    ),
}

ALIASES = {
    "tiny": "fixed-pattern-fixture-v1",
    "buro-lite": "pattern-v1-buro-lite",
    "endgame-lite": "pattern-v2-endgame-lite",
}


def resolve_pattern_set(name: str) -> PatternSetSpec:
    key = ALIASES.get(name, name)
    try:
        return PATTERN_SETS[key]
    except KeyError as error:
        choices = ", ".join(sorted((*PATTERN_SETS, *ALIASES)))
        raise RuntimeError(f"--pattern-set must be one of: {choices}") from error
