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
  opt-in canonical ternary index output for smoke comparison
* CTest-backed pattern feature extraction smoke over accepted tiny synthetic TSV
  records using runtime tiny pattern geometry and ternary encoding; raw ternary
  indices remain the default, with opt-in canonical ternary index output
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
  sign accuracy
* `tools/pattern/train/train_v0a.py --mode pattern-sgd-v0b` adds the first
  example-level pattern weight learning path on top of the same grouped
  examples: it initializes fixed per-phase train label means, learns
  `phase + pattern_id + ternary_index` weights with deterministic train-only
  SGD, writes local intermediate JSON weights, and reports baseline vs final
  split, phase, and epoch metrics with deterministic checksums
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
  JSON weights, validates schema/trainer versions, 13 phase biases, tiny fixture
  pattern ids, phase ids, ternary index bounds, and numeric weights, then writes
  a runtime-loader-compatible local smoke artifact
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
* local-only Egaroucid board-score corpus manifest for
  `Egaroucid_Train_Data.zip`, plus a streaming importer smoke that accepts raw
  zip files, extracted text files, and extracted directories without committing
  the payload; train/validation/test split ids are derived from
  `dataset_id + board`, while `record_id` is distinct from `position_id` so
  multiple labels for the same position stay in the same split
* CTest-backed Egaroucid importer -> pattern dataset builder smoke that feeds
  the normalized TSV into `tools/pattern/dataset`, validates board/count/label
  columns, preserves importer-provided `position_id` and `split`, keeps same
  position rows in one split, emits `ply` as `occupied_count - 4`, and retains
  exact duplicate records in input order

The Egaroucid normalized dataset report currently records:

* `schema_version`
* `source_dataset_ids`
* `input_rows`, `accepted_rows`, and `rejected_rows`
* `counts_by_split`, `counts_by_phase`, and `counts_by_label_kind`
* `label_min`, `label_max`, and `label_mean`
* `repeated_position_count` and `exact_duplicate_record_count`
* `checksum`
* `split_policy: position-sha256`
* `duplicate_policy: keep_all_input_order`

The dataset builder treats Egaroucid importer splits as authoritative. It does
not recompute split from `record_id`; it validates that every repeated
`position_id` stays in one importer-provided split.

These pieces can later support import validation, teacher labels, fixed-position
evaluation checks, and strength comparisons.

## Current Gaps

The current implementation does not yet have:

* local-only corpus download scripts
* production trainer with pattern weights
* calibration tool
* production artifact exporter
* production pattern-set symmetry enablement with a new pattern set id and any
  required artifact version changes
* publication gate for license and provenance status

No raw external corpora, derived datasets, or learned weights are currently
tracked in the repository. The checked-in TSV records are repository-local
synthetic smoke fixtures only. Publication of weights derived from Egaroucid
data remains unknown and gated by provenance review. Trainer v0a is an
example-level phase-bias baseline only. Trainer v0b is the first
example-level pattern weight learning smoke trainer, but it is not a production
trainer. The v0b local intermediate weights JSON can now be exported into a
runtime-loader-compatible local smoke artifact and loaded by `PatternEvaluator`
in CTest, but production artifact publication, full Egaroucid training,
self-play, ridge regression, search bench validation, match bench validation,
and publishable learned artifacts remain for later PRs. Publication of
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
| Add pattern weight learning | done | First smoke-only example-level trainer v0b learns deterministic train-only `phase + pattern_id + ternary_index` weights from grouped examples; no production trainer, ridge regression, full Egaroucid training, self-play, search bench validation, match bench validation, or learned artifact publication yet |
| Add calibration tool | not started | Optional score-to-probability mapping |
| Add tiny artifact exporter smoke | done | Minimal `tools/pattern/export` smoke writes a runtime-compatible binary payload plus manifest from the deterministic phase-bias trainer summary |
| Add runtime loader compatibility test | done | Exporter CTest round-trips dataset builder -> trainer -> exporter -> runtime loader -> `PatternEvaluator` with a fixed representative score and checksum; the tiny Egaroucid v0b path also round-trips importer -> dataset -> trainer v0b -> exporter -> runtime loader -> `PatternEvaluator` and verifies a score difference from the v0a phase-bias smoke artifact |
| Add production artifact exporter | not started | Production publication flow, provenance gates, and non-smoke training reports are still missing |
| Add Egaroucid board-score local importer | done | Streaming `tools/data-import/import_egaroucid_train_data.py` accepts raw zip or extracted `.txt` input, validates rows, emits `engine_disc_estimate` rows with occupied count and 13-phase ids, uses `dataset_id + board` position hashes for train/validation/test splits, separates `record_id` from `position_id`, keeps exact duplicate board+score rows in deterministic input order with an occurrence suffix, validates manifest JSON `dataset_id`, and keeps raw payloads under ignored `data/corpora/local/**` |
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

Next implementation steps are evaluation bench coverage and a fixed-position
search bench for exported local smoke artifacts before any production artifact
or publication work.

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
