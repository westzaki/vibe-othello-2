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

The v3 CLI has explicit pure modes:

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

Backend telemetry keeps the public `SearchStats` field names on each search
record and in every aggregate: incremental enablement and state
initializations, incremental and stateless evaluation calls, incremental
updates, and touched pattern instances. Aggregate records additionally report
the number of searches that enabled incremental evaluation. Combined with the
artifact phase bucket, this distinguishes learned incremental searches from
phase-aware fallback-only searches and makes phase-local NPS changes auditable.

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

## Fixed-Time Campaign Layer

The fixed-time artifact strength campaign is a Python orchestration layer over
the v3 full-game arena. Its default 3-by-3 matrix crosses 50, 100, and 500 ms
per move with exact thresholds 8, 10, and 12 while holding the TT budget and
session-retention policy constant. Every cell includes forward, argument-order
reverse, and both same-artifact comparisons; the arena remains responsible for
paired colors, legal-move adjudication, and search telemetry.

Candidate and baseline manifest and weights paths are required inputs. The
campaign does not resolve the default artifact pointer. An optional independent
holdout corpus repeats the complete matrix and becomes the decision-driving
opening set when supplied.

The campaign aggregates game outcomes, paired-opening bootstrap intervals,
disc differences, phase and side-to-move game-result exposures, and per-role
search telemetry into one decision report. Promotion is only a suggested local
category and requires correctness sanity, a positive primary-cell confidence
lower bound and score rate, sufficient matrix breadth, and no material p50
completed-depth regression. The runner does not mutate artifacts or defaults.

Each arena stage owns a resume sidecar. A stage is reusable only when its full
campaign config and command, input content, artifact identity, repository
identity, runner and executable identity, and output content all match. Missing,
partial, or mismatched resume state is rejected instead of silently mixing
campaigns.

Search-session retention is an explicit arena configuration, never an implicit
property of the evaluator. Candidate and baseline own independent sessions.
Sessions clear at game boundaries and may retain TT/history/killer knowledge
only between sequential moves of that game. Reports record whether retention
was enabled and the requested and actual TT allocation. The configured TT byte
budget is always bound to the session used by search; disabling persistence
clears that session before each move instead of falling back to a default TT.

Schema v3 is the first schema with split TT replacement, bucket-conflict, and
same-key-update telemetry. This intentionally supersedes the v2 `overwrites`
and `collisions` field names. Allocation reporting includes both enabled state
and allocation success so an intentional zero-byte table is distinguishable
from allocation failure.
