#pragma once

#include "move_ordering_internal.h"
#include "search_limits_internal.h"
#include "search_options_internal.h"
#include "search_position_internal.h"
#include "transposition_table_internal.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/hash.h"
#include "vibe_othello/search/search.h"

#include <array>
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
  SearchPositionUndo position_undo{};
  board_core::Bitboard legal_moves = 0;
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

struct SearchContext {
  SearchPositionState position_state;
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

} // namespace vibe_othello::search::internal
