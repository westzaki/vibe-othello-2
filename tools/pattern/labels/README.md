# Pattern Label Cache Operations

## Scope

This is the current contract for exact move-teacher cache reuse and
materialization. Cache payloads, materialized TSVs, and reports are local-only;
keep them outside the repository and outside disposable worktrees.

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
and selected root order. On cache hit it validates `board_id` and board
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

`--require-full-hit` means every selected root must be present and valid; any
missing root is an error.

`--allow-cache-miss-solve` permits a partial miss and requires both
`--reuse-move-teacher-cache` and `--write-move-teacher-cache`. The campaign
probes the full selection, solves only missing roots, writes those entries to
the local cache, then re-probes and requires a full hit before final
materialization. A partial-miss solve that still has misses after merge fails.

Without cache reuse, the campaign computes the requested exact move-teacher
labels directly.

## Minimal Reproduction

Require a complete cache hit and materialize current outputs:

```sh
python3 tools/pattern/labels/materialize_move_teacher_from_cache.py \
  --normalized-tsv <selected.tsv> \
  --cache-dir <cache-dir> \
  --move-teacher-out <move-teacher.tsv> \
  --child-normalized-out <child-normalized.tsv> \
  --report-out <report.json> \
  --max-empty 12 \
  --require-full-hit
```

## Reading Path

Use this file for label/cache operations involving
`run_move_teacher_decision_campaign.py`, `run_move_teacher_campaign_matrix.py`,
and `run_pattern_growth_cycle.py`. The checked smoke coverage is
`check_move_teacher_cache_smoke.py`; label data policy remains in
`../../../data/labels/README.md`.
