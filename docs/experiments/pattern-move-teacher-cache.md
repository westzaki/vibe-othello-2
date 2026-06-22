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

Partial-hit solving is intentionally not implemented in this PR. If a 100k
source partially overlaps a cached 50k set, the current next step is to either
materialize only full-hit subsets for validation or add a follow-up that solves
only missing roots and then merges them into the cache.

## Local Validation

Real local validation reused the same-source 50k seed 0 output from the growth
cycle and wrote a scratch cache outside the repository.

| Stage | Roots | Rows | Cache hit ratio | Exact nodes newly solved | Exact nodes saved/cached | Wall time |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| populate from same-source 50k seed 0 | 50,000 | 194,835 | n/a | 0 | 1,591,232,496 cached | 8.842 sec |
| same-source 50k seed 1 materialize | 50,000 | 194,835 | 1.000 | 0 | 1,591,232,496 saved | 22.348 sec |
| connected 50k seed 0 materialize | 50,000 | 194,835 | 1.000 | 0 | 1,591,232,496 saved | 22.296 sec |

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

## Next Action

Use this cache for the next fair 100k exact move-teacher validation. The cache
should first reuse the solved 50k subset, then a follow-up can add explicit
partial-miss solving for only the missing 100k roots. Do not compare a 100k
move-teacher run to a mismatched 50k exact-root baseline.
