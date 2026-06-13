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

constexpr bool has_legal_move_in_direction(Bitboard player, Bitboard opponent, Bitboard empty,
                                           Shift shift) noexcept {
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

template <Shift shift>
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

constexpr Position pass_position(Position position) noexcept {
  return Position{
      .player = position.opponent,
      .opponent = position.player,
      .side_to_move = opposite(position.side_to_move),
  };
}

constexpr Position apply_normal_delta_position(Position before, Move move,
                                               Bitboard flipped) noexcept {
  const Bitboard move_bit = bit(move.square);
  const Bitboard player_after_move = before.player | move_bit | flipped;
  const Bitboard opponent_after_move = before.opponent & ~flipped;

  return Position{
      .player = opponent_after_move,
      .opponent = player_after_move,
      .side_to_move = opposite(before.side_to_move),
  };
}

} // namespace

bool apply_move(Position* position, Move move, MoveDelta* delta) noexcept {
  if (position == nullptr || delta == nullptr) {
    return false;
  }

  MoveDelta move_delta{};
  if (!make_move_delta(*position, move, &move_delta)) {
    return false;
  }

  *position =
      move_delta.move.kind == MoveKind::pass
          ? pass_position(move_delta.before)
          : apply_normal_delta_position(move_delta.before, move_delta.move, move_delta.flipped);

  *delta = move_delta;
  return true;
}

bool apply_move_delta(Position* position, MoveDelta delta) noexcept {
  if (position == nullptr || *position != delta.before || !is_valid(delta.before)) {
    return false;
  }

  if (delta.move.kind == MoveKind::pass) {
    if (delta.flipped != 0 || has_legal_move(delta.before) ||
        !has_legal_move(pass_position(delta.before))) {
      return false;
    }
    *position = pass_position(delta.before);
    return true;
  }

  const Bitboard move_bit = bit(delta.move.square);
  if (move_bit == 0 || delta.flipped == 0 || (move_bit & occupied(delta.before)) != 0 ||
      (delta.flipped & delta.before.opponent) != delta.flipped) {
    return false;
  }

  *position = apply_normal_delta_position(delta.before, delta.move, delta.flipped);

  return true;
}

bool apply_pass(Position* position, MoveDelta* delta) noexcept {
  if (position == nullptr || delta == nullptr) {
    return false;
  }

  MoveDelta pass_delta{};
  if (!make_move_delta(*position, make_pass(), &pass_delta)) {
    return false;
  }

  *position = pass_position(pass_delta.before);
  *delta = pass_delta;
  return true;
}

bool make_move_delta(Position position, Move move, MoveDelta* delta) noexcept {
  if (delta == nullptr) {
    return false;
  }

  if (move.kind == MoveKind::pass) {
    if (!is_valid(position) || has_legal_move(position) ||
        !has_legal_move(pass_position(position))) {
      return false;
    }

    *delta = MoveDelta{
        .before = position,
        .move = make_pass(),
        .flipped = 0,
    };
    return true;
  }

  const Bitboard move_bit = bit(move.square);
  const Bitboard flipped = flips_for_move(position, move.square);
  if (move_bit == 0 || flipped == 0) {
    return false;
  }

  *delta = MoveDelta{
      .before = position,
      .move = move,
      .flipped = flipped,
  };
  return true;
}

void undo_move(Position* position, MoveDelta delta) noexcept {
  if (position == nullptr) {
    return;
  }

  *position = delta.before;
}

Bitboard flips_for_move(Position position, Square move) noexcept {
  const Bitboard move_bit = bit(move);
  if (!is_valid(position) || move_bit == 0 || (move_bit & occupied(position)) != 0) {
    return 0;
  }

  return flips_in_direction<shift_east>(move_bit, position.player, position.opponent) |
         flips_in_direction<shift_west>(move_bit, position.player, position.opponent) |
         flips_in_direction<shift_north>(move_bit, position.player, position.opponent) |
         flips_in_direction<shift_south>(move_bit, position.player, position.opponent) |
         flips_in_direction<shift_north_east>(move_bit, position.player, position.opponent) |
         flips_in_direction<shift_north_west>(move_bit, position.player, position.opponent) |
         flips_in_direction<shift_south_east>(move_bit, position.player, position.opponent) |
         flips_in_direction<shift_south_west>(move_bit, position.player, position.opponent);
}

bool has_legal_move(Position position) noexcept {
  if (!is_valid(position)) {
    return false;
  }

  const Bitboard empty = ~occupied(position);

  return has_legal_move_in_direction(position.player, position.opponent, empty, shift_east) ||
         has_legal_move_in_direction(position.player, position.opponent, empty, shift_west) ||
         has_legal_move_in_direction(position.player, position.opponent, empty, shift_north) ||
         has_legal_move_in_direction(position.player, position.opponent, empty, shift_south) ||
         has_legal_move_in_direction(position.player, position.opponent, empty, shift_north_east) ||
         has_legal_move_in_direction(position.player, position.opponent, empty, shift_north_west) ||
         has_legal_move_in_direction(position.player, position.opponent, empty, shift_south_east) ||
         has_legal_move_in_direction(position.player, position.opponent, empty, shift_south_west);
}

bool is_terminal(Position position) noexcept {
  return is_valid(position) && !has_legal_move(position) &&
         !has_legal_move(pass_position(position));
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
