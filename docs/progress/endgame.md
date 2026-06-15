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

The current repository already has the first production exact-score endgame
path.

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
* `use_endgame_parity_ordering`
* `exact_endgame`
* `endgame_exact_empties`
* `endgame_wld_empties`

Existing `SearchStats` include:

* `endgame_nodes`

Existing internal transposition-table entry kinds include:

* `TTEntryKind::midgame`
* `TTEntryKind::exact_endgame_score`
* `TTEntryKind::exact_endgame_wld`

The current exact endgame implementation includes:

* `engine/src/search/endgame.cc`
* root-triggered integration through `search_iterative`
* evaluator-free public direct exact-score solving through `solve_exact_endgame`
* evaluator-free public direct WLD solving through `solve_wld_endgame`
* root-triggered WLD orchestration through `search_iterative` when
  `SearchMode::win_loss_draw` is explicitly requested and the root position is
  at or below `endgame_wld_empties`
* internal leaf-triggered integration before heuristic evaluation when
  `exact_endgame` is enabled, the request is not WLD, and the leaf has at most
  `min(endgame_exact_empties, 4)` empty squares
* `SearchOptions::exact_endgame` and `endgame_exact_empties` threshold checks
* dedicated endgame move-ordering entry points that currently preserve the
  existing static Othello ordering
* generic exact final-disc-difference negamax with alpha-beta pruning
* uses board-core positions and moves
* handles pass through board-core move deltas
* returns terminal disc difference exactly
* avoids evaluator calls in exact endgame mode
* supports root move reporting for root-triggered exact search
* supports all-root exact reporting by default and best-only exact reporting
  when `SearchOptions::multi_pv == 1`
* avoids root move reporting for internal leaf cutover
* publishes replayable PVs
* marks result and root moves exact when completed
* uses ordering-only 4-neighbor empty-region parity hints when
  `SearchOptions::use_endgame_parity_ordering` is enabled
* counts exact endgame nodes through `SearchStats::endgame_nodes`
* checks `max_nodes`, `max_time`, and `stop_requested` cooperatively
* uses `TTEntryKind::exact_endgame_score` for optional exact-score endgame TT
  probe/store/cutoff when `SearchOptions::use_endgame_tt` is enabled
* uses legal `exact_endgame_score` and `exact_endgame_wld` TT best moves as
  ordering-only hints when a compatible endgame TT entry cannot cut off the
  current alpha-beta window
* uses `TTEntryKind::exact_endgame_wld` for optional WLD endgame TT
  probe/store/cutoff when `SearchOptions::use_endgame_tt` is enabled
* direct WLD search returns only `loss`, `draw`, or `win` as `-1`, `0`, or `1`
  in `SearchResult::score` and root move scores; it does not return final disc
  margins
* provides an endgame benchmark executable with a checked-in corpus default,
  deterministic built-in fallback, parity/TT/root-mode comparison options, and
  TT statistics in raw output
* has checked-in exact endgame benchmark baseline data for local comparison
* has production-vs-reference tests for terminal, one-empty, forced-pass, and
  deterministic small-empty positions
* has specialized exact-score paths for 0, 1, 2, 3, and 4 empty squares, guarded
  by generic-vs-small-empty differential tests
* avoids endgame move ordering and parity-map construction in specialized
  0/1/2/3/4-empty exact-score paths

## Current Gaps

The current implementation does not yet have:

* tuned native or WASM thresholds

`use_endgame_tt` is implemented for exact-score, direct WLD, and
root-triggered WLD endgame search. It remains separate from midgame TT semantics
by using
`TTEntryKind::exact_endgame_score` and `TTEntryKind::exact_endgame_wld`, and by
treating TT depth as remaining empty squares. WLD TT entries do not satisfy
exact-score probes, and exact-score TT entries do not satisfy WLD probes in the
current implementation. `endgame_wld_empties` gates root `search_iterative` WLD
orchestration, but only for explicit `SearchMode::win_loss_draw` requests.
`exact_endgame` is implemented when the root threshold is met and as a
conservative internal leaf cutover at four empties or fewer for non-WLD
requests. `multi_pv == 1` selects best-only exact/WLD root reporting for direct
endgame solvers and root-triggered WLD/exact endgame search; `multi_pv > 1`
remains a safe all-root no-op until top-N reporting is implemented.

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
| Add `engine/src/search/endgame.cc` | done | First production solver file |
| Add generic exact-score solver using board core | done | Root-only generic exact-score solver uses board-core move deltas and pass handling |
| Add `engine/tests/search/endgame_test.cc` | done | Covers root exact score, pass, terminal, threshold, flags, legality, and PV replay |
| Add reference endgame solver in test support | done | Slow and clear, no TT or heuristic evaluation |
| Add exact endgame golden corpus | done | `engine/fixtures/endgame/positions.tsv` plus deterministic `exact_score.jsonl` |
| Add small-empty golden tests | not started | Generate from trusted reference solver and inspect a subset |
| Integrate root threshold through `SearchOptions::exact_endgame` | done | Root integration before normal iterative deepening |
| Integrate internal leaf threshold through `SearchOptions::exact_endgame` | done | Conservative cutover before evaluator calls, capped at four empties and without root move reports |
| Mark exact root results with `exact = true` | done | Also marks root moves exact and non-selective |
| Add best-only exact root reporting | done | `SearchOptions::multi_pv == 1` returns only the exact best root move; default and `multi_pv > 1` keep all-root exact reports |
| Add WLD mode | done | Public direct `solve_wld_endgame` and explicit root-triggered `search_iterative` WLD return exact `-1/0/1` outcomes without final margins |
| Add endgame TT probe/store with separate entry kinds | done | Exact-score endgame uses `TTEntryKind::exact_endgame_score`; WLD uses `TTEntryKind::exact_endgame_wld`; the two probe paths do not satisfy each other |
| Add parity ordering as ordering only | done | Uses fixed 4-neighbor empty regions and an odd-region-first hint; disabled/enabled equality covered by corpus tests |
| Add specialized zero/one/two/three/four-empty path | done | 0/1 return through terminal or forced single-move/pass handling; 2/3/4 use direct legal-bit enumeration with board-core deltas; tested against generic solver through an internal generic-only policy |
| Add public direct exact-score API | done | `solve_exact_endgame` bypasses the root threshold gate and respects endgame limits/options without requiring an evaluator |
| Add public direct WLD API | done | `solve_wld_endgame` bypasses the root threshold gate and respects endgame limits/options without requiring an evaluator |
| Add `engine/benchmarks/endgame_bench.cc` | done | Measures root-only exact-score or WLD endgame search by empty count, parity-ordering mode, and endgame TT mode |
| Add endgame benchmark corpus | done | Built-in deterministic corpus covers 0/1/2/3/4/6/8/10/12 empty positions and a forced pass case |
| Add checked-in endgame benchmark baseline | done | `engine/benchmarks/baselines/endgame/2026-06-14-8f89540-apple-silicon-macos-arm64-apple-clang-17-release.json` |
| Tune thresholds for native builds | deferred | Requires repeated same-machine comparisons after baseline collection |
| Tune thresholds separately for WASM builds | deferred | Requires WASM measurement |
| Consider parallel endgame search | deferred | Only after single-thread stability |

## Benchmark Notes

The 0/1/2/3/4-empty exact-score positions can complete in only a few
microseconds in the current benchmark corpus, so elapsed-time comparisons for
those rows are noisy. Prefer score equality, node counts, and repeated
same-machine measurements when judging the small-empty specialized paths.

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
* small-empty exact-score path matches generic search
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
