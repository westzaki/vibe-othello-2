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

constexpr bool is_corner(board_core::Square square) noexcept {
  for (const CornerAdjacency adjacency : kCornerAdjacency) {
    if (square == adjacency.corner) {
      return true;
    }
  }
  return false;
}

constexpr bool is_edge(board_core::Square square) noexcept {
  const int file = board_core::file_of(square);
  const int rank = board_core::rank_of(square);
  return file == 0 || file == board_core::kBoardSize - 1 || rank == 0 ||
         rank == board_core::kBoardSize - 1;
}

bool is_dangerous_x_square(board_core::Position position, board_core::Square square) noexcept {
  for (const CornerAdjacency adjacency : kCornerAdjacency) {
    if (square != adjacency.x_square) {
      continue;
    }

    return (board_core::occupied(position) & board_core::bit(adjacency.corner)) == 0;
  }
  return false;
}

bool is_dangerous_c_square(board_core::Position position, board_core::Square square) noexcept {
  for (const CornerAdjacency adjacency : kCornerAdjacency) {
    if (square != adjacency.c_squares[0] && square != adjacency.c_squares[1]) {
      continue;
    }

    return (board_core::occupied(position) & board_core::bit(adjacency.corner)) == 0;
  }
  return false;
}

bool is_stable_like_edge(board_core::Position position, board_core::Square square) noexcept {
  if (!is_edge(square)) {
    return false;
  }

  for (const CornerAdjacency adjacency : kCornerAdjacency) {
    if ((position.player & board_core::bit(adjacency.corner)) == 0) {
      continue;
    }

    if (square == adjacency.c_squares[0] || square == adjacency.c_squares[1]) {
      return true;
    }
  }
  return false;
}

} // namespace

void add_static_othello_features(board_core::Position position, board_core::Move move,
                                 MoveOrderFeatures* features) noexcept {
  features->is_corner = is_corner(move.square);
  features->is_edge = is_edge(move.square);
  features->is_stable_like_edge = is_stable_like_edge(position, move.square);
  features->is_dangerous_x = is_dangerous_x_square(position, move.square);
  features->is_dangerous_c = is_dangerous_c_square(position, move.square);
}

} // namespace vibe_othello::search::internal
