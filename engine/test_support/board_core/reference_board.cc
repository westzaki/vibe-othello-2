#include "board_core/reference_board.h"

#include <array>

namespace vibe_othello::board_core::test_support {
namespace {

enum class Cell {
  empty,
  player,
  opponent,
};

struct Direction {
  int file_delta;
  int rank_delta;
};

constexpr std::array<Direction, 8> kDirections{{
    Direction{.file_delta = 1, .rank_delta = 0},
    Direction{.file_delta = -1, .rank_delta = 0},
    Direction{.file_delta = 0, .rank_delta = 1},
    Direction{.file_delta = 0, .rank_delta = -1},
    Direction{.file_delta = 1, .rank_delta = 1},
    Direction{.file_delta = -1, .rank_delta = 1},
    Direction{.file_delta = 1, .rank_delta = -1},
    Direction{.file_delta = -1, .rank_delta = -1},
}};

using Board = std::array<Cell, kSquareCount>;

Board make_board(Position position) noexcept {
  Board board{};
  board.fill(Cell::empty);

  for (int index = 0; index < kSquareCount; ++index) {
    const Square square = square_from_index(index);
    const Bitboard square_bit = bit(square);
    if ((position.player & square_bit) != 0) {
      board[static_cast<std::size_t>(index)] = Cell::player;
    } else if ((position.opponent & square_bit) != 0) {
      board[static_cast<std::size_t>(index)] = Cell::opponent;
    }
  }

  return board;
}

Position make_position(const Board& board, Color side_to_move) noexcept {
  Bitboard player = 0;
  Bitboard opponent = 0;

  for (int index = 0; index < kSquareCount; ++index) {
    const Cell cell = board[static_cast<std::size_t>(index)];
    const Bitboard square_bit = bit(square_from_index(index));
    if (cell == Cell::player) {
      player |= square_bit;
    } else if (cell == Cell::opponent) {
      opponent |= square_bit;
    }
  }

  return Position{
      .player = opponent,
      .opponent = player,
      .side_to_move = opposite(side_to_move),
  };
}

constexpr Position pass_position(Position position) noexcept {
  return Position{
      .player = position.opponent,
      .opponent = position.player,
      .side_to_move = opposite(position.side_to_move),
  };
}

Bitboard flips_in_direction(const Board& board, Square move, Direction direction) noexcept {
  int file = file_of(move) + direction.file_delta;
  int rank = rank_of(move) + direction.rank_delta;
  Bitboard captured = 0;

  while (is_valid_file_rank(file, rank)) {
    const Square square = square_from_file_rank(file, rank);
    const Cell cell = board[static_cast<std::size_t>(square.index)];

    if (cell == Cell::opponent) {
      captured |= bit(square);
      file += direction.file_delta;
      rank += direction.rank_delta;
      continue;
    }

    if (cell == Cell::player && captured != 0) {
      return captured;
    }

    return 0;
  }

  return 0;
}

} // namespace

bool reference_apply_move(Position* position, Move move, MoveDelta* delta) noexcept {
  if (move.kind == MoveKind::pass) {
    return reference_apply_pass(position, delta);
  }

  if (position == nullptr || delta == nullptr) {
    return false;
  }

  const Position before = *position;
  const Bitboard move_bit = bit(move.square);
  const Bitboard flipped = reference_flips_for_move(before, move.square);
  if (move_bit == 0 || flipped == 0) {
    return false;
  }

  Board board = make_board(before);
  board[static_cast<std::size_t>(move.square.index)] = Cell::player;
  for (int index = 0; index < kSquareCount; ++index) {
    const Square square = square_from_index(index);
    if ((flipped & bit(square)) != 0) {
      board[static_cast<std::size_t>(index)] = Cell::player;
    }
  }

  *delta = MoveDelta{
      .before = before,
      .move = move,
      .flipped = flipped,
  };
  *position = make_position(board, before.side_to_move);

  return true;
}

bool reference_apply_pass(Position* position, MoveDelta* delta) noexcept {
  if (position == nullptr || delta == nullptr) {
    return false;
  }

  const Position before = *position;
  if (!is_valid(before) || reference_has_legal_move(before) ||
      !reference_has_legal_move(pass_position(before))) {
    return false;
  }

  *delta = MoveDelta{
      .before = before,
      .move = make_pass(),
      .flipped = 0,
  };
  *position = pass_position(before);

  return true;
}

Bitboard reference_flips_for_move(Position position, Square move) noexcept {
  if (!is_valid(position) || !is_valid(move) || (bit(move) & occupied(position)) != 0) {
    return 0;
  }

  const Board board = make_board(position);
  Bitboard flips = 0;
  for (const Direction direction : kDirections) {
    flips |= flips_in_direction(board, move, direction);
  }

  return flips;
}

bool reference_has_legal_move(Position position) noexcept {
  return reference_legal_moves(position) != 0;
}

bool reference_is_terminal(Position position) noexcept {
  return is_valid(position) && !reference_has_legal_move(position) &&
         !reference_has_legal_move(pass_position(position));
}

Bitboard reference_legal_moves(Position position) noexcept {
  if (!is_valid(position)) {
    return 0;
  }

  Bitboard moves = 0;
  const Board board = make_board(position);
  for (int index = 0; index < kSquareCount; ++index) {
    const Square move = square_from_index(index);
    if (board[static_cast<std::size_t>(index)] == Cell::empty &&
        reference_flips_for_move(position, move) != 0) {
      moves |= bit(move);
    }
  }

  return moves;
}

} // namespace vibe_othello::board_core::test_support
