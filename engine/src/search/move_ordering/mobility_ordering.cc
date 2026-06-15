#include "move_ordering_features_internal.h"

#include <bit>

namespace vibe_othello::search::internal {

int opponent_mobility_after(board_core::Position position, board_core::Move move) noexcept {
  board_core::MoveDelta delta{};
  if (!board_core::make_move_delta(position, move, &delta)) {
    return board_core::kSquareCount;
  }

  board_core::apply_move_delta(&position, delta);
  return std::popcount(board_core::legal_moves(position));
}

} // namespace vibe_othello::search::internal
