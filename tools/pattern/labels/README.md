# Pattern Label and Root Selection Operations

## Scope

This is the current contract for exact move-teacher cache reuse and
materialization. Cache payloads, materialized TSVs, and reports are local-only;
keep them outside the repository and outside disposable worktrees.

## Phase-Stratified Root Selection

`select_phase_stratified_roots.py` selects a uniform local root quota from
each normalized schema v2 phase `0..12`. A repeated explicit
`--phase-quota PHASE=COUNT` can override the base quota for phases whose finite
unique-board capacity is lower than a larger campaign rung. It validates
schema-v2 rows, dedupes by `board_id`, rejects board/game-group cross-split
leakage, preserves the selected source row and, by default, its split, and emits
a local JSON coverage report. It does not generate teacher labels or train
artifacts.

```sh
python3 tools/pattern/labels/select_phase_stratified_roots.py \
  --normalized-tsv <normalized-v2.tsv> \
  --output-tsv <selected.tsv> \
  --report-out <report.json> \
  --roots-per-phase <count> \
  --phase-quota 0=<explicit-phase-0-count> \
  --seed 0 \
  --max-roots-per-game-group 1 \
  --selected-split-policy game-group-hash \
  --require-all-phases
```

Without `--require-all-phases`, a shortage is a successful partial diagnostic
selection and is explicit in the report. `--max-roots-per-game-group` is an
optional global cap for reducing over-representation of one semantic game.
Overrides are never inferred from input capacity and duplicate phase overrides
fail. The report records the base quota, explicit overrides, effective quota for
every phase, expected total rows, aggregate selected split counts, and a selected
phase × split cross-tab. Selected TSVs and reports are local-only and must not be
committed.

`--selected-split-policy game-group-hash` is an explicit escape hatch for an
input connected split that becomes extremely imbalanced because repeated early
boards join many source games. It requires `--max-roots-per-game-group 1`, runs
only after board dedupe and capacity matching, and assigns each selected root by
a stable hash of source dataset id plus game group id. The selected set therefore
contains at most one row per game and one row per board, so the report must retain
zero selected board/game cross-split collisions. The default `preserve` policy
continues to leave source splits unchanged. Reports record the policy version,
number of changed split assignments, and post-selection leakage audit.

After every selected split assignment, the selector requires both selected
board and selected game-group cross-split collision counts to be zero. Any
collision rejects the run before the selected TSV or report is written. The
full-phase growth cycle independently revalidates both report fields before
partitioning selected roots.

Repeated `--train-only-phase PHASE` options force selected roots in named
phases to `train` after selected-set split assignment. This is intended for
very early phases where different roots can transpose to the same canonical
child and therefore cannot be separated safely across train, validation, and
test. It is explicit in the report and must be limited to the smallest affected
phase set; all remaining phases keep normal held-out splits.

`run_full_phase_growth_cycle.py` accepts the same repeated `--phase-quota`
option and forwards it to root selection. For example, a campaign may keep
phase 0 at a previously validated 100 roots while increasing phases `1..12`:

```sh
python3 tools/pattern/train/run_full_phase_growth_cycle.py \
  <required-local-input-options> \
  --roots-per-phase 500 \
  --phase-quota 0=100 \
  --max-roots-per-game-group 1 \
  --selected-split-policy game-group-hash \
  --train-only-phase 0
```

This is an explicit non-uniform measurement design, not a claim that the phase
0 quota was satisfied at 500 or that phase coverage is balanced by example
count. Every phase must still have its effective quota satisfied and at least
one train root before training and export continue. Teacher outputs and pairwise
datasets preserve the selected-root split produced by this policy.

## Search Move-Teacher Generation

`vibe-othello-generate-search-move-teacher-dataset` generates every legal
root move for selected phases (default `0..9`) by searching each after-move
child from the child side-to-move perspective. The stored root score is the
negative child score. It writes explicit `move-teacher-tsv-v3` rows and
normalized-v2 child rows with `label_kind = teacher_search_final_disc_diff`.
Schema v3 also records the deterministic static fallback value for each child,
allowing the trainer to fit an additive residual without replacing the
existing early/midgame heuristic.

Each `child_board_id` is the importer-compatible `board-v1` SHA-256 identity
of its normalized child board, not a root/move-derived name. Move-teacher rows
retain every root move, while child-normalized rows dedupe equal canonical child
boards within a split. A canonical child board appearing across splits rejects
the transaction. Search scores outside the normalized disc-difference range
`[-64, 64]` reject ordinary fully learned teachers. An explicitly selected
phase-aware bootstrap or a teacher artifact with residual routing is normalized
at the teacher boundary; the generation report records that policy and the v3
baseline column retains the unnormalized static heuristic value.

The `full` search preset includes internal opponent-mobility ordering at
remaining depth 5 or greater. Its search configuration identity is versioned
so outputs created before and after this preset change cannot be resumed as the
same run.

The teacher manifest and weights are required explicitly. The default
`require-all` policy rejects an artifact without complete declared phase
coverage `0..12`. `--teacher-coverage-policy explicit-phase-aware` is an
intentional bootstrap exception: its report names trained and fallback phases,
and its distinct teacher source remains visible through training. Legacy
artifacts without `trained_phases` are always rejected. Depth, node, time,
preset, and exact-endgame threshold are also required explicitly. Use fixed
depth or fixed nodes; wall-clock-only runs are rejected. A stopped node-limited
child is accepted only after at least one completed depth; incomplete roots
reject the whole output transaction and produce no partial TSV.
The report records roots whose node-limited child searches completed at
different depths; fixed-depth completion remains the preferred campaign mode.

Use the checksum-guarded local runner for resume:

```sh
python3 tools/pattern/labels/run_search_move_teacher_generation.py \
  --generator build/tools/pattern/labels/vibe-othello-generate-search-move-teacher-dataset \
  --normalized-tsv <phase-0-to-9-selected.tsv> \
  --teacher-manifest <teacher.manifest.json> \
  --teacher-weights <teacher.weights.bin> \
  --output-dir <local-output-dir> \
  --max-depth 8 --max-nodes 200000 --max-time-ms 0 \
  --search-preset full --exact-endgame-empties 8 --resume
```

The runner validates checksums for the generator, input, teacher manifest,
teacher weights, and all outputs before reuse. It is a complete-output resume
contract, not the exact per-root cache contract below.

## Progressive hard-root teaching

`select_hard_search_teacher_roots.py` compares the current evaluator's best
move with a completed search-teacher root, ranks roots by evaluator regret, and
selects a deterministic phase-balanced subset for deeper relabeling. It joins
back to the normalized root by record id, requires normalized game-group and
source-dataset identity, and preserves the teacher split. Before writing, it
rejects board or game-group cross-split collisions after that split rewrite and
records both audit counts alongside checksums and regret statistics.

`overlay_search_move_teacher.py` then replaces those complete shallow roots
with their deeper versions. It never appends two labels for the same root. If a
retained shallow root shares a child whose deeper label differs, the complete
shallow root is excluded; the deeper child row wins. The output report records
both teacher provenances, excluded conflicts, split counts, and all input and
output checksums. When the search configurations differ, pass the retained-base
output and the overlay move-teacher as separate trainer sidecars; each TSV
keeps one provenance. Inputs, outputs, and reports are local-only.

## Contract

Cache entries are split-independent solve facts for one root board and its legal
after-move children. They are keyed by root board identity and include root
`board_id`, root board contents, `empty_count`, requested `max_empty`, legal
moves or forced-pass state, child board ids and contents, exact child final
disc-diff labels, root move scores, best-move rank data, teacher node counts,
cache schema version, generator semantic version, solver semantic version,
label kind, label unit, and label perspective.

Materialization is split-dependent. `materialize_move_teacher_from_cache.py`
reads the current normalized TSV for record ids, split names, phases, group ids,
and selected root order. Selection uses the normalized schema v2 root contract:
exact v2 header, required identity fields, split validation, board/count
validation, phase mapping, duplicate `board_id` handling, FNV sampling, and
post-sampling `board_id` order. Identical duplicate `board_id` + board contents
rows are accepted and counted; conflicting duplicate board contents fail before
cache lookup or materialization. On cache hit it validates `board_id` and board
contents, cache schema, cache semantic version, generator semantic version,
solver semantic version, label metadata, `max_empty`, non-terminal status, row
presence, and legal-move/row consistency. It does not recompute child boards,
exact labels, rank fields, teacher-node metadata, or forced-pass status.

When writing a cache key that already exists, the tool compares the complete
new entry with the existing entry. Any conflict fails instead of overwriting
solved labels.

## Hit And Miss Behavior

`--reuse-move-teacher-cache` asks the campaign runner to materialize exact
move-teacher outputs from cache instead of solving every selected root.

Materialization requires every selected root to be present and valid. Any
missing or invalid selected root is an error. Use `--probe-only` to inspect hits
and misses without writing complete materialized outputs.

`--allow-cache-miss-solve` permits a partial miss and requires both
`--reuse-move-teacher-cache` and `--write-move-teacher-cache`. The campaign
probes the full selection, solves only missing roots, writes those entries to
the local cache, then re-probes and requires a full hit before final
materialization. A partial-miss solve that still has misses after merge fails.

Without cache reuse, the campaign computes the requested exact move-teacher
labels directly.

## Minimal Reproduction

Materialize current outputs from a complete cache hit:

```sh
python3 tools/pattern/labels/materialize_move_teacher_from_cache.py \
  --normalized-tsv <selected.tsv> \
  --cache-dir <cache-dir> \
  --move-teacher-out <move-teacher.tsv> \
  --child-normalized-out <child-normalized.tsv> \
  --report-out <report.json> \
  --max-empty 12
```

## Reading Path

Use this file for label/cache operations involving
`run_move_teacher_decision_campaign.py`, `run_move_teacher_campaign_matrix.py`,
and `run_pattern_growth_cycle.py`. The checked smoke coverage is
`check_move_teacher_cache_smoke.py`; label data policy remains in
`../../../data/labels/README.md`.

The decision campaign and matrix can also select `pattern-rank-v0e`. It trains
the existing child-value model from a move-teacher TSV sidecar, ranks root moves
as negative child values, and keeps generated datasets, weights, reports, and
artifacts local-only. Its pair temperature, calibration weight, deterministic
per-root pair cap, tie margin, gradient clip, and early-stop patience are
explicit campaign options. Compare its final artifact with v0c/v0d using the
same move-teacher ranking evaluator; do not treat that local comparison as an
artifact promotion or strength claim.

The ranking evaluator loads each manifest through the same phase-aware runtime
path as CLI, WASM, and full-game arena consumers. Reported move scores therefore
respect `trained_phases`, fallback routing, fixed-point `score_scale`, and any
`fallback_additive_through_phase` residual policy; they are not raw pattern-table
scores detached from artifact policy.

The v0e trainer accepts `--residual-baseline-through-phase`; export must use the
same inclusive `fallback_additive_through_phase`. For affected child phases,
trainer predictions are `static baseline + learned pattern residual`;
gradients update only the learned term.
