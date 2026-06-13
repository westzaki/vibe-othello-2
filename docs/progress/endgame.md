# Endgame Progress

## Purpose

This document tracks current implementation status for exact endgame search.

The intended design lives in `docs/architecture/endgame.md`.

This file may change frequently as implementation progresses.

## Design Sources

Relevant design documents:

* `docs/architecture/endgame.md`
* `docs/architecture/search.md`
* `docs/architecture/board-core.md`

## Current Foundation

The current repository already has the foundations needed for endgame search.

Existing search public types include:

* `Score`
* `Depth`
* `Ply`
* `NodeCount`
* `Line`
* `BoundType`
* `SearchMode::exact_score`
* `SearchMode::win_loss_draw`
* `WldResult`

Existing `SearchOptions` include:

* `use_endgame_tt`
* `exact_endgame`
* `endgame_exact_empties`
* `endgame_wld_empties`

Existing `SearchStats` include:

* `endgame_nodes`

Existing internal transposition-table entry kinds include:

* `TTEntryKind::midgame`
* `TTEntryKind::exact_endgame_score`
* `TTEntryKind::exact_endgame_wld`

The current recursive search already:

* uses board-core positions and moves
* handles pass through board-core move deltas
* returns terminal disc difference exactly
* supports alpha-beta
* supports PVS
* supports iterative deepening
* supports root move reporting
* supports midgame TT cutoffs
* supports TT best-move ordering

## Current Gaps

The current implementation does not yet have:

* `engine/src/search/endgame.cc`
* public direct endgame solve API
* exact endgame search path
* WLD search path
* endgame TT probing or storing
* parity-region ordering
* small-empty specialized routines
* endgame benchmark executable
* exact endgame golden corpus

Unimplemented endgame options are currently expected to be safe no-ops.

Adding endgame search must preserve that safety property until each option is
implemented.

## Implementation Plan

Status values:

* `done` means implemented in the repository
* `not started` means no production implementation exists yet
* `deferred` means intentionally left for a later phase

| Step | Status | Notes |
| --- | --- | --- |
| Add endgame architecture document | done | `docs/architecture/endgame.md` |
| Add docs index rows | done | `docs/README.md` |
| Add progress documentation home | done | `docs/progress/README.md` |
| Add endgame progress document | done | this file |
| Add `engine/src/search/endgame.cc` | not started | First production solver file |
| Add generic exact-score solver using board core | not started | Use board-core move deltas and pass handling |
| Add `engine/tests/search/endgame_test.cc` | not started | Start with root exact score, pass, terminal, and small-empty cases |
| Add reference endgame solver in test support | not started | Slow and clear, no TT or heuristic evaluation |
| Add small-empty golden tests | not started | Generate from trusted reference solver and inspect a subset |
| Integrate root threshold through `SearchOptions::exact_endgame` | not started | Prefer root-only integration first |
| Mark exact root results with `exact = true` | not started | Also mark root moves exact |
| Add WLD mode | not started | May be deferred after exact score |
| Add endgame TT probe/store with separate entry kinds | not started | Test enabled/disabled equality |
| Add parity ordering as ordering only | not started | Test enabled/disabled equality |
| Add specialized zero/one/two/three-empty routines | not started | Test against generic solver |
| Add `engine/benchmarks/endgame_bench.cc` | not started | Use fixed corpora by empty count |
| Add endgame benchmark corpus | not started | Include pass, parity, and mobility categories |
| Tune thresholds for native builds | deferred | Requires benchmark baselines |
| Tune thresholds separately for WASM builds | deferred | Requires WASM measurement |
| Consider parallel endgame search | deferred | Only after single-thread stability |

## Completion Bar

Endgame search is strong enough to build on when:

* generic exact-score solver exists
* WLD solver exists or is explicitly deferred
* terminal score semantics are documented
* pass positions are tested
* terminal positions with empty squares are tested
* exact endgame does not call evaluator
* returned best moves are always legal
* returned PVs are replayable
* exact score matches reference solver
* WLD sign matches exact score
* TT enabled and disabled produce the same exact results
* parity ordering enabled and disabled produce the same exact results
* specialized small-empty routines match generic search
* root integration sets exact flags correctly
* interrupted search is not incorrectly marked exact
* search stats include endgame node counts
* benchmark baselines exist
* thresholds are configurable
* single-thread deterministic mode is stable

## Progress Update Rules

Update this document when:

* an implementation milestone changes status
* a temporary safety no-op becomes real behavior
* a benchmark baseline is added or replaced
* a known gap is discovered
* a deferred item is intentionally moved into scope

Update `docs/architecture/endgame.md` only when the intended design, boundary,
semantics, or correctness rules change.
