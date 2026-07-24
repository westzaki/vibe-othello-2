# Endgame Architecture

## Purpose

Endgame search computes exact Othello outcomes for positions with a small number
of empty squares.

It is used by search, analysis, review, tools, WASM adapters, and UI features
when heuristic evaluation is no longer good enough.

Endgame search must be exact, deterministic in single-thread mode, measurable,
interruptible, and isolated from selective midgame pruning.

## Implemented System

Exact endgame is a production submodule of `engine/src/search/`. It provides:

* evaluator-free `solve_exact_endgame` and `solve_wld_endgame` APIs, with
  temporary-session and caller-owned-session overloads
* root exact-score and explicit root WLD routing from `search_iterative`
* conservative internal exact-score handoff through eight empties at heuristic
  leaves
* separate exact-score and WLD TT entry kinds
* generic negamax/alpha-beta recursion using board-core move deltas and
  incremental hashes
* disableable exact-score PVS above the specialized small-empty boundary
* exact-score specializations through eight empty squares, including direct
  one-empty flip counting and precomputed move deltas through four empties
* ordering-only empty-region parity hints and a cheap odd/even region branch
  partition for five-to-eight-empty search
* conservative stable-disc lower/upper bounds before move generation, with
  off, shadow-verification, and cutoff modes
* all-root and best-only exact root reporting, replayable PVs, cooperative
  limits, and endgame node accounting

Tests compare production and reference solvers, generic and specialized paths,
TT on/off, PVS on/off, parity on/off, stability off/shadow/cutoff, root-move
scores, pass positions, terminal positions, and checked-in corpus results.
`engine/benchmarks/endgame_bench.cc` provides exact/WLD, TT, PVS, parity,
stability, and root-reporting comparisons with checked-in aggregate baseline
data.

## Current Limitations

Native and WASM exact-handoff thresholds are caller or preset policy; there is
no universally tuned engine default. WLD uses the generic recursion rather
than the exact-score small-empty specializations, and exact-score requests do
not run a WLD proof first. Exact endgame uses PVS only above the specialized
eight-empty boundary, and parity regions only order branches rather than prune
them.
`reporting.multi_pv > 1` still means all-root reporting rather than top N.
Endgame search is single-threaded, and time/node cancellation remains
cooperative.

## Boundary

Endgame search owns:

* exact final disc-difference solving
* win/loss/draw solving
* endgame-specific alpha-beta search
* endgame-specific null-window search
* endgame transposition-table semantics
* parity-aware move ordering
* endgame root candidate reporting
* specialized small-empty routines
* endgame statistics
* endgame benchmark corpora

Endgame search does not own:

* board representation
* legal move rules
* flip calculation
* move application semantics
* move undo semantics
* pass legality
* terminal detection
* board hash source of truth
* heuristic evaluation
* pattern feature definitions
* pattern weight learning
* ProbCut or Multi-ProbCut calibration
* opening book data
* UI rendering
* review labels
* blunder thresholds

Endgame search depends on board core.

Endgame search is a search submodule.

Board core, evaluation, tools, WASM, and UI must not depend on endgame internals.

## Core Approach

Use exact negamax with alpha-beta pruning as the reference endgame solver.

Use null-window search for WLD solving.

Use full-window alpha-beta as the exact final disc-difference reference and PVS
as a disableable high-empty optimization.

Use endgame-specific transposition-table entries.

Use move ordering aggressively.

Do not use heuristic evaluation at exact endgame leaves.

Do not use selective pruning in exact endgame modes.

Do not use ProbCut, Multi-ProbCut, LMR, or any unproven reduction in exact
endgame modes.

Correctness comes before speed.

Every optimization must be disableable or testable against the generic solver.

## Representation Dependency

Endgame search uses board-core positions and moves.

```cpp
namespace vibe_othello::search::internal {

using board_core::Bitboard;
using board_core::Move;
using board_core::MoveDelta;
using board_core::Position;
using board_core::PositionHash;

}
```

Endgame search must not duplicate board rules.

Legal moves come from `board_core::legal_moves`.

Move deltas come from `board_core::make_move_delta`.

Move application comes from `board_core::apply_move_delta`.

Undo comes from `board_core::undo_move`.

Terminal detection comes from `board_core::is_terminal`.

Position hashing comes from `board_core::hash_position`.

Production endgame recursion consumes the search-internal incremental key. A
full hash is computed once at the root, normal moves and passes update it from
their deltas, and undo restores it. Exact-score and WLD probes always include
their distinct `TTEntryKind`.

Board-core full hash recomputation remains the source of truth.

Endgame search must not manually swap `player`, `opponent`, or `side_to_move`
for pass handling.

Pass must flow through board-core pass deltas.

## Score Semantics

Exact endgame scores are side-to-move-relative final disc differences.

Positive score means the side to move at the current node can finish ahead.

Negative score means the side to move at the current node finishes behind.

Zero means draw.

After making a move, child scores are negated.

```cpp
score = -exact_score_search(child, -beta, -alpha, empties_after, ply + 1);
```

Exact endgame scores must stay in the final disc-difference range.

The current engine terminal score counts discs only:

```cpp
score = popcount(position.player) - popcount(position.opponent);
```

This document follows that semantic.

If the project later changes to an empties-awarded terminal convention, update:

* `docs/architecture/search.md`
* this document
* terminal scoring tests
* exact endgame golden results
* review score interpretation
* UI score labels

WLD results are not exact score results.

Exact score can be converted to WLD:

* `score > 0` means win
* `score == 0` means draw
* `score < 0` means loss

WLD must not be converted back into an exact score.

## Solve Modes

Endgame supports two exact modes.

```cpp
enum class SolveMode : std::uint8_t {
  exact_score,
  win_loss_draw,
};
```

`exact_score` returns final disc differential.

`win_loss_draw` returns only whether the side to move can force a win, draw, or
loss.

Public exact-score endgame results must set `score_kind` to `exact_disc_diff`.
Public WLD endgame results must set `score_kind` to `win_loss_draw`.
Stopped endgame results that do not publish a completed root or terminal score
must set `score_kind` to `unavailable`.

The two modes may share recursion, move ordering, stack frames, and board-core
operations.

The two modes must use distinct transposition-table entry kinds.

Exact-score TT entries must not satisfy WLD probes unless explicitly converted
through sign checks at probe time.

WLD TT entries must never satisfy exact-score probes.

## Triggering Endgame Search

Exact-score and WLD root triggers are separate. Exact-score root integration
requires `endgame.exact_endgame`, a non-WLD request, and a root empty count at
or below `endgame.endgame_exact_empties`; it returns a final disc-difference
margin. WLD root integration requires an explicit
`SearchMode::win_loss_draw`, a nonzero `endgame.endgame_wld_empties`, and a
root empty count at or below that threshold; it returns only `-1`, `0`, or `1`.
The direct solver APIs bypass both threshold gates.

Thresholds should be independently configurable for:

* exact final score
* WLD
* native builds
* WASM builds
* debug builds
* benchmark runs

Root search does not automatically cascade from WLD into exact-score solving.
The requested mode selects one result domain. A caller that wants a WLD proof
followed by a final margin must make two explicit requests.

Correctness is more important than threshold size.

## Empty Count

Empty count is derived from board core.

```cpp
std::uint8_t empty_count(board_core::Position position) noexcept {
  return static_cast<std::uint8_t>(
      std::popcount(~board_core::occupied(position)));
}
```

Rules:

* normal moves reduce empty count by one
* pass does not reduce empty count
* terminal positions may have empty squares
* terminal score must be computed from final discs, not from empty count alone

Pass must not cause infinite recursion.

Board core only permits pass when the current side has no legal move and the
opponent has a legal move.

After a legal pass, the next expanded node must have at least one legal normal
move unless the position became terminal.

## Public API Shape

Endgame is exposed through the existing search entry points.

```cpp
SearchResult search_iterative(board_core::Position position,
                              const Evaluator& evaluator,
                              SearchLimits limits,
                              SearchOptions options);
```

When `options.endgame.exact_endgame` is enabled and the exact-score empty
threshold is met, `search_iterative` may route to exact-score endgame search.
When `options.mode == SearchMode::win_loss_draw` and the WLD empty threshold is
met, `search_iterative` may route to WLD endgame search.

Tools and callers that need evaluator-free exact score or WLD solving may call
the direct public APIs.

```cpp
SearchResult solve_exact_endgame(board_core::Position position,
                                 SearchLimits limits = {},
                                 SearchOptions options = {});

SearchResult solve_wld_endgame(board_core::Position position,
                               SearchLimits limits = {},
                               SearchOptions options = {});
```

Direct exact solving starts exact final-disc-difference search regardless of the
root empty count. It does not use the `endgame.exact_endgame` or
`endgame.endgame_exact_empties` threshold gate, does not require an evaluator,
and does not fall back to midgame search. `max_nodes`, `max_time`, and
`stop_requested` are respected. `max_depth` is ignored because exact endgame
depth is the root empty count.

Direct WLD solving starts exact win/loss/draw search regardless of the root empty
count. It returns only `loss`, `draw`, or `win` as `-1`, `0`, or `1`; it does
not return final disc margins.

Root-triggered WLD through `search_iterative` uses the same result semantics.
`SearchResult::score` and root move scores are WLD values, and `exact` means the
exact WLD outcome was completed rather than an exact final margin.

`endgame.use_endgame_tt`, `endgame.stability_mode`,
`ordering.use_endgame_parity_ordering`, and root reporting options such as
`reporting.multi_pv` are honored. Stability bounds apply to exact-score search;
WLD search does not consume final-margin stability bounds. Midgame-only options
must not change direct exact-score or WLD semantics.

Reusable endgame logic lives in `engine/src/search/`; tool entry points remain
under `tools/`.

## Internal API Shape

The internal implementation uses a dedicated `EndgameContext` with
`SearchPositionState`, resolved options, the shared limit state, an optional
session TT, accumulated public stats, and a fixed per-ply `EndgameStackFrame`
array. Exact-score and WLD policy types share root and recursive orchestration
while selecting separate terminal score ranges and TT entry kinds.

The important internal entry points are:

```cpp
namespace vibe_othello::search::internal {

SearchNodeResult exact_score_search(EndgameContext*, Score alpha, Score beta,
                                    std::uint8_t empties, Ply ply);

SearchNodeResult wld_search(EndgameContext*, Score alpha, Score beta,
                            std::uint8_t empties, Ply ply);

SearchResult solve_exact_endgame(board_core::Position position,
                                 SearchLimits limits,
                                 SearchOptions options,
                                 TranspositionTable* tt);

SearchResult solve_wld_endgame(board_core::Position position,
                               SearchLimits limits,
                               SearchOptions options,
                               TranspositionTable* tt);

}
```

Endgame recursive code must not call `Evaluator::evaluate`.

## Generic Exact-Score Algorithm

Generic exact-score search is negamax alpha-beta.

At each node:

* increment `nodes` and `endgame_nodes`
* check cancellation periodically
* probe exact stability bounds before legal-move generation when enabled
* return terminal disc difference if terminal
* generate legal moves
* probe the compatible exact endgame TT kind if enabled
* if no legal move exists, apply pass and recurse without reducing empty count
* order legal moves
* apply each move through board-core delta
* recurse with negated window and `empties - 1`; when exact-score PVS is active,
  later moves first use a null window and re-search scores strictly inside the
  full window
* undo through board core
* update best score, alpha, PV, and best move
* store exact endgame TT entry if enabled

This algorithm should stay intentionally close to existing alpha-beta search.

That makes differential testing easier.

## WLD Algorithm

WLD search proves only win, draw, or loss.

It may use null-window alpha-beta.

Recommended score mapping:

```text
loss = -1
draw = 0
win  = 1
```

WLD search may call exact-score terminal scoring and convert by sign at terminal
nodes.

WLD search must not return final margins.

WLD search can be useful before exact score because a narrow proof may be enough
for move choice or UI outcome display.

WLD TT entries store only WLD-compatible bounds.

WLD TT entries must use `TTEntryKind::exact_endgame_wld`.

WLD and exact-score TT entries must not be silently interchangeable.

## Move Ordering

Endgame move ordering is a speed optimization.

It must not change the minimax result.

The current deterministic ordering computes weighted features for:

1. TT and previous-root best-move matches
2. corner, edge, and stable-like edge status
3. dangerous X/C-square penalties while the corner is empty
4. opponent mobility after the move
5. odd 4-neighbor empty-region parity
6. lower square index as deterministic tie-breaker

Existing midgame ordering concepts should be reused where possible.

Endgame ordering may add parity-specific hints.

Expensive ordering should be used only where it saves more nodes than it costs.

For small empty counts, the cost of ordering can exceed the saved nodes.

Benchmark ordering policies by empty count.

## Parity Ordering

Parity is an endgame move-ordering heuristic.

It must be implemented as ordering first.

It must not prune moves unless the pruning rule is proven exact and tested
against the generic solver.

The implemented `EmptyRegionMap` stores a region id per square and the size of
each region. It is built allocation-free from the current empty mask.

```cpp
struct EmptyRegionMap {
  std::array<std::uint8_t, board_core::kSquareCount> region_for_square{};
  std::array<std::uint8_t, board_core::kSquareCount> region_sizes{};
  std::uint8_t size = 0;
};
```

Rules:

* compute empty regions from the current position
* record region size parity
* prefer moves in odd regions when that is favorable
* use deterministic tie-breaking
* keep the region-connectivity rule documented and tested

The fixed policy is 4-neighbor connectivity. A move whose destination
belongs to an odd-sized empty region is treated as parity-favorable and ordered
ahead of otherwise similar moves in even-sized regions. In the dedicated
five-to-eight-empty path, the same region policy directly partitions the legal
branches into odd-region then even-region groups and skips the more expensive
generic mobility scoring. It remains an ordering strategy and never removes a
legal branch.

If 4-neighbor and 8-neighbor region policies are compared, they must be separate
benchmark options.

Parity ordering must be tested by checking that enabling and disabling it
returns the same exact score.

Benchmark-only counters may track:

* parity maps built
* parity-ordered moves
* odd-region first moves
* parity ordering time

## Corner, Edge, and Stability Bounds

Corners are high-priority ordering candidates.

Edges and stable-like edge moves are useful ordering hints.

They are not proof rules by default.

A move may be ordered earlier because it:

* takes a corner
* is on an edge
* extends from an owned corner
* reduces opponent mobility
* avoids a dangerous X-square
* avoids a dangerous C-square

Do not prune based on these heuristics in exact mode.

Exact-score endgame search may use a conservative stable-disc proof. The
implementation starts from complete rows, files, and diagonals and grows a
fixed point only when a same-color disc is protected on all four axes by a full
axis, a board boundary, or an already-proven stable neighbor. A missed stable
disc only weakens the bound; an unproven disc must never be included.

For `p` proven stable current-player discs and `o` proven stable opponent discs,
the final disc-difference bounds are:

```text
lower = 2 * p - 64
upper = 64 - 2 * o
```

These follow by assigning every disc not covered by the proof to the opponent
for the lower bound, or to the current player for the upper bound.

`EndgameStabilityMode` has three behaviors:

* `off`: do not probe the proof
* `shadow`: record hypothetical fail-high/fail-low candidates, complete normal
  search, and verify each candidate against the returned alpha-beta result
* `cutoff`: return a proven lower or upper bound and store the matching exact
  endgame TT bound

The probe runs before legal-move generation. A window/empty-count gate may skip
positions where the bound cannot plausibly reach alpha or beta; this is only a
cost control and must not affect correctness. The optimization must remain
disableable and differential corpus tests must cover `off`, `shadow`, and
`cutoff`, including zero shadow false-cut candidates.

## Transposition Table Semantics

Endgame TT entries are exact-search data.

They must be marked separately from midgame entries.

Use existing kinds:

```cpp
enum class TTEntryKind : std::uint8_t {
  midgame,
  exact_endgame_score,
  exact_endgame_wld,
};
```

Rules:

* midgame entries must not cause exact endgame cutoffs
* exact-score entries must not satisfy heuristic midgame probes
* WLD entries must not satisfy exact-score probes
* exact-score and WLD entries remain separate in the current implementation
* TT entries store side-to-move-relative scores
* TT depth should mean remaining empties or exact remaining plies, not midgame
  depth
* replacement policy must be deterministic in single-thread mode

Endgame uses the session-owned four-way, configurable, generation-aware TT.
Exact-score and WLD probes include the entry kind and treat depth as remaining
empty squares. Tests can disable the table, and benchmark/report telemetry uses
the same public counters as midgame search. Nodes with four or fewer remaining
empty squares bypass TT probes and stores because specialized/shallow recursion
is cheaper than table access and saturated-table replacement attempts.

The shared TT first handles same-key/same-kind refinement, then uses an empty
slot, otherwise prefers an older, shallower, or weaker-bound victim. A current-
generation entry with a stronger depth/bound may reject the incoming store.

Do not make TT correctness depend on collision luck.

If hash verification becomes necessary, add full-board verification or a wider
tag before relying on exact TT in large searches.

## Bound Semantics

Exact endgame TT entries may store bounds.

```cpp
enum class BoundType : std::uint8_t {
  exact,
  lower,
  upper,
};
```

Probe rules:

* exact entry may return immediately
* lower entry may cutoff when `score >= beta`
* upper entry may cutoff when `score <= alpha`
* entries with insufficient remaining depth must not cutoff
* entries with incompatible kind must not cutoff

For exact endgame, remaining empty count is usually a better draft measure than
nominal search depth.

Pass does not reduce empty count.

If depth is used as draft, document whether pass reduces draft.

Do not mix the two meanings silently.

## Specialized Small-Empty Routines

Specialized zero-to-eight-empty exact-score routines are implemented as speed
optimizations. WLD remains on the generic path. The implemented tiers are:

1. zero empty squares
2. one empty square
3. two empty squares
4. three empty squares
5. four empty squares
6. five through eight empty squares

They must preserve exact-score semantics.

They must handle pass exactly.

They must handle terminal positions with empty squares.

They must be tested against the generic solver.

Small-empty routines should avoid unnecessary:

* move-list construction
* repeated full legal-move generation
* heap allocation
* virtual calls
* logging
* file I/O

The one-empty path counts flips from precomputed directional rays and the
nearest player anchor for normal, forced-pass, and terminal positions. The
two-to-four-empty tiers use those rays with the nearest player-or-empty blocker
to derive flip masks directly from the remaining empty squares, then reuse the
precomputed deltas when applying each move. The
five-to-eight-empty tier keeps recursive alpha-beta but replaces generic
per-move mobility scoring with direct legal-bit enumeration and optional
odd-region/even-region partitioning. Empty-region discovery uses bitboard flood
fill while preserving deterministic low-square region IDs.

They must still use board-core flip logic or a tested equivalent.

If a specialized routine duplicates board-core flip behavior for speed, it needs
extra differential tests against board core.

## Pass Handling

Pass is not a search shortcut.

Pass is a legal state transition owned by board core.

Rules:

* if legal moves exist, do not pass
* if no legal moves exist and opponent has a legal move, pass is forced
* if neither side has a legal move, the position is terminal
* pass does not reduce empty count
* pass counts as a ply in existing search depth semantics
* pass must be represented in PVs
* pass must be replayable from root PVs

Endgame search must use:

* `board_core::make_pass`
* `board_core::make_move_delta`
* `board_core::apply_move_delta`
* `board_core::undo_move`

Endgame search must not manually swap position fields.

## Integration with Midgame Search

Endgame search integrates at root and internal nodes.

Root integration:

* if root empty count is under threshold, solve exactly
* report legal root moves
* report exact score
* set `SearchResult::exact = true`
* set each exact root move `RootMoveInfo::exact = true`
* set `RootMoveInfo::selective = false`

Internal integration:

* when a midgame leaf reaches depth zero, exact-score endgame is enabled, and
  the request is not WLD, call exact endgame search instead of heuristic
  evaluation if the position is under the conservative internal threshold
* the internal exact-score threshold is capped at eight empties, even
  when `endgame_exact_empties` is larger
* internal endgame calls do not publish root move reports
* negate returned score according to normal negamax rules
* keep exact entries separate from midgame TT entries
* do not mark the whole root `SearchResult` exact unless root integration solved
  the root itself exactly

Root integration and internal integration are distinct. Root integration
publishes exact root moves and may mark the `SearchResult` exact. Internal
integration only replaces heuristic leaf evaluation with an exact side-to-move
disc-difference score for that internal position.

## Interaction with PVS

Exact-score endgame uses its own PVS path, independent of midgame PVS. The
first ordered move receives the full window. Later moves at nodes with at least
nine remaining empty squares first receive `[alpha, alpha + 1]`; a completed
score strictly between alpha and beta is re-searched with the full window.

`endgame.use_endgame_pvs` defaults enabled and can be disabled to run the plain
alpha-beta reference path. Re-searches increment `stats.pvs_researches`.
Through eight empties the exact solver uses its specialized path without scout
overhead. WLD continues to use the generic recursion with its narrow score
range and ignores this option.

Endgame PVS must match alpha-beta exact scores, remain independently
benchmarkable, and never call heuristic evaluation. It is an optimization, not
a correctness requirement.

## Interaction with Selective Search

Selective search must never run in exact endgame modes.

Disabled in exact endgame:

* ProbCut
* Multi-ProbCut
* LMR
* speculative pruning
* heuristic futility pruning
* unverified stability pruning
* unverified mobility pruning

Selective options may exist in `SearchOptions`.

Exact endgame code must ignore them or assert that they are disabled.

Search results from exact endgame must report:

* `exact = true`
* `selective = false`

Transposition-table entries produced by selective midgame search must not be
used for exact endgame cutoffs.

## Search Limits and Cancellation

Endgame search must respect `SearchLimits`.

Supported limits:

* max nodes
* max time
* external stop flag
* infinite analysis until cancelled

Cancellation must be cooperative.

Recommended checks:

* before root move search
* after each root move
* every fixed number of internal nodes
* before expensive ordering computation
* before storing or publishing a root result

Interrupted exact endgame search must not pretend to be exact.

If interrupted:

* `SearchResult::stopped = true`
* `SearchResult::exact = false`
* completed root move entries may remain exact even when the whole result is not exact
* if at least one root move completed, top-level best move, score, and PV may be
  published from the best completed root move only
* when publishing only completed root moves, top-level `bound` must be
  `BoundType::lower`
* if no root move completed, do not publish top-level best move, PV, or score
* if the public result type still carries a score when no root move completed,
  set it to the most conservative lower bound sentinel
* no illegal move may be returned
* partial TT entries must not be stored as exact

A stopped search may store safe lower or upper bounds only if the bound
semantics are valid.

The current TT store path checks the shared stop state and does not store after
cancellation is observed.

## Root Reporting

Root endgame solving must produce stable user-facing information.

Root output should include:

* best move
* exact score
* WLD result by score sign
* completed empty count or equivalent depth
* root candidate list
* node count
* elapsed time
* PV
* exact flag
* stopped flag

For exact endgame, `SearchResult::completed_depth` is the root empty count that
has been solved to terminal leaves for the published top-level result. Completed
exact results set it to the root empty count. Interrupted results that publish
one or more completed root moves also set it to the root empty count, because
the published candidate scores are solved to terminal leaves. Interrupted
results with no completed root move set it to zero.

`RootMoveInfo::depth` for exact endgame is the root empty count solved for that
root move.

All root moves must be legal.

Root PVs must be replayable.

Exact root reporting has two initial modes:

* all-root exact reporting returns every legal root move with an exact score
* best-only reporting returns only the exact best root move in `root_moves`

All-root exact reporting is the default because it is useful for analysis,
review, and backward-compatible consumers. Best-only reporting is intended for
play paths that only need the exact decision and principal variation. Terminal
roots still report no root moves. Forced-pass roots report the pass move.

If multiple moves have the same exact score, tie-break by lower square index
unless a higher-level root ordering policy says otherwise.

Root reporting should remain deterministic in single-thread mode.

## Multi-PV

Multi-PV is a root feature.

Endgame search should support Multi-PV only at the root.

Rules:

* all reported moves must be legal
* all reported scores must be exact when `exact = true`
* all reported PVs must be replayable
* candidate ordering must be deterministic
* Multi-PV may be slower than single-PV

The default returns all root moves with exact scores, which is useful for UI and
review.

For exact endgame root search, `SearchOptions::reporting.multi_pv` is
interpreted as:

* `0`: default all-root exact reporting
* `1`: best-only exact reporting
* `>1`: top-N reporting is not implemented yet and behaves like `0`

The `multi_pv == 1` path may use root alpha-beta windows to avoid producing
unneeded exact root move reports. The published best move, score, and PV must
still be exact when the result is not stopped.

## Statistics

Existing stats should be used first.

Mandatory endgame stats:

* `nodes`
* `terminal_nodes`
* `pass_nodes`
* `beta_cutoffs`
* `alpha_updates`
* `tt_probes`
* `tt_hits`
* `tt_stores`
* `tt_cutoffs`
* `endgame_nodes`
* `endgame_last_flip_solved`
* `endgame_stability_probes`
* `endgame_stability_lower_candidates`
* `endgame_stability_upper_candidates`
* `endgame_stability_cutoffs`
* `endgame_stability_shadow_verifications`
* `endgame_stability_shadow_false_cutoffs`

Recommended future stats:

* `endgame_tt_probes`
* `endgame_tt_hits`
* `endgame_tt_cutoffs`
* `wld_nodes`
* `exact_score_nodes`
* `small_empty_hits`
* `parity_ordered_nodes`
* `cancel_checks`

Do not add many public counters before they are useful.

Benchmark-only counters may live in private benchmark output.

Stats must be cheap to collect.

Detailed stats may be compiled out or disabled in hot builds.

## Directory Layout

Endgame lives inside the engine static library.

The public APIs and options are declared in `search.h`; no separate public
endgame header exists. The private implementation is split as follows:

```text
engine/src/search/
  endgame_context_internal.h
  endgame_policy_internal.h
  endgame_tt_internal.h
  endgame_root.cc
  endgame_search.cc
  endgame_small_empty.cc
  endgame_stability.cc
  endgame_stability_internal.h
  endgame_tt.cc
  move_ordering/
    endgame_ordering.cc
    parity_ordering.cc
```

Endgame tests live alongside other search tests. Reference solver and position
helpers live in `engine/tests/support/search/`. The reusable checked-in corpus
and benchmark are:

```text
engine/fixtures/endgame/positions.tsv
engine/benchmarks/endgame_bench.cc
```

## Testing Strategy

Endgame correctness requires multiple test styles.

Use unit tests for:

* terminal score
* empty count
* root exact score
* root WLD
* pass in endgame
* terminal positions with empty squares
* one-empty positions
* two-empty positions
* three-empty positions
* exact-score bound interpretation
* WLD bound interpretation
* TT entry-kind compatibility
* cancellation result flags

Use differential tests to compare:

* generic endgame vs reference exhaustive solver
* generic endgame vs specialized small-empty routines
* exact TT disabled vs exact TT enabled
* parity ordering disabled vs parity ordering enabled
* stability disabled vs shadow verification vs enabled cutoff
* root-only exact integration vs direct solver
* WLD sign vs exact-score sign

Use property tests for:

* returned best move is legal
* returned PV is replayable
* exact score is unchanged by move ordering policy
* exact score is unchanged by TT enablement
* exact score is unchanged by specialized routine enablement
* proven stable discs survive every legal continuation
* stability shadow candidates have no false cuts
* pass does not change occupancy
* normal moves reduce empty count by one
* apply and undo restore root position
* interrupted result is not marked exact unless fully completed

Use regression corpora for:

* zero empty squares
* one empty square
* two empty squares
* three empty squares
* forced pass endgames
* double-pass terminal positions
* corner race positions
* edge parity positions
* odd-region parity positions
* even-region parity positions
* high-mobility late endgames
* low-mobility late endgames
* known exact endgames

Use benchmarks for:

* nodes per second
* time to solve by empty count
* effective branching factor
* TT hit rate
* TT cutoff rate
* WLD solve time
* exact-score solve time
* parity ordering overhead
* specialized small-empty speedup

## Reference Endgame Solver

A slow, clear reference endgame solver lives in test support.

It:

* use board core
* enumerate all legal moves
* handle pass through board core
* compute exact terminal disc difference
* avoid TT
* avoid move ordering beyond square-index order
* avoid heuristic evaluation
* be easy to read

It exists to catch mistakes in optimized endgame search.

It should not be used in production search.

Reference code may be slower.

Correctness is its only job.

## Golden Results

Golden endgame results should include:

* position
* side to move
* empty count
* mode
* expected exact score
* expected WLD result
* expected best move set
* expected PV if stable
* options
* notes

Best move may be a set when multiple moves have the same exact score.

PV should be required only when ordering is deterministic and intentionally
stable.

Exact-score golden results are stronger than WLD golden results.

Do not create golden results from the implementation being tested unless another
trusted solver verifies them.

For early development, generate small-empty golden results from the reference
endgame solver and inspect a subset manually.

## Benchmark Suites

Benchmarks should use fixed corpora.

Recommended endgame corpora:

* 4 empties
* 6 empties
* 8 empties
* 10 empties
* 12 empties
* 14 empties
* forced pass positions
* corner race positions
* parity-sensitive positions
* high-mobility positions
* low-mobility positions

Benchmark output should include:

* position id
* category
* empty count
* mode
* score
* WLD
* best move
* nodes
* endgame nodes
* TT probes
* TT hits
* TT cutoffs
* elapsed time
* nps
* enabled options
* compiler
* build type
* CPU
* hash size
* thread count

Performance changes without correctness tests are not trustworthy.

Correctness changes without benchmark data are hard to evaluate.

## Determinism

Single-thread endgame search must be deterministic.

Given the same:

* position
* mode
* options
* limits that do not interrupt early
* hash generation policy
* TT size
* thread count of one

Endgame search should return the same:

* score
* WLD result
* best move
* PV
* root move scores
* exact flag

Node counts should also be deterministic in single-thread mode unless the TT
replacement policy intentionally allows variation.

There is no parallel mode in the current implementation.

## Thread Safety

Endgame search is single-threaded. Independent sessions may be used on separate
threads when their evaluators and inputs are also safe to use independently,
but a session and its TT must not be shared by concurrent searches. External
cancellation communicates only through the caller-owned atomic stop flag.

## Error Handling

Recursive endgame search should not throw exceptions.

Hot-path endgame code should be:

* allocation-free
* exception-free
* logging-free
* file-I/O-free

Public entry points may return status objects later.

Invalid public inputs should be rejected at public boundaries.

Debug builds should assert internal invariants.

Release builds must avoid undefined behavior.

Cancellation is not an error.

Interrupted results must be marked as stopped and non-exact unless a fully exact
result was completed.

## Performance Principles

Endgame hot paths should be:

* deterministic
* allocation-free
* exception-free
* logging-free
* file-I/O-free
* explicit about enabled options
* measurable through stats
* benchmarked by empty count

Prefer simple correct code first.

Optimize after reference tests and benchmark corpora exist.

Avoid virtual dispatch in deep endgame recursion.

Do not call heuristic evaluation from exact endgame recursion.

Use board-core primitives rather than duplicating board rules.

Add specialized small-empty code only after the generic solver is correct.

Add parity ordering only after exact score is protected by tests.

Add TT cutoffs only after TT enabled/disabled equality tests exist.

## Change Checklist

When changing endgame behavior, update this document for implementation,
boundary, or semantic changes.

Semantic changes should run:

* endgame unit tests
* reference endgame differential tests
* exact TT enabled/disabled tests
* parity ordering enabled/disabled tests
* specialized small-empty differential tests
* pass and terminal regression tests
* sanitizer tests where available

Hot-path changes should also run:

* endgame benchmarks
* search benchmarks
* board-core tests if board-core behavior is touched

Boundary changes should update:

* `docs/architecture/search.md`
* `docs/README.md`
* public headers
* WASM adapters when exposed
* tools that call search entry points
* review or UI score interpretation when exact flags change
