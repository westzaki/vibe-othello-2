#pragma once

#include "move_ordering_internal.h"
#include "search_options_internal.h"
#include "transposition_table_internal.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/hash.h"
#include "vibe_othello/search/search.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace vibe_othello::search::internal {

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

struct MidgameOrderingState {
  MidgameOrderingState() noexcept {
    for (std::array<board_core::Move, 2>& killers : killer_moves) {
      killers = {board_core::make_pass(), board_core::make_pass()};
    }
  }

  std::array<std::array<board_core::Move, 2>, kMaxPly> killer_moves{};
  std::array<int, board_core::kSquareCount> history{};
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
  ResolvedSearchOptions options{};
  TranspositionTable* transposition_table = nullptr;
  MidgameOrderingState* ordering_state = nullptr;
  SearchLimitState* limit_state = nullptr;
  bool in_iid = false;
  std::array<StackFrame, kMaxPly> stack{};
};

struct EndgameContext {
  board_core::Position position;
  SearchLimits limits{};
  ResolvedSearchOptions options{};
  TranspositionTable* transposition_table = nullptr;
  SearchLimitState* limit_state = nullptr;
  SearchStats stats{};
  std::array<StackFrame, kMaxPly> stack{};
};

enum class SmallEndgamePolicy : std::uint8_t {
  enabled,
  generic_only,
};

} // namespace vibe_othello::search::internal
