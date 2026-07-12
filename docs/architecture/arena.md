# Arena and Strength-Gate Architecture

## Purpose

The full-game artifact arena is a local-only paired comparison harness. It is
not an Elo system, an artifact-promotion mechanism, or a production-strength
claim. It compares two manifest-backed evaluators from identical opening
positions while the current checkout remains the referee.

## Pairing

Every selected opening produces exactly two games: candidate as Black and
candidate as White. Opening selection is deterministic from the seed, opening
identity, and source index. A timeout/stopped search without a move, malformed
search result, or illegal move is adjudicated as a loss for the offending side
and remains visible in the report.

The statistical unit is an opening pair, not an individual game. A pair score
is the mean of its two game scores, where win/draw/loss are 1/0.5/0. The
reported confidence interval is a deterministic percentile cluster bootstrap
over opening pairs with a supplied seed.

## Search Limits

The v2 CLI has explicit pure modes:

* `--limit-mode depth --depth N`
* `--limit-mode nodes --nodes N`
* `--limit-mode time --time-ms N`

Node and time modes use iterative search's infinite mode with the selected
cooperative stopping condition. Legacy combinations remain runnable for
compatibility but are reported as `legacy_combined` and are not gate eligible.
Reports always include the resolved limits and all search options.

## Telemetry

Each engine search call records its engine role, side to move, occupied count,
artifact phase id, completed depth, arena-measured nanosecond elapsed time, all public `SearchStats`
counters, exact/stopped flags, and exact-handoff status. Aggregates are emitted
separately for candidate and baseline overall, by phase, and by side to move.

Nodes/sec and evals/sec use the arena timer rather than the engine's
millisecond-rounded elapsed field. Both timings and their accounting delta are
reported. Exact handoff usage is derived from nonzero endgame nodes; root exact
search is reported separately.

Wall-time search is cooperative. Its elapsed time and completed depth can vary
with machine load, so reports expose time-budget overshoot rather than treating
wall time as a hard deadline. Deterministic report checksums exclude elapsed
time while retaining all non-time outcomes and counters.

## Identity and Sanity

The report fingerprints repository SHA and dirty state observed at configure
time, executable content where readable, compiler/build metadata, artifact
manifest and weights content, opening input, selected openings, and resolved
search configuration.

Formal strength-gate eligibility requires a pure limit mode, zero failed and
illegal games, complete pairs, the configured minimum pair count, and telemetry
from both engines. Bootstrap output from an ineligible run is descriptive only.

Same-artifact sanity requires each opening pair to have score exactly 0.5 and
candidate disc-difference sum exactly zero. The companion
`run_full_game_artifact_arena_sanity.py` runs candidate/candidate,
baseline/baseline, and candidate/baseline in both argument orders. It preserves
weights overrides and checks content identities, selected openings, search
configuration, failures, score complementarity, and disc-difference inversion.

Generated reports, logs, and sanity output are local-only and must not be
committed.

Search-session retention is an explicit arena configuration, never an implicit
property of the evaluator. Candidate and baseline own independent sessions.
Sessions clear at game boundaries and may retain TT/history/killer knowledge
only between sequential moves of that game. Reports record whether retention
was enabled and the requested and actual TT allocation.
