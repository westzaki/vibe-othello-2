#include "vibe_othello/evaluation/early_midgame_heuristic_evaluator.h"

#include "vibe_othello/board_core/board.h"

#include <array>
#include <bit>
#include <cstdint>

namespace vibe_othello::evaluation {
namespace {

struct CornerAdjacency {
  board_core::Square corner;
  board_core::Square x_square;
  std::array<board_core::Square, 2> c_squares;
};

// Keep every hand-written coefficient together. These are conservative baseline
// weights for routing gaps, not tuned learned-model replacements.
struct HeuristicWeights {
  search::Score actual_mobility = 2;
  search::Score potential_mobility = 1;
  search::Score frontier = 1;
  search::Score corner = 12;
  search::Score empty_corner_x = 8;
  search::Score empty_corner_c = 4;
  search::Score disc_difference_midgame = 1;
  search::Score disc_difference_late = 2;
};

constexpr HeuristicWeights kWeights{};
constexpr int kDiscDifferenceMidgameStart = 20;
constexpr int kDiscDifferenceLateStart = 44;

constexpr std::array<CornerAdjacency, 4> kCornerAdjacency{{
    {
        .corner = board_core::square_from_file_rank(0, 0),
        .x_square = board_core::square_from_file_rank(1, 1),
        .c_squares =
            {
                board_core::square_from_file_rank(1, 0),
                board_core::square_from_file_rank(0, 1),
            },
    },
    {
        .corner = board_core::square_from_file_rank(7, 0),
        .x_square = board_core::square_from_file_rank(6, 1),
        .c_squares =
            {
                board_core::square_from_file_rank(6, 0),
                board_core::square_from_file_rank(7, 1),
            },
    },
    {
        .corner = board_core::square_from_file_rank(0, 7),
        .x_square = board_core::square_from_file_rank(1, 6),
        .c_squares =
            {
                board_core::square_from_file_rank(0, 6),
                board_core::square_from_file_rank(1, 7),
            },
    },
    {
        .corner = board_core::square_from_file_rank(7, 7),
        .x_square = board_core::square_from_file_rank(6, 6),
        .c_squares =
            {
                board_core::square_from_file_rank(7, 6),
                board_core::square_from_file_rank(6, 7),
            },
    },
}};

constexpr board_core::Bitboard file_mask(int file) noexcept {
  board_core::Bitboard result = 0;
  for (int rank = 0; rank < board_core::kBoardSize; ++rank) {
    result |= board_core::bit(board_core::square_from_file_rank(file, rank));
  }
  return result;
}

constexpr board_core::Bitboard kFileA = file_mask(0);
constexpr board_core::Bitboard kFileH = file_mask(board_core::kBoardSize - 1);

constexpr search::Score kMaxAbsoluteScore =
    (kWeights.actual_mobility * board_core::kSquareCount) +
    (kWeights.potential_mobility * board_core::kSquareCount) +
    (kWeights.frontier * board_core::kSquareCount) +
    (kWeights.corner * static_cast<search::Score>(kCornerAdjacency.size())) +
    (kWeights.empty_corner_x * static_cast<search::Score>(kCornerAdjacency.size())) +
    (kWeights.empty_corner_c *
     static_cast<search::Score>(kCornerAdjacency.size() *
                                kCornerAdjacency.front().c_squares.size())) +
    (kWeights.disc_difference_late * board_core::kSquareCount);
static_assert(kMaxAbsoluteScore < search::kScoreWin);

search::Score count(board_core::Bitboard squares) noexcept {
  return static_cast<search::Score>(std::popcount(squares));
}

board_core::Bitboard adjacent_squares(board_core::Bitboard squares) noexcept {
  const board_core::Bitboard east = (squares & ~kFileH) << 1U;
  const board_core::Bitboard west = (squares & ~kFileA) >> 1U;
  const board_core::Bitboard north = squares << board_core::kBoardSize;
  const board_core::Bitboard south = squares >> board_core::kBoardSize;
  const board_core::Bitboard north_east = (squares & ~kFileH) << (board_core::kBoardSize + 1);
  const board_core::Bitboard north_west = (squares & ~kFileA) << (board_core::kBoardSize - 1);
  const board_core::Bitboard south_east = (squares & ~kFileH) >> (board_core::kBoardSize - 1);
  const board_core::Bitboard south_west = (squares & ~kFileA) >> (board_core::kBoardSize + 1);
  return east | west | north | south | north_east | north_west | south_east | south_west;
}

search::Score corner_difference(board_core::Position position) noexcept {
  search::Score score = 0;
  for (const CornerAdjacency& adjacency : kCornerAdjacency) {
    const board_core::Bitboard corner = board_core::bit(adjacency.corner);
    if ((position.player & corner) != 0) {
      ++score;
    } else if ((position.opponent & corner) != 0) {
      --score;
    }
  }
  return score;
}

search::Score empty_corner_x_difference(board_core::Position position) noexcept {
  search::Score score = 0;
  const board_core::Bitboard occupied = board_core::occupied(position);
  for (const CornerAdjacency& adjacency : kCornerAdjacency) {
    if ((occupied & board_core::bit(adjacency.corner)) != 0) {
      continue;
    }

    const board_core::Bitboard x_square = board_core::bit(adjacency.x_square);
    if ((position.player & x_square) != 0) {
      --score;
    } else if ((position.opponent & x_square) != 0) {
      ++score;
    }
  }
  return score;
}

search::Score empty_corner_c_difference(board_core::Position position) noexcept {
  search::Score score = 0;
  const board_core::Bitboard occupied = board_core::occupied(position);
  for (const CornerAdjacency& adjacency : kCornerAdjacency) {
    if ((occupied & board_core::bit(adjacency.corner)) != 0) {
      continue;
    }

    for (board_core::Square c_square : adjacency.c_squares) {
      const board_core::Bitboard mask = board_core::bit(c_square);
      if ((position.player & mask) != 0) {
        --score;
      } else if ((position.opponent & mask) != 0) {
        ++score;
      }
    }
  }
  return score;
}

search::Score disc_difference_weight(int discs) noexcept {
  if (discs < kDiscDifferenceMidgameStart) {
    return 0;
  }
  if (discs < kDiscDifferenceLateStart) {
    return kWeights.disc_difference_midgame;
  }
  return kWeights.disc_difference_late;
}

} // namespace

search::Score
EarlyMidgameHeuristicEvaluator::evaluate(const board_core::Position& position) const noexcept {
  const board_core::Bitboard occupied = board_core::occupied(position);
  const board_core::Bitboard empty = ~occupied;
  const board_core::Position opponent_view{
      .player = position.opponent,
      .opponent = position.player,
      .side_to_move = board_core::opposite(position.side_to_move),
  };

  const search::Score actual_mobility =
      count(board_core::legal_moves(position)) - count(board_core::legal_moves(opponent_view));
  const search::Score potential_mobility = count(empty & adjacent_squares(position.opponent)) -
                                           count(empty & adjacent_squares(position.player));
  const search::Score frontier = count(position.opponent & adjacent_squares(empty)) -
                                 count(position.player & adjacent_squares(empty));
  const search::Score discs = count(occupied);
  const search::Score disc_difference = count(position.player) - count(position.opponent);

  return (kWeights.actual_mobility * actual_mobility) +
         (kWeights.potential_mobility * potential_mobility) + (kWeights.frontier * frontier) +
         (kWeights.corner * corner_difference(position)) +
         (kWeights.empty_corner_x * empty_corner_x_difference(position)) +
         (kWeights.empty_corner_c * empty_corner_c_difference(position)) +
         (disc_difference_weight(discs) * disc_difference);
}

} // namespace vibe_othello::evaluation
