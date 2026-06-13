# Search Architecture

## Purpose

Search chooses moves for Othello positions.

It turns board rules and position scores into engine decisions.

Search must be correct, deterministic in single-thread mode, interruptible,
measurable, and strong enough to support play, analysis, review, tools, WASM, and
UI adapters.

## Boundary

Search owns:

* move choice
* root analysis
* fixed-depth search
* iterative deepening
* alpha-beta search
* Principal Variation Search
* null-window search
* transposition tables
* move ordering
* search stack management
* search limits and cancellation checks
* exact endgame solving
* win/loss/draw solving
* principal variation recovery
* root candidate reporting
* search statistics

Search does not own:

* board representation
* legal move rules
* flip calculation
* move application semantics
* move undo semantics
* pass legality
* terminal detection
* serialization formats
* board hash source of truth
* evaluation feature definitions
* evaluation weight learning
* evaluation data generation
* opening book format
* opening book generation
* UI rendering
* WASM memory layout
* game review policy
* blunder thresholds
* perft correctness rules

Search depends on board core.

Search depends on an evaluation interface for heuristic leaf scores.

Search may query an opening book interface, but book data and book building are
outside search.

Board core, evaluation, tools, WASM, and UI must not depend on search internals.

## Core Approach

Use deterministic negamax as the reference search.

Use alpha-beta pruning as the first optimized search.

Use Principal Variation Search for production midgame search after alpha-beta is
validated.

Use iterative deepening at the root.

Use transposition tables for cutoffs, best-move ordering, exact endgame reuse,
and principal variation recovery.

Use exact endgame search when the number of empty squares is small enough.

Use selective pruning only after non-selective search is correct, measurable, and
easy to disable.

Search strength should be built in this order:

1. correct board core
2. reference negamax
3. alpha-beta pruning
4. search stack, limits, and statistics
5. transposition table
6. iterative deepening
7. root move ordering
8. Principal Variation Search
9. exact endgame solving
10. optional calibrated selective pruning
11. optional parallel search

Correctness must be preserved before adding strength optimizations.

Every non-baseline optimization must be disableable for tests.

## Representation Dependency

Search uses board-core positions and moves.

```cpp
namespace vibe_othello::search {

using board_core::Move;
using board_core::MoveDelta;
using board_core::Position;
using board_core::PositionHash;

}
```

Search must not duplicate board rules.

Legal moves come from `board_core::legal_moves`.

Move application comes from `board_core::apply_move` and
`board_core::apply_pass`.

Undo comes from `board_core::undo_move`.

Terminal detection comes from `board_core::is_terminal`.

Position hashing comes from `board_core::hash_position`.

Full board-core hash recomputation remains the source of truth for any
incremental hash state.

Pass undo must be provided by board core, either as `undo_pass` or as a
pass-capable delta type.

Search must not reconstruct pass undo by manually swapping board fields.

## Score Semantics

Scores are signed integers.

Positive score means good for the side to move at the current node.

Negative score means good for the opponent of the side to move at the current
node.

After making a move, child scores are negated.

```cpp
score = -search(child, -beta, -alpha, depth - 1);
```

Search must not mix root-relative scores and side-to-move-relative scores.

Transposition table entries store side-to-move-relative scores.

Root output may convert scores for UI or review adapters.

Exact endgame score is the final disc differential from the side-to-move
perspective.

Heuristic score scale is defined by the evaluation module, not by search.

`kScoreWin` and `kScoreLoss` are search sentinels, not exact Othello disc
differences.

Exact endgame scores must stay in the final disc-difference range.

WLD results are win, draw, or loss outcomes.

WLD results are not exact final disc differences.

Exact score results may be converted to WLD results by checking whether the final
disc difference is positive, zero, or negative.

WLD results must not be converted to exact score results.

## Core Types

Search public types should be small and explicit.

```cpp
namespace vibe_othello::search {

using Score = std::int32_t;
using Depth = std::int16_t;
using Ply = std::uint8_t;
using NodeCount = std::uint64_t;

constexpr Score kScoreInf = 32'000;
constexpr Score kScoreWin = 30'000;
constexpr Score kScoreLoss = -kScoreWin;

struct Line {
  std::array<board_core::Move, 64> moves;
  std::uint8_t size;
};

enum class BoundType : std::uint8_t {
  exact,
  lower,
  upper,
};

enum class SearchMode : std::uint8_t {
  move,
  analyze,
  exact_score,
  win_loss_draw,
};

enum class WldResult : std::int8_t {
  loss = -1,
  draw = 0,
  win = 1,
};

}
```

The public C++ API may use convenience wrappers.

WASM and UI adapters should expose separate flat structures if needed.

## Search Limits

Search limits describe when search must stop.

```cpp
struct SearchLimits {
  Depth max_depth;
  std::uint64_t max_nodes;
  std::chrono::milliseconds max_time;
  bool infinite;
};
```

Rules:

* `max_depth` limits completed iterative-deepening depth
* `max_nodes` limits visited nodes
* `max_time` limits wall-clock time
* `infinite` searches until cancelled

Cancellation must be cooperative.

Search should check limits at root, before starting a new depth, and at periodic
internal nodes.

Search must return the best completed result when interrupted.

Search must never return an illegal move.

## Search Options

Search options control algorithms.

```cpp
struct SearchOptions {
  bool use_midgame_tt;
  bool use_endgame_tt;
  bool use_pv_table;
  bool use_aspiration;
  bool use_pvs;
  bool use_iid;
  bool use_history;
  bool use_killers;
  bool use_exact_endgame;
  bool use_probcut;
  bool use_parallel;
  std::uint8_t multi_pv;
  std::uint8_t endgame_exact_empties;
  std::uint8_t endgame_wld_empties;
  std::uint8_t selectivity_level;
};
```

Every non-baseline option must be independently disableable.

Regression tests should run with all selective options disabled.

Benchmarks should report which options were enabled.

Midgame TT and endgame TT have different semantics.

`use_midgame_tt` controls heuristic and selective midgame entries.

`use_endgame_tt` controls exact endgame score and WLD entries.

`use_endgame_tt` has no effect when exact endgame search is disabled.

Endgame TT entries may only be probed or stored by exact endgame search paths.

`use_pv_table` controls a dedicated PV table, if one is used.

Disabling one table must not silently disable or weaken the semantics of another
table.

## Search Result

Search result is the stable public output.

```cpp
struct RootMoveInfo {
  board_core::Move move;
  Score score;
  BoundType bound;
  Depth depth;
  NodeCount nodes;
  Line pv;
  bool exact;
  bool selective;
};

struct SearchResult {
  std::optional<board_core::Move> best_move;
  Score score;
  BoundType bound;
  Depth completed_depth;
  NodeCount nodes;
  std::chrono::milliseconds elapsed;
  Line pv;
  std::vector<RootMoveInfo> root_moves;
  bool exact;
  bool stopped;
};
```

`best_move` must be legal unless the position is terminal.

`root_moves` must contain only legal moves.

`pv` must be legal when replayed from the root position.

A stopped search may have a lower completed depth than requested.

Terminal positions should return no best move.

## Evaluator Interface

Search depends on an evaluator interface only for heuristic leaf scores.

```cpp
class Evaluator {
 public:
  Score evaluate(const board_core::Position& position) noexcept;
};
```

An incremental evaluator provides explicit scratch-state updates.

```cpp
class IncrementalEvaluator {
 public:
  void reset(const board_core::Position& position) noexcept;
  void apply(const board_core::Position& before, board_core::MoveDelta delta) noexcept;
  void undo(const board_core::Position& after, board_core::MoveDelta delta) noexcept;
  Score evaluate(const board_core::Position& position) noexcept;
};
```

Search owns evaluator scratch state per search context or search thread.

Evaluators must not mutate caller-owned position state.

Evaluation must be deterministic for the same position and same configured
weights.

Evaluation must not allocate in recursive search.

Evaluation must not do file I/O in recursive search.

Search does not define evaluation features, train weights, or produce training
data.

## Move List

Search should expand legal move masks into a fixed-capacity move list.

```cpp
struct MoveList {
  std::array<board_core::Move, 64> moves;
  std::array<Score, 64> ordering_score;
  std::uint8_t size;
};
```

Rules:

* no heap allocation in recursive search
* all moves must be legal
* move list ordering may change
* move list membership must not change during ordering
* deterministic mode must break ties by square index

Move generation is owned by board core.

Move ordering is owned by search.

## Search Stack

Recursive search uses a fixed-size stack.

```cpp
struct StackFrame {
  board_core::Move current_move;
  board_core::MoveDelta delta;
  MoveList moves;
  Line pv;
  board_core::Move killers[2];
  Score static_eval;
  board_core::PositionHash key;
  bool passed;
};
```

The stack has at most one frame per ply.

Search should mutate a single `Position` with apply and undo.

Search must restore the exact position after every recursive call.

Search must restore evaluator incremental state after every recursive call.

Search must restore hash key state after every recursive call if incremental
hashing is used.

Debug builds should assert stack and position consistency.

## Pass Handling

Pass is part of the game tree.

If the side to move has legal moves, search must not generate pass.

If the side to move has no legal moves and the opponent can move, search must
search exactly one pass child.

If neither side can move, search must evaluate the terminal result.

Pass counts as one ply.

Pass must use board-core pass behavior.

Pass undo must use a board-core operation or a board-core delta.

Search must not manually swap `player`, `opponent`, or `side_to_move` to undo a
pass.

Pass must update:

* position perspective
* side to move
* hash key if incremental hashing is used
* evaluator state if needed
* search stack frame

Pass must be undoable without recomputing the whole search path.

## Baseline Negamax

A simple negamax implementation is the reference search.

It should use:

* board-core move generation
* board-core apply and undo
* evaluator leaf scores
* pass handling
* terminal handling

It must not use:

* transposition tables
* selective pruning
* parallelism
* aspiration windows
* Principal Variation Search

Reference negamax exists to validate optimized search.

Optimized search at fixed depth must match reference negamax when selective
pruning is disabled.

## Alpha-Beta Search

Alpha-beta is the first optimized search.

Rules:

* `alpha` is the best lower bound for the side to move
* `beta` is the fail-high threshold
* callers must satisfy `alpha < beta`
* returned scores must be within the valid score range
* cutoffs must not skip required state restoration

Alpha-beta should be tested before PVS.

Alpha-beta with deterministic move ordering must match reference negamax at the
same depth.

## Fail-Soft and Bound Classification

Search should use fail-soft alpha-beta.

A returned score may be outside the original alpha-beta window.

Transposition table storage must classify results using the original search
window:

* `score <= original_alpha` stores an upper bound
* `score >= original_beta` stores a lower bound
* otherwise stores an exact value

Do not classify bounds using a mutated alpha value.

This rule applies to alpha-beta, PVS, null-window search, and endgame search.

## Null-Window Search

Null-window search uses `beta = alpha + 1`.

It answers whether a position can beat a bound.

```cpp
Score search_null_window(SearchContext& context,
                         board_core::Position& position,
                         Score alpha,
                         Depth depth);
```

Null-window search is used by:

* PVS sibling refutation
* transposition-table cutoffs
* endgame WLD search
* optional ProbCut verification searches

Null-window search must share the same make and unmake code path as full-window
search.

## Principal Variation Search

PVS is the production midgame search.

At a PV node:

1. search the first move with the full `alpha..beta` window
2. search later moves with a null window
3. if a later move raises alpha, re-search it with a full window

PVS depends on good move ordering.

PVS must be disableable.

PVS with selective pruning disabled must return the same score as alpha-beta at
the same depth.

## Iterative Deepening

Root search uses iterative deepening.

Rules:

* start at shallow depth
* complete depths in increasing order
* reuse previous best move for ordering
* update the public result only after completing a depth
* stop cleanly when limits are reached

Iterative deepening improves:

* move ordering
* transposition-table quality
* aspiration-window accuracy
* anytime behavior for UI and WASM

The root result should remain valid after every completed depth.

## Aspiration Windows

Aspiration windows use the previous depth score as a guess.

Initial non-trivial depths may search with a full window.

Later depths may search with:

```text
alpha = previous_score - window
beta  = previous_score + window
```

If search fails low, widen downward and re-search.

If search fails high, widen upward and re-search.

Widening must eventually reach a full window.

Aspiration failures must be counted in statistics.

Aspiration must be disableable.

## Principal Variation

Search should maintain a principal variation line.

Rules:

* PV must contain legal moves
* PV must be replayable from the root position
* PV may be recovered from stack state
* PV may be recovered from a dedicated PV table
* TT best moves may help reconstruct PV but must not be trusted blindly

A PV table is optional.

If a PV table is used, it should be separate from the main transposition table.

PV corruption must not affect best move legality.

## Transposition Table

Search owns transposition tables.

Board core owns deterministic position hashing.

A transposition table stores search results for positions.

Only fields compatible with `TTEntryKind` are valid.

For `heuristic_midgame`, `selective_midgame`, and `exact_endgame_score`,
`score` is valid and `wld_result` must be ignored.

For `exact_endgame_wld`, `wld_result` is valid and `score` must be ignored
except when explicitly encoded for internal bound comparison.

Recommended entry shape:

```cpp
enum class TTEntryKind : std::uint8_t {
  heuristic_midgame,
  selective_midgame,
  exact_endgame_score,
  exact_endgame_wld,
};

struct TTEntry {
  board_core::PositionHash key;
  std::uint32_t tag;
  board_core::Move best_move;
  Score score;
  WldResult wld_result;
  Score static_eval;
  Depth depth;
  std::uint8_t empty_count;
  std::uint8_t generation;
  std::uint8_t selectivity;
  BoundType bound;
  TTEntryKind kind;
};
```

Recommended table layout:

* power-of-two bucket count
* 4-way or 8-way set-associative buckets
* cache-line-aware bucket size
* generation aging
* depth-preferred replacement
* optional full-position verification in debug builds

Probe rules:

* key or tag must match
* stored selectivity must be compatible
* entry kind must be compatible with the current search mode
* heuristic midgame entries require stored depth at least the requested depth
* exact endgame score entries are reusable for the same position regardless of
  requested depth
* endgame bound entries require solve mode and remaining-empty semantics
  compatible with the current query
* exact entries may return immediately
* lower-bound entries may cut when `score >= beta`
* upper-bound entries may cut when `score <= alpha`
* best move may be used for ordering even if the bound cannot cut

Store rules:

* exact score stores `exact`
* fail-high stores `lower`
* fail-low stores `upper`
* best legal move should be stored when available
* exact endgame entries must remain distinguishable from heuristic entries
* WLD entries must not be interpreted as exact disc-difference entries
* selective midgame entries must remain distinguishable from exact entries

Selective and exact results must not be mixed.

Heuristic and exact results must not be mixed.

## Endgame TT Probe Rules

Exact endgame TT entries are reusable only when the entry kind matches the
requested solve mode or can safely answer it.

`exact_endgame_score` entries may answer exact score queries directly.

`exact_endgame_score` entries may answer WLD queries by converting final disc
difference to win, draw, or loss.

`exact_endgame_wld` entries must not answer exact score queries.

WLD entries may be used for WLD bounds and move ordering.

Exact score entries have higher replacement priority than WLD entries.

WLD results must never be exposed as exact disc-difference scores.

## Move Ordering

Move ordering is critical for alpha-beta and PVS.

Recommended order:

1. root move order from previous completed depth
2. transposition-table best move
3. principal variation move
4. winning corner moves
5. endgame parity moves
6. cheap Othello-specific ordering features
7. killer moves
8. history heuristic
9. static square ordering
10. remaining moves by deterministic square order

Move ordering must not remove moves.

Move ordering must not add illegal moves.

Ties should be deterministic in single-thread mode.

Othello-specific ordering should be preferred before generic killer and history
heuristics.

Killer and history heuristics are auxiliary ordering signals.

## Othello-Specific Ordering

Othello-specific ordering should use cheap board-derived features.

Useful features include:

* corners
* X-squares near empty corners
* C-squares near empty corners
* edge moves
* opponent mobility after move
* frontier impact
* empty-region parity in the endgame

These features are ordering hints.

They must not change the minimax result when selective pruning is disabled.

Expensive ordering should be used only where it saves more nodes than it costs.

## Exact Endgame Solver

Endgame solver computes exact outcomes.

It should be a separate search path sharing board core, search stack concepts,
and transposition table infrastructure.

Supported modes:

```cpp
enum class SolveMode : std::uint8_t {
  exact_score,
  win_loss_draw,
};
```

Exact score returns final disc differential.

WLD returns whether the side to move can force win, draw, or loss.

WLD does not return the margin of victory.

Exact score and WLD search may share code paths, but their public result types
and transposition table entry kinds must remain distinct.

Endgame solver should use:

* alpha-beta or null-window search
* parity ordering
* transposition tables
* corner and mobility ordering
* specialized routines for small empty counts

Endgame solver must handle pass exactly.

Endgame solver must not call heuristic evaluation at terminal leaves.

Endgame solver may use WLD first and exact score second.

Exact endgame entries must be marked separately from heuristic or selective
midgame entries.

Endgame thresholds must be configurable and benchmarked.

## Specialized Endgame Routines

Specialized routines may replace generic recursion for very small empty counts.

Recommended special cases:

* zero empty squares
* one empty square
* two empty squares
* three empty squares

They must preserve exact score semantics.

They must be tested against the generic solver.

They should be added only after the generic solver is correct.

## Selective Search

Selective search may reduce tree size at the cost of risk.

Selective search is optional advanced work.

Selective search must never be used in exact solving mode.

Selective search must be marked in transposition table entries and result
metadata.

Selective search should be introduced only after:

* fixed-depth search is correct
* evaluation score scale is stable
* regression positions exist
* benchmark counters exist
* engine-vs-engine tests can measure strength

ProbCut and Multi-ProbCut require calibrated error estimates.

ProbCut and Multi-ProbCut are the preferred selective pruning family for
Othello.

They must be implemented as optional, calibrated, null-window verification
searches.

They must not be enabled in exact endgame modes.

The calibration data and tuning process are outside this search architecture
document.

## Time Management

Search must support fixed-depth, fixed-node, fixed-time, and infinite analysis.

Time management owns allocation of time across iterative-deepening depths.

Rules:

* keep the last completed result available
* allow soft and hard deadlines
* allow immediate cancellation
* avoid time checks at every node if they hurt performance

Recommended checks:

* every root move
* before starting a new iterative-deepening depth
* every fixed number of nodes
* every split point if parallel search is enabled

Search must be usable inside a Web Worker.

Time management must not depend on UI frameworks.

## Parallel Search

Parallel search is optional advanced work.

The preferred architecture is Young Brothers Wait Concept.

Rules:

* search the first child before splitting siblings
* split only at sufficiently deep nodes
* split only when enough legal moves remain
* keep per-thread search stacks
* keep per-thread evaluator scratch
* share transposition tables carefully
* aggregate node counts at safe points
* support deterministic single-thread mode

Parallel search may produce different node counts from single-thread search.

Parallel search should return the same best move in deterministic test suites
when selective pruning is disabled, or document why not.

Browser multithreading is an adapter concern.

Search architecture must not require browser threads to be available.

## Root Search

Root search is special.

It must produce stable user-facing information.

Root search owns:

* legal root move list
* root move ordering
* multi-PV handling
* iterative-deepening loop
* aspiration control
* search result publication
* observer callbacks

After each completed depth, root search must publish a coherent result.

Root search must not publish half-applied positions or illegal PVs.

## Multi-PV

Multi-PV returns multiple candidate moves.

Rules:

* all reported moves must be legal root moves
* each line must be replayable
* candidate scores must use the same score scale
* candidate ordering must be deterministic in single-thread mode
* Multi-PV may be slower than single-PV

Multi-PV should be implemented at the root.

It should not complicate internal node search unless necessary.

## Analysis and Review Output

Search provides per-position analysis.

Game review composes many per-position searches.

Search may return:

* best move
* score
* bound type
* completed depth
* exact flag
* selective flag
* node count
* elapsed time
* principal variation
* root candidate list

Search must not decide:

* blunder thresholds
* highlight labels
* human explanations
* UI colors
* chart formatting

Those belong to review and UI layers.

## Statistics

Search statistics are mandatory.

Recommended counters:

* nodes
* leaf nodes
* eval calls
* beta cutoffs
* alpha updates
* TT probes
* TT hits
* TT cutoffs
* TT stores
* TT exact-endgame hits
* TT selective hits
* aspiration fail-high
* aspiration fail-low
* PVS re-searches
* IID calls
* selective pruning attempts
* selective pruning cutoffs
* endgame nodes
* pass nodes
* split points

Stats must be cheap to collect.

Detailed stats may be compiled out or disabled.

Benchmarks should store stats with timing results.

## Directory Layout

Search lives inside the engine static library.

Recommended public header layout:

```text
engine/include/vibe_othello/search/
  score.h
  limits.h
  options.h
  result.h
  evaluator.h
  search.h
  stats.h
```

Recommended private implementation layout:

```text
engine/src/search/
  negamax.cc
  alphabeta.cc
  pvs.cc
  root_search.cc
  aspiration.cc
  move_ordering.cc
  transposition_table.cc
  endgame.cc
  time_manager.cc
  stats.cc
```

Recommended test layout:

```text
engine/tests/search/
  negamax_test.cc
  alphabeta_test.cc
  pvs_test.cc
  transposition_table_test.cc
  move_ordering_test.cc
  aspiration_test.cc
  endgame_test.cc
  time_limit_test.cc
```

Recommended test-support layout:

```text
engine/test_support/search/
  reference_search.h
  reference_search.cc
  search_positions.h
  search_positions.cc
```

Recommended benchmark layout:

```text
engine/benchmarks/
  search_bench.cc
  endgame_bench.cc
```

Tools may provide command-line entry points for solving, comparing, and measuring
positions, but reusable search code belongs in `engine/`.

## Testing Strategy

Search correctness requires multiple test styles.

Use unit tests for:

* score sign convention
* terminal scoring
* pass search
* alpha-beta bounds
* TT bound interpretation
* TT entry-kind compatibility
* TT replacement policy
* move ordering membership
* aspiration widening
* PV replay
* endgame small-empty routines
* time-limit cancellation

Use differential tests to compare:

* negamax vs alpha-beta
* alpha-beta vs PVS
* generic endgame vs specialized endgame
* TT disabled vs TT enabled
* exact endgame TT disabled vs exact endgame TT enabled
* single-thread vs parallel deterministic mode

Use property tests for:

* optimized search equals reference search at small depth
* changing move order does not change score when pruning is exact
* TT hit cannot change exact result
* exact endgame TT cannot change exact result
* apply and undo restore the root position
* returned best move is legal
* returned PV is replayable
* interrupted search returns no move or a legal best-so-far move

Use regression corpora for:

* initial position
* pass positions
* forced pass sequences
* corner tactics
* X-square traps
* edge parity
* low-mobility tactics
* random midgames
* known exact endgames

Use benchmarks for:

* nodes per second
* effective branching factor
* time to fixed depth
* TT hit rate
* TT cutoff rate
* exact endgame TT hit rate
* PVS re-search rate
* move ordering quality
* exact endgame solve time
* multi-PV overhead
* parallel speedup

## Golden Results

Golden search results should include:

* position
* side to move
* depth
* mode
* options
* expected score
* expected best move set
* expected PV if stable

Best move may be a set when multiple moves are equivalent.

PV should be required only when ordering is deterministic and intentionally
stable.

Golden results must record whether selective pruning was enabled.

Exact golden results are stronger than heuristic golden results.

## Benchmark Suites

Benchmarks should use fixed corpora.

Recommended corpora:

* opening positions
* early midgame positions
* late midgame positions
* tactical positions
* pass positions
* exact endgame positions by empty count
* random legal positions

Benchmark output should include:

* compiler
* build type
* CPU
* enabled options
* hash size
* thread count
* depth or time limit
* node count
* elapsed time
* score
* best move

Performance changes without correctness tests are not trustworthy.

Correctness changes without benchmark data are hard to evaluate.

## Determinism

Single-thread search must be deterministic.

Given the same:

* position
* evaluator configuration
* search options
* limits that do not interrupt early
* hash generation policy

Search should return the same result.

Determinism helps:

* tests
* tuning
* regression debugging
* WASM analysis
* game review reproducibility

Parallel mode may have relaxed determinism, but deterministic single-thread mode
must remain available.

## Thread Safety

Independent search contexts may run concurrently.

Shared transposition tables require synchronization or lock-free-safe writes.

Per-thread data includes:

* search stack
* move lists
* evaluator scratch
* local stats

Shared data includes:

* transposition table
* stop flag
* root result publication
* task queues

Hot-path synchronization should be minimized.

Correctness must not depend on data races.

## Error Handling

Recursive search should not throw exceptions.

Hot-path search should avoid allocation failures by preallocating required
memory.

Public entry points may return status objects.

Invalid inputs should be rejected at public boundaries.

Debug builds should assert internal invariants.

Release builds should avoid undefined behavior.

## Performance Principles

Hot-path search functions should be:

* allocation-free
* exception-free
* logging-free
* file-I/O-free
* deterministic in single-thread mode
* explicit about enabled heuristics
* measurable through stats

Prefer simple correct code first.

Optimize after reference tests and benchmarks exist.

Avoid virtual dispatch in deep recursion unless profiling shows it is harmless.

Use board-core primitives rather than duplicating board rules.

## Build Order

Recommended build order:

1. define score semantics
2. define search limits, options, result, and stats types
3. define evaluator interface
4. implement reference negamax
5. implement alpha-beta
6. add search stack and pass handling tests
7. add transposition table
8. add iterative deepening
9. add aspiration windows
10. add PVS and null-window search
11. add root move ordering
12. add TT best-move ordering
13. add Othello-specific ordering
14. add killer and history heuristics
15. add exact endgame solver
16. add exact endgame TT semantics
17. add specialized endgame routines
18. add Multi-PV root search
19. add time management and cancellation
20. add optional selective pruning after calibration
21. add optional parallel search after single-thread search is stable
22. add analysis and review-facing result adapters

Do not add selective pruning before non-selective search is correct.

Do not add parallel search before single-thread search is stable.

Do not use search work as an evaluation-training plan.

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
