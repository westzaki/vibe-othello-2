#include "vibe_othello/board_core/board.h"

namespace vibe_othello::board_core {
namespace {

constexpr Position pass_position(Position position) noexcept {
  return Position{
      .player = position.opponent,
      .opponent = position.player,
      .side_to_move = opposite(position.side_to_move),
  };
}

} // namespace

bool is_terminal(Position position) noexcept {
  return is_valid(position) && !has_legal_move(position) &&
         !has_legal_move(pass_position(position));
}

} // namespace vibe_othello::board_core
