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

struct StackFrame {
  board_core::Move current_move = board_core::make_pass();
  board_core::MoveDelta delta{};
  MoveList moves{};
  Line pv{};
  board_core::PositionHash key = 0;
};

class TTBestMoveTable {
public:
  std::optional<board_core::Move> probe(board_core::Position position,
                                        SearchStats* stats) const noexcept;

  void store(board_core::Position position, board_core::Move best_move,
             SearchStats* stats) noexcept;

private:
  struct Entry {
    board_core::PositionHash key = 0;
    board_core::Move best_move = board_core::make_pass();
    bool occupied = false;
  };

  static constexpr std::size_t kEntryCount = 4096;

  static constexpr std::size_t index_for(board_core::PositionHash key) noexcept {
    return static_cast<std::size_t>(key % kEntryCount);
  }

  std::array<Entry, kEntryCount> entries_{};
};

struct SearchContext {
  board_core::Position position;
  const Evaluator& evaluator;
  SearchStats stats{};
  SearchLimits limits{};
  SearchOptions options{};
  TTBestMoveTable* best_move_table = nullptr;
  std::chrono::steady_clock::time_point start_time{};
  bool stopped = false;
  std::array<StackFrame, kMaxPly> stack{};
};

Score terminal_score(board_core::Position position) noexcept;
bool is_valid_evaluator_score(Score score) noexcept;
void require_invariant(bool condition) noexcept;
void prepend_move(board_core::Move move, const Line& child, Line* line) noexcept;
void add_stats(SearchStats* total, SearchStats delta) noexcept;
bool limits_reached(const SearchLimits& limits, std::chrono::steady_clock::time_point start_time,
                    NodeCount nodes) noexcept;
bool should_stop(SearchContext* context) noexcept;

MoveList ordered_moves(board_core::Position position, MoveOrderingHints hints) noexcept;

SearchValue alphabeta(SearchContext* context, Score alpha, Score beta, Depth depth, Ply ply);

SearchResult search_fixed_depth_with_hint(board_core::Position position, const Evaluator& evaluator,
                                          Depth depth, MoveOrderingHints root_hints,
                                          SearchLimits limits, SearchOptions options,
                                          TTBestMoveTable* tt,
                                          std::chrono::steady_clock::time_point start_time);

} // namespace vibe_othello::search::internal
