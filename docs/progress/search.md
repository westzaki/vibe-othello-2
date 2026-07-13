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
* `MidgameSearchOptions`
* `MoveOrderingOptions`
* `EndgameSearchOptions`
* `SearchReportingOptions`
* `ExperimentalSearchOptions`
* `SearchOptions`
* `SearchStats`
* `ShadowCalibrationSample`
* `ShadowCalibrationSink`
* `ShadowCalibrationStats`
* `SelectiveSearchOptionsV1`
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
* killer and history midgame move ordering heuristics
* real internal iterative deepening for deep full-window midgame nodes without
  legal midgame TT best-move hints
* separate midgame and endgame move-ordering entry points sharing legal-move
  expansion and insertion-sort mechanics
* typed `SearchOptions` sub-configs for midgame, ordering, endgame, reporting,
  and experimental options, with a legacy flat-field compatibility path through
  internal option normalization
* `max_nodes`, `max_time`, `infinite`, and `stop_requested` enforcement
* best-completed-depth publication when iterative search is interrupted
* search statistics aggregation
* separated internal headers for search limits, shared search utilities, and
  endgame-specific context/TT declarations, so the midgame internal header no
  longer owns endgame solver declarations
* search benchmark coverage
* CTest-backed fixed-position learned pattern search smoke that generates
  local-only v0a phase-bias and v0b pattern-SGD artifacts from the checked-in
  tiny Egaroucid fixture, injects them through `PatternEvaluator`, and compares
  best move, score, and node counts under the same explicitly configured
  depth-1 fixed-depth smoke settings
* deterministic search golden-check tooling
* root-triggered generic exact-score endgame solving through `search_iterative`
* public direct exact-score endgame solving through `solve_exact_endgame`
* public direct WLD endgame solving through `solve_wld_endgame`
* `SearchOptions::endgame.exact_endgame` and
  `SearchOptions::endgame.endgame_exact_empties` threshold integration
* root-triggered WLD endgame solving through `search_iterative` when
  `SearchMode::win_loss_draw` is explicitly requested and
  `SearchOptions::endgame.endgame_wld_empties` is met
* exact endgame result flags, root-move reports, PVs, and `endgame_nodes`
  statistics
* exact endgame root reporting selection through
  `SearchOptions::reporting.multi_pv`, where `0` keeps all-root exact reporting
  and `1` enables best-only exact reporting
* exact-score endgame TT semantics through `TTEntryKind::exact_endgame_score`
  when `SearchOptions::endgame.use_endgame_tt` is enabled, including
  ordering-only legal TT best-move hints when a probed entry cannot cut off
* WLD endgame TT semantics through `TTEntryKind::exact_endgame_wld` when
  `SearchOptions::endgame.use_endgame_tt` is enabled in direct or
  root-triggered WLD search
* specialized small-empty exact-score paths for 0, 1, 2, 3, and 4 empty squares
* ordering-only exact endgame parity hints through
  `SearchOptions::ordering.use_endgame_parity_ordering`
* endgame benchmark coverage through `vibe_othello_endgame_bench`, including
  parity-ordering and exact endgame TT comparison modes
* checked-in exact endgame benchmark baseline data for local comparison
* WASM C ABI and plain JavaScript wrapper support for legacy bounded best-move
  search plus `easy`/`normal`/`hard` preset-based bounded search through a
  loaded phase-aware evaluation artifact, with Worker and React CPU opponent
  wiring implemented separately under `apps/web`
* caller-owned `SearchSession` overloads with deterministic clear/new-game and
  explicit analysis reuse policy plus automatic evaluator/search-semantics TT
  invalidation
* entry- or byte-configured disabled/small/large TT allocation with auditable
  actual capacity and allocation-failure fallback
* typed key-plus-kind TT probes, protected same-key replacement, and split
  replacement/conflict/age/probe-slot telemetry
* root-once incremental position hashing and cached legal-mask node preparation
* root alpha carry and root PVS with exact MultiPV/teacher report completion
* controlled pass-depth A/B option; compatibility default still consumes depth
* opt-in persistent session plumbing for Arena and WASM
* temporary `experimental.use_legacy_search_kernel` rollback switch for the
  previous full-window root orchestration
* default-disabled MPC shadow calibration with deterministic capped sampling,
  compact schema-v2 samples, isolated shallow searches, automatically identified
  collection policy, raw hypothetical-cut diagnostics, and telemetry separate
  from official `SearchStats`/nodes
* deterministic local offline calibration analysis grouped by phase, deep and
  shallow depth, and node type, with exact-pair-only regression, strict
  provenance isolation, small-sample guards, and JSON plus Markdown reports

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
* legacy flat-field and typed-config option equivalence tests
* exhaustive search option normalization tests covering defaults, legacy fields,
  typed sub-configs, and compatibility conflict rules
* shadow disabled/on official-result parity, deterministic sampling, cap,
  metadata, PV/pass/exact-region policy, and official-node-limit tests
* offline analyzer determinism, bound exclusion from regression, mixed
  provenance/policy rejection, small-sample guards, invalid/malformed schema,
  and empty-input tests
* deterministic search golden-check tooling
* checked-in search benchmark aggregate baseline data for local comparison

## Current Gaps

The current implementation does not yet have:

* dedicated PV table
* top-N Multi-PV limiting for `multi_pv > 1`
* actual ProbCut/Multi-ProbCut cutoffs or adoption of calibrated coefficients
* parallel search
* analysis and review-specific result adapters
* match-bench, self-play, or production strength validation for learned
  pattern artifacts

The exact endgame path currently has root-triggered and internal leaf-triggered
generic exact-score solving. Root exact search is entered before normal
iterative deepening when the root position is at or below
`SearchOptions::endgame.endgame_exact_empties`. WLD root search is entered
before normal iterative deepening when `SearchMode::win_loss_draw` is explicitly
requested and the root position is at or below
`SearchOptions::endgame.endgame_wld_empties`; it returns `-1`, `0`, or `1`
rather than a final disc-difference margin. Internal leaf cutover is entered
from depth-limited midgame search before heuristic evaluation when
`SearchOptions::endgame.exact_endgame` is enabled, the request is not WLD, and
the leaf is at or below `min(endgame_exact_empties, 4)`. Internal cutover does
not publish root exact move reports or mark the whole root result exact. Parity
ordering is available as an ordering-only hint and must not change exact
results. Exact/WLD endgame root reporting uses `reporting.multi_pv == 0` for the
default all-root exact report, `reporting.multi_pv == 1` for best-only exact
reporting, and treats `reporting.multi_pv > 1` as a safe all-root no-op until
top-N reporting is implemented.

Current time limits are cooperative and checked periodically inside recursive
midgame and endgame search. This keeps the hot path smaller, but it is not a
hard real-time deadline.

Endgame-specific gaps are tracked in `docs/progress/endgame.md`.

Remaining unimplemented search options are expected to remain safe no-ops until
each option is implemented. `midgame.use_iid` now enables ordering-only shallow
midgame searches, and `endgame.exact_endgame` is no longer a no-op when the root
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
| Add transposition table | done | Configurable 4-way power-of-two bucket table with typed access and allocation reporting |
| Add reusable search session | done | Compatibility APIs use temporary sessions; caller-owned reuse is explicit |
| Add incremental search hash | done | Full root hash plus move/pass delta updates; public Position remains unchanged |
| Add single-movegen node preparation | done | Current mask once, opponent mask only for zero-current pass/terminal nodes |
| Add iterative deepening | done | `search_iterative` |
| Add aspiration windows | done | Optional root-depth orchestration |
| Add PVS and null-window search | done | Optional PVS path and null-window primitive |
| Add root PVS | done | First full window, later null window, qualifying full re-search, deterministic tie break |
| Add controlled pass depth semantics | done | Default consumes depth; `midgame.pass_consumes_depth=false` is the A/B variant |
| Add root move ordering | done | Previous root best move and deterministic ordering |
| Add TT best-move ordering | done | Controlled by `use_tt_best_move_ordering` |
| Add Othello-specific ordering | done | Corner, edge, X/C-square, and mobility-style hints |
| Add max-node, max-time, infinite, and external-stop enforcement | done | Cooperative cancellation returns the best completed iterative result |
| Add killer and history heuristics | done | Ordering-only midgame heuristics controlled by `use_killers` and `use_history` |
| Add real internal iterative deepening | done | Ordering-only shallow midgame search controlled by `use_iid` |
| Add exact endgame solver | done | Root-triggered and internal leaf-triggered generic exact-score solver; details in `docs/progress/endgame.md` |
| Add exact endgame TT semantics | done | Exact-score endgame uses `TTEntryKind::exact_endgame_score`; direct and root-triggered WLD use `TTEntryKind::exact_endgame_wld` |
| Add specialized small-empty exact-score path | done | 0/1/2/3/4 empty path is tested against generic exact endgame search |
| Add public direct exact endgame solve API | done | `solve_exact_endgame` calls the exact-score solver without an evaluator or threshold gate |
| Add public direct WLD endgame solve API | done | `solve_wld_endgame` calls the WLD solver without an evaluator or threshold gate |
| Add root-triggered WLD search path | done | `SearchMode::win_loss_draw` plus `endgame.endgame_wld_empties` routes `search_iterative` to WLD endgame solving without exposing final margins |
| Add exact endgame best-only root reporting | done | `multi_pv == 1` returns only the exact best root move while preserving exact score and PV |
| Add learned artifact fixed-position search smoke | done | `vibe_othello_pattern_search_bench_smoke` compares temp-only v0a/v0b learned artifacts under explicitly configured deterministic depth-1 search and emits a checksum-stable JSON report; this is local smoke coverage for evaluator signal propagation, not a production benchmark or strength claim |
| Add Multi-PV top-N root search | not started | `multi_pv > 1` currently behaves like default all-root exact reporting |
| Add advanced time management | not started | Soft/hard allocation and clock policy are deferred |
| Add optional selective pruning after calibration | deferred | `probcut` currently safe no-op |
| Add MPC shadow calibration | done | Diagnostics-only reduced-depth sampling; no official cutoff or runtime coefficient |
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

Next search-adjacent steps for learned artifacts are repeatable local Egaroucid
sequence subset reports with capped search smoke input. The fixed-position
learned artifact search smoke is not a production benchmark, match bench,
self-play run, or strength claim, and learned Egaroucid-derived
weights/artifacts remain uncommitted and publication-gated.
