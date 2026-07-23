# Arena and Strength-Gate Architecture

## Purpose

The full-game artifact arena is a local-only paired comparison harness. It is
not an Elo system, an artifact-promotion mechanism, or a production-strength
claim. It compares two manifest-backed evaluators from identical opening
positions while the current checkout remains the referee.

## Implemented Surface

Arena tooling has three distinct comparison layers:

* `vibe-othello-arena` referees external one-shot engine commands.
* `vibe-othello-pattern-artifact-arena` compares loaded artifacts on selected
  normalized late-game positions.
* `vibe-othello-full-game-artifact-arena` loads two phase-aware evaluators once
  and plays paired full games with explicit depth, node, or time limits.

The full-game path is the foundation for the fixed-time artifact campaign and
the Multi-ProbCut campaign. Its v4 report includes deterministic paired-opening
selection, color swaps, runtime/build/input identities, complete public search
telemetry, explicit TT allocation and session-retention policy, artifact and
search-option identities, failure adjudication, opening-pair bootstrap
intervals, and strength-gate eligibility. Python campaign runners add
checksum-guarded stage resume and schema-checked decision reports without
changing artifacts or the default pointer.

## Current Limitations

Wall-time search is cooperative and machine/load-sensitive. Fixed-time sanity
and argument-order symmetry are therefore diagnostics rather than exact timing
invariants. Matrix-wide outcome aggregates are descriptive because repeated
openings across cells are not independent. Long campaigns remain local-only;
CI exercises small real-tool smoke overrides. No reviewed Multi-ProbCut
calibration or fixed-time strength report is committed, and campaign output
cannot enable a search preset by itself.

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

The v4 CLI has explicit pure modes:

* `--limit-mode depth --depth N`
* `--limit-mode nodes --nodes N`
* `--limit-mode time --time-ms N`

Node and time modes use iterative search's infinite mode with the selected
cooperative stopping condition. The mode and its corresponding single non-zero
limit are required; implicit or mixed limits are rejected. Reports always
include the resolved limits and all search options.

## Telemetry

Each engine search call records its engine role, side to move, occupied count,
artifact phase id, completed depth, arena-measured nanosecond elapsed time, all public `SearchStats`
counters, exact/stopped flags, and exact-handoff status. Aggregates are emitted
separately for candidate and baseline overall, by phase, and by side to move.
ProbCut policy is independently resolved for candidate and baseline. Global and
phase/deep/shallow telemetry includes attempts, shallow nodes, successes,
rejections, real beta cuts, and shadow false cuts.

`SearchStats` is the canonical telemetry payload: the arena stores the complete
engine struct on each call and aggregates it in `full_game_artifact_arena_core`.
It does not maintain a parallel arena-only copy of the counters. Report and
deterministic-checksum serialization must therefore be updated together when a
new public counter is added. Schema v4 reports the resolved endgame PVS option
and stability mode in each option payload and emits last-flip and stability
counters in every per-search and aggregate telemetry record. The PVS option,
stability mode, and counters are part of deterministic report identity.

Backend telemetry keeps stable report fields corresponding to the public
`SearchStats` payload on each search record and in every aggregate:
incremental enablement and state
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

Same-artifact same-search-configuration sanity requires each opening pair to
have score exactly 0.5 and candidate disc-difference sum exactly zero. A
same-artifact comparison with different search policies is an A/B strength
cell, not a neutrality sanity cell. The companion
`run_full_game_artifact_arena_sanity.py` runs candidate/candidate,
baseline/baseline, and candidate/baseline in both argument orders. It preserves
weights overrides and checks content identities, selected openings, search
configuration, failures, score complementarity, and disc-difference inversion.

Generated reports, logs, and sanity output are local-only and must not be
committed.

## Fixed-Time Campaign Layer

The fixed-time artifact strength campaign is a Python orchestration layer over
the v4 full-game arena. Its default 3-by-3 matrix crosses 50, 100, and 500 ms
per move with exact thresholds 8, 10, and 12 while holding the TT budget and
session-retention policy constant. Every cell includes forward, argument-order
reverse, and both same-artifact comparisons; the arena remains responsible for
paired colors, legal-move adjudication, and search telemetry.

Candidate and baseline manifest and weights paths are required inputs. The
campaign does not resolve the default artifact pointer. An optional independent
holdout corpus repeats the complete matrix and becomes the decision-driving
opening set when supplied.

The campaign converts the reversed argument-order result back to the original
candidate perspective and averages both orders by opening before producing each
cell's strength interval. It aggregates game outcomes, cell-level paired-opening
bootstrap intervals, disc differences, phase and side-to-move game-result
exposures, and per-role search telemetry into one decision report. Conditions
from different matrix cells are not independent, so the heterogeneous aggregate
is descriptive and has no confidence interval.

Promotion is only a suggested local category. The promotion contract always
uses a fixed 95% cell interval regardless of the optional displayed confidence
level. It also requires a configurable opening-pair floor, positive score and
95% lower bound, no material p50 completed-depth regression, and passing cells
at multiple distinct time limits. Same-artifact exact neutrality and exact
argument-order complementarity under fixed wall time are timing-sensitive
diagnostics, not correctness rejection gates. Failed or illegal games remain
correctness failures. The runner does not mutate artifacts or defaults.

Each arena stage owns a resume sidecar. A stage is reusable only when its full
campaign config and command, input content, artifact identity, repository
identity, runner and executable identity, and output content all match. Missing,
partial, or mismatched resume state is rejected instead of silently mixing
campaigns. Independently of resume, every arena report is checked against the
requested search config, runtime artifact identities, executable, opening
source, selected count, and seed before it can enter the decision report.

Search-session retention is an explicit arena configuration, never an implicit
property of the evaluator. Candidate and baseline own independent sessions.
Sessions clear at game boundaries and may retain TT/history/killer knowledge
only between sequential moves of that game. Reports record whether retention
was enabled and the requested and actual TT allocation. The configured TT byte
budget is always bound to the session used by search; disabling persistence
clears that session before each move instead of falling back to a default TT.

Schema v4 adds independently configured candidate/baseline ProbCut policy and
phase/depth-pair telemetry. Schema v3 was the first schema with split TT replacement, bucket-conflict, and
same-key-update telemetry. This intentionally supersedes the v2 `overwrites`
and `collisions` field names. Allocation reporting includes both enabled state
and allocation success so an intentional zero-byte table is distinguishable
from allocation failure.

## Multi-ProbCut Campaign Layer

The Multi-ProbCut campaign uses one artifact and reviewed calibration identity
for off/off sanity, separately audited first-pair versus off, ordered multi-pair versus
off, multi-pair versus the first-pair policy, and shadow multi-pair versus off,
all in both policy assignments. Fixed-depth, fixed-node, and fixed-time cells
share openings, exact policy, TT size, and session-retention settings.
Multiple seeds and repeatable opening inputs are first-class dimensions.
Before any game starts, Arena resolves candidate and baseline ProbCut options
with the same public resolver used by engine search normalization. A requested
non-off mode that is not effective—including a prefix missing evidence for one
enabled domain—is a hard configuration error. Reports keep requested modes and
requested prefix/probe fields separate from effective enablement, effective
prefix, and effective probe cap; `candidate_resolved_options` and
`baseline_resolved_options` contain only the effective configuration.

The campaign report is evidence input, not an enablement action. Automatic
checks cover clean games, exact fixed-depth/fixed-node off/off sanity, at least
100 primary opening pairs, 500-ms multi-versus-off score/CI/depth/efficiency,
multi-versus-single non-inferiority, 100-ms direction, later-pair attempts and
successes, and scheduler-level shadow false-cut audit. Fixed-time same-config
runs are statistical diagnostics rather than strict-neutral gates. Production
review must additionally accept fixed-depth correctness and exact holdouts.
The runner never changes a preset or authorizes production enablement.
