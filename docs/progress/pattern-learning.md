# Pattern Learning Progress

## Purpose

This document tracks current implementation status for pattern learning.

The intended design lives in `docs/architecture/pattern-learning.md`.

Runtime evaluation implementation status lives in
`docs/progress/evaluation.md`.

This file may change frequently as implementation progresses.

## Design Sources

Relevant design documents:

* `docs/architecture/pattern-learning.md`
* `docs/architecture/evaluation.md`
* `docs/architecture/search.md`
* `docs/architecture/board-core.md`

## Current Foundation

The current repository has stable board-core semantics and search/endgame
surfaces that future pattern-learning tools can build on.

Existing foundations include:

* board-core position, move, serialization, and hashing APIs
* search-facing `Evaluator` interface
* exact endgame search for small positions
* deterministic search and endgame golden-check tooling
* benchmark infrastructure for board core, search, and endgame
* runtime-owned fixed pattern schema fixture with validation coverage
* repository data policy READMEs for corpus manifests and evaluation artifacts
* dataset manifest JSON schema with a tiny synthetic sample
* CTest-backed dataset manifest smoke validation
* CTest-backed board-core replay smoke validation for tiny synthetic TSV records
* CTest-backed pattern dataset builder smoke over accepted tiny synthetic TSV
  records using deterministic split ids, runtime pattern feature indices, and
  final-disc-difference labels; raw ternary indices remain the default, with
  opt-in canonical ternary index output for smoke comparison; expanded
  one-feature-row-per-occurrence TSV remains the default, and explicit
  `--output-format compact-tsv` emits one row per normalized example with
  deterministic `pattern_id:instance:ternary_index` feature lists
* CTest-backed pattern feature extraction smoke over accepted tiny synthetic TSV
  records using runtime tiny pattern geometry and ternary encoding; raw ternary
  indices remain the default, with opt-in canonical ternary index output
* runtime-owned `pattern-v1-buro-lite` production-ish pattern schema and
  feature geometry with raw `edge-8`, `near-edge-8`, `diagonal-8`,
  `diagonal-7`, `corner-2x5`, and `corner-3x3` tables, 26 total feature
  instances, and no learned weights committed
* `tools/pattern/common` now keeps production-safe helpers separate from
  smoke-only fixture helpers: dataset and feature smoke tools share the same
  raw/canonical index policy and feature-set validation through production-safe
  targets, while tiny phase mapping and fixture pattern-set selection live in a
  smoke-only helper target
* runtime-owned opt-in pattern symmetry canonicalization primitives that future
  feature extraction, training, and export steps can share
* a symmetry-aware tiny pattern-set fixture used only by canonical smoke tooling
  today (`edge-8` uses `reverse`, `corner-3x3` uses `square_d4`)
* CTest-backed tiny deterministic pattern trainer smoke that consumes the
  pattern dataset TSV, fits a train-split-only phase-bias baseline, and fixes
  summary counts, a representative learned value, and checksum
* `tools/pattern/train/train_v0a.py` consumes pattern rows TSV from the dataset
  builder, groups emitted feature rows into `record_id` examples, rejects
  examples with inconsistent `ply`, split, label, or phase metadata, learns only
  13-phase train-split example label means, writes `phase,bias` weights TSV, and
  reports example-level train/validation/test plus phase-level MAE, RMSE, and
  sign accuracy; it detects compact example-row TSV by header and preserves the
  same feature occurrence semantics as expanded input
* `tools/pattern/train/train_v0a.py --mode pattern-sgd-v0b` adds the first
  example-level pattern weight learning path on top of the same grouped
  examples: it initializes fixed per-phase train label means, learns
  `phase + pattern_id + ternary_index` weights with deterministic train-only
  SGD, writes local intermediate JSON weights, and reports baseline vs final
  split, phase, and epoch metrics with deterministic checksums; `instance` is
  validated and duplicate occurrences are reported, while learned weight keys
  continue to ignore `instance`
* `tools/pattern/train/train_v0a.py --mode pattern-sgd-v0c` adds an explicitly
  versioned local research trainer mode that preserves the same runtime score
  shape and v0b-compatible intermediate weights while improving diagnostics and
  optimizer controls. v0c learns phase bias from train examples only, keeps
  phase bias fixed during pattern SGD, learns residual pattern weights with
  deterministic feature-occurrence updates, supports constant or inverse-sqrt
  learning-rate schedules, optional per-feature gradient clipping, pattern-only
  weight decay, and validation-MAE early stopping, and reports split,
  split-by-phase, residual, epoch, and weight diagnostics. The report is a
  fitting diagnostic only, not a production trainer, match bench, Elo,
  self-play result, or strength claim.
* CTest-backed Egaroucid importer -> dataset builder -> trainer v0a smoke checks
  deterministic report/weights output, train-only bias fitting, held-out
  validation/test metrics, invalid-row rejection counts, malformed example
  rejection, and duplicate feature row reporting
* CTest-backed Egaroucid importer -> dataset builder -> trainer v0b smoke checks
  deterministic report/weights output, train-only pattern fitting, held-out
  validation/test metrics, invalid-row rejection counts, malformed example
  rejection, duplicate feature row reporting, and no train-metric regression
  versus the v0a phase-bias baseline on the tiny fixture
* `tools/pattern/export/export_v0b.py` accepts trainer v0b local intermediate
  JSON weights, validates schema/trainer versions, 13 phase biases, selected
  pattern-set ids, phase ids, ternary index bounds, and numeric weights, then
  writes a runtime-loader-compatible local smoke artifact; the default remains
  the tiny fixture set, while `--pattern-set pattern-v1-buro-lite` writes the
  wider local artifact layout
* CTest-backed tiny artifact exporter smoke that converts the deterministic
  trainer summary into a runtime-loader-compatible artifact with phase bias
  slots and zero-filled tiny fixture pattern tables
* CTest-backed runtime loader compatibility smoke that loads the tiny exported
  artifact, converts it to `PatternWeights`, constructs `PatternEvaluator`, and
  fixes a representative deterministic score
* CTest-backed tiny Egaroucid importer -> dataset builder -> trainer v0b ->
  exporter -> runtime loader -> `PatternEvaluator` smoke that confirms v0b
  learned weights can now be exported and loaded by runtime smoke, keeps all
  generated datasets and artifacts in temporary directories, compares against
  the v0a phase-bias smoke artifact, and checks deterministic exporter/loader
  round-trip checksums
* CTest-backed fixed-position evaluation smoke that generates both the v0a
  phase-bias artifact and the v0b pattern-SGD artifact from the checked-in tiny
  Egaroucid fixture, loads both with the runtime loader, evaluates a
  deterministic fixed position set, emits a checksum-stable JSON report, and
  confirms learned v0b scores can differ from the v0a baseline
* CTest-backed fixed-position search smoke that generates the same temp-only
  v0a/v0b learned artifacts, loads them through `PatternEvaluator`, runs the
  same explicitly configured depth-1 fixed-depth smoke settings for both
  evaluators, and reports deterministic best move, score, and node rows
* local-only Egaroucid board-score corpus manifest for
  `Egaroucid_Train_Data.zip`, plus a streaming importer smoke that accepts raw
  zip files, extracted text files, and extracted directories without committing
  the payload; train/validation/test split ids are derived from
  `dataset_id + board`, while `record_id` is distinct from `position_id` so
  multiple labels for the same position stay in the same split
* local-only Egaroucid v0002 sequence/transcript importer at
  `tools/data-import/import_egaroucid_sequences.py` that accepts local files,
  directories containing `.txt` files and/or `.zip` archives, and zip archives,
  replays Othello transcripts with legal move
  validation plus explicit or implicit pass handling, emits normalized TSV
  schema v2 with explicit `game_group_id`, `board_id`, and
  `source_occurrence_id`, and writes an import report with identity policy,
  game, duplicate occurrence, exact-board leakage, reject, pass, terminal,
  split, phase, label, checksum, streaming-target / bounded-dev sampling
  frames, and provenance
  notes; checked-in coverage uses only a synthetic transcript fixture, not raw
  Egaroucid data. Direct raw zip full-scan import remains the accurate
  full-corpus top-k path, but it can be slow for local iteration because every
  selected transcript must be replayed. The `streaming-target` sampling mode
  fingerprints candidate sources, deterministically walks content-addressed
  source order until the requested retained position target is reached, reports
  progress, keeps traversal independent of local paths and zip member names,
  and bounds replay cost by target size; it is not a full-corpus exact top-k
  sample. The opt-in
  `bounded-dev` sampling mode deterministically bounds files and games before
  replay, reports progress and sampling-frame fields, and is only for
  repeatable local measurement loops, not full-corpus exact sampling or a
  production strength claim.
* CTest-backed Egaroucid importer -> pattern dataset builder smoke that feeds
  the normalized TSV into `tools/pattern/dataset`, validates board/count/label
  columns, preserves importer-provided `position_id` and `split`, keeps same
  position rows in one split, emits `ply` as `occupied_count - 4`, and retains
  exact duplicate records in input order
* local-only Egaroucid subset training runner at
  `tools/pattern/train/run_egaroucid_local_training.py` that runs importer or
  normalized TSV input through deterministic position-id subset sampling,
  dataset building, configurable trainer v0b/v0c, v0b-compatible export,
  optional v0a baseline export,
  fixed-position evaluation smoke, fixed-position search smoke, and a local
  training run report JSON; it also supports sequence/transcript input through
  the local sequence importer, can bound sequence import/conversion itself with
  ply, max-position, file/game cap, sample-rate, hash file ordering, and
  progress controls before the normalized TSV is materialized, can cap
  evaluation/search smoke input rows independently from the training sample
  for large local measurements, and can fail local measurement early with
  `--strict-board-disjoint-splits` when schema v2 exact boards appear across
  train/validation/test splits. The sequence-input path can also use an
  opt-in local-only content-addressed replay cache keyed by canonical source
  content bytes (including zip member text bytes, independent of local paths or
  zip member names), manifest bytes, importer identity, schema/policy versions,
  dataset id, and semantic importer options; cache hits, misses, invalidations,
  source fingerprints, file sizes, and per-stage wall/CPU/RSS telemetry are
  recorded in the local run report.
* CTest-backed local training runner smoke that uses only the checked-in tiny
  Egaroucid fixture, checks deterministic report output, verifies sample split
  and phase counts, confirms trainer/export checksums are present, and keeps
  generated files under the test temporary directory; an additional smoke runs
  the same local runner path with `pattern-v1-buro-lite`
* local training run analyzer at
  `tools/pattern/train/analyze_local_training_runs.py` that compares one or
  more `local-training-run-report.json` files, emits stable JSON and optional
  Markdown summaries, validates the runner report shape, extracts available
  v0b/v0c trainer metrics, surfaces v0c best/final validation MAE, test MAE,
  weight diagnostics, optional sequence cache status, and stage telemetry, and
  records warning-only review flags for small, incomplete,
  mixed cache-hit/cache-miss, mixed trainer-mode, mixed expanded/compact
  dataset, and suspicious split-by-phase local measurements
* local-only pattern measurement suite runner at
  `tools/pattern/train/run_pattern_measurement_suite.py` that orchestrates
  repeatable sequence-derived smoke, 10k, 100k, and 1M preset runs through the
  local sequence cache, compact dataset output, `pattern-v1-buro-lite`, trainer
  v0c diagnostics, scalable real-preset final-epoch diagnostics, trainer
  progress stderr, per-run stdout/stderr logs, resume/skip handling, suite JSON
  and Markdown reports, and analyzer comparison outputs without committing any
  generated corpora, datasets, caches, weights, artifacts, or summaries
* CTest-backed local training analyzer smoke that uses temp-only synthetic
  report fixtures, checks deterministic JSON and Markdown output, fixes the
  expected warning list, and keeps Egaroucid-derived generated reports out of
  the repository

The Egaroucid normalized dataset report currently records:

* `schema_version`
* `normalized_schema_version`
* `output_format`
* `example_rows`, `feature_occurrence_count`,
  `average_features_per_example`, and `max_features_per_example`
* `pattern_set_id` and `index_mode`
* `source_dataset_ids`
* `input_rows`, `accepted_rows`, and `rejected_rows`
* `counts_by_split`, `counts_by_phase`, and `counts_by_label_kind`
* `label_min`, `label_max`, and `label_mean`
* `repeated_position_count` and `exact_duplicate_record_count`
* for sequence schema v2, `game_group_count`, `unique_board_count`,
  `cross_split_board_collision_count`, and collision counts by split pair
* `checksum`
* `split_policy`, which remains `position-sha256` for board-score v1 data and
  records importer-preserved `dataset_id + game_group_id` splitting for
  sequence schema v2
* `duplicate_policy: keep_all_input_order`

The dataset builder treats Egaroucid importer splits as authoritative. It does
not recompute split from `record_id`; it validates that every repeated
`position_id` stays in one importer-provided split.

The local training runner sample report records:

* `schema_version`
* `input_rows` and `sampled_rows`
* `source_dataset_ids`
* `counts_by_split` and `counts_by_phase`
* `label_min`, `label_max`, and `label_mean`
* `checksum`
* `board_leakage_audit` for schema v2 inputs
* `sample_policy` with method, limits, seed, split policy, memory policy, and
  strict exact-board setting

The current sampler uses deterministic `sha256(seed, position_id)` top-k
selection over `position_id` groups. `--max-examples` and `--max-per-phase`
therefore limit selected position groups before duplicate-label expansion; any
duplicate labels for a selected position are preserved so importer-provided
split assignments are not broken.

Local-only measurement directories should normally live outside the git
repository and any disposable worktree:

```sh
export VIBE_OTHELLO_LOCAL="${VIBE_OTHELLO_LOCAL:-$HOME/vibe-othello-local}"
export VIBE_OTHELLO_CORPORA="${VIBE_OTHELLO_CORPORA:-$VIBE_OTHELLO_LOCAL/corpora}"
export VIBE_OTHELLO_SEQUENCE_CACHE="${VIBE_OTHELLO_SEQUENCE_CACHE:-$VIBE_OTHELLO_LOCAL/sequence-cache}"
export VIBE_OTHELLO_MEASUREMENTS="${VIBE_OTHELLO_MEASUREMENTS:-$VIBE_OTHELLO_LOCAL/measurements}"

mkdir -p "$VIBE_OTHELLO_CORPORA"
mkdir -p "$VIBE_OTHELLO_SEQUENCE_CACHE"
mkdir -p "$VIBE_OTHELLO_MEASUREMENTS"
```

`VIBE_OTHELLO_CORPORA` is a local-only input root, but corpus filenames and
manifests are still explicit command inputs. Generated corpora, sequence
caches, measurements, TSVs, weights, artifacts, logs, and reports remain
local-only and must not be committed. Use generic paths in docs and examples;
do not commit personal local paths.

Example local run:

```sh
python3 tools/pattern/train/run_egaroucid_local_training.py \
  --raw-input "$VIBE_OTHELLO_CORPORA/<sequence-input>.zip" \
  --manifest data/corpora/manifests/egaroucid-train-data-board-score-v2025-02-02.manifest.json \
  --max-examples 100000 \
  --max-per-phase 10000 \
  --epochs 8 \
  --learning-rate 0.1 \
  --output-dir "$VIBE_OTHELLO_MEASUREMENTS/local-training-smoke-100k"
```

These pieces can later support import validation, teacher labels, fixed-position
evaluation checks, fixed-position search smoke checks, and later strength
comparisons.

The analyzer is intended for local measurement review of 10k, 100k, 1M, and
similar subset runs. It reports warnings for suspicious inputs, missing smoke
summaries, missing artifact checksums, unknown source kinds, and zero v0a/v0b
score-difference summaries, plus mixed expanded/compact pattern dataset
comparisons, but these warnings do not fail the analysis.
`egaroucid-local` and `egaroucid-sequence-local` are both recognized local
Egaroucid source kinds. Egaroucid-derived `local-training-run-report.json`
files may contain local artifact names and measurement results derived from
external data, so they must remain under ignored local run directories and must
not be committed.

The board-score importer and sequence importer intentionally use different
identity policies. The board-score importer uses `dataset_id + board` for
`position_id` so repeated boards stay together. The sequence importer derives
`game_group_id` from canonical replayed move/pass content, keeps local path,
archive member, and line number only in `source_occurrence_id`, derives split
from `dataset_id + game_group_id`, and builds `position_id` from
`dataset_id + game_group_id + ply + board_id`. Duplicate source copies of the
same semantic game share `game_group_id`, `position_id`, `board_id`, and split,
while `record_id` remains occurrence-scoped. Cross-game duplicate boards are
reported through `board_id` leakage audit instead of being hidden by
game-scoped position ids.

Sequence-derived labels use `observed_final_disc_diff`: final-disc-difference
labels computed from the transcript final board and converted to side-to-move
perspective at each emitted ply. They are not teacher-search scores. A local
10k / 100k / 1M review
using Egaroucid v0002 sequence-derived normalized TSV showed trainer v0b
held-out MAE improving as data increased (10k test MAE 11.616, 100k test MAE
9.256, 1M partial test MAE 7.486) while v0a phase-bias baseline validation/test
MAE stayed around 14. This is evidence that the tiny pattern set plus v0b
trainer reacts to more data, but it is not a match bench, Elo result,
self-play result, production artifact, or strength claim. The 1M local run was
interrupted during uncapped search smoke, so large local runs should use the
runner's sequence import cap plus search smoke cap, for example importing a
bounded 1M sequence-derived position pool and using 10k search smoke positions,
while keeping the training sample uncapped by smoke settings.

For repeatable 10k / 100k / 1M raw sequence local runs, prefer either a
prebuilt normalized local cache, the runner's `--sequence-cache-dir`, or the
runner's scalable sequence sampling options, for example
`--sequence-sampling-mode streaming-target`, `--sequence-file-order hash`,
`--sequence-max-positions`, `--sequence-progress-every-games`, and
`--sequence-progress-every-files`. Use `--sequence-sampling-mode
full-scan-topk` only when an exact full-corpus top-k import is required and the
full replay cost is acceptable. Generated normalized caches, local run reports,
weights, artifacts, and raw Egaroucid payloads remain ignored local-only files.
Metrics from `streaming-target` and `bounded-dev` runs are useful measurement
signals for iteration, but they are not full-corpus exact measurements, match
bench results, Elo results, self-play results, production artifacts, or
strength claims.

The suite runner wraps the same local runner for comparable preset runs:

```sh
python3 tools/pattern/train/run_pattern_measurement_suite.py \
  --sequence-input "$VIBE_OTHELLO_CORPORA/<sequence-input>.zip" \
  --sequence-manifest "$VIBE_OTHELLO_CORPORA/<sequence-manifest>.json" \
  --sequence-cache-dir "$VIBE_OTHELLO_SEQUENCE_CACHE" \
  --suite-output-dir "$VIBE_OTHELLO_MEASUREMENTS/<suite-name>" \
  --preset all \
  --resume
```

When `VIBE_OTHELLO_MEASUREMENTS` or `VIBE_OTHELLO_LOCAL` is set, the suite
runner can derive a safe output root; when `VIBE_OTHELLO_SEQUENCE_CACHE` or
`VIBE_OTHELLO_LOCAL` is set, it can derive the shared sequence cache:

```sh
python3 tools/pattern/train/run_pattern_measurement_suite.py \
  --sequence-input "$VIBE_OTHELLO_CORPORA/<sequence-input>.zip" \
  --sequence-manifest "$VIBE_OTHELLO_CORPORA/<sequence-manifest>.json" \
  --preset smoke
```

Use `--preset smoke --dry-run` to inspect planned commands without executing a
training run. `--preset all` expands to the stable 10k, 100k, and 1M local
measurement presets. Those presets use `streaming-target`, hash file ordering,
importer progress logging, trainer progress logging, and final-epoch-only v0c
diagnostics by default so replay stops at the target after canonical source
fingerprinting, and repeated diagnostic passes do not dominate 1M training;
pass `--sequence-sampling-mode full-scan-topk` to intentionally run the exact
full replay path, or `--trainer-eval-every-epoch` to restore per-epoch v0c
metrics.
Smoke remains an explicit CTest/local iteration preset. Suite reports, per-run
stdout/stderr logs, and analyzer summaries are local-only measurement
diagnostics, not strength, Elo, match bench, self-play, production artifact, or
publication claims.

## Current Gaps

The current implementation does not yet have:

* local-only corpus download scripts
* production trainer with pattern weights
* calibration tool
* production artifact exporter
* production pattern-set symmetry enablement with a new pattern set id and any
  required artifact version changes
* match bench or Elo/strength measurement
* publication gate for license and provenance status

No raw external corpora, derived datasets, or learned weights are currently
tracked in the repository. The checked-in TSV records are repository-local
synthetic smoke fixtures only. Publication of weights derived from Egaroucid
data remains unknown and gated by provenance review. Trainer v0a is an
example-level phase-bias baseline only. Trainer v0b is the first
example-level pattern weight learning smoke trainer, but it is not a production
trainer. The v0b local intermediate weights JSON can now be exported into a
runtime-loader-compatible local smoke artifact, loaded by `PatternEvaluator`,
measured by a fixed-position evaluation smoke in CTest, and measured in a
fixed-position search smoke against the v0a phase-bias baseline. Production
artifact publication, full Egaroucid training, committed learned weights,
self-play, ridge regression, match bench validation, production strength
claims, production-ish pattern set implementation, and publishable learned
artifacts remain for later PRs. Local training run reports may contain
Egaroucid-derived metrics and local artifact names, so they should stay under
ignored local run directories and should not be committed. Publication of
Egaroucid-derived learned artifacts remains unknown and gated by provenance
review.

## Implementation Plan

Status values:

* `done` means implemented in the repository
* `not started` means no production implementation exists yet
* `deferred` means intentionally left for a later phase

| Step | Status | Notes |
| --- | --- | --- |
| Add pattern-learning architecture document | done | `docs/architecture/pattern-learning.md` |
| Add pattern-learning progress document | done | this file |
| Add docs index rows | done | `docs/README.md` |
| Add corpus data policy README | done | `data/corpora/README.md` |
| Add evaluation artifact README | done | `data/eval/README.md` |
| Add dataset manifest schema | done | `data/corpora/dataset-manifest.schema.json` plus CTest smoke validation |
| Add tiny synthetic fixture records | done | `data/corpora/samples/tiny-local-synthetic.records.tsv` contains checked-in synthetic good and bad replay smoke records |
| Add importer for one simple text format | done | Minimal `tools/data-import` replay smoke accepts expected-good rows and rejects malformed, illegal, or bad-pass rows through board-core move application |
| Add dataset builder and deterministic splitter | done | Minimal `tools/pattern/dataset` smoke replays expected-good tiny records, emits labeled pattern rows, records `split_policy`, keeps duplicate input rows in deterministic input order, supports opt-in canonical index output for smoke comparison, and depends on smoke fixture helpers only through an explicit smoke-only target |
| Add pattern schema fixtures | done | Runtime evaluation owns fixed `edge-8` and `corner-3x3` fixture schemas |
| Add symmetry canonicalization primitive | done | Evaluation exposes an isolated helper for raw, reverse, and square D4 canonical ternary indices; default tools still emit raw ternary indices, while canonical smoke mode opts in through the shared helper |
| Add feature extractor | done | Minimal `tools/pattern/features` smoke replays accepted tiny synthetic records through board core and emits `edge-8` / `corner-3x3` `record_id`, `ply`, `phase`, `pattern_id`, `instance`, and runtime ternary indices, with opt-in canonical index output for smoke comparison |
| Add tiny deterministic trainer smoke test | done | Minimal `tools/pattern/train` smoke consumes the pattern dataset TSV, trains a phase-bias baseline from train rows only, counts validation/test rows, and fixes the summary checksum |
| Add trainer v0a phase-bias report | done | `tools/pattern/train/train_v0a.py` reads dataset builder pattern rows TSV, groups rows into `record_id` examples, rejects malformed examples, reports duplicate feature rows, learns only train-split example phase means, writes deterministic phase-bias weights TSV and JSON metrics, and is covered by the tiny Egaroucid importer -> dataset -> trainer smoke |
| Add pattern weight learning | done | First smoke-only example-level trainer v0b learns deterministic train-only `phase + pattern_id + ternary_index` weights from grouped examples; no production trainer, ridge regression, full Egaroucid training, self-play, match bench validation, production strength claim, or learned artifact publication yet |
| Add calibration tool | not started | Optional score-to-probability mapping |
| Add tiny artifact exporter smoke | done | Minimal `tools/pattern/export` smoke writes a runtime-compatible binary payload plus manifest from the deterministic phase-bias trainer summary |
| Add runtime loader compatibility test | done | Exporter CTest round-trips dataset builder -> trainer -> exporter -> runtime loader -> `PatternEvaluator` with a fixed representative score and checksum; the tiny Egaroucid v0b path also round-trips importer -> dataset -> trainer v0b -> exporter -> runtime loader -> `PatternEvaluator` and verifies a score difference from the v0a phase-bias smoke artifact |
| Add learned artifact fixed-position evaluation smoke | done | `vibe_othello_pattern_evaluation_bench_smoke` generates local-only v0a/v0b artifacts from the tiny Egaroucid fixture, evaluates fixed positions with runtime `PatternEvaluator`, reports deterministic score rows, and keeps learned Egaroucid-derived artifacts temp-only |
| Add learned artifact fixed-position search smoke | done | `vibe_othello_pattern_search_bench_smoke` generates local-only v0a/v0b artifacts from the tiny Egaroucid fixture, runs explicitly configured deterministic depth-1 search with each artifact-backed `PatternEvaluator`, reports best move, score, nodes, and score deltas, and keeps learned Egaroucid-derived artifacts temp-only |
| Add local Egaroucid subset training runner | done | `tools/pattern/train/run_egaroucid_local_training.py` runs raw, normalized, or sequence local Egaroucid input through deterministic position-id sampling, dataset builder, trainer v0b, export, optional v0a baseline, fixed-position evaluation/search smoke checks, optional local-only sequence replay cache restore/import keyed by canonical source content bytes, and a local run report with cache/source/stage telemetry; generated corpora, caches, datasets, learned weights, artifacts, and Egaroucid-derived reports remain local-only and uncommitted |
| Add local training run analyzer | done | `tools/pattern/train/analyze_local_training_runs.py` compares local run reports, emits deterministic JSON/Markdown review summaries, extracts available trainer metrics, surfaces cache hit/miss and major stage timings, and reports warning-only sanity flags using synthetic temp-only CTest coverage |
| Add compact pattern example dataset format | done | Dataset builder `--output-format compact-tsv` emits one row per normalized example with deterministic feature occurrence lists, trainer v0a/v0b accepts compact input by header detection, local runner can request compact datasets, analyzer surfaces dataset format, and synthetic smoke coverage checks compact/expanded equivalence without changing runtime evaluation, exporter format, pattern definitions, or training semantics |
| Add local pattern measurement suite runner | done | `tools/pattern/train/run_pattern_measurement_suite.py` runs named smoke/10k/100k/1M sequence presets through local cache, scalable `streaming-target` sequence import by default for real presets, compact dataset output, trainer v0c diagnostics with real-preset final-epoch diagnostics and trainer progress, live per-run logs, resume/skip handling, suite reports, and analyzer comparison; synthetic CTest coverage checks dry-run, execute, resume, failure, and all-preset expansion without committing generated measurement outputs |
| Add production-ish pattern set design | done | `pattern-v1-buro-lite` adds raw edge, near-edge, diagonal, and corner table families plus matching runtime feature geometry and local exporter/runner selection; no learned weights or production artifact are committed |
| Add production artifact exporter | not started | Production publication flow, provenance gates, and non-smoke training reports are still missing |
| Add Egaroucid board-score local importer | done | Streaming `tools/data-import/import_egaroucid_train_data.py` accepts raw zip or extracted `.txt` input, validates rows, emits `engine_disc_estimate` rows with occupied count and 13-phase ids, uses `dataset_id + board` position hashes for train/validation/test splits, separates `record_id` from `position_id`, keeps exact duplicate board+score rows in deterministic input order with an occurrence suffix, validates manifest JSON `dataset_id`, and keeps raw payloads under ignored `data/corpora/local/**` |
| Add Egaroucid sequence/transcript local importer | done | `tools/data-import/import_egaroucid_sequences.py` accepts local transcript files, directories containing `.txt` files and/or `.zip` archives, and zip archives, validates legal Othello replay with pass handling, emits normalized TSV with final-disc-difference side-to-move labels, records sequence-specific game-hash split and game/ply-scoped position ids, supports scalable content-addressed streaming-target sampling plus opt-in bounded-dev file/game sampling with progress reporting for local iteration, and is covered by synthetic CTest fixture plus local runner sequence smoke; raw sequence zips and generated TSVs remain ignored local-only inputs |
| Connect Egaroucid importer TSV to dataset builder | done | `tools/pattern/dataset` accepts the importer normalized TSV schema, validates labels and `a1,b1,...,h8` board counts, preserves importer `position_id` / `split`, emits deterministic pattern rows, writes a dataset report JSON, and has a tiny importer -> dataset CTest smoke |
| Add local-only external corpus scripts | deferred | Download automation remains out of scope; the importer expects a locally obtained payload |
| Add match benchmark for artifacts | deferred | Needs at least two comparable artifacts |
| Add publication gate | not started | Policy documented; enforcement beyond manifest smoke validation is still pending |

## Completion Bar

Pattern learning is strong enough to support production evaluation when:

* every training input has a manifest
* raw external corpora are kept out of git by default
* tiny fixtures exercise import, replay, feature extraction, and export
* feature schema is versioned and shared with runtime evaluation
* any production enabled symmetry policy is shared by trainer, feature
  extractor, exporter, and runtime evaluator through the same canonicalization
  helper; current canonical feature/dataset smoke coverage is opt-in and does
  not change trainer, exporter, artifact, or runtime evaluator behavior
* train/validation/test splits are deterministic
* tiny trainer output is reproducible
* exported artifacts load in runtime evaluation
* validation metrics are generated automatically
* strength checks can compare two artifacts
* license and provenance status is visible before publishing weights

Next implementation steps are local 10k / 100k / 1M `pattern-v1-buro-lite`
sequence subset measurements, then trainer improvements or production
pattern-set symmetry enablement before any production artifact publication,
committed learned weights, match bench, or strength-claim work.

## Progress Update Rules

Update this document when:

* an implementation milestone changes status
* a known gap is discovered
* a dataset or artifact policy changes
* a training run format is added or changed
* benchmark or validation metrics are added
* a deferred item is intentionally moved into scope

Update `docs/architecture/pattern-learning.md` only when the intended design,
boundary, semantics, or correctness rules change.
