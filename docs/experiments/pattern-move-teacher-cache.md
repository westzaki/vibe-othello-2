# Pattern Move-Teacher Cache

## Scope

This note records the local-only exact move-teacher cache added for
`pattern-v2-endgame-lite` decision-leverage experiments.

The cache separates exact solving from split/training materialization. It is a
local infrastructure optimization only. It is not Elo, not self-play, not a
production strength claim, not publication readiness, and not a reason to
commit generated cache files, TSVs, datasets, weights, artifacts, raw reports,
logs, or corpora.

## What Is Cached

A cache entry is split-independent and keyed by one root board identity:

* `board_id`
* `board_a1_to_h8`
* `empty_count`
* `max_empty`
* legal move list / forced-pass status
* child board ids and child boards
* exact child final disc-diff labels
* root move scores, best-move rank data, and teacher node counts
* cache schema and semantic versions
* exact solver semantic version
* label kind/unit/perspective

Materialization is split-dependent. It uses the current normalized TSV for:

* `root_record_id`
* `root_split`
* `root_phase`
* `game_group_id` in child-normalized rows
* current selected root order

That is what allows a connected-board-game split remap to reuse exact
move-teacher labels when `board_id` and board contents match.

## CLI

Populate a local cache from an existing solved move-teacher TSV:

```sh
python3 tools/pattern/labels/materialize_move_teacher_from_cache.py \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-source-v1/selected-low-empty-normalized.tsv" \
  --cache-dir "$VIBE_OTHELLO_MOVE_TEACHER_CACHE" \
  --report-out "$VIBE_OTHELLO_MEASUREMENTS/<run>/move-teacher-cache-populate-report.json" \
  --max-empty 12 \
  --max-roots 50000 \
  --seed 0 \
  --source-move-teacher-tsv "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-v1/decision-leverage-matrix/roots-50000-seed-0/move-teacher.tsv" \
  --populate-only
```

Materialize from cache:

```sh
python3 tools/pattern/labels/materialize_move_teacher_from_cache.py \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-connected-source-v1/selected-low-empty-normalized.tsv" \
  --cache-dir "$VIBE_OTHELLO_MOVE_TEACHER_CACHE" \
  --move-teacher-out "$VIBE_OTHELLO_MEASUREMENTS/<run>/move-teacher.tsv" \
  --child-normalized-out "$VIBE_OTHELLO_MEASUREMENTS/<run>/child-normalized.tsv" \
  --report-out "$VIBE_OTHELLO_MEASUREMENTS/<run>/move-teacher-report.json" \
  --max-empty 12 \
  --max-roots 50000 \
  --seed 0 \
  --require-full-hit
```

Use the cache through a campaign:

```sh
python3 tools/pattern/labels/run_move_teacher_decision_campaign.py \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-source-v1/selected-low-empty-normalized.tsv" \
  --output-dir "$VIBE_OTHELLO_MEASUREMENTS/<run>/cache-check" \
  --max-empty 12 \
  --max-roots 50000 \
  --seed 1 \
  --pattern-set pattern-v2-endgame-lite \
  --move-teacher-cache-dir "$VIBE_OTHELLO_MOVE_TEACHER_CACHE" \
  --reuse-move-teacher-cache \
  --resume
```

To populate cache during a solved campaign, add:

```sh
--move-teacher-cache-dir "$VIBE_OTHELLO_MOVE_TEACHER_CACHE" \
--write-move-teacher-cache
```

To solve only missing roots on a partial hit, also add:

```sh
--allow-cache-miss-solve
```

This mode requires both `--reuse-move-teacher-cache` and
`--write-move-teacher-cache`. The campaign probes the full selected root set,
writes a local missing-root normalized TSV, runs the exact move-teacher
generator only for those missing roots, merges the solved missing roots into
cache, re-probes to require a full hit, and then materializes the final
complete outputs from cache using the original full normalized TSV.

Exact root teacher labels can be derived from a complete move-teacher TSV:

```sh
python3 tools/pattern/labels/derive_exact_root_labels_from_move_teacher.py \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/<source>/selected-low-empty-normalized.tsv" \
  --move-teacher-tsv "$VIBE_OTHELLO_MEASUREMENTS/<run>/move-teacher.tsv" \
  --teacher-labels-out "$VIBE_OTHELLO_MEASUREMENTS/<source>/exact-root-derived-teacher-labels.tsv" \
  --report-out "$VIBE_OTHELLO_MEASUREMENTS/<source>/exact-root-derived-report.json" \
  --missing-policy fail
```

The derived root score is `max(root_move_score_side_to_move)`. The output TSV
uses `teacher_source = exact-move-teacher-derived-root-v1` and is compatible
with `apply_teacher_labels.py`.

`run_move_teacher_campaign_matrix.py` and `run_pattern_growth_cycle.py` pass the
same cache flags through to the campaign runner.

## Safety Rules

Cache reuse fails loudly when:

* a requested root is missing under full-hit mode
* the same `board_id` has different board contents
* `empty_count` or `max_empty` differs
* cache schema, generator semantic version, solver semantic version, label
  kind, label unit, or label perspective differs
* cached legal moves do not match cached move rows
* an existing cache entry differs from a newly supplied source move-teacher row
* a partial-miss solve does not become a full hit after merge
* a derived exact-root label input has missing roots, duplicate root/move rows,
  stale root record/split/phase/empty metadata, or a normalized `board_id` that
  does not match `board_a1_to_h8`

New cache entries are written through a temporary file in the target root-entry
directory and then renamed into place. Existing entries are compared before
reuse, so conflicts fail instead of overwriting solved labels.

## Local Validation

Real local validation reused the same-source 50k seed 0 output from the growth
cycle and wrote a scratch cache outside the repository.

| Stage | Roots | Rows | Cache hit ratio | Exact nodes newly solved | Exact nodes saved/cached | Wall time |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| populate from same-source 50k seed 0 | 50,000 | 194,835 | n/a | 0 | 1,591,232,496 cached | 8.842 sec |
| same-source 50k seed 1 materialize | 50,000 | 194,835 | 1.000 | 0 | 1,591,232,496 saved | 22.348 sec |
| connected 50k seed 0 materialize | 50,000 | 194,835 | 1.000 | 0 | 1,591,232,496 saved | 22.296 sec |
| connected 100k probe-only | 100,000 | n/a | 0.500 | 0 | 1,591,232,496 reusable from 50k cache | 11.657 sec |
| connected 40-root capped partial solve | 40 | 152 | 0.500 initial / 1.000 final | 467,555 | 377,529 saved | 1.068 sec missing solve; 0.013 sec final materialize |
| derive exact-root labels from capped complete move-teacher | 40 | n/a | n/a | 0 | 845,084 represented in derived labels | 0.002 sec |

Previous exact generation wall times for the same class of work were about
3,695 to 4,636 seconds. Cache-only materialization reduced those stages to
about 22 seconds in this local check.

A 100-root campaign was also run with `--reuse-move-teacher-cache` and an
intentionally missing generator path. It completed materialization, dataset
building, one-epoch training, export, and ranking with:

* root hits: 100/100
* materialized rows: 346
* exact nodes newly solved: 0
* exact nodes saved estimate: 1,929,568
* arena skipped because no baseline was supplied

The capped real partial-miss validation used a 40-root subset from the connected
100k source and a scratch cache containing only the 20 overlapping solved
roots. The campaign solved exactly the 20 missing roots, wrote 20 new cache
entries, re-probed to 40/40 hits, and materialized complete final outputs:

* initial root hits/misses: 20/20
* final root hits/misses: 40/0
* roots newly solved: 20
* move-teacher rows materialized: 152
* child-normalized rows materialized: 152
* exact nodes reused/saved: 377,529
* exact nodes newly solved: 467,555
* exact nodes materialized from cache after merge: 845,084
* final move-teacher checksum:
  `sha256:5ae20d2a89cc9d40b909bb255daa350a82a4a70c613d7e60059c616dbd41694c`
* final child-normalized checksum:
  `sha256:b78fe74b8c6e3a646bab3616197691076f66e46eee5ed4efb6636d3a5200bc43`
* derived exact-root labels: 40 roots,
  `sha256:c82ff61c18abdc2d9b006044c9b0eea743edd7545cccbb71db157ca49794957d`

The full connected 100k partial-miss solve was not run in this PR. The full
100k probe showed 50,000 hits and 50,000 misses against the PR #168 scratch
cache, so the full run would solve 50k missing roots rather than recomputing
the already solved 50k subset.

## Next Action

Run the full connected 100k partial-miss solve with the existing 50k cache,
derive exact-root teacher labels from the complete 100k move-teacher TSV, build
the fair 100k exact-root v2 baseline, and only then run the 100k seed 0 growth
cycle. Do not compare a 100k move-teacher run to a mismatched 50k exact-root
baseline.
