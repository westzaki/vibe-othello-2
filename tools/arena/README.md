# Engine Regression Arena

`vibe_othello_arena` runs deterministic baseline vs candidate regression matches.
It launches each engine as an external one-shot command and uses the current
checkout's board core as the referee for opening replay, legal move validation,
pass handling, terminal detection, and final disc counts.

## Example

```sh
build/tools/arena/vibe-othello-arena \
  --baseline-cmd "build/tools/engine-cli/vibe-othello-engine-cli bestmove --depth 4" \
  --candidate-cmd "build/tools/engine-cli/vibe-othello-engine-cli bestmove --depth 4" \
  --openings tools/arena/openings/smoke.txt \
  --out out/matches/smoke \
  --swap-colors \
  --timeout-ms 5000
```

The baseline and candidate commands are shell command fragments. The arena
appends `--moves "<move sequence>"` for each engine call.
Engine commands return one line in the form
`bestmove <coordinate|pass|none> score <score> depth <depth>`. `pass` means the
searched root is a legal forced pass, while `none` means search found no root
move, such as a terminal root or depth-zero query.

`vibe-othello-engine-cli` defaults to the disc-difference evaluator. For local
learned-pattern artifact checks, pass pattern evaluator options inside the
engine command:

```sh
build/tools/engine-cli/vibe-othello-engine-cli bestmove \
  --depth 3 \
  --eval pattern \
  --pattern-set pattern-v2-endgame-lite \
  --pattern-weights /path/to/v0b.weights.bin
```

## Persistent Pattern Artifact Arena

`vibe-othello-pattern-artifact-arena` compares two local pattern artifacts over
normalized schema v2 late-game positions. Unlike the process arena above, it
loads the candidate and baseline artifacts once, constructs both
`PatternEvaluator` instances once, and reuses them for every game. It does not
invoke Python per game and does not reload artifact files per game.

The primary local diagnostic use is `pattern-v2-endgame-lite` versus
`pattern-v1-buro-lite`:

```sh
build/tools/arena/vibe-othello-pattern-artifact-arena \
  --positions-tsv "$VIBE_OTHELLO_MEASUREMENTS/<connected-or-teacher-run>/runs/<run-id>/resplit-normalized.tsv" \
  --candidate-weights "$VIBE_OTHELLO_MEASUREMENTS/<v2-run>/v0c.weights.bin" \
  --candidate-manifest "$VIBE_OTHELLO_MEASUREMENTS/<v2-run>/v0c.manifest.json" \
  --candidate-name pattern-v2-endgame-lite \
  --baseline-weights "$VIBE_OTHELLO_MEASUREMENTS/<v1-run>/v0c.weights.bin" \
  --baseline-manifest "$VIBE_OTHELLO_MEASUREMENTS/<v1-run>/v0c.manifest.json" \
  --baseline-name pattern-v1-buro-lite \
  --max-empty 12 \
  --max-positions 1000 \
  --seed 0 \
  --side-swap \
  --depth 3 \
  --report-out "$VIBE_OTHELLO_MEASUREMENTS/arena/v2-vs-v1-late-game-1000/arena-report.json" \
  --summary-out "$VIBE_OTHELLO_MEASUREMENTS/arena/v2-vs-v1-late-game-1000/arena-summary.md" \
  --progress-every 100
```

Input must be normalized schema v2 TSV with at least `board_id`,
`board_a1_to_h8`, `empty_count`, `phase`, and `split`. The arena filters to
`empty_count <= --max-empty`, de-duplicates by `board_id`, and, when
`--max-positions` is set, chooses a deterministic bounded sample by hashing
`board_id` with `--seed`.

With `--side-swap`, each selected position is played twice: candidate as the
side-to-move player, then baseline as the side-to-move player. This reduces the
chance that a result is only measuring the color/side assignment of a fixed
position. Move choice uses the existing fixed-depth search path with the
selected artifact-backed evaluator; tie-breaking follows the current search
implementation.

The JSON report records artifact paths and checksums, input/eligible/selected
position counts, game counts, W-L-D, score and score rate, average/median disc
diff from candidate perspective, an approximate score-rate interval, breakdowns
by empty count, phase, split, and side assignment, failed game count, timing,
command, caveats, and a report checksum. The Markdown summary repeats the key
settings and result tables for local review.

For repeated local validation across depths, seeds, and comparison pairs, use
the matrix helper:

```sh
python3 tools/arena/run_pattern_artifact_arena_matrix.py \
  --positions-tsv "$VIBE_OTHELLO_MEASUREMENTS/<source>/selected-low-empty-normalized.tsv" \
  --output-dir "$VIBE_OTHELLO_MEASUREMENTS/<arena-matrix-run>" \
  --comparison-name move_teacher_v2_vs_exact_root_v2 \
  --candidate-weights "$VIBE_OTHELLO_MEASUREMENTS/<candidate>/move-teacher-child.weights.bin" \
  --candidate-manifest "$VIBE_OTHELLO_MEASUREMENTS/<candidate>/move-teacher-child.manifest.json" \
  --candidate-name move-teacher-v2-100k-seed0 \
  --baseline-weights "$VIBE_OTHELLO_MEASUREMENTS/<source>/exact-root-v2.weights.bin" \
  --baseline-manifest "$VIBE_OTHELLO_MEASUREMENTS/<source>/exact-root-v2.manifest.json" \
  --baseline-name exact-root-v2-100k \
  --depths 3,5,7 \
  --seeds 0,10,20,30,40 \
  --max-positions 1000 \
  --side-swap \
  --same-artifact-sanity candidate \
  --swap-sanity primary \
  --resume
```

The helper validates artifact manifests before running, writes per-run local
arena reports under `runs/`, writes aggregate `arena-matrix-report.json` and
`arena-matrix-summary.md`, and validates per-run resume sidecars with input and
output checksums. Additional comparison pairs can be supplied with
`--pair-json`. The helper treats `--candidate-name` and `--baseline-name` as
display names; the runtime pattern set passed to the arena is read from each
manifest's `pattern_set_id`.

The same tool can also emit a focused bottleneck diagnostic JSON report without
reloading artifacts per position:

```sh
build/tools/arena/vibe-othello-pattern-artifact-arena \
  --positions-tsv "$VIBE_OTHELLO_MEASUREMENTS/<connected-or-teacher-run>/runs/<run-id>/resplit-normalized.tsv" \
  --candidate-weights "$VIBE_OTHELLO_MEASUREMENTS/<v2-run>/v0c.weights.bin" \
  --candidate-manifest "$VIBE_OTHELLO_MEASUREMENTS/<v2-run>/v0c.manifest.json" \
  --candidate-name pattern-v2-endgame-lite \
  --baseline-weights "$VIBE_OTHELLO_MEASUREMENTS/<v1-run>/v0c.weights.bin" \
  --baseline-manifest "$VIBE_OTHELLO_MEASUREMENTS/<v1-run>/v0c.manifest.json" \
  --baseline-name pattern-v1-buro-lite \
  --max-empty 12 \
  --max-positions 1000 \
  --seed 0 \
  --side-swap \
  --depth 3 \
  --diagnostics-out "$VIBE_OTHELLO_MEASUREMENTS/arena/v2-vs-v1-diagnostics/diagnostics.json" \
  --compare-static-scores \
  --compare-best-moves \
  --depth-sweep 1,3,5 \
  --exact-adjudicate-disagreements \
  --max-disagreements 200 \
  --report-out "$VIBE_OTHELLO_MEASUREMENTS/arena/v2-vs-v1-diagnostics/arena-report.json" \
  --summary-out "$VIBE_OTHELLO_MEASUREMENTS/arena/v2-vs-v1-diagnostics/arena-summary.md"
```

`--compare-static-scores` records candidate and baseline static scores on each
selected root position, their score diff, score ranges, zero/nonzero counts,
and sign agreement against any side-to-move labels present in the normalized
TSV. `--compare-best-moves` records each artifact's fixed-depth root best move
and search score, best-move disagreement counts, and whether static score
ordering agrees with search score ordering. `--depth-sweep 1,3,5` reruns the
same selected side-swapped positions at each listed depth and stores
`results_by_depth` in the diagnostics report. `--exact-adjudicate-disagreements`
solves at most `--max-disagreements` low-empty root move disagreements exactly
when feasible.

The diagnostics report also includes runtime/export compatibility and
perspective checks: manifest pattern-set id versus runtime-selected pattern
set, normalized row phase versus runtime phase mapping, label perspective,
same-artifact mirror sanity fields, side-assignment buckets, candidate/baseline
arena buckets, and suspicious indicator strings for obvious sign, perspective,
side-swap, depth, or activation problems. Feature activation counts are grouped
by pattern family for both artifacts; candidate families not present in the
baseline are counted separately so `pattern-v2-endgame-lite` additions can be
checked for actual activation on late-game selected positions. Activation means
the runtime feature geometry fired an instance with a non-empty ternary index;
it does not inspect learned weight magnitude.

For decision-leverage work, run move-teacher ranking before treating arena
results as informative. `vibe-othello-evaluate-move-teacher-ranking` compares an
artifact against exact per-move child labels from
`vibe-othello-generate-exact-move-teacher-dataset`. It evaluates each legal
child board, converts child side-to-move scores to predicted root move scores
with `-eval(child)`, and reports top-1 exact-best accuracy, tie-aware top-1,
best-in-top-2, pairwise accuracy, root regret, exact-best predicted rank, and
all-same predicted-score roots. This catches the PR #161 bottleneck where static
scores and feature activation changed, but root best moves changed too rarely
to move arena results.

The local campaign helper can train an after-move child-label artifact and then
optionally run this arena when a baseline artifact is supplied:

```sh
python3 tools/pattern/labels/run_move_teacher_decision_campaign.py \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/<run>/resplit-normalized.tsv" \
  --output-dir "$VIBE_OTHELLO_MEASUREMENTS/move-teacher/<campaign-id>" \
  --max-empty 12 \
  --max-roots 5000 \
  --pattern-set pattern-v2-endgame-lite \
  --arena-baseline-weights "$VIBE_OTHELLO_MEASUREMENTS/<baseline>/v0c.weights.bin" \
  --arena-baseline-manifest "$VIBE_OTHELLO_MEASUREMENTS/<baseline>/v0c.manifest.json" \
  --arena-baseline-name pattern-v2-endgame-lite \
  --resume
```

`--resume` validates per-stage sidecar metadata before skipping. The helper
compares the current command, input checksums, and output checksums, and fails
on missing or mismatched metadata rather than combining stale ranking or arena
outputs with new arguments.

If ranking metrics do not improve, arena noise should not be interpreted as a
strength result. The likely next bottleneck is decision leverage in the score
shape or rank objective, not another local learning-rate or weight-decay tweak.

Suggested local diagnostic sequence:

```sh
# Same-artifact mirror sanity.
build/tools/arena/vibe-othello-pattern-artifact-arena ... \
  --candidate-weights "$VIBE_OTHELLO_MEASUREMENTS/<v1-run>/v0c.weights.bin" \
  --candidate-manifest "$VIBE_OTHELLO_MEASUREMENTS/<v1-run>/v0c.manifest.json" \
  --candidate-name pattern-v1-buro-lite \
  --baseline-weights "$VIBE_OTHELLO_MEASUREMENTS/<v1-run>/v0c.weights.bin" \
  --baseline-manifest "$VIBE_OTHELLO_MEASUREMENTS/<v1-run>/v0c.manifest.json" \
  --baseline-name pattern-v1-buro-lite \
  --diagnostics-out "$VIBE_OTHELLO_MEASUREMENTS/arena/sanity-v1-v1/diagnostics.json" \
  --compare-static-scores --compare-best-moves --side-swap

# Candidate/baseline swap sanity on the same positions, seed, and depth.
build/tools/arena/vibe-othello-pattern-artifact-arena ... \
  --candidate-name pattern-v2-endgame-lite --baseline-name pattern-v1-buro-lite \
  --diagnostics-out "$VIBE_OTHELLO_MEASUREMENTS/arena/swap-v2-v1/diagnostics.json" \
  --compare-static-scores --compare-best-moves --side-swap

build/tools/arena/vibe-othello-pattern-artifact-arena ... \
  --candidate-name pattern-v1-buro-lite --baseline-name pattern-v2-endgame-lite \
  --diagnostics-out "$VIBE_OTHELLO_MEASUREMENTS/arena/swap-v1-v2/diagnostics.json" \
  --compare-static-scores --compare-best-moves --side-swap
```

The two swap sanity reports should approximately complement each other:
candidate score rates should sum to about `1.0`, and average disc diff from
candidate perspective should approximately negate. A failure is not by itself a
strength result; it is a cue to inspect side assignment, perspective, or
deterministic move-order effects.

Interpret score rate cautiously. A rough local diagnostic guide is:

* `> 0.55` over at least 500 side-swapped games: strong local positive signal
* `0.52` to `0.55`: weak positive signal
* `0.48` to `0.52`: inconclusive
* `< 0.48`: negative signal

This guide is not a strength definition. The persistent pattern artifact arena
is a local late-game artifact-vs-artifact diagnostic only. It is not Elo, not
self-play, not a production strength claim, and not a publication gate.
Generated arena reports, logs, weights, artifacts, and corpus payloads must stay
out of git.

## Persistent Full-Game Artifact Arena

`vibe-othello-full-game-artifact-arena` evaluates two manifest-backed artifacts
from opening positions through terminal games. It loads each manifest and
constructs each phase-aware evaluator once before all games, then runs every
selected opening twice with candidate as Black and White. Opening syntax reuses
the process arena parser, so `start:`, move-sequence shorthand, and `id: moves`
are all accepted.

```sh
build/tools/arena/vibe-othello-full-game-artifact-arena \
  --candidate-manifest "$VIBE_OTHELLO_MEASUREMENTS/<candidate>/manifest.json" \
  --baseline-manifest "$VIBE_OTHELLO_MEASUREMENTS/<baseline>/manifest.json" \
  --openings tools/arena/openings/smoke.txt \
  --report-out "$VIBE_OTHELLO_MEASUREMENTS/arena/full-game/arena-report.json" \
  --search-preset full --limit-mode nodes --nodes 200000 --exact-endgame-empties 8 \
  --seed 0 --opening-limit 100 --minimum-opening-pairs 100
```

`basic` uses default search options; `full` enables the non-experimental search
stack used for bounded artifact evaluation. Depth, node, and time limits are
identical per move for both artifacts. An exact-endgame threshold requires a
node or time cap because exact root search does not use the depth cap.

The versioned `full-game-artifact-arena-v3` report records every engine search
call separately for candidate and baseline: completed depth, nanosecond-resolution elapsed time,
public node/evaluator/TT/PVS/aspiration/IID/endgame/selective counters, exact
and stopped flags, side to move, occupied count, and runtime phase. It also
contains per-engine aggregates, phase and side-to-move buckets, depth
percentiles, TT rates, time-budget overshoot, and a deterministic opening-pair
cluster-bootstrap 95% interval. The confidence interval resamples opening
pairs, never individual games.

Schema v3 replaces the v2 TT `overwrites` and `collisions` fields with explicit
`replacements`, `bucket_conflicts`, and `same_key_updates` counters. The
configured `--tt-bytes` budget always applies to the actual search session;
`--persistent-session` controls only whether knowledge is retained between
moves. Allocation output includes `tt_enabled` and `tt_allocation_succeeded` so
an intentional zero-byte table is distinguishable from allocation failure.

Speed rates use the arena's `steady_clock` nanosecond measurement around each
search call. The engine-reported integer milliseconds and their difference
from the arena timer remain in the report as timer-accounting diagnostics.

For new strength-gate measurements, select exactly one explicit limit mode:

```sh
--limit-mode depth --depth 5
--limit-mode nodes --nodes 200000
--limit-mode time --time-ms 250
```

Legacy combined depth/node/time commands remain runnable and are labeled
`legacy_combined` in the report; they are not gate-eligible. Time limits are
cooperative and may overshoot, which is measured in the report.

`strength_gate.eligible` additionally requires zero failed and illegal games,
complete opening pairs, at least `--minimum-opening-pairs`, and non-empty
candidate and baseline telemetry. The paired interval remains available for an
invalid run but is marked `descriptive_only`.

Use the local-only sanity companion to verify same-artifact, color-swap, and
candidate/baseline argument-order behavior:

```sh
python3 tools/arena/run_full_game_artifact_arena_sanity.py \
  --exe build/tools/arena/vibe-othello-full-game-artifact-arena \
  --candidate-manifest "$VIBE_OTHELLO_MEASUREMENTS/<candidate>/manifest.json" \
  --candidate-weights "$VIBE_OTHELLO_MEASUREMENTS/<candidate>/weights.bin" \
  --baseline-manifest "$VIBE_OTHELLO_MEASUREMENTS/<baseline>/manifest.json" \
  --baseline-weights "$VIBE_OTHELLO_MEASUREMENTS/<baseline>/weights.bin" \
  --openings tools/arena/openings/smoke.txt \
  --output-dir "$VIBE_OTHELLO_MEASUREMENTS/arena/full-game-sanity" \
  --limit-mode nodes --nodes 200000 --exact-endgame-empties 8 --opening-limit 100
```

The JSON-only report includes runtime artifact identities and checksums,
resolved search options, selected openings, per-game and per-opening results,
color-assignment buckets, pass/failure/illegal counts, elapsed time, and
same-artifact paired-neutral sanity data. Its deterministic checksum excludes
paths and elapsed time while retaining all non-time outcomes and counters,
including backend telemetry. Per-call and phase-aggregated telemetry reports
`incremental_eval_enabled`, state initializations,
incremental/stateless evaluation calls, incremental updates, and touched
pattern instances, so fallback-only and incremental phase behavior can be
compared directly.

This is a local strength-gate foundation, not Elo,
artifact promotion, or a production-strength claim; generated reports and local
artifacts must not be committed.

## Openings

Opening files are plain text:

```text
# comment
start:
f5:
custom: d3 c3
```

Comments start with `#`. Blank lines are ignored. Each non-comment line may use
`id: moves...`. If the right side is empty and the id itself is a legal move
sequence, the id is also used as the sequence. Invalid openings are rejected at
startup.

## Output

The `--out` directory receives:

```text
manifest.json
summary.json
games.jsonl
```

`manifest.json` records the commands, openings path, color-swap flag, and
timeout. `games.jsonl` contains one JSON object per game with final disc counts,
winner, candidate result, candidate disc differential, and reason. `summary.json`
aggregates candidate wins, draws, losses, score, win rate, average disc
differential, and invalid game count.

## Color Swaps

With `--swap-colors`, each opening is played twice: candidate as black, then
candidate as white. Without it, candidate plays black for every opening.

## Timeout And Invalid Moves

`--timeout-ms` applies to each one-shot engine call. A timeout, crash, parse
error, or illegal move ends that game immediately and records a loss for the
offending engine.

## Scope

This tool is for engine regression and match evaluation. Human playable UI, Web
UI, WASM integration, production learned weights, and external Edax/Egaroucid
adapters are future work. Process launching currently targets Mac/Linux; Windows
portability is outside the initial scope.
