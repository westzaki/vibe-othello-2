#ifndef VIBE_OTHELLO_BOARD_CORE_POSITION_H_
#define VIBE_OTHELLO_BOARD_CORE_POSITION_H_

#include "vibe_othello/board_core/coordinates.h"

namespace vibe_othello::board_core {

struct Position {
  Bitboard player;
  Bitboard opponent;
  Color side_to_move;

  friend constexpr bool operator==(Position, Position) noexcept = default;
};

constexpr Color opposite(Color color) noexcept {
  return color == Color::black ? Color::white : Color::black;
}

constexpr Bitboard initial_black_discs() noexcept {
  return bit(square_from_file_rank(3, 4)) | bit(square_from_file_rank(4, 3));
}

constexpr Bitboard initial_white_discs() noexcept {
  return bit(square_from_file_rank(4, 4)) | bit(square_from_file_rank(3, 3));
}

constexpr Position initial_position() noexcept {
  return Position{
      .player = initial_black_discs(),
      .opponent = initial_white_discs(),
      .side_to_move = Color::black,
  };
}

constexpr bool is_valid(Position position) noexcept {
  return (position.player & position.opponent) == 0;
}

constexpr Bitboard occupied(Position position) noexcept {
  return position.player | position.opponent;
}

constexpr Bitboard black_discs(Position position) noexcept {
  return position.side_to_move == Color::black ? position.player : position.opponent;
}

constexpr Bitboard white_discs(Position position) noexcept {
  return position.side_to_move == Color::white ? position.player : position.opponent;
}

} // namespace vibe_othello::board_core

#endif // VIBE_OTHELLO_BOARD_CORE_POSITION_H_
