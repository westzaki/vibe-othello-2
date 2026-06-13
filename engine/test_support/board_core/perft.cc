#include "board_core/perft.h"

#include "board_core/reference_board.h"

namespace vibe_othello::board_core::test_support {
namespace {

using ApplyMove = bool (*)(Position*, Move, MoveDelta*) noexcept;
using IsTerminal = bool (*)(Position) noexcept;
using LegalMoves = Bitboard (*)(Position) noexcept;

PerftCount perft_impl(Position position, int depth, LegalMoves legal_moves_fn,
                      ApplyMove apply_move_fn, IsTerminal is_terminal_fn) noexcept {
  if (depth <= 0) {
    return 1;
  }

  if (is_terminal_fn(position)) {
    return 0;
  }

  const Bitboard moves = legal_moves_fn(position);
  if (moves == 0) {
    Position next = position;
    MoveDelta delta{};
    if (!apply_move_fn(&next, make_pass(), &delta)) {
      return 0;
    }
    return perft_impl(next, depth - 1, legal_moves_fn, apply_move_fn, is_terminal_fn);
  }

  PerftCount nodes = 0;
  for (int index = 0; index < kSquareCount; ++index) {
    const Square square = square_from_index(index);
    if ((moves & bit(square)) == 0) {
      continue;
    }

    Position next = position;
    MoveDelta delta{};
    if (apply_move_fn(&next, make_move(square), &delta)) {
      nodes += perft_impl(next, depth - 1, legal_moves_fn, apply_move_fn, is_terminal_fn);
    }
  }

  return nodes;
}

} // namespace

PerftCount perft(Position position, int depth) noexcept {
  return perft_impl(position, depth, legal_moves, apply_move, is_terminal);
}

PerftCount reference_perft(Position position, int depth) noexcept {
  return perft_impl(position, depth, reference_legal_moves, reference_apply_move,
                    reference_is_terminal);
}

} // namespace vibe_othello::board_core::test_support
