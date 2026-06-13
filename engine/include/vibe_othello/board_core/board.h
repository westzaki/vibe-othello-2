#ifndef VIBE_OTHELLO_BOARD_CORE_BOARD_H_
#define VIBE_OTHELLO_BOARD_CORE_BOARD_H_

#include "vibe_othello/board_core/position.h"

namespace vibe_othello::board_core {

struct Move {
  Square square;

  friend constexpr bool operator==(Move, Move) noexcept = default;
};

struct MoveDelta {
  Position before;
  Move move;
  Bitboard flipped;

  friend constexpr bool operator==(MoveDelta, MoveDelta) noexcept = default;
};

constexpr Move make_move(Square square) noexcept {
  return Move{
      .square = square,
  };
}

bool apply_move(Position* position, Move move, MoveDelta* delta) noexcept;
void undo_move(Position* position, MoveDelta delta) noexcept;
Bitboard flips_for_move(Position position, Square move) noexcept;
Bitboard legal_moves(Position position) noexcept;

} // namespace vibe_othello::board_core

#endif // VIBE_OTHELLO_BOARD_CORE_BOARD_H_
