# Search Progress

## Purpose

This document tracks current implementation status for search.

The intended design lives in `docs/architecture/search.md`.

Exact endgame implementation status lives in `docs/progress/endgame.md`.

This file may change frequently as implementation progresses.

## Design Sources

Relevant design documents:

* `docs/architecture/search.md`
* `docs/architecture/endgame.md`
* `docs/architecture/board-core.md`

## Current Foundation

The current repository already has the core search surface, production midgame
search paths, and a root-triggered exact-score endgame path.

Existing search public types include:

* `Score`
* `Depth`
* `Ply`
* `NodeCount`
* `Line`
* `BoundType`
* `SearchMode`
* `WldResult`
* `SearchLimits`
* `SearchOptions`
* `SearchStats`
* `RootMoveInfo`
* `SearchResult`
* `Evaluator`

The current search implementation includes:

* fixed-depth root search
* iterative deepening
* alpha-beta search
* null-window search primitive
* optional Principal Variation Search
* optional aspiration windows
* root move reporting
* principal variation propagation
* pass handling through board-core move deltas
* terminal disc-difference scoring
* midgame transposition-table cutoffs
* transposition-table best-move ordering
* Othello-specific move ordering
* `max_nodes`, `max_time`, `infinite`, and `stop_requested` enforcement
* best-completed-depth publication when iterative search is interrupted
* search statistics aggregation
* search benchmark coverage
* deterministic search golden-check tooling
* root-triggered generic exact-score endgame solving through `search_iterative`
* `SearchOptions::exact_endgame` and `endgame_exact_empties` threshold
  integration
* exact endgame result flags, root-move reports, PVs, and `endgame_nodes`
  statistics
* endgame benchmark coverage through `vibe_othello_endgame_bench`

Existing search tests include:

* alpha-beta tests
* iterative search tests
* search-limit and cancellation tests
* move-ordering tests
* null-window search tests
* PVS tests
* reference-search differential tests
* transposition-table tests
* exact endgame tests
* exact endgame reference differential tests
* deterministic search golden-check tooling

## Current Gaps

The current implementation does not yet have:

* WLD search path
* endgame TT probing or storing
* parity-region endgame ordering
* specialized endgame routines
* public direct endgame solve API
* checked-in search or endgame performance baselines
* real internal iterative deepening
* killer heuristic
* history heuristic
* dedicated PV table
* Multi-PV limiting
* ProbCut or calibrated selective pruning
* parallel search
* analysis and review-specific result adapters

The exact endgame path is currently a root-triggered generic exact-score solver.
It is entered before normal iterative deepening when the root position is at or
below `endgame_exact_empties`. It does not yet provide WLD, endgame TT, parity
ordering, or small-empty specialized routines.

Current time limits are cooperative and checked periodically inside recursive
midgame and endgame search. This keeps the hot path smaller, but it is not a
hard real-time deadline.

Endgame-specific gaps are tracked in `docs/progress/endgame.md`.

Remaining unimplemented search options are expected to remain safe no-ops until
each option is implemented. `exact_endgame` is no longer a no-op when the root
threshold is met.

## Implementation Plan

Status values:

* `done` means implemented in the repository
* `not started` means no production implementation exists yet
* `deferred` means intentionally left for a later phase

| Step | Status | Notes |
| --- | --- | --- |
| Define score semantics | done | `score.h` |
| Define search limits, options, result, and stats types | done | `search.h`, `result.h` |
| Define evaluator interface | done | `evaluator.h` |
| Implement reference negamax | done | Test support reference search |
| Implement alpha-beta | done | Production fixed-depth path |
| Add search stack and pass handling tests | done | Covered by search tests |
| Add transposition table | done | Direct-mapped internal table |
| Add iterative deepening | done | `search_iterative` |
| Add aspiration windows | done | Optional root-depth orchestration |
| Add PVS and null-window search | done | Optional PVS path and null-window primitive |
| Add root move ordering | done | Previous root best move and deterministic ordering |
| Add TT best-move ordering | done | Controlled by `use_tt_best_move_ordering` |
| Add Othello-specific ordering | done | Corner, edge, X/C-square, and mobility-style hints |
| Add max-node, max-time, infinite, and external-stop enforcement | done | Cooperative cancellation returns the best completed iterative result |
| Add killer and history heuristics | not started | Options currently safe no-ops |
| Add exact endgame solver | done | Root-triggered generic exact-score solver; details in `docs/progress/endgame.md` |
| Add exact endgame TT semantics | not started | `use_endgame_tt` currently safe no-op; tracked in `docs/progress/endgame.md` |
| Add specialized endgame routines | not started | Tracked in `docs/progress/endgame.md` |
| Add WLD search path | not started | `endgame_wld_empties` currently safe no-op; tracked in `docs/progress/endgame.md` |
| Add Multi-PV root search | not started | `multi_pv` currently safe no-op |
| Add advanced time management | not started | Soft/hard allocation and clock policy are deferred |
| Add optional selective pruning after calibration | deferred | `probcut` currently safe no-op |
| Add optional parallel search after single-thread search is stable | deferred | `use_parallel` currently safe no-op |
| Add analysis and review-facing result adapters | deferred | Requires consumer needs |

## Completion Bar

Search is strong enough to build on when:

* score semantics are documented
* reference negamax exists
* alpha-beta matches reference search
* PVS matches alpha-beta with selectivity disabled
* TT enabled and disabled produce the same exact fixed-depth results
* exact endgame TT enabled and disabled produce the same exact results
* returned best moves are always legal
* returned PVs are replayable
* pass positions are tested
* exact endgame solver has known-position tests
* specialized endgame routines match generic endgame search
* time-limited search returns best completed results
* search stats are available
* benchmark baselines exist
* selective pruning is optional and measured
* single-thread deterministic mode is stable
* public results are stable enough for WASM, UI, tools, and review adapters

## Progress Update Rules

Update this document when:

* an implementation milestone changes status
* a search option changes from a safe no-op to real behavior
* a benchmark baseline is added or replaced
* a known gap is discovered
* a deferred item is intentionally moved into scope

Update `docs/architecture/search.md` only when the intended design, boundary,
semantics, or correctness rules change.
