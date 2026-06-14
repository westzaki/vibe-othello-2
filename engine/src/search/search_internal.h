#pragma once

#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/hash.h"
#include "vibe_othello/search/search.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace vibe_othello::search::internal {

struct SearchValue {
  Score score = 0;
  Line pv{};
};

enum class SearchNodeStatus : std::uint8_t {
  complete,
  stopped,
};

class SearchNodeResult {
public:
  static SearchNodeResult completed(SearchValue value) noexcept;
  static SearchNodeResult stopped() noexcept;

  bool is_complete() const noexcept;
  bool is_stopped() const noexcept;

  const SearchValue& value() const noexcept;

private:
  SearchNodeStatus status_ = SearchNodeStatus::stopped;
  SearchValue value_{};
};

struct MoveList {
  std::array<board_core::Move, board_core::kSquareCount> moves{};
  std::uint8_t size = 0;
};

struct MidgameOrderingHints {
  std::optional<board_core::Move> root_best_move;
  std::optional<board_core::Move> tt_best_move;
  std::array<board_core::Move, 2> killer_moves{board_core::make_pass(), board_core::make_pass()};
  const std::array<int, board_core::kSquareCount>* history = nullptr;
  bool use_opponent_mobility = false;
};

// Compatibility name for existing call sites. New policy-specific code should
// choose MidgameOrderingHints or EndgameOrderingHints explicitly.
using MoveOrderingHints = MidgameOrderingHints;

struct EndgameOrderingHints {
  std::optional<board_core::Move> tt_best_move;
  std::optional<board_core::Move> root_best_move;
  bool use_parity_ordering = true;
};

struct ExactEndgameTtProbe {
  std::optional<Score> cutoff_score;
  std::optional<board_core::Move> best_move;
};

enum class SearchDispatch : std::uint8_t {
  alphabeta,
  pvs,
};

struct RootSearchWindow {
  Score alpha = kScoreLoss;
  Score beta = kScoreWin;
  bool enabled = false;
};

struct StackFrame {
  board_core::Move current_move = board_core::make_pass();
  board_core::MoveDelta delta{};
  MoveList moves{};
  Line pv{};
  board_core::PositionHash key = 0;
};

enum class TTEntryKind : std::uint8_t {
  midgame,
  exact_endgame_score,
  exact_endgame_wld,
};

struct TTEntry {
  board_core::PositionHash key = 0;
  Depth depth = 0;
  Score score = 0;
  BoundType bound = BoundType::exact;
  board_core::Move best_move = board_core::make_pass();
  bool has_best_move = false;
  std::uint8_t generation = 0;
  TTEntryKind kind = TTEntryKind::midgame;
  bool occupied = false;
};

class TranspositionTable {
public:
  static constexpr std::size_t kBucketWidth = 4;
  static constexpr std::size_t kDefaultEntryCount = 4096;
  static constexpr std::size_t kMaxBucketCount = std::size_t{1} << 20;

  explicit TranspositionTable(std::size_t requested_entries = kDefaultEntryCount);

  void clear() noexcept;
  void new_generation() noexcept;

  std::optional<TTEntry> probe(board_core::Position position, SearchStats* stats) const noexcept;

  void store(board_core::Position position, Depth depth, Score score, BoundType bound,
             board_core::Move best_move, TTEntryKind kind, SearchStats* stats) noexcept;
  void store_value(board_core::Position position, Depth depth, Score score, BoundType bound,
                   TTEntryKind kind, SearchStats* stats) noexcept;

private:
  struct TTBucket {
    std::array<TTEntry, kBucketWidth> entries{};
  };

  std::size_t index_for(board_core::PositionHash key) const noexcept;
  void store_entry(board_core::Position position, Depth depth, Score score, BoundType bound,
                   std::optional<board_core::Move> best_move, TTEntryKind kind,
                   SearchStats* stats) noexcept;

  std::vector<TTBucket> buckets_;
  std::uint8_t generation_ = 1;
};

struct SearchLimitState {
  std::chrono::steady_clock::time_point start{};
  std::chrono::steady_clock::time_point deadline{};
  const std::atomic_bool* stop_requested = nullptr;
  NodeCount max_nodes = 0;
  NodeCount nodes = 0;
  NodeCount nodes_until_next_time_check = 0;
  bool has_deadline = false;
  bool stopped = false;
};

enum class SearchNodeAccounting : std::uint8_t {
  normal,
  endgame,
};

struct SearchContext {
  board_core::Position position;
  const Evaluator& evaluator;
  SearchStats stats{};
  SearchLimits limits{};
  SearchOptions options{};
  TranspositionTable* transposition_table = nullptr;
  SearchLimitState* limit_state = nullptr;
  std::array<StackFrame, kMaxPly> stack{};
};

struct EndgameContext {
  board_core::Position position;
  SearchLimits limits{};
  SearchOptions options{};
  TranspositionTable* transposition_table = nullptr;
  SearchLimitState* limit_state = nullptr;
  SearchStats stats{};
  std::array<StackFrame, kMaxPly> stack{};
};

enum class SmallEndgamePolicy : std::uint8_t {
  enabled,
  generic_only,
};

Score terminal_score(board_core::Position position) noexcept;
bool is_valid_evaluator_score(Score score) noexcept;
void require_invariant(bool condition) noexcept;
void prepend_move(board_core::Move move, const Line& child, Line* line) noexcept;
void add_stats(SearchStats* total, SearchStats delta) noexcept;
SearchLimitState initialize_limit_state(SearchLimits limits);
bool should_stop(SearchLimitState* state);
bool note_node_visited(SearchLimitState* state, SearchStats* stats,
                       SearchNodeAccounting accounting);
bool should_stop_search(SearchContext* context);
bool note_node_visited(SearchContext* context);
bool is_better_root_move(Score score, board_core::Move move, Score best_score,
                         std::optional<board_core::Move> best_move) noexcept;

MoveList order_midgame_moves(board_core::Position position, MidgameOrderingHints hints) noexcept;
MoveList ordered_moves(board_core::Position position, MoveOrderingHints hints) noexcept;
MoveList order_endgame_moves(board_core::Position position, EndgameOrderingHints hints) noexcept;
BoundType classify_bound(Score score, Score original_alpha, Score original_beta) noexcept;
std::optional<Score> midgame_tt_cutoff_score(const TTEntry& entry, Depth depth, Score alpha,
                                             Score beta) noexcept;
ExactEndgameTtProbe exact_endgame_score_tt_probe(const TTEntry& entry,
                                                 board_core::Position position,
                                                 Depth remaining_empties, Score alpha,
                                                 Score beta) noexcept;
std::optional<Score> exact_endgame_score_tt_cutoff_score(const TTEntry& entry,
                                                         Depth remaining_empties, Score alpha,
                                                         Score beta) noexcept;
std::optional<SearchNodeResult> prepare_search_node(SearchContext* context, Score alpha, Score beta,
                                                    Depth depth, Ply ply,
                                                    std::optional<TTEntry>* tt_entry);
MoveOrderingHints build_ordering_hints_from_tt(const SearchContext& context,
                                               const std::optional<TTEntry>& tt_entry) noexcept;
SearchNodeResult search_pass_child(SearchContext* context, Score alpha, Score beta, Depth depth,
                                   Ply ply, SearchDispatch dispatch);
SearchNodeResult search_full_window_child(SearchContext* context, board_core::Move move,
                                          Score alpha, Score beta, Depth depth, Ply ply,
                                          SearchDispatch dispatch);
SearchNodeResult search_null_window_child(SearchContext* context, board_core::Move move, Score beta,
                                          Depth depth, Ply ply);
void update_best_line_and_move(const SearchValue& child, board_core::Move move, SearchValue* best,
                               std::optional<board_core::Move>* best_move, StackFrame* frame);
bool update_alpha_and_check_cutoff(SearchContext* context, Score score, Score* alpha,
                                   Score beta) noexcept;
void maybe_store_midgame_tt(SearchContext* context, Depth depth, Score score, BoundType bound,
                            std::optional<board_core::Move> best_move) noexcept;

SearchNodeResult alphabeta(SearchContext* context, Score alpha, Score beta, Depth depth, Ply ply);
SearchNodeResult null_window_search(SearchContext* context, Score beta, Depth depth, Ply ply);
SearchNodeResult pvs(SearchContext* context, Score alpha, Score beta, Depth depth, Ply ply);
SearchNodeResult full_window_search(SearchContext* context, Score alpha, Score beta, Depth depth,
                                    Ply ply);
std::uint8_t empty_count(board_core::Position position) noexcept;
bool should_use_exact_endgame(board_core::Position position, SearchOptions options) noexcept;
SearchNodeResult exact_score_search(EndgameContext* context, Score alpha, Score beta,
                                    std::uint8_t empties, Ply ply);
SearchResult solve_exact_endgame(board_core::Position position, SearchLimits limits,
                                 SearchOptions options, TranspositionTable* tt,
                                 SearchLimitState* limit_state = nullptr);
SearchResult solve_exact_endgame_with_small_endgame_policy(board_core::Position position,
                                                           SearchLimits limits,
                                                           SearchOptions options,
                                                           TranspositionTable* tt,
                                                           SmallEndgamePolicy small_endgame_policy,
                                                           SearchLimitState* limit_state = nullptr);

SearchResult search_fixed_depth_with_hint(board_core::Position position, const Evaluator& evaluator,
                                          Depth depth, MoveOrderingHints root_hints,
                                          SearchOptions options, TranspositionTable* tt,
                                          RootSearchWindow root_window = {},
                                          SearchLimitState* limit_state = nullptr);

} // namespace vibe_othello::search::internal
