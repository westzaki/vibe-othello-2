#include "directions.h"
#include "vibe_othello/board_core/board.h"

namespace vibe_othello::board_core {
namespace {

template <detail::Shift shift>
constexpr Bitboard flips_in_direction(Bitboard move, Bitboard player, Bitboard opponent) noexcept {
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

  return flips_in_direction<detail::shift_east>(move_bit, position.player, position.opponent) |
         flips_in_direction<detail::shift_west>(move_bit, position.player, position.opponent) |
         flips_in_direction<detail::shift_north>(move_bit, position.player, position.opponent) |
         flips_in_direction<detail::shift_south>(move_bit, position.player, position.opponent) |
         flips_in_direction<detail::shift_north_east>(move_bit, position.player,
                                                      position.opponent) |
         flips_in_direction<detail::shift_north_west>(move_bit, position.player,
                                                      position.opponent) |
         flips_in_direction<detail::shift_south_east>(move_bit, position.player,
                                                      position.opponent) |
         flips_in_direction<detail::shift_south_west>(move_bit, position.player, position.opponent);
}

} // namespace vibe_othello::board_core
