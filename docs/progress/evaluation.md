# Evaluation Progress

## Purpose

This document tracks current implementation status for evaluation.

The intended design lives in `docs/architecture/evaluation.md`.

Pattern-learning implementation status lives in
`docs/progress/pattern-learning.md`.

This file may change frequently as implementation progresses.

## Design Sources

Relevant design documents:

* `docs/architecture/evaluation.md`
* `docs/architecture/pattern-learning.md`
* `docs/architecture/search.md`
* `docs/architecture/board-core.md`

## Current Foundation

The current repository has the search-facing evaluator interface but not a
production evaluation module.

Existing search public types include:

* `Score`
* `kScoreWin`
* `kScoreLoss`
* `Evaluator`

The current search-facing evaluator interface is:

* `engine/include/vibe_othello/search/evaluator.h`

Existing search and benchmark code uses small local evaluator implementations
for tests and measurement, including constant and disc-difference evaluators.

The current repository already documents that:

* search depends on an evaluation interface for heuristic leaf scores
* search does not own evaluation feature definitions
* endgame exact search must not call heuristic evaluation
* board core is the source of truth for positions and moves

## Current Gaps

The current implementation does not yet have:

* production baseline evaluator
* pattern evaluator
* pattern index encoder
* evaluation explanation API
* calibration API
* incremental evaluator state
* evaluation benchmarks
* evaluation data or artifact README

The existing `search::Evaluator` interface is the only production boundary for
heuristic evaluation today.

## Implementation Plan

Status values:

* `done` means implemented in the repository
* `not started` means no production implementation exists yet
* `deferred` means intentionally left for a later phase

| Step | Status | Notes |
| --- | --- | --- |
| Add evaluation architecture document | done | `docs/architecture/evaluation.md` |
| Add evaluation progress document | done | this file |
| Add docs index rows | done | `docs/README.md` |
| Keep search-facing evaluator interface stable | done | `engine/include/vibe_othello/search/evaluator.h` |
| Add evaluation namespace and public runtime headers | done | First runtime header is `engine/include/vibe_othello/evaluation/pattern_weights.h` |
| Add simple baseline evaluator | not started | Useful before learned artifacts are available |
| Add pattern schema types | not started | Only minimal loader manifest and pattern definition structs exist |
| Add pattern index encoder | not started | Include symmetry and phase boundary tests |
| Add artifact manifest and binary loader | done | First binary loader validates version, bit order, score unit, phase count, pattern set id, pattern shape, weight count, and checksum |
| Add tiny hand-authored artifact fixture | done | Synthetic in-test fixture covers deterministic loader success and rejection paths |
| Add production `PatternEvaluator` | not started | Should implement `search::Evaluator` |
| Add evaluation explanation API | not started | Non-recursive adapter for tools and UI |
| Add calibration API | not started | Must not alter search scores |
| Add incremental evaluator path | deferred | Only after benchmarks show it is needed |
| Add evaluation benchmarks | not started | Track latency and native/WASM parity |
| Add native/WASM parity coverage | not started | Requires WASM target and fixed fixtures |

## Completion Bar

Evaluation is strong enough to build on when:

* the runtime module has public headers and tests
* evaluator output is deterministic for fixed positions
* scores remain strictly inside search sentinels
* side-to-move-relative score signs are tested
* artifact loading rejects incompatible data
* pattern index encoding is tested against hand-computed fixtures
* search can run with the evaluator enabled or replaced by a reference evaluator
* exact endgame paths still avoid heuristic evaluation
* evaluation benchmark baselines exist
* UI calibration remains separate from search scoring

## Progress Update Rules

Update this document when:

* an implementation milestone changes status
* a known gap is discovered
* an evaluator option changes from planned to real behavior
* an artifact format is added or changed
* a benchmark baseline is added or replaced
* a deferred item is intentionally moved into scope

Update `docs/architecture/evaluation.md` only when the intended design,
boundary, semantics, or correctness rules change.
