# Search Architecture

## Purpose

Search chooses moves for Othello positions.

It turns board rules and position scores into engine decisions.

Search must be correct, deterministic in single-thread mode, interruptible,
measurable, and strong enough to support play, analysis, review, tools, WASM, and
UI adapters.

Implementation status and milestone tracking live in `docs/progress/search.md`.

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
constexpr std::uint8_t kMaxPly = 128;

struct Line {
  std::array<board_core::Move, kMaxPly> moves;
  std::uint8_t size;
};

enum class BoundType : std::uint8_t {
  exact,
  lower,
  upper,
};

enum class ScoreKind : std::uint8_t {
  unavailable,
  heuristic,
  exact_disc_diff,
  win_loss_draw,
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
  const std::atomic_bool* stop_requested;
};
```

Rules:

* `max_depth` limits completed iterative-deepening depth
* `max_nodes` limits visited nodes
* `max_time` limits wall-clock time
* `infinite` searches until cancelled
* `stop_requested` carries future cooperative cancellation requests
* callers that provide `stop_requested` must keep the pointed value alive for
  the whole search call
* callers that provide `stop_requested` must use external synchronization through
  the atomic value when requesting cancellation from another thread

Cancellation must be cooperative.

Search should check limits at root, before starting a new depth, and at periodic
internal nodes.

Wall-clock limits are cooperative, not hard preemption deadlines. Recursive
search may check time periodically to keep the hot path small, so a `max_time`
budget can be exceeded before the next cancellation checkpoint observes it.

Search must return the best completed result when interrupted.

Search must never return an illegal move.

## Search Options

Search options control algorithms.

```cpp
struct MidgameSearchOptions {
  bool use_pvs;
  bool use_aspiration;
  bool use_iid;
  bool use_midgame_tt;
};

struct MoveOrderingOptions {
  bool use_tt_best_move_ordering;
  bool use_history;
  bool use_killers;
  bool use_endgame_parity_ordering;
};

struct EndgameSearchOptions {
  bool exact_endgame;
  bool use_endgame_tt;
  std::uint8_t endgame_exact_empties;
  std::uint8_t endgame_wld_empties;
};

struct SearchReportingOptions {
  std::uint8_t multi_pv;
};

struct ExperimentalSearchOptions {
  bool probcut;
  bool use_pv_table;
  bool use_parallel;
  std::uint8_t selectivity_level;
};

struct SearchOptions {
  MidgameSearchOptions midgame;
  MoveOrderingOptions ordering;
  EndgameSearchOptions endgame;
  SearchReportingOptions reporting;
  ExperimentalSearchOptions experimental;
  SearchMode mode;
};
```

The public API still accepts legacy flat fields during migration:

```cpp
struct SearchOptions {
  bool use_pvs;
  bool use_aspiration;
  bool use_iid;
  bool use_history;
  bool use_killers;
  bool use_midgame_tt;
  bool use_endgame_tt;
  bool exact_endgame;
  bool probcut;
  bool use_pv_table;
  bool use_parallel;
  bool use_tt_best_move_ordering;
  bool use_endgame_parity_ordering;
  std::uint8_t multi_pv;
  std::uint8_t endgame_exact_empties;
  std::uint8_t endgame_wld_empties;
  std::uint8_t selectivity_level;
  MidgameSearchOptions midgame;
  MoveOrderingOptions ordering;
  EndgameSearchOptions endgame;
  SearchReportingOptions reporting;
  ExperimentalSearchOptions experimental;
  SearchMode mode;
};
```

Every non-baseline option must be independently disableable.

Most options default to disabled. `ordering.use_endgame_parity_ordering` is the
compatibility exception and defaults enabled because it is an ordering-only
exact-endgame hint; either legacy or typed spelling can disable it during the
compatibility period. Unimplemented options must be safely ignored or explicitly
treated as disabled.

Search internals normalize public `SearchOptions` into a resolved typed config
before dispatch. During the compatibility period, legacy flat fields and typed
sub-configs express the same behavior. Existing legacy callers keep working,
and new call sites should prefer the typed sub-configs.

The compatibility normalizer has an explicit conflict policy:

| Field kind | Resolution rule |
| --- | --- |
| `mode` | Use `SearchOptions::mode`. |
| Boolean enable flags | Use legacy flat `true` OR typed sub-config `true`. |
| `ordering.use_endgame_parity_ordering` | Use legacy flat value AND typed sub-config value. This option defaults enabled, so either spelling can disable it during the compatibility period. |
| Numeric option fields | Use the legacy flat value when it is nonzero; otherwise use the typed sub-config value. |

The current public `bool` fields cannot distinguish "not specified" from an
explicit `false`. New callers should therefore avoid mixing legacy flat fields
with typed sub-configs. Code inside the search implementation should consume
only `ResolvedSearchOptions` after the API boundary has normalized public
options.

`midgame.use_pvs` switches the recursive full-window search path from plain alpha-beta
to Principal Variation Search. It is disabled by default.

Regression tests should run with all selective options disabled.

Benchmarks should report which options were enabled.

Midgame TT and endgame TT have different semantics.

`midgame.use_midgame_tt` controls midgame transposition-table cutoffs from compatible
stored entries.

`endgame.use_endgame_tt` controls exact endgame score and WLD entries.

`endgame.use_endgame_tt` has no effect in iterative midgame search when `endgame.exact_endgame`
is disabled. Direct endgame solver APIs honor it independently of the root
threshold gate.

`endgame.use_endgame_tt` enables exact-score endgame transposition-table probe, store,
and cutoff behavior when exact endgame search is active. It also enables WLD
endgame transposition-table probe, store, and cutoff behavior when direct or
root-triggered WLD search is active.

Endgame TT entries may only be probed or stored by exact endgame search paths.

`ordering.use_endgame_parity_ordering` controls ordering-only parity hints in exact
endgame search. It must not prune legal moves or change exact results.

`experimental.use_pv_table` controls a dedicated PV table, if one is used.

`ordering.use_tt_best_move_ordering` controls transposition-table best-move ordering.

TT best-move ordering may reorder legal moves.

TT best-move ordering must not perform TT cutoffs by itself.

`midgame.use_midgame_tt` and `ordering.use_tt_best_move_ordering` are independent. Midgame TT
cutoffs may be enabled without TT best-move ordering, and TT best-move ordering
may be enabled without TT cutoffs.

Disabling one table must not silently disable or weaken the semantics of another
table.

`reporting.multi_pv` controls root line reporting only where implemented. For exact
endgame root search, `0` keeps backward-compatible all-root exact reporting, `1`
selects best-only exact reporting, and values greater than one are currently a
safe no-op that behave like `0` until top-N reporting is implemented.

`mode` is the caller's requested search result mode. `SearchMode::move` is the
default. Root-triggered WLD endgame search is used only when `mode` is
`SearchMode::win_loss_draw` and the root empty count is less than or equal to
`endgame.endgame_wld_empties`. Exact-score endgame search remains separate and
returns final disc-difference margins.

## Direct Exact Endgame API

Search exposes an evaluator-free direct exact endgame entry point:

```cpp
SearchResult solve_exact_endgame(board_core::Position position,
                                 SearchLimits limits = {},
                                 SearchOptions options = {});
```

This API starts exact final-disc-difference search directly. It does not use the
`SearchOptions::endgame.exact_endgame` or
`SearchOptions::endgame.endgame_exact_empties` root threshold gate and does not
fall back to evaluator-based midgame search.

Because direct exact solving may be expensive for high-empty positions, callers
should use `SearchLimits` when appropriate. `max_nodes`, `max_time`, and
`stop_requested` are respected with the same cooperative semantics as other
search entry points. `max_depth` is not meaningful for direct exact endgame
solving and is ignored.

`endgame.use_endgame_tt`, `ordering.use_endgame_parity_ordering`, and exact
endgame root reporting options such as `reporting.multi_pv` keep their exact
endgame meanings. Midgame options such as PVS, IID, history, killers, midgame
TT, and selective pruning do not change direct exact result semantics.

## Direct WLD Endgame API

Search exposes an evaluator-free direct WLD endgame entry point:

```cpp
SearchResult solve_wld_endgame(board_core::Position position,
                               SearchLimits limits = {},
                               SearchOptions options = {});
```

This API starts exact win/loss/draw search directly. It does not use the
`SearchOptions::endgame.exact_endgame`,
`SearchOptions::endgame.endgame_exact_empties`, or
`SearchOptions::endgame.endgame_wld_empties` root threshold gates and does not
fall back to evaluator-based midgame search.

The returned `SearchResult::score` and root move scores are WLD values from the
side-to-move perspective: `-1` for loss, `0` for draw, and `1` for win. They are
not final disc-difference margins. `endgame.use_endgame_tt`,
`ordering.use_endgame_parity_ordering`, and root reporting options such as
`reporting.multi_pv` keep their endgame meanings.

`search_iterative` can also route the root to WLD endgame search when
`SearchOptions::mode == SearchMode::win_loss_draw` and the root empty count is
less than or equal to `SearchOptions::endgame_wld_empties`. This path returns
the same WLD score semantics as `solve_wld_endgame`; it does not expose final
disc-difference margins.

## Search Result

Search result is the stable public output.

```cpp
struct RootMoveInfo {
  board_core::Move move;
  Score score;
  ScoreKind score_kind;
  BoundType bound;
  Depth depth;
  NodeCount nodes;
  Line pv;
  bool exact;
  bool selective;
};

struct SearchStats {
  NodeCount nodes;
  NodeCount leaf_nodes;
  NodeCount eval_calls;
  bool incremental_eval_enabled;
  NodeCount incremental_state_initializations;
  NodeCount incremental_eval_calls;
  NodeCount stateless_eval_calls;
  NodeCount incremental_updates;
  NodeCount incremental_touched_instances;
  NodeCount terminal_nodes;
  NodeCount pass_nodes;
  NodeCount beta_cutoffs;
  NodeCount alpha_updates;
  NodeCount root_moves_searched;
  NodeCount tt_probes;
  NodeCount tt_hits;
  NodeCount tt_stores;
  NodeCount tt_cutoffs;
  NodeCount tt_replacements;
  NodeCount tt_bucket_conflicts;
  NodeCount tt_rejected_stores;
  NodeCount tt_invalid_best_move_stores;
  NodeCount pvs_researches;
  NodeCount aspiration_fail_lows;
  NodeCount aspiration_fail_highs;
  NodeCount iid_searches;
  NodeCount endgame_nodes;
  NodeCount selective_cuts;
};

struct SearchResult {
  std::optional<board_core::Move> best_move;
  Score score;
  ScoreKind score_kind;
  BoundType bound;
  Depth completed_depth;
  NodeCount nodes;
  SearchStats stats;
  std::chrono::milliseconds elapsed;
  Line pv;
  std::vector<RootMoveInfo> root_moves;
  bool exact;
  bool stopped;
};
```

`best_move` must be legal unless the position is terminal.

`score_kind` describes how to interpret `score`. `unavailable` means no
completed score is available and consumers must not interpret `score`.
`heuristic` means evaluator score semantics, `exact_disc_diff` means final disc
differential, and `win_loss_draw` means WLD outcome values only: `-1`, `0`, or
`1`.

`SearchResult::score_kind` describes `SearchResult::score`.
`RootMoveInfo::score_kind` describes that root move's `score`.

Direct exact endgame and root-triggered exact endgame results use
`exact_disc_diff`. Direct WLD endgame and root-triggered WLD results use
`win_loss_draw`. Normal fixed-depth and iterative midgame search results use
`heuristic`, even when internal leaf cutover uses exact endgame to avoid
calling the evaluator.

Terminal root results use `exact_disc_diff` because their score is the final
disc differential, including when a stop request interrupts the root before
midgame search publishes any move.

`root_moves` must contain only legal moves.

`root_moves` are reported in search order. Consumers that need a stable display
order should sort by move, score, or their own policy.

`pv` must be legal when replayed from the root position.

A stopped search may have a lower completed depth than requested.

If search is stopped before the first depth completes, `best_move` may be empty
even when the root position is not terminal.

If an endgame search is stopped before publishing any completed root or terminal
score, `SearchResult::score_kind` must be `unavailable`. If a stopped endgame
search publishes a completed partial best score, it keeps that score's
`score_kind`.

Terminal positions should return no best move.

`nodes` must equal `stats.nodes`.

`stats.nodes` counts all visited root, internal, leaf, and terminal nodes.

`stats.leaf_nodes` counts depth-cutoff nodes that call the evaluator.

`stats.eval_calls` counts calls to the evaluator.

`stats.incremental_eval_enabled` reports whether at least one root search bound an
incremental pattern state. Iterative and aspiration searches OR this flag while
aggregating their root attempts.

`stats.incremental_state_initializations` counts incremental state construction
at search roots. `stats.incremental_eval_calls` and
`stats.stateless_eval_calls` partition `stats.eval_calls`; their sum must equal
`stats.eval_calls`.

`stats.incremental_updates` counts incremental state transitions for apply and
undo, including pass transitions. `stats.incremental_touched_instances` sums
the unique pattern instances updated by those transitions; pass transitions
therefore add an update but touch zero instances.

For a `PhaseAwareEvaluator`, a fallback-only search reports incremental
evaluation disabled and stateless calls. A search that can reach a learned
phase binds the incremental state at the root and reports incremental calls,
while individual fallback-only leaves remain stateless calls. Generic and
deliberately wrapped flat-pattern stateless evaluators also report stateless
calls; consumers such as `search_bench` distinguish those using the reported
evaluator mode.

`stats.terminal_nodes` counts terminal nodes that return exact disc difference.

`stats.pass_nodes` counts nodes that expand a pass child.

`stats.beta_cutoffs` counts beta cutoffs.

`stats.alpha_updates` counts alpha-window updates.

`stats.root_moves_searched` counts root candidates actually searched.

`stats.tt_probes` counts transposition table probes.

`stats.tt_hits` counts transposition table probes that match the current position.

`stats.tt_stores` counts transposition table stores.

`stats.tt_cutoffs` counts transposition table cutoffs.

`stats.tt_replacements` counts transposition table stores that replace an occupied
entry with a different key.

`stats.tt_bucket_conflicts` counts transposition table stores whose target bucket
already contains a different occupied key.

`stats.tt_same_key_updates` counts stores that find the same position key and
entry kind already present. `stats.tt_replacements` is reserved for victim
replacement, not same-key refinement.

`stats.tt_probe_slots` is the total bucket slots inspected, allowing callers to
derive average probe slots. `stats.tt_generation_age_hits` counts hits retained
from an older root generation.

`stats.tt_rejected_stores` counts transposition table stores rejected because the
incoming entry is not useful enough for the target bucket.

`stats.tt_invalid_best_move_stores` counts transposition table stores rejected
because the incoming entry has no storable legal normal best move.

`stats.pvs_researches` counts full-window PVS re-searches after null-window
fail-highs.

`stats.aspiration_fail_lows` counts aspiration windows that failed low.

`stats.aspiration_fail_highs` counts aspiration windows that failed high.

`stats.iid_searches` counts internal iterative-deepening searches.

`stats.endgame_nodes` counts nodes visited by exact endgame search paths.

`stats.selective_cuts` counts selective-pruning cuts.

Iterative-deepening stats are the sum of completed depth stats.

## Evaluator Interface

Search depends on an evaluator interface only for heuristic leaf scores.

```cpp
class Evaluator {
 public:
  virtual ~Evaluator() = default;

  virtual Score evaluate(const board_core::Position& position) const noexcept = 0;
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

Search-owned mutable state should be gathered in a search context instead of
threading every dependency through recursive calls.

```cpp
struct SearchContext {
  board_core::Position position;
  const Evaluator& evaluator;
  SearchStats stats;
  SearchLimits limits;
  SearchOptions options;
  TranspositionTable* transposition_table;
  std::array<StackFrame, kMaxPly> stack;
};
```

The context owns the single mutable position used during search, the current
statistics, configured limits and options, optional search tables, and per-ply
scratch frames.

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

Initial stack-frame implementations may leave future fields such as hash keys,
killer moves, history-related scratch, or static eval unused until the
corresponding feature is added.

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

Null-window search uses the narrow alpha-beta window `alpha = beta - 1`,
`beta = beta`.

It answers whether a position can beat a bound.

```cpp
SearchValue null_window_search(SearchContext* context,
                               Score beta,
                               Depth depth,
                               Ply ply);
```

Null-window search is an internal primitive for PVS and future scout searches.

It may also be used by:

* PVS sibling refutation
* transposition-table cutoffs
* endgame WLD search
* optional ProbCut verification searches

Null-window search must share the same make and unmake code path as full-window
search.

The production search path calls the null-window primitive when PVS or another
scout-style search is implemented and enabled.

## Principal Variation Search

PVS is an optional production midgame search.

At a PV node:

1. search the first move with the full `alpha..beta` window
2. search later moves with a null window
3. if a later move raises alpha, re-search it with a full window

PVS depends on good move ordering.

PVS must be disableable.

Default search uses alpha-beta unless `SearchOptions::use_pvs` is enabled.

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

Aspiration is an optional iterative-deepening wrapper controlled by
`SearchOptions::use_aspiration`.

Aspiration is disabled by default.

Fixed-depth search always uses the normal full-window root search.

Depth 1 of iterative deepening always uses a full window.

Recursive searches inside an aspiration window must go through
`full_window_search()` so `SearchOptions::use_pvs` continues to dispatch between
alpha-beta and PVS.

Initial non-trivial depths may search with a full window.

Later depths may search with:

```text
alpha = previous_score - window
beta  = previous_score + window
```

If search fails low, widen downward and re-search.

If search fails high, widen upward and re-search.

Widening must eventually reach a full window.

The result of the first search whose root score is inside the window is the
completed depth result. Search must not unconditionally discard a successful
aspiration result and run a second full-window depth search for publication.

If widening reaches the full score range, that full-range result is published.

Fail-low and fail-high attempts may contain bounded root move information. The
published result must be either an exact in-window result or the full-range
result. Published root move scores and bounds must be semantically consistent;
if bounded root move entries from the successful aspiration attempt would be
reported, they should be completed with exact root move searches rather than
re-running the whole depth unconditionally.

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

The current implementation stores `key`, `depth`, `score`, `bound`,
`best_move`, `has_best_move`, `generation`, `kind`, and `occupied`.

The midgame table uses a search-internal default capacity. Public search
defaults do not enable TT cutoff or TT ordering, and no public capacity option is
currently exposed.

The table is organized as power-of-two 4-way buckets. Construction may allocate
the bucket vector; recursive probe and store operations must not allocate.
Requested capacities are rounded up to buckets and capped at the internal
maximum bucket count to avoid power-of-two rounding overflow. Public capacity
options must keep equivalent validation before exposing this constructor path.

Iterative deepening advances the table generation before starting each new
depth after depth 1. Aspiration re-searches at the same depth stay in the same
generation.

Replacement policy is intentionally simple:

* update a same-key entry in place
* use an empty bucket slot before overwriting
* prefer replacing old-generation entries
* otherwise prefer replacing shallower current-generation entries
* reject a store only when every bucket entry is current-generation and deeper
  than the incoming entry

When `use_tt_best_move_ordering` is enabled, a matching entry's `best_move` may
be used as an ordering hint.

When `use_midgame_tt` is enabled, compatible midgame entries may cut off search.
Midgame cutoff requires `entry.kind == TTEntryKind::midgame` and
`entry.depth >= current depth`.

TT entries store side-to-move-relative scores.

The current implementation uses a simplified `midgame` entry kind. Future
selective TT support may split it into separate `heuristic_midgame` and
`selective_midgame` kinds if selective results need different compatibility
rules.

For `midgame`, `exact_endgame_score`, and `exact_endgame_wld`, `score` is valid
within that entry kind's score semantics. WLD-specific scores are kept distinct
from exact disc-difference scores. WLD entries must never satisfy exact-score
probes.

Current internal entry shape:

```cpp
enum class TTEntryKind : std::uint8_t {
  midgame,
  exact_endgame_score,
  exact_endgame_wld,
};

struct TTEntry {
  board_core::PositionHash key;
  Depth depth;
  Score score;
  BoundType bound;
  board_core::Move best_move;
  bool has_best_move;
  std::uint8_t generation;
  TTEntryKind kind;
  bool occupied;
};
```

Future selective search, WLD solving, or cache-layout tuning may add fields such
as a short hash tag, WLD result payload, static evaluation, empty count, or
selectivity marker. Adding those fields must preserve the mode-compatibility
rules in this document.

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

The current WLD implementation keeps exact-score and WLD probes fully separate
and does not perform exact-score-to-WLD conversion at probe time.

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

Detailed endgame design lives in `docs/architecture/endgame.md`.

This section records the search-level integration boundary.

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
  move_ordering/
    move_list.cc
    static_othello_ordering.cc
    mobility_ordering.cc
    parity_ordering.cc
    midgame_ordering.cc
    endgame_ordering.cc
  transposition_table.cc
  endgame_policy_internal.h
  endgame_tt.cc
  endgame_small_empty.cc
  endgame_search.cc
  endgame_root.cc
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
engine/tests/support/search/
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

## Reusable Search Sessions

`SearchSession` owns mutable single-thread search knowledge: the transposition
table, history and killer ordering state, root generation, and reusable stack
state where applicable. Compatibility entry points construct a temporary
session. Session overloads let a caller retain knowledge across sequential
moves without putting mutable hash state in `board_core::Position`.

Session policy is explicit:

* `start_new_game`, `reset`, and `clear` deterministically clear TT, generation,
  history, and killers;
* sequential moves in one game may retain the session;
* unrelated analysis roots should use `prepare_analysis(clear)` unless
  cross-root reuse is intentional;
* one session is single-thread-only and must not serve concurrent searches.

Every root binds the session TT to the evaluator object identity, the
evaluator's `transposition_table_revision()`, normalized pass/endgame/mode and
experimental semantics, and the direct-search domain. A binding change clears
the TT before probing. Mutable evaluators must increment their revision whenever
their scoring behavior changes in place. Ordering state is safe to retain across
an automatic semantic rebind because it does not supply cutoff values.

The TT accepts an entry or byte budget. Zero disables it. Allocation reports
requested and actual bytes, entries, power-of-two buckets, entry size, enabled
state, and allocation success. Native and WASM callers choose their own byte
budgets; engine core does not impose one platform-wide size.

Production probes use an already-maintained position key plus `TTEntryKind`.
Search computes the full absolute-color hash once at a root, applies Zobrist
deltas for normal moves, multiple flips, side-to-move changes, and passes, and
restores the previous key on undo. Full `hash_position` remains the source of
truth for tests.

Normal node preparation computes the current legal mask once. Only a zero mask
causes one opponent-mask computation to distinguish pass from terminal. Move
ordering consumes the prepared mask. Midgame pass depth is controlled by
`MidgameSearchOptions::pass_consumes_depth`; the compatibility default remains
`true`. Exact endgame empties never decrease on pass.

Root PVS searches the first move with a full window, later moves with a null
window, and performs a full re-search only for a score strictly between alpha
and beta. `multi_pv != 1` preserves exact per-move reports by re-searching only
root reports that remain bounded.

`ExperimentalSearchOptions::use_legacy_search_kernel` is the temporary rollback
switch for the previous full-window root orchestration. It keeps the shared
correctness, typed-TT, and hash infrastructure but disables root PVS and the new
unconditional root-alpha carry. It is intended for A/B diagnosis and later
removal, not as a second permanent search implementation.
