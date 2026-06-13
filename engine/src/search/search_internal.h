#pragma once

#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/hash.h"
#include "vibe_othello/search/search.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace vibe_othello::search::internal {

struct SearchValue {
  Score score = 0;
  Line pv{};
  // When stopped is true, score and pv are incomplete and must not be published
  // or stored as semantic search results.
  bool stopped = false;
};

struct MoveList {
  std::array<board_core::Move, board_core::kSquareCount> moves{};
  std::uint8_t size = 0;
};

struct MoveOrderingHints {
  std::optional<board_core::Move> root_best_move;
  std::optional<board_core::Move> tt_best_move;
  std::array<board_core::Move, 2> killer_moves{board_core::make_pass(), board_core::make_pass()};
  const std::array<int, board_core::kSquareCount>* history = nullptr;
  bool use_opponent_mobility = false;
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
  std::uint8_t generation = 0;
  TTEntryKind kind = TTEntryKind::midgame;
  bool occupied = false;
};

class TranspositionTable {
public:
  std::optional<TTEntry> probe(board_core::Position position, SearchStats* stats) const noexcept;

  void store(board_core::Position position, Depth depth, Score score, BoundType bound,
             board_core::Move best_move, TTEntryKind kind, SearchStats* stats) noexcept;

private:
  static constexpr std::size_t kEntryCount = 4096;

  static constexpr std::size_t index_for(board_core::PositionHash key) noexcept {
    return static_cast<std::size_t>(key % kEntryCount);
  }

  std::array<TTEntry, kEntryCount> entries_{};
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

Score terminal_score(board_core::Position position) noexcept;
bool is_valid_evaluator_score(Score score) noexcept;
void require_invariant(bool condition) noexcept;
void prepend_move(board_core::Move move, const Line& child, Line* line) noexcept;
void add_stats(SearchStats* total, SearchStats delta) noexcept;
SearchLimitState initialize_limit_state(SearchLimits limits);
bool should_stop_search(SearchContext* context);
bool note_node_visited(SearchContext* context);

MoveList ordered_moves(board_core::Position position, MoveOrderingHints hints) noexcept;
BoundType classify_bound(Score score, Score original_alpha, Score original_beta) noexcept;
std::optional<Score> tt_cutoff_score(const TTEntry& entry, Depth depth, Score alpha,
                                     Score beta) noexcept;
std::optional<SearchValue> prepare_search_node(SearchContext* context, Score alpha, Score beta,
                                               Depth depth, Ply ply,
                                               std::optional<TTEntry>* tt_entry);
MoveOrderingHints build_ordering_hints_from_tt(const SearchContext& context,
                                               const std::optional<TTEntry>& tt_entry) noexcept;
SearchValue search_pass_child(SearchContext* context, Score alpha, Score beta, Depth depth, Ply ply,
                              SearchDispatch dispatch);
SearchValue search_full_window_child(SearchContext* context, board_core::Move move, Score alpha,
                                     Score beta, Depth depth, Ply ply, SearchDispatch dispatch);
SearchValue search_null_window_child(SearchContext* context, board_core::Move move, Score beta,
                                     Depth depth, Ply ply);
void update_best_line_and_move(const SearchValue& child, board_core::Move move, SearchValue* best,
                               std::optional<board_core::Move>* best_move, StackFrame* frame);
bool update_alpha_and_check_cutoff(SearchContext* context, Score score, Score* alpha,
                                   Score beta) noexcept;
void maybe_store_midgame_tt(SearchContext* context, Depth depth, Score score, BoundType bound,
                            std::optional<board_core::Move> best_move) noexcept;

SearchValue alphabeta(SearchContext* context, Score alpha, Score beta, Depth depth, Ply ply);
SearchValue null_window_search(SearchContext* context, Score beta, Depth depth, Ply ply);
SearchValue pvs(SearchContext* context, Score alpha, Score beta, Depth depth, Ply ply);
SearchValue full_window_search(SearchContext* context, Score alpha, Score beta, Depth depth,
                               Ply ply);
std::uint8_t empty_count(board_core::Position position) noexcept;
bool should_use_exact_endgame(board_core::Position position, SearchOptions options) noexcept;
SearchValue exact_score_search(EndgameContext* context, Score alpha, Score beta,
                               std::uint8_t empties, Ply ply);
SearchResult solve_exact_endgame(board_core::Position position, SearchLimits limits,
                                 SearchOptions options, TranspositionTable* tt,
                                 SearchLimitState* limit_state = nullptr);

SearchResult search_fixed_depth_with_hint(board_core::Position position, const Evaluator& evaluator,
                                          Depth depth, MoveOrderingHints root_hints,
                                          SearchOptions options, TranspositionTable* tt,
                                          RootSearchWindow root_window = {},
                                          SearchLimitState* limit_state = nullptr);

} // namespace vibe_othello::search::internal
