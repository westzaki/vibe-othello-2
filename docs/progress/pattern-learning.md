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
* runtime-owned opt-in pattern symmetry canonicalization primitives that future
  feature extraction, training, and export steps can share
* a symmetry-aware tiny pattern-set fixture used only by canonical smoke tooling
  today (`edge-8` uses `reverse`, `corner-3x3` uses `square_d4`)
* CTest-backed tiny deterministic pattern trainer smoke that consumes the
  pattern dataset TSV, fits a train-split-only phase-bias baseline, and fixes
  summary counts, a representative learned value, and checksum
* CTest-backed tiny artifact exporter smoke that converts the deterministic
  trainer summary into a runtime-loader-compatible artifact with phase bias
  slots and zero-filled tiny fixture pattern tables
* CTest-backed runtime loader compatibility smoke that loads the tiny exported
  artifact, converts it to `PatternWeights`, constructs `PatternEvaluator`, and
  fixes a representative deterministic score

These pieces can later support import validation, teacher labels, fixed-position
evaluation checks, and strength comparisons.

## Current Gaps

The current implementation does not yet have:

* local-only corpus download scripts
* production trainer
* calibration tool
* production artifact exporter
* production pattern-set symmetry enablement with a new pattern set id and any
  required artifact version changes
* production training reports
* publication gate for license and provenance status

No raw external corpora, derived datasets, or learned weights are currently
tracked in the repository. The checked-in TSV records are repository-local
synthetic smoke fixtures only.

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
| Add dataset builder and deterministic splitter | done | Minimal `tools/pattern/dataset` smoke replays expected-good tiny records, emits labeled pattern rows, records `split_policy`, keeps duplicate input rows in deterministic input order, and supports opt-in canonical index output for smoke comparison |
| Add pattern schema fixtures | done | Runtime evaluation owns fixed `edge-8` and `corner-3x3` fixture schemas |
| Add symmetry canonicalization primitive | done | Evaluation exposes an isolated helper for raw, reverse, and square D4 canonical ternary indices; default tools still emit raw ternary indices, while canonical smoke mode opts in through the shared helper |
| Add feature extractor | done | Minimal `tools/pattern/features` smoke replays accepted tiny synthetic records through board core and emits `edge-8` / `corner-3x3` `record_id`, `ply`, `phase`, `pattern_id`, `instance`, and runtime ternary indices, with opt-in canonical index output for smoke comparison |
| Add tiny deterministic trainer smoke test | done | Minimal `tools/pattern/train` smoke consumes the pattern dataset TSV, trains a phase-bias baseline from train rows only, counts validation/test rows, and fixes the summary checksum |
| Add calibration tool | not started | Optional score-to-probability mapping |
| Add tiny artifact exporter smoke | done | Minimal `tools/pattern/export` smoke writes a runtime-compatible binary payload plus manifest from the deterministic phase-bias trainer summary |
| Add runtime loader compatibility test | done | Exporter CTest round-trips dataset builder -> trainer -> exporter -> runtime loader -> `PatternEvaluator` with a fixed representative score and checksum |
| Add production artifact exporter | not started | Production publication flow, provenance gates, and non-smoke training reports are still missing |
| Add local-only external corpus scripts | deferred | Requires source-specific license review |
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
