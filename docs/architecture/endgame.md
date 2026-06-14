# Endgame Architecture

## Purpose

Endgame search computes exact Othello outcomes for positions with a small number
of empty squares.

It is used by search, analysis, review, tools, WASM adapters, and UI features
when heuristic evaluation is no longer good enough.

Endgame search must be exact, deterministic in single-thread mode, measurable,
interruptible, and isolated from selective midgame pruning.

Implementation status and milestone tracking live in
`docs/progress/endgame.md`.

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

Use full-window search for exact final disc-difference solving.

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

The two modes may share recursion, move ordering, stack frames, and board-core
operations.

The two modes must use distinct transposition-table entry kinds.

Exact-score TT entries must not satisfy WLD probes unless explicitly converted
through sign checks at probe time.

WLD TT entries must never satisfy exact-score probes.

## Triggering Endgame Search

Endgame search is triggered from root search when all of the following hold:

* `SearchOptions::exact_endgame` is true
* empty count is less than or equal to a configured threshold
* the requested mode is compatible with endgame solving
* cancellation has not already been requested

Recommended initial thresholds:

* `endgame_exact_empties = 10`
* `endgame_wld_empties = 12`

These are conservative defaults for early implementation.

They are not strength claims.

They must be benchmarked and tuned after the generic solver is correct.

Thresholds should be independently configurable for:

* exact final score
* WLD
* native builds
* WASM builds
* debug builds
* benchmark runs

The root search may call WLD first and exact score second.

For example, WLD may quickly prove win, draw, or loss, and exact score may run
only when UI, review, or engine decision code needs margin.

The first implementation may skip WLD-first orchestration and solve exact score
directly.

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

The first implementation should expose endgame through existing search entry
points.

```cpp
SearchResult search_iterative(board_core::Position position,
                              const Evaluator& evaluator,
                              SearchLimits limits,
                              SearchOptions options);
```

When `options.exact_endgame` is enabled and the empty threshold is met,
`search_iterative` may route to endgame search.

A future direct public API may be added for tools.

```cpp
struct EndgameOptions {
  SolveMode mode = SolveMode::exact_score;
  bool use_tt = false;
  bool use_parity_ordering = true;
  bool use_stability_ordering = true;
  bool use_mobility_ordering = true;
  bool use_specialized_small_empty = true;
  std::uint8_t exact_empties = 10;
  std::uint8_t wld_empties = 12;
};

struct EndgameResult {
  std::optional<board_core::Move> best_move;
  Score score = 0;
  WldResult wld = WldResult::draw;
  BoundType bound = BoundType::exact;
  Line pv{};
  SearchStats stats{};
  NodeCount nodes = 0;
  std::chrono::milliseconds elapsed{0};
  bool exact = true;
  bool stopped = false;
};

EndgameResult solve_endgame(board_core::Position position,
                            SearchLimits limits,
                            EndgameOptions options);
```

A direct public API is optional.

Reusable endgame logic should live in `engine/src/search/`.

Tool entry points should live in `tools/`.

## Internal API Shape

Recommended internal functions:

```cpp
namespace vibe_othello::search::internal {

struct EndgameContext {
  board_core::Position position;
  SearchLimits limits{};
  SearchOptions options{};
  TranspositionTable* transposition_table = nullptr;
  SearchStats stats{};
  std::array<StackFrame, kMaxPly> stack{};
};

SearchValue exact_score_search(EndgameContext* context,
                               Score alpha,
                               Score beta,
                               std::uint8_t empties,
                               Ply ply);

SearchValue wld_search(EndgameContext* context,
                       Score alpha,
                       Score beta,
                       std::uint8_t empties,
                       Ply ply);

SearchResult solve_exact_endgame(board_core::Position position,
                                 SearchLimits limits,
                                 SearchOptions options,
                                 TranspositionTable* tt);

}
```

The internal API may evolve.

The important boundary is semantic, not the exact function names.

Endgame recursive code must not call `Evaluator::evaluate`.

## Generic Exact-Score Algorithm

Generic exact-score search is negamax alpha-beta.

At each node:

* increment `nodes` and `endgame_nodes`
* check cancellation periodically
* return terminal disc difference if terminal
* probe exact endgame TT if enabled
* generate legal moves
* if no legal move exists, apply pass and recurse without reducing empty count
* order legal moves
* apply each move through board-core delta
* recurse with negated window and `empties - 1`
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

The first move ordering policy should be deterministic and simple.

Recommended ordering priority:

1. TT best move
2. previous root best move
3. legal corner moves
4. parity-favorable moves
5. safe edge moves
6. stable-like edge moves
7. moves that minimize opponent mobility
8. moves that avoid dangerous X-squares and C-squares when the corner is empty
9. lower square index as deterministic tie-breaker

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

Recommended first shape:

```cpp
struct EmptyRegion {
  board_core::Bitboard squares = 0;
  std::uint8_t size = 0;
};

struct EmptyRegionMap {
  std::array<EmptyRegion, board_core::kSquareCount> regions{};
  std::uint8_t size = 0;
};
```

Rules:

* compute empty regions from the current position
* record region size parity
* prefer moves in odd regions when that is favorable
* use deterministic tie-breaking
* keep the region-connectivity rule documented and tested

The initial implementation should use one fixed connectivity policy.

If 4-neighbor and 8-neighbor region policies are compared, they must be separate
benchmark options.

Parity ordering must be tested by checking that enabling and disabling it
returns the same exact score.

Benchmark-only counters may track:

* parity maps built
* parity-ordered moves
* odd-region first moves
* parity ordering time

## Corner, Edge, and Stability Ordering

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

Optional exact stability bounds may be added later only if:

* the bound is mathematically exact
* the implementation has a clear proof comment
* differential tests compare against generic exact search
* known tactical edge cases are in the corpus
* the optimization can be disabled

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
* exact-score entries may be converted to WLD only through sign checks
* TT entries store side-to-move-relative scores
* TT depth should mean remaining empties or exact remaining plies, not midgame
  depth
* replacement policy must be deterministic in single-thread mode

A stronger endgame TT should eventually become:

* configurable in size
* shared with search context
* multi-way or replacement-aware
* generation-aware
* benchmarked by hit rate and cutoff rate
* optional for correctness tests

Recommended early replacement policy:

1. empty slot
2. same key and same kind
3. deeper remaining search
4. older generation
5. otherwise replace the current slot only if the new entry is more useful

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

Specialized small-empty routines are optional speed optimizations.

Recommended order:

1. zero empty squares
2. one empty square
3. two empty squares
4. three empty squares
5. four empty squares only after benchmark evidence

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

Small-empty routines may use direct flip computation.

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

* when a midgame leaf reaches depth zero and exact endgame is enabled, call
  exact endgame search instead of heuristic evaluation if the position is under
  the conservative internal threshold
* the initial internal exact-score threshold is capped at four empties, even
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

PVS is a midgame search optimization.

Exact endgame may use alpha-beta directly.

Endgame does not need PVS in the first implementation.

WLD search may use null-window search.

If PVS is added to endgame later:

* it must match generic alpha-beta exact results
* it must be disableable
* it must count re-searches
* it must be benchmarked separately from midgame PVS
* it must not call heuristic evaluation

Do not block the first exact solver on endgame PVS.

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

* fixed empty threshold
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

The first implementation should avoid storing entries after cancellation.

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

The first implementation may return all root moves with exact scores.

That is often useful for UI and review.

Later, `SearchOptions::multi_pv` may restrict how many lines are reported.

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

Recommended public header layout:

```text
engine/include/vibe_othello/search/
  endgame.h
```

The first implementation may skip a public `endgame.h` and route through
`search.h`.

Recommended private implementation layout:

```text
engine/src/search/
  endgame.cc
  endgame_ordering.cc
  endgame_tt.cc
```

The first implementation may put generic endgame search in one file:

```text
engine/src/search/endgame.cc
```

Recommended test layout:

```text
engine/tests/search/
  endgame_test.cc
  endgame_tt_test.cc
  endgame_ordering_test.cc
```

Recommended test-support layout:

```text
engine/test_support/search/
  reference_endgame.h
  reference_endgame.cc
  endgame_positions.h
  endgame_positions.cc
```

Recommended benchmark layout:

```text
engine/benchmarks/
  endgame_bench.cc
```

Recommended corpus layout:

```text
engine/benchmarks/corpora/
  endgame_positions.txt
```

or:

```text
testdata/endgame/
  exact_score.txt
  wld.txt
  pass.txt
  parity.txt
```

Use the repository's existing layout before introducing new top-level
directories.

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
* root-only exact integration vs direct solver
* WLD sign vs exact-score sign

Use property tests for:

* returned best move is legal
* returned PV is replayable
* exact score is unchanged by move ordering policy
* exact score is unchanged by TT enablement
* exact score is unchanged by specialized routine enablement
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

A slow, clear reference endgame solver should live in test support.

It should:

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

Parallel mode may relax node-count determinism.

Deterministic single-thread mode must remain available.

## Thread Safety

Independent endgame contexts may run concurrently.

Per-thread data includes:

* search stack
* move lists
* parity maps
* local stats
* local cancellation counters

Shared data may include:

* transposition table
* stop flag
* root result publication
* task queues

Shared TT writes require synchronization, lock-free-safe writes, or a clearly
documented benign-race policy.

Correctness must not depend on data races.

The first implementation should be single-threaded.

Parallel endgame search should come after single-thread correctness and
benchmarks are stable.

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

When changing endgame behavior, update this document only for intended design or
semantic changes.

Update `docs/progress/endgame.md` for current implementation state, milestones,
temporary gaps, benchmark notes, or rollout status.

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
