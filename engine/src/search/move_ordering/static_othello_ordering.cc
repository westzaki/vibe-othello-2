#include "move_ordering_features_internal.h"

#include <array>

namespace vibe_othello::search::internal {
namespace {

struct CornerAdjacency {
  board_core::Square corner;
  board_core::Square x_square;
  std::array<board_core::Square, 2> c_squares;
};

// Square index follows board_core's a1=0, h1=7, a8=56, h8=63 bit order.
// Define corner/X/C-square tables from coordinates so a bit-order change fails
// close to the ordering logic instead of silently changing heuristics.
constexpr std::array<CornerAdjacency, 4> kCornerAdjacency{{
    {
        .corner = board_core::square_from_file_rank(0, 0),   // a1
        .x_square = board_core::square_from_file_rank(1, 1), // b2
        .c_squares =
            {
                board_core::square_from_file_rank(1, 0), // b1
                board_core::square_from_file_rank(0, 1), // a2
            },
    },
    {
        .corner = board_core::square_from_file_rank(7, 0),   // h1
        .x_square = board_core::square_from_file_rank(6, 1), // g2
        .c_squares =
            {
                board_core::square_from_file_rank(6, 0), // g1
                board_core::square_from_file_rank(7, 1), // h2
            },
    },
    {
        .corner = board_core::square_from_file_rank(0, 7),   // a8
        .x_square = board_core::square_from_file_rank(1, 6), // b7
        .c_squares =
            {
                board_core::square_from_file_rank(0, 6), // a7
                board_core::square_from_file_rank(1, 7), // b8
            },
    },
    {
        .corner = board_core::square_from_file_rank(7, 7),   // h8
        .x_square = board_core::square_from_file_rank(6, 6), // g7
        .c_squares =
            {
                board_core::square_from_file_rank(7, 6), // h7
                board_core::square_from_file_rank(6, 7), // g8
            },
    },
}};

constexpr board_core::Bitboard mask_for_corners() noexcept {
  board_core::Bitboard mask = 0;
  for (const CornerAdjacency adjacency : kCornerAdjacency) {
    mask |= board_core::bit(adjacency.corner);
  }
  return mask;
}

constexpr board_core::Bitboard mask_for_x_squares() noexcept {
  board_core::Bitboard mask = 0;
  for (const CornerAdjacency adjacency : kCornerAdjacency) {
    mask |= board_core::bit(adjacency.x_square);
  }
  return mask;
}

constexpr board_core::Bitboard mask_for_c_squares() noexcept {
  board_core::Bitboard mask = 0;
  for (const CornerAdjacency adjacency : kCornerAdjacency) {
    mask |= board_core::bit(adjacency.c_squares[0]);
    mask |= board_core::bit(adjacency.c_squares[1]);
  }
  return mask;
}

constexpr board_core::Bitboard mask_for_edges() noexcept {
  board_core::Bitboard mask = 0;
  for (int index = 0; index < board_core::kSquareCount; ++index) {
    const board_core::Square square = board_core::square_from_index(index);
    const int file = board_core::file_of(square);
    const int rank = board_core::rank_of(square);
    if (file == 0 || file == board_core::kBoardSize - 1 || rank == 0 ||
        rank == board_core::kBoardSize - 1) {
      mask |= board_core::bit(square);
    }
  }
  return mask;
}

constexpr std::array<board_core::Bitboard, board_core::kSquareCount>
corner_by_adjacent_square() noexcept {
  std::array<board_core::Bitboard, board_core::kSquareCount> corners{};
  for (const CornerAdjacency adjacency : kCornerAdjacency) {
    const board_core::Bitboard corner = board_core::bit(adjacency.corner);
    corners[adjacency.x_square.index] = corner;
    corners[adjacency.c_squares[0].index] = corner;
    corners[adjacency.c_squares[1].index] = corner;
  }
  return corners;
}

constexpr board_core::Bitboard kCornerMask = mask_for_corners();
constexpr board_core::Bitboard kXSquareMask = mask_for_x_squares();
constexpr board_core::Bitboard kCSquareMask = mask_for_c_squares();
constexpr board_core::Bitboard kEdgeMask = mask_for_edges();
constexpr auto kCornerByAdjacentSquare = corner_by_adjacent_square();

} // namespace

void add_static_othello_features(board_core::Position position, board_core::Move move,
                                 MoveOrderFeatures* features) noexcept {
  const board_core::Bitboard move_bit = board_core::bit(move.square);
  const board_core::Bitboard adjacent_corner = kCornerByAdjacentSquare[move.square.index];
  const bool adjacent_corner_is_empty = (board_core::occupied(position) & adjacent_corner) == 0;
  features->is_corner = (move_bit & kCornerMask) != 0;
  features->is_edge = (move_bit & kEdgeMask) != 0;
  features->is_stable_like_edge =
      (move_bit & kCSquareMask) != 0 && (position.player & adjacent_corner) != 0;
  features->is_dangerous_x = (move_bit & kXSquareMask) != 0 && adjacent_corner_is_empty;
  features->is_dangerous_c = (move_bit & kCSquareMask) != 0 && adjacent_corner_is_empty;
}

} // namespace vibe_othello::search::internal
