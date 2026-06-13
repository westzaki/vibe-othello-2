#ifndef VIBE_OTHELLO_BOARD_CORE_BOARD_H_
#define VIBE_OTHELLO_BOARD_CORE_BOARD_H_

#include "vibe_othello/board_core/position.h"

namespace vibe_othello::board_core {

enum class MoveKind {
  normal,
  pass,
};

struct Move {
  MoveKind kind;
  Square square;

  friend constexpr bool operator==(Move, Move) noexcept = default;
};

struct MoveDelta {
  Move move;
  Bitboard flipped;

  friend constexpr bool operator==(MoveDelta, MoveDelta) noexcept = default;
};

constexpr Move make_move(Square square) noexcept {
  return Move{
      .kind = MoveKind::normal,
      .square = square,
  };
}

constexpr Move make_pass() noexcept {
  return Move{
      .kind = MoveKind::pass,
      .square = square_from_index(-1),
  };
}

bool apply_move(Position* position, Move move, MoveDelta* delta) noexcept;
bool apply_pass(Position* position, MoveDelta* delta) noexcept;
// Computes the move delta without modifying the position.
bool make_move_delta(Position position, Move move, MoveDelta* delta) noexcept;
// Precondition: delta was produced by make_move_delta for the current position.
// This applies the precomputed delta without recomputing flips.
void apply_move_delta(Position* position, MoveDelta delta) noexcept;
// Recomputes and verifies the delta before applying it.
bool apply_move_delta_checked(Position* position, MoveDelta delta) noexcept;
void undo_move(Position* position, MoveDelta delta) noexcept;
Bitboard flips_for_move(Position position, Square move) noexcept;
bool has_legal_move(Position position) noexcept;
bool is_terminal(Position position) noexcept;
Bitboard legal_moves(Position position) noexcept;

} // namespace vibe_othello::board_core

#endif // VIBE_OTHELLO_BOARD_CORE_BOARD_H_
