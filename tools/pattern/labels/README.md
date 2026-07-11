# Pattern Label and Root Selection Operations

## Scope

This is the current contract for exact move-teacher cache reuse and
materialization. Cache payloads, materialized TSVs, and reports are local-only;
keep them outside the repository and outside disposable worktrees.

## Phase-Stratified Root Selection

`select_phase_stratified_roots.py` selects a uniform local root quota from
each normalized schema v2 phase `0..12`. It validates schema-v2 rows, dedupes
by `board_id`, rejects board/game-group cross-split leakage, preserves the
selected source row and split, and emits a local JSON coverage report. It does
not resplit data, generate teacher labels, or train artifacts.

```sh
python3 tools/pattern/labels/select_phase_stratified_roots.py \
  --normalized-tsv <normalized-v2.tsv> \
  --output-tsv <selected.tsv> \
  --report-out <report.json> \
  --roots-per-phase <count> \
  --seed 0 \
  --require-all-phases
```

Without `--require-all-phases`, a shortage is a successful partial diagnostic
selection and is explicit in the report. `--max-roots-per-game-group` is an
optional global cap for reducing over-representation of one semantic game.
The report includes aggregate selected split counts and a selected phase × split
cross-tab. Selected TSVs and reports are local-only and must not be committed.

## Search Move-Teacher Generation

`vibe-othello-generate-search-move-teacher-dataset` generates every legal
root move for selected phases (default `0..9`) by searching each after-move
child from the child side-to-move perspective. The stored root score is the
negative child score. It writes explicit `move-teacher-tsv-v2` rows and
normalized-v2 child rows with `label_kind = teacher_search_final_disc_diff`.

Each `child_board_id` is the importer-compatible `board-v1` SHA-256 identity
of its normalized child board, not a root/move-derived name. Move-teacher rows
retain every root move, while child-normalized rows dedupe equal canonical child
boards within a split. A canonical child board appearing across splits rejects
the transaction. Search scores outside the normalized disc-difference range
`[-64, 64]` also reject the transaction rather than being clamped.

The teacher manifest and weights are required explicitly. The tool rejects an
artifact without complete declared phase coverage `0..12`, so the runtime
phase-aware fallback cannot silently become a teacher. Depth, node, time,
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
