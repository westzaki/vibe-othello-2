# Search Architecture

## Purpose

Search chooses moves for Othello positions.

It turns board rules and position scores into engine decisions.

Search must be correct, deterministic in single-thread mode, interruptible,
measurable, and strong enough to support play, analysis, review, tools, WASM, and
UI adapters.

## Implemented System

The public surface is defined by `score.h`, `evaluator.h`, `probcut.h`,
`shadow_calibration.h`, `result.h`, `search_session.h`, and `search.h`.
Production search currently includes:

* fixed-depth alpha-beta and optional PVS
* iterative deepening with optional aspiration windows and publication of the
  last completed depth when stopped
* deterministic root search, PV propagation, and root-move reports
* TT, history, killer, Othello-static, mobility, and IID ordering paths
* a four-way bucketed, entry- or byte-budgeted transposition table
* caller-owned reusable `SearchSession` state with semantic TT invalidation
* root-once hashing with incremental move/pass updates
* fixed-node, fixed-time, depth, infinite, and external-stop limits
* exact-score and WLD endgame entry points and configured handoff
* exact-score stability bounds and zero-to-eight-empty specialized recursion
* incremental built-in pattern evaluation where the root can reach learned
  phases
* diagnostics-only shadow calibration, disabled by default, plus a fail-closed
  reviewed-profile-gated cut-high Multi-ProbCut production selector

The recursive implementation is split by responsibility under
`engine/src/search/`; reference search lives only in test support. Search is
single-threaded. It has fixed-position, reference differential, option/limit,
TT/session, PVS, endgame, ProbCut, and shadow-calibration tests plus local
search and endgame benchmarks.

## Current Limitations

The following are deliberately not represented as completed capabilities:

* `reporting.multi_pv > 1` does not limit output to top N; exact root search
  treats it as all-root reporting
* time limits are cooperative and can overshoot before the next check
* there is no dedicated PV table, advanced clock allocation, parallel search,
  or review-specific result adapter
* ProbCut has no cut-low path; the built-in speed-gated production profile is
  intentionally narrow and applies only to one exact evaluator/artifact identity
* learned-artifact fixed-position smoke coverage is not match, self-play, Elo,
  or production-strength evidence

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
* `stop_requested` carries cooperative cancellation requests
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
  bool pass_consumes_depth;
};

struct MoveOrderingOptions {
  bool use_tt_best_move_ordering;
  bool use_history;
  bool use_killers;
  bool use_midgame_mobility_ordering;
  bool use_endgame_parity_ordering;
};

struct EndgameSearchOptions {
  bool exact_endgame;
  bool use_endgame_tt;
  bool use_endgame_pvs;
  std::uint8_t endgame_exact_empties;
  std::uint8_t endgame_wld_empties;
  EndgameStabilityMode stability_mode;
};

struct SearchReportingOptions {
  std::uint8_t multi_pv;
};

struct SelectiveSearchOptionsV1 {
  bool enable_shadow_calibration;
  std::uint32_t sample_rate;
  std::uint32_t max_samples_per_search;
  std::span<const ShadowCalibrationDepthPairV1> ordered_depth_pairs;
  bool include_pv_nodes;
  bool include_pass_nodes;
  bool include_near_exact_nodes;
  std::uint64_t sampling_seed;
  std::string_view repo_sha;
  std::string_view search_config_id;
  std::string_view evaluator_id;
  std::string_view artifact_id;
  ShadowCalibrationSink* sink;
};

struct ProbCutOptionsV1 {
  bool use_probcut;
  Depth minimum_depth;
  Depth shallow_depth_reduction;
  std::uint8_t maximum_probes_per_node;
  std::span<const ProbCutDepthPairV1> ordered_depth_pairs;
  bool stop_after_first_success;
  double confidence_multiplier;
  double minimum_confidence;
  Score minimum_margin;
  Score maximum_margin;
  double maximum_shallow_overhead_ratio;
  NodeCount minimum_official_nodes_before_probe;
  std::uint16_t enabled_phase_mask;
  bool non_pv_only;
  bool beta_only;
  bool disable_near_exact;
  std::uint8_t near_exact_disable_empties;
  bool shadow_verify;
  std::string_view evaluator_family;
  std::string_view artifact_family;
  std::string_view calibration_profile_id;
  const ProbCutCalibrationProfileV1* calibration_profile;
};

struct SearchOptions {
  MidgameSearchOptions midgame;
  MoveOrderingOptions ordering;
  EndgameSearchOptions endgame;
  SearchReportingOptions reporting;
  SelectiveSearchOptionsV1 selective;
  ProbCutOptionsV1 probcut_options;
  SearchMode mode;
};
```

`SelectiveSearchOptionsV1` is diagnostics-only and defaults to disabled.
`sample_rate` is an integer millionth rate in `[0, 1'000'000]`; integer
sampling avoids platform-dependent floating-point decisions. Enabling requires
a non-null caller-owned sink, positive rate and cap, and a non-empty unique list
of valid deep/shallow pairs. Pair storage, metadata strings, and the sink must
outlive the search call. Normal runtime and WASM presets keep the whole config
at its disabled default.

`ProbCutOptionsV1` is the typed runtime option and defaults disabled. Runtime
resolution enables it only when its caller-owned profile is complete and internally valid,
the profile ID matches, both report checksums are lowercase SHA-256 values, the
profile node class is exactly `non_pv_scout_beta_only`, calibration entries are
finite and non-overlapping, every profile carries a complete unique validated
pair order plus internally consistent scheduler/domain evidence, the caller's
pair selection is a reviewed prefix, the probe count is within the reviewed
maximum, every domain enabled by that exact prefix/probe combination has
passing holdout evidence, the caller's evaluator/artifact family exactly matches the profile,
all conservative scope flags remain true, and margins are ordered. Invalid or
incomplete configuration resolves to disabled; it never invents a
coefficient or fallback margin. Profile storage, entries, and strings must
outlive the search call.

`probcut_configuration_is_reviewed()` exposes the exact prefix/probe/domain
evidence check, while `resolve_probcut_configuration()` applies the complete
runtime normalization contract and returns an effective disabled-or-enabled
configuration. Engine normalization, Arena, and search benchmarks use this
shared resolver so measurement tooling cannot label a raw request as active
when search would normalize it to off.

Every non-baseline option must be independently disableable.

Most options default to disabled. `ordering.use_endgame_parity_ordering` and
`endgame.use_endgame_pvs` default enabled; parity is an ordering-only hint and
endgame PVS is an exact null-window optimization that can be disabled for
differential measurements.
`endgame.stability_mode` defaults to `EndgameStabilityMode::cutoff`; callers can
select `shadow` to collect and verify hypothetical bound cuts without changing
the searched result, or `off` for a strict no-probe baseline.

The typed sub-configs in `SearchOptions` are the only configuration source of
truth. Search internals resolve public options once at the API boundary, validate
typed ProbCut configuration, and consume only `ResolvedSearchOptions` after that
boundary.

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
root-triggered WLD search is active. Both paths intentionally bypass TT probe
and store work below five remaining empty squares, where the shallow search
cost is lower than the table overhead.

`endgame.use_endgame_pvs` enables exact-score endgame Principal Variation
Search at nodes with at least nine remaining empty squares. It does not affect
WLD search. A scout result strictly inside the full window is re-searched and
counted in `stats.pvs_researches`.

Endgame TT entries may only be probed or stored by exact endgame search paths.

`ordering.use_endgame_parity_ordering` controls ordering-only parity hints in exact
endgame search. It must not prune legal moves or change exact results.

`endgame.stability_mode` controls conservative stable-disc bounds in exact-score
endgame search. `shadow` records and verifies candidates while continuing the
normal search. `cutoff` may return only a mathematically proven final-margin
lower or upper bound. WLD search ignores this option.

`ordering.use_tt_best_move_ordering` controls transposition-table best-move ordering.

TT best-move ordering may reorder legal moves.

TT best-move ordering must not perform TT cutoffs by itself.

`midgame.use_midgame_tt` and `ordering.use_tt_best_move_ordering` are independent. Midgame TT
cutoffs may be enabled without TT best-move ordering, and TT best-move ordering
may be enabled without TT cutoffs.

`ordering.use_midgame_mobility_ordering` enables opponent-mobility scoring at
internal midgame nodes with at least five plies remaining. Shallower nodes skip
the extra make/move-generation work because measured node savings do not repay
its per-node cost there. Root ordering keeps its existing mobility hint
independently of this option.

Disabling one table must not silently disable or weaken the semantics of another
table.

`reporting.multi_pv` controls root line reporting. For exact endgame root search,
`0` selects all-root exact reporting, `1` selects best-only exact reporting, and
values greater than one currently select all-root reporting because top-N
reporting is not implemented. For heuristic midgame search, `1` permits
bounded sibling reports while preserving an exact best result; other values
complete bounded siblings for all-root reporting.

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

`endgame.use_endgame_tt`, `endgame.use_endgame_pvs`, `endgame.stability_mode`,
`ordering.use_endgame_parity_ordering`, and exact endgame root reporting
options such as `reporting.multi_pv` keep their exact endgame meanings.
Midgame options such as PVS, IID, history, killers, midgame TT, and selective
pruning do not change direct exact result semantics.

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
less than or equal to `SearchOptions::endgame.endgame_wld_empties`. This path returns
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
  NodeCount endgame_last_flip_solved;
  NodeCount endgame_stability_probes;
  NodeCount endgame_stability_lower_candidates;
  NodeCount endgame_stability_upper_candidates;
  NodeCount endgame_stability_cutoffs;
  NodeCount endgame_stability_shadow_verifications;
  NodeCount endgame_stability_shadow_false_cutoffs;
  NodeCount selective_cuts;
};

struct ShadowCalibrationStats {
  NodeCount shadow_candidates;
  NodeCount shadow_samples;
  NodeCount shadow_shallow_nodes;
  NodeCount shadow_deep_verification_nodes;
  NodeCount shadow_best_move_agreements;
  NodeCount hypothetical_cut_highs;
  NodeCount hypothetical_cut_lows;
  NodeCount false_cut_high_candidates;
  NodeCount false_cut_low_candidates;
};

struct SearchResult {
  std::optional<board_core::Move> best_move;
  Score score;
  ScoreKind score_kind;
  BoundType bound;
  Depth completed_depth;
  NodeCount nodes;
  SearchStats stats;
  ShadowCalibrationStats shadow_calibration;
  std::chrono::milliseconds elapsed;
  Line pv;
  std::vector<RootMoveInfo> root_moves;
  bool exact;
  bool stopped;
};
```

`SearchStats` and `SearchResult::nodes` account only for official search.
Shadow candidates, emitted samples, and shallow/deep verification nodes live exclusively
in `SearchResult::shadow_calibration`. This separation is intentional:
collecting calibration data must not change official node-limit accounting,
TT statistics, result bounds, PVs, or completed depth.

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

`stats.endgame_last_flip_solved` counts one-empty exact-score nodes completed by
direct flip counting. Stability counters separate proof probes, lower/upper
candidates, real cutoffs, shadow verifications, and shadow false-cut findings.

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

The public evaluator interface remains stateless. At root initialization,
search recognizes the built-in `PatternEvaluator` and `PhaseAwareEvaluator`
and may create their concrete `IncrementalState`. Other evaluator types use the
virtual stateless path. This optimization is internal: third-party evaluators
do not need to implement a second public interface.

Search owns the incremental evaluator state inside its search context and must
apply and undo it in lockstep with the position.

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
  std::uint8_t size;
};
```

Rules:

* no heap allocation in recursive search
* only the `[0, size)` prefix is initialized, copied, or inspected
* all moves must be legal
* move list ordering may change
* move list membership must not change during ordering
* deterministic mode must break ties by square index

Move generation is owned by board core.

Move ordering is owned by search.

The production implementation expands only set bits from the legal mask and
sorts the initialized prefix in place. Ordering scores are short-lived local
scratch indexed by list position rather than persistent per-node state. The
sort packs signed score order and the deterministic square tie-break into one
integer key, so each insertion shifts one scalar.

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

Recursive node results carry only score, completion, and selectivity metadata.
The per-ply frame owns the PV scratch line, and a parent prepends a move only
when that child becomes the best line. This avoids copying a maximum-length
`Line` on every recursive return while keeping root publication independent of
the transposition table.

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

Default search uses alpha-beta unless `SearchOptions::midgame.use_pvs` is enabled.

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
`SearchOptions::midgame.use_aspiration`.

Aspiration is disabled by default.

Fixed-depth search always uses the normal full-window root search.

Depth 1 of iterative deepening always uses a full window.

Recursive searches inside an aspiration window must go through
`full_window_search()` so `SearchOptions::midgame.use_pvs` continues to dispatch between
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
result. Published root move scores and bounds must be semantically consistent.
Single-PV reporting may retain bounded sibling entries, matching normal root
PVS behavior; all-root reporting completes bounded entries with exact root move
searches rather than re-running the whole depth unconditionally.

Aspiration failures must be counted in statistics.

Aspiration must be disableable.

The production iterative search starts with a symmetric 8-point window around
the previous completed score and doubles only the failed side until the result
fits or the full score range is reached.

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
`best_move`, `has_best_move`, `generation`, `kind`, `selective`, and
`occupied`.

Temporary-session entry points use the public default entry capacity. Callers
that own a `SearchSession` select an entry or byte capacity and can inspect the
requested/actual allocation. Search options still control TT cutoffs and
best-move ordering independently.

The table is organized as power-of-two 4-way buckets. Construction may allocate
the bucket vector; recursive probe and store operations must not allocate.
Requested capacities are rounded up to buckets and capped at the internal
maximum bucket count to avoid power-of-two rounding overflow. Allocation
failure produces a disabled table and an auditable allocation result.

Iterative deepening advances the table generation before starting each new
depth after depth 1. Aspiration re-searches at the same depth stay in the same
generation.

Replacement policy is intentionally simple:

* update a same-key entry in place
* use an empty bucket slot before overwriting
* prefer replacing old-generation entries
* otherwise prefer replacing shallower current-generation entries
* among current-generation entries, prefer replacing shallower or weaker-bound
  victims and reject a less useful incoming store

When `use_tt_best_move_ordering` is enabled, a matching entry's `best_move` may
be used as an ordering hint.

When `use_midgame_tt` is enabled, compatible midgame entries may cut off search.
Midgame cutoff requires `entry.kind == TTEntryKind::midgame` and
`entry.depth >= current depth`.

TT entries store side-to-move-relative scores.

The current implementation uses a simplified `midgame` entry kind. A selective
bit distinguishes a ProbCut lower bound from a non-selective bound. A selective
TT cutoff propagates selective provenance through its `SearchNodeResult`, so
only that result's ancestors cannot store a derived exact bound. Unrelated
sibling subtrees continue normal TT storage. A future layout may split this
into separate
`heuristic_midgame` and `selective_midgame` kinds.

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
  bool selective;
  bool occupied;
};
```

Future cache-layout tuning may add a short hash tag, static evaluation, or
debug verification data. Adding fields must preserve the mode-compatibility
rules in this document.

Implemented table layout:

* power-of-two bucket count
* 4-way set-associative buckets
* generation aging
* depth-preferred replacement

Probe rules:

* key must match
* stored selectivity must be compatible
* entry kind must be compatible with the current search mode
* heuristic midgame entries require stored depth at least the requested depth
* endgame entries require a matching solve kind and stored remaining-empty depth
  at least as large as the current remaining-empty request
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

WLD entries may be used for WLD bounds and move ordering. Entry kind does not
grant replacement priority; the shared replacement policy uses generation,
depth, and bound quality.

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

It is a separate search path sharing board core, incremental position state,
limit state, result builders, and session TT infrastructure.

Exact score returns final disc differential.

WLD returns whether the side to move can force win, draw, or loss.

WLD does not return the margin of victory.

Exact score and WLD search may share code paths, but their public result types
and transposition table entry kinds must remain distinct.

Endgame solver uses:

* alpha-beta or null-window search
* parity ordering
* transposition tables
* corner and mobility ordering
* specialized routines for small empty counts

Endgame solver must handle pass exactly.

Endgame solver must not call heuristic evaluation at terminal leaves.

WLD and exact-score requests are independent; the engine does not implicitly
run one and then the other.

Exact endgame entries must be marked separately from heuristic or selective
midgame entries.

Endgame thresholds must be configurable and benchmarked.

## Specialized Endgame Routines

Specialized exact-score routines replace generic recursion for zero through
eight empty squares. The one-empty path counts the last move's flips directly;
five-to-eight-empty search avoids generic mobility scoring and can partition
branches by odd/even empty-region parity.

They must preserve exact score semantics.

They must be tested against the generic solver.

WLD currently keeps the generic recursion.

## Selective Search

Selective search may reduce tree size at the cost of risk and is optional.

Selective search must never be used in exact solving mode.

Selective search must be marked in transposition table entries and result
metadata.

Default enablement and production-strength claims for selective search should be
introduced only after:

* fixed-depth search is correct
* evaluation score scale is stable
* regression positions exist
* benchmark counters exist
* engine-vs-engine tests can measure strength

ProbCut and Multi-ProbCut require calibrated error estimates.

ProbCut and Multi-ProbCut are the preferred selective pruning family for
Othello.

Runtime supports a bounded Multi-ProbCut cut-high scheduler. Cut-low remains
deferred because it requires a separate calibration population, confidence
decision, and telemetry.

They must not be enabled in exact endgame modes.

### Speed-gated non-PV Multi-ProbCut

Generic runtime ProbCut options remain off by default; their default depths and
margins are zero/invalid as an additional guard. The production selector owns
one checked-in reviewed profile and resolves it only for the exact evaluator,
artifact, weights checksum, score scale, trained-phase mask, fallback-additive
phase boundary, move-search mode, and exact-handoff threshold. It is
attempted only at an explicit PVS scout/null-window entry marked
as cut-node-equivalent. Plain PV/full-window nodes, recursive alpha-beta nodes
without that marker, IID work, root/terminal/pass nodes,
depths below `minimum_depth`, disabled phases, unsupported complete profile
domains, sentinel-adjacent windows, and positions at or below the configured
near-exact threshold cannot cut.
Only beta-direction cut-high is implemented. The WASM `normal` and `hard`
presets use the production selector; `easy`, the legacy API, identity mismatch,
and the build-time kill switch leave `probcut_options` disabled. The selected
production profile tries threshold-directed `7:3` and then `7:4` probes in its
reviewed phase 2, 3, 4, 6, 7, 9, and 10 domains, has no cold-start delay, and
caps cumulative shallow work at 20%. The `7:4` probe runs only after `7:3`
rejects and only where its exact scheduler domain has passing evidence.
Promotion additionally requires phase-balanced WASM comparisons against a
kill-switch build. The original fixed-depth gate requires at least 1%
aggregate node reduction, at least 1% median wall-time reduction, and exact
best-move, score, and completed-depth parity. When production limits or TT
capacity change, an extended fixed-depth gate must cover every depth observed
under the new time budget, and a time-bounded gate must verify best-move and
score parity, equal-depth output parity, and non-regression in median and
aggregate completed depth.

Each `ProbCutCalibrationProfileV1` identifies its schema version, profile ID,
source calibration report SHA-256, independent joint holdout SHA-256, evaluator
family, artifact family, `node_class = non_pv_scout_beta_only`,
`validated_pair_order`, and the validated maximum probes per node. The full
scheduler retains aggregate joint evidence, while scheduler evidence records
holdout nodes, false cuts, cut candidates, and the 95% upper bound separately
for each pair-prefix length, probe cap, and exact profile domain. Runtime
requires this node class to match the explicit PVS scout role;
post-result `cut` groups are not an adoption population. Entries are keyed by
phase, search mode, inclusive empties range, deep/shallow pair, exact-handoff
enabled state and threshold, and inclusive exact-handoff-distance range. They also contain slope
`a`, intercept `b`, residual sigma, audited confidence multiplier, and inclusive
shallow-score/beta validity ranges. The caller supplies the evaluator and
artifact family plus its score scale, trained-phase mask, and optional
fallback-additive boundary actually in use; every field must exactly match the
reviewed profile.
Missing or ambiguous domains are rejected. Adjacent phase, empties, depth,
handoff, evaluator, and artifact profiles are never extrapolated.
Runtime pair options must be an identical prefix of `validated_pair_order`, and
the requested probe count must not exceed the reviewed maximum or prefix
length. It is enabled only when that exact prefix/probe combination has a
passing evidence record for every domain reached by its selected entries. A
reordered or suffix-only selection, unsafe prefix, or unobserved domain disables
MPC during normalization. Thus a full scheduler may be accepted while a
statistically unsafe single-pair prefix remains unavailable.
Callers that produce comparative evidence must reject a requested non-off mode
when `resolve_probcut_configuration()` returns disabled; silently continuing as
an off-policy run is not a valid comparison.

At a node, the scheduler walks the configured pair preference, considering
only pairs whose deep depth exactly equals the current depth. It stops at the
first successful cut and never runs more than `maximum_probes_per_node`.
`enabled_phase_mask`, `minimum_depth`, `minimum_confidence`,
`near_exact_disable_empties`, `maximum_shallow_overhead_ratio`, and
`minimum_official_nodes_before_probe` provide additional gates. The overhead
ratio is cumulative shallow nodes divided by non-shallow official nodes; zero
disables only this ratio gate. The official-node minimum prevents the first
probe from escaping the cumulative budget before enough current-depth work is
available to amortize it. The current V1 contract requires
`stop_after_first_success=true`.

For an eligible node, runtime first inverts the reviewed regression condition
to find the smallest integer shallow score that can cut:

```text
k_effective = max(profile_k, option_k, minimum_confidence)
margin = max(ceil(k_effective * residual_sigma), minimum_margin)
shallow_beta = max(minimum_shallow_score,
                   ceil((beta + margin - intercept) / slope))
probe [shallow_beta - 1, shallow_beta]
cut-high iff the shallow probe fails high
```

`maximum_margin` is an eligibility ceiling: a larger computed margin rejects
the candidate instead of clamping it downward. A threshold above the reviewed
maximum shallow score is also rejected. All floating inputs must be finite.
The implementation computes the threshold and margin in wider types, checks
conversion ranges before integer conversion, and keeps both sides of the
shallow null window away from `kScoreLoss`/`kScoreWin`. This is algebraically
equivalent to `floor(slope * s + intercept) - margin >= beta` for an exact
integer shallow score, but it avoids paying for an exact full-window result.
Terminal exact disc differences never reach the gate, and the shallow search
temporarily disables exact handoff, so the fitted heuristic domain is not mixed
with exact score kinds.

The reviewed shallow-score maximum limits the derived threshold, not the
eventual fail-high value. If `shallow_beta` is eligible, any result at or above
that threshold cuts, including an exact shallow value above the reviewed
maximum. Holdout conversion replays this threshold-directed null-window
decision exactly when counting candidates and false cuts.

The shallow search shares the official stop flag, deadline, node limit, node
counter, and evaluator state. Its nodes therefore contribute to
`SearchResult::nodes`; `probcut_shallow_nodes` is a subset for overhead review.
Nested ProbCut, IID, and MPC shadow collection are disabled during that shallow
search. The official TT and shared killer/history state are detached, so a
shallow exact-looking value cannot enter reusable state or perturb official
ordering. A stopped shallow search always propagates stop and never cuts.

A successful real cut returns exactly `beta` with an empty continuation: it is
a heuristic lower bound, not an exact value or invented PV. The TT may store
that lower bound with `selective=true` and no fabricated best move. Once a real
or TT-reused selective cutoff contributes to a subtree, exact midgame stores at
its ancestors and root are suppressed. Root move metadata stays heuristic,
non-exact, bounded, and marked selective where the cut occurred.

With `shadow_verify=true`, the same high-confidence candidate does not cut.
Normal deep search continues, after which `deep_score < beta` increments the
false-cut counter. Shadow verification is intended for unit tests and local
profile audit. Unlike the separate sample-collection facility below, its
shallow overhead is official work and consumes the same node/time budget.

Telemetry includes attempts, shallow nodes, confidence successes, unsupported
profiles, near-exact/pass/PV/root/overhead/probe-limit/confidence rejections,
real beta cutoffs, shadow candidates, shadow verifications, and shadow false
cuts. Reporting tools aggregate the same counters by phase and exact depth pair
and derive average shallow overhead and cut success rate. Cut-low attempts stay
zero because cut-low is not implemented. Estimated saved nodes remain
unavailable (`probcut_estimated_saved_nodes_available=false`); the engine does
not publish a guessed saving.

### MPC shadow calibration

The current implementation provides calibration-only shadow mode. It never
cuts off the official tree and does not apply fitted coefficients at runtime.
At a deterministically sampled eligible node, it first completes the normal
deep search, then runs one deep-depth verification and one reduced-depth
verification for every configured same-deep pair in separate isolated contexts
over `[kScoreLoss, kScoreWin]`. The
verification contexts have no official TT, no shared history/killer state, no
official node/time budget, no exact handoff, no ProbCut, and no nested shadow
collection.
Their wall time is excluded from the cooperative official deadline. The main
search position is already restored before verification begins.

Disabled mode leaves the context shadow pointer null. The hot path then pays
only a predictable null check at completed, expanded midgame nodes; it performs
no hashing, allocation, metadata construction, callback, or extra search.

Sampling combines the canonical side-to-move-relative position hash, root
identity, config/artifact/evaluator identities, repository SHA, seed, window,
depth, ply, and deterministic candidate ordinal. The per-search reservation
count enforces the cap even when a parent candidate remains pending while
recursive candidates complete. Given the same root, config, artifact, seed,
and repository SHA, traversal and the emitted sample set are deterministic.
The engine also derives `collection_config_id` from the effective sampling
rate, cap, the complete ordered depth-pair list, and PV/pass/near-exact
inclusion flags plus the sample schema version. It is persisted in each
schema-v5 sample and mixed into
`search_identity`; a collection-policy change therefore creates a distinct
population identity without relying on a caller-maintained config string.

By default, PV nodes, forced-pass nodes, and positions at or below the enabled
exact-handoff threshold are excluded. They can be included independently for
diagnostic strata. Schema v5 assigns a result-independent `search_role` at
node entry: `pv`, explicit `non_pv_scout`, or `other`. The analyzer fits
ProbCut candidates only from the complete `non_pv_scout` population, including
both eventual fail-high and fail-low results. After official search, the
existing result diagnostic `node_type` remains `pv`, non-PV `cut` for
fail-high, or non-PV `all` otherwise. Schema v5 preserves the official
alpha/beta, score, bound, and result for this classification. It separately
records shallow and deep full-window
verification scores, bounds, and best moves, plus search mode, exact-handoff
enablement/threshold/distance, global pair index, and same-deep pair index.
`hypothetical_cut_high` means the
shallow verification score crossed the official beta; `_low` means it crossed
the official alpha. False-cut candidates compare that crossing with the deep
verification score. These are observations, not runtime cutoffs.

Samples contain compact scalar metadata, hashes, scores, bounds, and moves;
they never contain a `Position`, search stack, TT, evaluator state, or PV tree.
Offline value OLS uses only exact/exact shallow/deep verification pairs. The
official null-window bound is not a regression teacher, but it remains usable
for cut/all classification and window diagnostics. Result-conditioned
`cut`/`all` groups are diagnostics only and cannot produce an adoptable
profile. The analyzer rejects mixed repository/search
config/evaluator/artifact provenance and mixed collection policy, records the
accepted inventories, and withholds margins from under-threshold groups.
Separate-seed or holdout validation remains mandatory before any coefficient
can be proposed for runtime use.
The analyzer rejects partial same-deep populations, groups by phase/pair/role,
search mode, observed empties bucket, and observed exact-handoff-distance
bucket. Profile conversion requires training and holdout to be disjoint by
canonical position hash, regardless of ply, official window, or search role,
and offline-replays the runtime threshold-directed first-success policy.
Pair-local fits without joint scheduler evidence cannot produce a profile.
The JSON/JSONL contract and deterministic offline analyzer are documented in
`tools/search-calibration/README.md`. Samples and reports are local-only.

## Time Management

Search supports fixed-depth, fixed-node, fixed-time, and infinite analysis.
The current implementation uses one cooperative deadline; it does not allocate
a clock across moves or expose separate soft and hard deadlines.

Rules:

* keep the last completed result available
* honor node, time, and atomic external-stop requests cooperatively
* avoid time checks at every node if they hurt performance

Recommended checks:

* every root move
* before starting a new iterative-deepening depth
* every fixed number of nodes

Search must be usable inside a Web Worker.

Time management must not depend on UI frameworks.

## Parallel Search

Parallel search is not implemented and no public option promises it. A future
design must preserve deterministic single-thread mode, use independent stacks
and evaluator scratch, define shared-TT synchronization, and keep browser
threading requirements in the adapter layer.

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

After each completed depth, root search must publish a coherent result.

Root search must not publish half-applied positions or illegal PVs.

## Multi-PV

Root reporting can return multiple candidate moves. `multi_pv == 1` requests a
best-only exact/WLD report; `0` and values greater than one currently retain
all-root behavior where applicable. Top-N limiting is not implemented.

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
* ProbCut shallow nodes and beta cutoffs
* ProbCut rejection reason counts
* ProbCut shadow verifications and false cuts
* ProbCut phase/deep/shallow attempts, overhead, successes, and rejection
  aggregates
* endgame nodes
* pass nodes
* split points

Stats must be cheap to collect.

Detailed stats may be compiled out or disabled.

Benchmarks should store stats with timing results.

## Directory Layout

Search lives inside the engine static library.

The implemented public headers are:

```text
engine/include/vibe_othello/search/
  evaluator.h
  probcut.h
  result.h
  score.h
  search.h
  search_session.h
  shadow_calibration.h
```

The implementation separates entry-point orchestration, recursive algorithms,
state, options, TT, endgame, and ordering:

```text
engine/src/search/
  alphabeta.cc
  pvs.cc
  root_search.cc
  search_node_common.cc
  search_options.cc
  search_session.cc
  search_util.cc
  transposition_table.cc
  probcut.cc
  shadow_calibration.cc
  *_internal.h
  move_ordering/
  endgame_*.cc
```

Reference negamax and reference endgame implementations are test support, not
production alternatives. Tests live in `engine/tests/search/`; shared reference
code and fixed-position loaders live under `engine/tests/support/search/`.
Search and endgame benchmark entry points live in `engine/benchmarks/`.

Tools may provide command-line entry points for solving, comparing, calibrating,
and measuring positions, but reusable search code belongs in `engine/`.

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
* single-thread production paths vs reference implementations

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
state where applicable. Non-session entry points construct a temporary session.
Session overloads let a caller retain knowledge across sequential
moves without putting mutable hash state in `board_core::Position`.

Session policy is explicit:

* `clear` deterministically clears TT, generation, history, and killers;
* sequential moves in one game may retain the session;
* unrelated analysis roots should call `clear` unless cross-root reuse is
  intentional;
* one session is single-thread-only and must not serve concurrent searches.

Every root binds the session TT to the evaluator object identity, the
evaluator's `transposition_table_revision()`, resolved midgame, move-ordering,
endgame, reporting, mode, and effective ProbCut runtime semantics, plus the
direct-search domain. A binding change clears the TT before probing. Shadow
calibration configuration is observational and is deliberately excluded from
the binding. Mutable evaluators must increment their revision whenever their
scoring behavior changes in place. Ordering state is safe to retain across an
automatic semantic rebind because it does not supply cutoff values.

The binding also includes every Multi-ProbCut scheduler setting and the complete
reviewed profile semantics: profile ID, calibration report SHA-256, evaluator
and artifact families, ordered pairs, domain keys, coefficients, confidence,
and validity ranges. Caller-owned strings/spans/pointers are replaced by this
stable content fingerprint before the session retains the identity.

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
`MidgameSearchOptions::pass_consumes_depth`; the default is `true`. Exact
endgame empties never decrease on pass.

Root PVS searches the first move with a full window, later moves with a null
window, and performs a full re-search only for a score strictly between alpha
and beta. `multi_pv != 1` preserves exact per-move reports by re-searching only
root reports that remain bounded. Before either root PVS or root alpha-beta
selects a lexicographically preferred upper-bound tie, it re-searches that move
from `best_score - 1` so the selected tie is exact rather than merely bounded.
