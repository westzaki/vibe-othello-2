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

Engines such as NTest that do not implement this command/response contract can
use the persistent NBoard match tool described below. The artifact arenas only
compare evaluators inside the current process.

`vibe-othello-engine-cli` defaults to the committed learned artifact. To compare
a custom artifact, pass its manifest inside the engine command:

```sh
build/tools/engine-cli/vibe-othello-engine-cli bestmove \
  --depth 3 \
  --eval-artifact /path/to/manifest.json
```

## Persistent NBoard Paired Match

`vibe-othello-nboard-match` keeps one NBoard v1 or v2 engine process alive across a
paired color-swapped match while running Vibe in-process. It performs one
warm-up search per engine by default, applies `OMP_NUM_THREADS=1` to the child,
validates every external move, and writes an optional JSON game record. Vibe
uses the `full` non-experimental search stack, a persistent search session
within each game, and the supplied time, TT, and exact-endgame limits. Report
schema v3 records the engine name, executable, user-supplied runtime identity,
NBoard protocol version, requested depth, and child arguments so fixed-depth,
average-time, book, and engine configurations remain distinguishable without
recording the potentially sensitive working-directory path.

Protocol 1 sends `depth N` and `game <GGF>` for Edax-compatible engines.
Protocol 2 sends `set depth N` and `set game <GGF>` for NTest-compatible
engines. Both dialects use `move`, `go`, and `ping`/`pong` synchronization.

For NTest average-time mode at one second per move:

```sh
build/tools/arena/vibe-othello-nboard-match \
  --nboard-exe /path/to/ntest-runtime/ntest \
  --nboard-working-directory /path/to/ntest-runtime \
  --nboard-name ntest --nboard-runtime-id ntest-book-off \
  --nboard-protocol 2 --nboard-arg x --nboard-arg a1 --nboard-depth 1 \
  --artifact-manifest data/eval/artifacts/<artifact-id>/manifest.json \
  --openings tools/arena/openings/smoke.txt --max-openings 1 \
  --time-ms 1000 --tt-bytes 268435456 --exact-endgame-empties 12 \
  --report-out /path/to/local/match-report.json
```

NTest's book policy is configured by its runtime `parameters.txt`, not by this
CLI. Use a separate runtime directory for book-on and book-off runs so their
configuration and outputs cannot be mixed, and give each configuration a
distinct `--nboard-runtime-id`. `--max-openings 1` plays exactly two games from
the first opening, with Vibe once as Black and once as White.

This command is an integration and short-match surface. It does not yet provide
CPU-affinity control, 10/100 ms NTest time profiles, campaign orchestration, or
paired-opening confidence intervals, so its JSON output is not a formal
strength report.

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

TSV splitting, CRLF handling, and relative-board validation use the shared
`tools/pattern/common/normalized_tsv` implementation also used by dataset and
label tooling, so all normalized-data consumers enforce the same board format.

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
stack used for bounded artifact evaluation, including internal opponent-mobility
ordering at remaining depth 5 or greater. Depth, node, and time limits are
identical per move for both artifacts. An exact-endgame threshold requires a
node or time cap because exact root search does not use the depth cap.

The versioned `full-game-artifact-arena-v4` report records every engine search
call separately for candidate and baseline: completed depth, nanosecond-resolution elapsed time,
public node/evaluator/TT/PVS/aspiration/IID/endgame/selective counters, exact
and stopped flags, side to move, occupied count, and runtime phase. It also
contains per-engine aggregates, phase and side-to-move buckets, depth
percentiles, TT rates, time-budget overshoot, and a deterministic opening-pair
cluster-bootstrap 95% interval. The confidence interval resamples opening
pairs, never individual games.

Schema v4 retains the v3 split TT telemetry and adds independently resolved
candidate/baseline ProbCut policies plus global and phase/depth-pair MPC
telemetry. Schema v3 replaced the v2 TT `overwrites` and `collisions` fields with explicit
`replacements`, `bucket_conflicts`, and `same_key_updates` counters. The
configured `--tt-bytes` budget always applies to the actual search session;
`--persistent-session` controls only whether knowledge is retained between
moves. Allocation output includes `tt_enabled` and `tt_allocation_succeeded` so
an intentional zero-byte table is distinguishable from allocation failure.
For ProbCut, `candidate_requested_probcut_mode` and requested prefix/probe
fields record the command, while `effective_enabled`,
`effective_ordered_depth_pairs`, and `effective_maximum_probes_per_node` record
the shared runtime resolution. Arena exits before playing or writing a report
if any requested non-off policy resolves to disabled.

Speed rates use the arena's `steady_clock` nanosecond measurement around each
search call. The engine-reported integer milliseconds and their difference
from the arena timer remain in the report as timer-accounting diagnostics.

For new strength-gate measurements, select exactly one explicit limit mode:

```sh
--limit-mode depth --depth 5
--limit-mode nodes --nodes 200000
--limit-mode time --time-ms 250
```

The CLI rejects an omitted mode, a missing corresponding limit, and mixed
depth/node/time limits. Time limits are cooperative and may overshoot, which is
measured in the report.

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
  --limit-mode nodes --nodes 200000 --exact-endgame-empties 8 \
  --opening-limit 100 --minimum-opening-pairs 100
```

For corpus-independent promotion openings, the board-core generator samples
uniformly from the sorted legal move set using a fixed SplitMix64 seed and
deduplicates final side-to-move-relative boards:

```sh
build/tools/arena/vibe-othello-generate-independent-openings \
  --output "$VIBE_OTHELLO_MEASUREMENTS/openings/random-16ply-1000.txt" \
  --report-out "$VIBE_OTHELLO_MEASUREMENTS/openings/generator-report.json" \
  --count 1000 --plies 16 --seed 20260727
```

The output and report remain local. When the candidate learned from WHTOR,
run the complete overlap audit documented in `tools/data-import/README.md`
before using the suite as promotion evidence.

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

## Multi-ProbCut Strength Campaign

`run_multi_probcut_strength_campaign.py` runs the same reviewed calibration
profile and artifact through MPC off, separately audited first-pair ProbCut, ordered
Multi-ProbCut, and shadow Multi-ProbCut. In addition to each policy versus off,
the matrix directly runs multi versus single and swaps every policy assignment.
It covers fixed depth, fixed nodes, and 50/100/500 ms by default, repeats
multiple seeds and opening corpora, and includes an off/off same-config sanity
cell. Artifact, opening, exact threshold, TT budget, and persistent-session
policy are held constant within each comparison.

```sh
python3 tools/arena/run_multi_probcut_strength_campaign.py \
  --artifact-manifest "$VIBE_OTHELLO_MEASUREMENTS/<artifact>/manifest.json" \
  --artifact-weights "$VIBE_OTHELLO_MEASUREMENTS/<artifact>/weights.bin" \
  --openings "$VIBE_OTHELLO_CORPORA/<campaign>/openings-a.txt" \
  --openings "$VIBE_OTHELLO_CORPORA/<campaign>/openings-b.txt" \
  --probcut-profile "$VIBE_OTHELLO_MEASUREMENTS/<calibration>/reviewed-profile.tsv" \
  --maximum-margin "$PROBCUT_MAXIMUM_MARGIN" \
  --maximum-probes 2 \
  --maximum-shallow-overhead-ratio 0.20 \
  --seeds 0,1 \
  --output-dir "$VIBE_OTHELLO_MEASUREMENTS/arena/mpc/<campaign>" \
  --resume
```

The profile evaluator/artifact families, training checksum, joint holdout
checksum, per-prefix/probe/domain scheduler evidence, ordered pairs, and reviewed probe cap must match
the resolved Arena options. The runner also binds requested node/depth/time
limits, bootstrap settings, opening checksum/count, artifact runtime identity,
TT/session settings, and Arena strength-gate eligibility. Primary 500-ms cells
are accepted only when both sides' effective policies match the requested
off/single/multi/shadow assignment; a raw single request normalized to off
cannot enter a multi-versus-single result. They are ineligible below 100
opening pairs. Automatic checks require multi to beat
off with a lower 95% bound above 0.5, remain non-degraded versus single, avoid a
large 100-ms reversal, exercise and cut with a later pair, and pass joint shadow
false-cut auditing. Fixed-depth/fixed-node same-config runs require exact
neutrality; fixed-time sanity is reported statistically because scheduling can
change completed depth. It intentionally leaves
`production_enablement_authorized=false`: fixed-depth differential correctness
and exact holdouts still require review. Generated reports remain local and
must not be committed.
No reviewed profile is shipped, so this command is opt-in tooling and does not
enable any preset.

## Fixed-Time Artifact Strength Campaign

`run_fixed_time_artifact_strength_campaign.py` orchestrates a reproducible,
local-only fixed-wall-time matrix over the persistent full-game arena. It never
reads the default artifact pointer: both manifest and weights paths are required
for the candidate and baseline.

The default matrix is 50, 100, and 500 ms per move crossed with exact-endgame
thresholds 8, 10, and 12. It uses a 16 MiB TT and persistent search sessions by
default. Every cell runs candidate-versus-baseline, the reversed argument order,
and candidate/candidate plus baseline/baseline sanity matches. The full-game
arena supplies paired candidate-Black/candidate-White games and deterministic
opening selection.

```sh
python3 tools/arena/run_fixed_time_artifact_strength_campaign.py \
  --candidate-manifest "$VIBE_OTHELLO_MEASUREMENTS/<candidate>/manifest.json" \
  --candidate-weights "$VIBE_OTHELLO_MEASUREMENTS/<candidate>/weights.bin" \
  --baseline-manifest "$VIBE_OTHELLO_MEASUREMENTS/<baseline>/manifest.json" \
  --baseline-weights "$VIBE_OTHELLO_MEASUREMENTS/<baseline>/weights.bin" \
  --opening-corpus "$VIBE_OTHELLO_CORPORA/<campaign>/openings.txt" \
  --arena-executable build/tools/arena/vibe-othello-full-game-artifact-arena \
  --output-dir "$VIBE_OTHELLO_MEASUREMENTS/arena/fixed-time/<campaign>" \
  --campaign-seed 227 --opening-count 100 \
  --time-limits-ms 50,100,500 --exact-thresholds 8,10,12 \
  --tt-bytes 16777216 --persistent-session \
  --bootstrap-iterations 10000 --confidence-level 0.95 \
  --minimum-promotion-opening-pairs 100 \
  --minimum-promotion-time-limits 2
```

Pass `--holdout-opening-corpus` (and optionally
`--holdout-opening-count`) to repeat the complete matrix on an independent
corpus. When present, the holdout primary cell drives the suggested decision.
The default primary cell is the largest configured time limit and exact
threshold; `--primary-time-ms` and `--primary-exact-threshold` override it.

Search-session reuse must remain independently measurable. Run otherwise
identical campaigns once with `--persistent-session` and once with
`--no-persistent-session`, using separate output directories. Compare the
reported completed-depth percentiles, TT hit rates, node counts, and paired
win-rate interval before changing the browser policy; do not infer strength
from latency alone.

The output directory contains `decision.json`, `summary.md`,
`arena-report-inventory.json`, `campaign-manifest.json`,
`campaign.resume.json`, and per-stage reports and resume sidecars under
`runs/`. Resume validates the full campaign config, command, input and artifact
checksums, repository SHA and dirty state, runner and executable identity, and
report checksum before skipping a stage. Any missing or mismatched metadata is
an error. Each completed arena report is additionally bound back to its
requested preset, time, exact threshold, persistence mode, TT budget, runtime
artifact identities, executable, opening corpus, selected count, seed, and
bootstrap config before the campaign accepts it.

The suggested decision distinguishes `promote`, `continue_validation`,
`reject_strength`, `reject_correctness`, and `inconclusive`. The default
promotion gate requires clean games and at least 100 opening pairs in every
counted cell. Each counted cell must have score rate above 0.5, a fixed 95%
paired-bootstrap lower bound above 0.5, and no material p50 completed-depth
regression. At least two distinct time limits must pass all of those gates;
multiple exact thresholds at one time limit do not satisfy that breadth
requirement. `--confidence-level` controls only a supplementary displayed
interval and cannot weaken the fixed 95% promotion contract.
`--minimum-promotion-opening-pairs` may raise the sample floor but cannot lower
it below 100.

Forward and reversed argument-order results are converted to the original
candidate perspective and averaged by opening before computing each cell's
strength interval. Exact fixed-time same-artifact neutrality and exact
forward/reverse complementarity remain visible as timing-sensitive diagnostics,
but they are not correctness rejection gates. Failed or illegal games remain
correctness failures. The heterogeneous matrix aggregate is descriptive only
and intentionally has no confidence interval because the same openings repeat
across conditions. The suggestion is local evidence only: this runner does not
promote artifacts, change weights, or update the default pointer.

Validate a completed decision report independently with:

```sh
python3 tools/arena/check_fixed_time_artifact_strength_decision.py \
  --decision "$VIBE_OTHELLO_MEASUREMENTS/arena/fixed-time/<campaign>/decision.json"
```

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
campaigns are outside its scope. A tested internal POSIX persistent-process and
NBoard session layer now handles protocol versions 1 and 2, synchronization,
GGF position setup, noisy output, and process timeouts. The public
`vibe-othello-arena` command still uses one-shot engine calls; Edax/NTest
profiles, fixed-time configuration, process-lifetime selection, and campaign
reporting remain to be connected. Process launching currently targets
Mac/Linux; Windows portability is outside the initial scope.
