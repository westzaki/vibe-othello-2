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
* dataset manifest JSON schema with a manifest-only sample
* CTest-backed dataset manifest smoke validation

These pieces can later support import validation, teacher labels, fixed-position
evaluation checks, and strength comparisons.

## Current Gaps

The current implementation does not yet have:

* local-only corpus download scripts
* data importer tools
* pattern dataset builder
* pattern feature extractor
* deterministic train/validation/test splitter
* trainer
* calibration tool
* artifact exporter
* training reports
* publication gate for license and provenance status

No raw external corpora, derived datasets, or learned weights are currently
tracked in the repository.

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
| Add tiny synthetic fixture records | not started | Manifest-only sample exists; no raw records are committed |
| Add importer for one simple text format | not started | Should reject illegal or malformed records |
| Add dataset builder and deterministic splitter | not started | Should record duplicate handling and split ids |
| Add pattern schema fixtures | done | Runtime evaluation owns fixed `edge-8` and `corner-3x3` fixture schemas |
| Add feature extractor | not started | Should call board-core helpers for parsing and replay |
| Add tiny deterministic trainer smoke test | not started | Reproducibility before throughput |
| Add calibration tool | not started | Optional score-to-probability mapping |
| Add artifact exporter | not started | Writes binary weights plus manifest |
| Add runtime loader compatibility test | not started | Bridges learning output to evaluation runtime |
| Add local-only external corpus scripts | deferred | Requires source-specific license review |
| Add match benchmark for artifacts | deferred | Needs at least two comparable artifacts |
| Add publication gate | not started | Policy documented; enforcement beyond manifest smoke validation is still pending |

## Completion Bar

Pattern learning is strong enough to support production evaluation when:

* every training input has a manifest
* raw external corpora are kept out of git by default
* tiny fixtures exercise import, replay, feature extraction, and export
* feature schema is versioned and shared with runtime evaluation
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
