#pragma once

#include "vibe_othello/board_core/board.h"

#include <array>
#include <cstdint>
#include <optional>

namespace vibe_othello::search::internal {

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

MoveList order_midgame_moves(board_core::Position position, MidgameOrderingHints hints) noexcept;
MoveList ordered_moves(board_core::Position position, MoveOrderingHints hints) noexcept;
MoveList order_endgame_moves(board_core::Position position, EndgameOrderingHints hints) noexcept;

} // namespace vibe_othello::search::internal
