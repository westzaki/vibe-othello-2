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

The current repository has the first runtime evaluation module and a stable
search-facing evaluator interface.

Existing search public types include:

* `Score`
* `kScoreWin`
* `kScoreLoss`
* `Evaluator`

The current search-facing evaluator interface is:

* `engine/include/vibe_othello/search/evaluator.h`

The current evaluation runtime includes:

* public evaluation headers under `engine/include/vibe_othello/evaluation/`
* minimal pattern schema types
* `LoadedPatternWeights` for artifact-loader output
* explicit runtime `PatternWeights` container for immutable evaluator tables
* ternary pattern index encoding
* fixed tiny pattern instances for edges and corners
* `TinyPatternEvaluator`

Ternary pattern digits are:

* empty square: `0`
* side-to-move disc: `1`
* opponent disc: `2`

`TinyPatternEvaluator` implements `search::Evaluator`, returns side-to-move
relative scores, performs no file I/O, and reads scores from explicit
`PatternWeights`. Tiny fixture weights are now test-only helpers rather than
the evaluator implementation. Scores are kept strictly inside the search
sentinel range by construction.

Existing evaluation tests cover:

* deterministic scoring
* side-to-move score convention
* hand-computed ternary pattern index
* fixture-backed score compatibility
* rejection of corrupted or incompatible tiny weights
* phase boundary behavior
* search sentinel score range
* artifact loader success and rejection paths
* conversion from loaded artifact data to runtime `PatternWeights`

The current repository already documents that:

* search depends on an evaluation interface for heuristic leaf scores
* search does not own evaluation feature definitions
* endgame exact search must not call heuristic evaluation
* board core is the source of truth for positions and moves

## Current Gaps

The current implementation does not yet have:

* production baseline evaluator
* production learned pattern evaluator
* schema validation for artifact-backed pattern definitions
* evaluation explanation API
* calibration API
* incremental evaluator state
* evaluation benchmarks
* evaluation data or artifact README
* trained weights
* trainer tooling
* calibrated score scale

The existing `search::Evaluator` interface is the production boundary for
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
| Add evaluation namespace and public runtime headers | done | `engine/include/vibe_othello/evaluation/` |
| Add simple baseline evaluator | not started | Useful before learned artifacts are available |
| Add minimal pattern schema types | done | `evaluation/pattern.h` |
| Add explicit pattern weight container | done | `LoadedPatternWeights` stores loader output; runtime `PatternWeights` stores phase maps and immutable tables |
| Add ternary pattern index encoding | done | Empty/player/opponent digits are `0/1/2` |
| Add tiny fixed edge and corner pattern instances | done | Runtime pattern geometry only |
| Add tiny pattern-only evaluator | done | Implements `search::Evaluator` and consumes `PatternWeights` |
| Add evaluator unit coverage | done | Determinism, sign convention, index, fixture compatibility, weight validation, phase, range, artifact loader paths, and loaded-to-runtime conversion |
| Add artifact manifest and binary loader | done | First binary loader validates version, bit order, score unit, phase count, pattern set id, pattern shape, weight count, and checksum |
| Add tiny hand-authored artifact fixture | done | Synthetic in-test fixture covers deterministic loader success and rejection paths |
| Add production `PatternEvaluator` | not started | Should implement `search::Evaluator` |
| Add evaluation explanation API | not started | Non-recursive adapter for tools and UI |
| Add calibration API | not started | Must not alter search scores |
| Add incremental evaluator path | deferred | Only after benchmarks show it is needed |
| Add evaluation benchmarks | not started | Track latency and native/WASM parity |
| Add native/WASM parity coverage | not started | Requires WASM target and fixed fixtures |
| Add trainer | not started | Tracked under pattern learning |

## Completion Bar

Evaluation is strong enough to build on when:

* the runtime module has public headers and tests
* evaluator output is deterministic for fixed positions
* scores remain strictly inside search sentinels
* side-to-move-relative score signs are tested
* artifact loading rejects incompatible data
* artifact-backed pattern schemas validate length against square count
* pattern index encoding is tested against hand-computed fixtures
* fixture-weight runtime evaluation is separated from future artifact loading
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
