#include "vibe_othello/board_core/board.h"

#include <cassert>

namespace vibe_othello::board_core {
namespace {

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

#ifndef NDEBUG
bool is_plausible_delta_for_current_position(Position position, MoveDelta delta) noexcept {
  if (!is_valid(position)) {
    return false;
  }

  if (delta.move.kind == MoveKind::pass) {
    return delta == MoveDelta{.move = make_pass(), .flipped = 0} && !has_legal_move(position) &&
           has_legal_move(pass_position(position));
  }

  if (delta.move.kind != MoveKind::normal) {
    return false;
  }

  const Bitboard move_bit = bit(delta.move.square);
  if (move_bit == 0 || delta.flipped == 0) {
    return false;
  }
  if ((move_bit & occupied(position)) != 0) {
    return false;
  }
  if ((delta.flipped & position.opponent) != delta.flipped) {
    return false;
  }

  return (delta.flipped & move_bit) == 0;
}
#endif

} // namespace

bool apply_move(Position* position, Move move, MoveDelta* delta) noexcept {
  if (position == nullptr || delta == nullptr) {
    return false;
  }

  MoveDelta move_delta{};
  if (!make_move_delta(*position, move, &move_delta)) {
    return false;
  }

  apply_move_delta(position, move_delta);
  *delta = move_delta;
  return true;
}

void apply_move_delta(Position* position, MoveDelta delta) noexcept {
  if (position == nullptr) {
    return;
  }

#ifndef NDEBUG
  assert(is_plausible_delta_for_current_position(*position, delta));
#endif

  if (delta.move.kind == MoveKind::pass) {
    *position = pass_position(*position);
    return;
  }

  *position = apply_normal_delta_position(*position, delta.move, delta.flipped);
}

bool apply_move_delta_checked(Position* position, MoveDelta delta) noexcept {
  if (position == nullptr) {
    return false;
  }

  MoveDelta expected{};
  if (!make_move_delta(*position, delta.move, &expected) || expected != delta) {
    return false;
  }

  apply_move_delta(position, delta);
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

  apply_move_delta(position, pass_delta);
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
        .move = make_pass(),
        .flipped = 0,
    };
    return true;
  }

  if (move.kind != MoveKind::normal) {
    return false;
  }

  const Bitboard move_bit = bit(move.square);
  const Bitboard flipped = flips_for_move(position, move.square);
  if (move_bit == 0 || flipped == 0) {
    return false;
  }

  *delta = MoveDelta{
      .move = move,
      .flipped = flipped,
  };
  return true;
}

void undo_move(Position* position, MoveDelta delta) noexcept {
  if (position == nullptr) {
    return;
  }

  if (delta.move.kind == MoveKind::pass) {
    *position = pass_position(*position);
    return;
  }

  const Bitboard move_bit = bit(delta.move.square);
  const Bitboard player_before = position->opponent & ~(move_bit | delta.flipped);
  const Bitboard opponent_before = position->player | delta.flipped;

  *position = Position{
      .player = player_before,
      .opponent = opponent_before,
      .side_to_move = opposite(position->side_to_move),
  };
}

} // namespace vibe_othello::board_core
