#include "directions.h"
#include "vibe_othello/board_core/board.h"

namespace vibe_othello::board_core {
namespace {

constexpr Bitboard legal_moves_in_direction(Bitboard player, Bitboard opponent, Bitboard empty,
                                            detail::Shift shift) noexcept {
  Bitboard run = shift(player) & opponent;
  Bitboard captured = run;

  while (run != 0) {
    run = shift(run) & opponent;
    captured |= run;
  }

  return shift(captured) & empty;
}

constexpr bool has_legal_move_in_direction(Bitboard player, Bitboard opponent, Bitboard empty,
                                           detail::Shift shift) noexcept {
  Bitboard run = shift(player) & opponent;
  Bitboard captured = run;

  while (run != 0) {
    if ((shift(captured) & empty) != 0) {
      return true;
    }
    run = shift(run) & opponent;
    captured |= run;
  }

  return false;
}

} // namespace

bool has_legal_move(Position position) noexcept {
  if (!is_valid(position)) {
    return false;
  }

  const Bitboard empty = ~occupied(position);

  return has_legal_move_in_direction(position.player, position.opponent, empty,
                                     detail::shift_east) ||
         has_legal_move_in_direction(position.player, position.opponent, empty,
                                     detail::shift_west) ||
         has_legal_move_in_direction(position.player, position.opponent, empty,
                                     detail::shift_north) ||
         has_legal_move_in_direction(position.player, position.opponent, empty,
                                     detail::shift_south) ||
         has_legal_move_in_direction(position.player, position.opponent, empty,
                                     detail::shift_north_east) ||
         has_legal_move_in_direction(position.player, position.opponent, empty,
                                     detail::shift_north_west) ||
         has_legal_move_in_direction(position.player, position.opponent, empty,
                                     detail::shift_south_east) ||
         has_legal_move_in_direction(position.player, position.opponent, empty,
                                     detail::shift_south_west);
}

Bitboard legal_moves(Position position) noexcept {
  if (!is_valid(position)) {
    return 0;
  }

  const Bitboard empty = ~occupied(position);

  return legal_moves_in_direction(position.player, position.opponent, empty, detail::shift_east) |
         legal_moves_in_direction(position.player, position.opponent, empty, detail::shift_west) |
         legal_moves_in_direction(position.player, position.opponent, empty, detail::shift_north) |
         legal_moves_in_direction(position.player, position.opponent, empty, detail::shift_south) |
         legal_moves_in_direction(position.player, position.opponent, empty,
                                  detail::shift_north_east) |
         legal_moves_in_direction(position.player, position.opponent, empty,
                                  detail::shift_north_west) |
         legal_moves_in_direction(position.player, position.opponent, empty,
                                  detail::shift_south_east) |
         legal_moves_in_direction(position.player, position.opponent, empty,
                                  detail::shift_south_west);
}

} // namespace vibe_othello::board_core
