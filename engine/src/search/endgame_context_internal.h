#pragma once

#include "move_ordering_internal.h"
#include "search_limits_internal.h"
#include "search_options_internal.h"
#include "transposition_table_internal.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/search/search.h"

#include <array>
#include <cstdint>

namespace vibe_othello::search::internal {

struct EndgameStackFrame {
  board_core::Move current_move = board_core::make_pass();
  board_core::MoveDelta delta{};
  MoveList moves{};
  Line pv{};
};

struct EndgameContext {
  board_core::Position position;
  SearchLimits limits{};
  ResolvedSearchOptions options{};
  TranspositionTable* transposition_table = nullptr;
  SearchLimitState* limit_state = nullptr;
  SearchStats stats{};
  std::array<EndgameStackFrame, kMaxPly> stack{};
};

enum class SmallEndgamePolicy : std::uint8_t {
  enabled,
  generic_only,
};

} // namespace vibe_othello::search::internal
