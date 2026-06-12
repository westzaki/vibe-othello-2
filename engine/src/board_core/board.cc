#include "vibe_othello/board_core/board.h"

namespace vibe_othello::board_core {
namespace {

constexpr Bitboard kFileA = 0x0101010101010101ULL;
constexpr Bitboard kFileH = 0x8080808080808080ULL;

using Shift = Bitboard (*)(Bitboard) noexcept;

constexpr Bitboard shift_east(Bitboard bits) noexcept {
  return (bits & ~kFileH) << 1;
}

constexpr Bitboard shift_west(Bitboard bits) noexcept {
  return (bits & ~kFileA) >> 1;
}

constexpr Bitboard shift_north(Bitboard bits) noexcept {
  return bits << kBoardSize;
}

constexpr Bitboard shift_south(Bitboard bits) noexcept {
  return bits >> kBoardSize;
}

constexpr Bitboard shift_north_east(Bitboard bits) noexcept {
  return (bits & ~kFileH) << (kBoardSize + 1);
}

constexpr Bitboard shift_north_west(Bitboard bits) noexcept {
  return (bits & ~kFileA) << (kBoardSize - 1);
}

constexpr Bitboard shift_south_east(Bitboard bits) noexcept {
  return (bits & ~kFileH) >> (kBoardSize - 1);
}

constexpr Bitboard shift_south_west(Bitboard bits) noexcept {
  return (bits & ~kFileA) >> (kBoardSize + 1);
}

constexpr Bitboard legal_moves_in_direction(Bitboard player, Bitboard opponent, Bitboard empty,
                                            Shift shift) noexcept {
  Bitboard run = shift(player) & opponent;
  Bitboard captured = run;

  while (run != 0) {
    run = shift(run) & opponent;
    captured |= run;
  }

  return shift(captured) & empty;
}

constexpr Bitboard flips_in_direction(Bitboard move, Bitboard player, Bitboard opponent,
                                      Shift shift) noexcept {
  Bitboard run = shift(move) & opponent;
  Bitboard captured = run;

  while (run != 0) {
    run = shift(run);
    if ((run & player) != 0) {
      return captured;
    }

    run &= opponent;
    captured |= run;
  }

  return 0;
}

} // namespace

Bitboard flips_for_move(Position position, Square move) noexcept {
  const Bitboard move_bit = bit(move);
  if (!is_valid(position) || move_bit == 0 || (move_bit & occupied(position)) != 0) {
    return 0;
  }

  return flips_in_direction(move_bit, position.player, position.opponent, shift_east) |
         flips_in_direction(move_bit, position.player, position.opponent, shift_west) |
         flips_in_direction(move_bit, position.player, position.opponent, shift_north) |
         flips_in_direction(move_bit, position.player, position.opponent, shift_south) |
         flips_in_direction(move_bit, position.player, position.opponent, shift_north_east) |
         flips_in_direction(move_bit, position.player, position.opponent, shift_north_west) |
         flips_in_direction(move_bit, position.player, position.opponent, shift_south_east) |
         flips_in_direction(move_bit, position.player, position.opponent, shift_south_west);
}

Bitboard legal_moves(Position position) noexcept {
  if (!is_valid(position)) {
    return 0;
  }

  const Bitboard empty = ~occupied(position);

  return legal_moves_in_direction(position.player, position.opponent, empty, shift_east) |
         legal_moves_in_direction(position.player, position.opponent, empty, shift_west) |
         legal_moves_in_direction(position.player, position.opponent, empty, shift_north) |
         legal_moves_in_direction(position.player, position.opponent, empty, shift_south) |
         legal_moves_in_direction(position.player, position.opponent, empty, shift_north_east) |
         legal_moves_in_direction(position.player, position.opponent, empty, shift_north_west) |
         legal_moves_in_direction(position.player, position.opponent, empty, shift_south_east) |
         legal_moves_in_direction(position.player, position.opponent, empty, shift_south_west);
}

} // namespace vibe_othello::board_core
