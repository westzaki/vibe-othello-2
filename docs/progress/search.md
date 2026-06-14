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
* separate midgame and endgame move-ordering entry points sharing legal-move
  expansion and insertion-sort mechanics
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
* exact endgame root reporting selection through `SearchOptions::multi_pv`,
  where `0` keeps all-root exact reporting and `1` enables best-only exact
  reporting
* exact-score endgame TT semantics through `TTEntryKind::exact_endgame_score`
  when `SearchOptions::use_endgame_tt` is enabled, including ordering-only legal
  TT best-move hints when a probed entry cannot cut off
* specialized small-empty exact-score paths for 0, 1, 2, and 3 empty squares
* ordering-only exact endgame parity hints through
  `SearchOptions::use_endgame_parity_ordering`
* endgame benchmark coverage through `vibe_othello_endgame_bench`, including
  parity-ordering and exact endgame TT comparison modes
* checked-in exact endgame benchmark baseline data for local comparison

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
* checked-in search benchmark aggregate baseline data for local comparison

## Current Gaps

The current implementation does not yet have:

* WLD search path
* WLD endgame TT probing or storing
* public direct endgame solve API
* real internal iterative deepening
* killer heuristic
* history heuristic
* dedicated PV table
* top-N Multi-PV limiting for `multi_pv > 1`
* ProbCut or calibrated selective pruning
* parallel search
* analysis and review-specific result adapters

The exact endgame path currently has root-triggered and internal leaf-triggered
generic exact-score solving. Root exact search is entered before normal
iterative deepening when the root position is at or below
`endgame_exact_empties`. Internal leaf cutover is entered from depth-limited
midgame search before heuristic evaluation when `exact_endgame` is enabled and
the leaf is at or below `min(endgame_exact_empties, 4)`. Internal cutover does
not publish root exact move reports or mark the whole root result exact. Exact
endgame does not yet provide WLD. Parity ordering is available as an
ordering-only hint and must not change exact results. Exact endgame root
reporting uses `multi_pv == 0` for the default all-root exact report,
`multi_pv == 1` for best-only exact reporting, and treats `multi_pv > 1` as a
safe all-root no-op until top-N reporting is implemented.

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
| Add exact endgame solver | done | Root-triggered and internal leaf-triggered generic exact-score solver; details in `docs/progress/endgame.md` |
| Add exact endgame TT semantics | done | Exact-score endgame uses `TTEntryKind::exact_endgame_score`; WLD remains not started |
| Add specialized small-empty exact-score path | done | 0/1/2/3 empty path is tested against generic exact endgame search |
| Add WLD search path | not started | `endgame_wld_empties` currently safe no-op; tracked in `docs/progress/endgame.md` |
| Add exact endgame best-only root reporting | done | `multi_pv == 1` returns only the exact best root move while preserving exact score and PV |
| Add Multi-PV top-N root search | not started | `multi_pv > 1` currently behaves like default all-root exact reporting |
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
* small-empty exact-score path matches generic endgame search
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
