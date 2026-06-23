# Pattern Move-Teacher Cache Contract

## Scope

The move-teacher cache separates exact child-label solving from later
split-dependent materialization. It lets local runs reuse exact move-teacher
labels for the same root board while rebuilding current `move-teacher.tsv` and
`child-normalized.tsv` outputs from the active normalized TSV.

The cache is local-only infrastructure. Cache payloads, generated
move-teacher TSVs, child-normalized TSVs, teacher labels, datasets, weights,
reports, logs, corpora, and scratch outputs must stay out of git.

Historical decision-leverage results live in `pattern-learning-history.md`.

## Cache Responsibility

Cache entries are split-independent facts about one root board and its legal
after-move children. Materialization remains split-dependent and reads the
current normalized TSV for record ids, split names, phases, group ids, and
selected root order.

This boundary lets a split remap reuse solved exact labels when the root board
identity and board contents still match, without reusing stale split metadata.

## Key And Validation Contract

A cache entry is keyed by the root board identity. Entries contain:

* `board_id` and `board_a1_to_h8`
* `empty_count` and requested `max_empty`
* legal move list or forced-pass status
* child board ids and child board contents
* exact child final disc-diff labels
* root move scores, best-move rank data, and teacher node counts
* cache schema and generator semantic versions
* exact solver semantic version
* label kind, unit, and perspective

On cache-hit reuse, the materializer currently validates schema and semantic
versions, label metadata, `max_empty`, root identity and contents,
non-terminal status, row presence, and legal-move/row consistency including
duplicates. It does not recompute child boards, exact labels, rank fields,
teacher-node metadata, or forced-pass status from the current solver.

When populating an already-existing key, the complete newly constructed entry
is compared with the existing entry. Conflicts invalidate the operation by
failing loudly rather than overwriting solved labels.

## Hit, Partial Miss, And Miss Solve

`--reuse-move-teacher-cache` asks the campaign to materialize exact
move-teacher outputs from cache instead of solving every requested root.

`--require-full-hit` means every requested root must be present and valid. A
missing root is an error.

`--allow-cache-miss-solve` permits a partial miss. It requires both
`--reuse-move-teacher-cache` and `--write-move-teacher-cache`. The campaign
probes the full selection, solves only the missing roots, writes those solved
entries to the local cache, re-probes to require a full hit, then materializes
complete outputs from cache for the original full selection. A partial-miss
solve that does not become a full hit after merge fails.

Without cache reuse, the campaign behaves as a normal miss solve and computes
the requested exact move-teacher labels directly.

## Local-only Boundary

The cache is not a repository artifact, release artifact, Elo result,
self-play result, production strength claim, or publication artifact. Keep the
cache root outside the repository and outside disposable worktrees. Do not
commit cache entries, materialized TSVs, local reports, or path-specific
metadata.

## Minimal Reproduction Command

Use this shape to require a complete cache hit and materialize current outputs:

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

## Current Tools

`tools/pattern/labels/materialize_move_teacher_from_cache.py` is the direct
cache materializer and population tool.

`tools/pattern/labels/run_move_teacher_decision_campaign.py` owns cache reuse,
write-through, and partial-miss solve behavior for a single campaign.

`tools/pattern/labels/run_move_teacher_campaign_matrix.py` and
`tools/pattern/train/run_pattern_growth_cycle.py` pass the same cache flags
through to the campaign runner.

`tools/pattern/labels/check_move_teacher_cache_smoke.py` covers the cache and
partial-miss solve workflow smoke checks.

`tools/pattern/labels/derive_exact_root_labels_from_move_teacher.py` can derive
exact root labels from a complete move-teacher TSV, using
`teacher_source = exact-move-teacher-derived-root-v1`.
