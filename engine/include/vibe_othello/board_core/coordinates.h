#ifndef VIBE_OTHELLO_BOARD_CORE_COORDINATES_H_
#define VIBE_OTHELLO_BOARD_CORE_COORDINATES_H_

#include "vibe_othello/board_core/types.h"

namespace vibe_othello::board_core {

inline constexpr int kBoardSize = 8;
inline constexpr int kSquareCount = kBoardSize * kBoardSize;
inline constexpr std::uint8_t kInvalidSquareIndex = kSquareCount;

constexpr bool is_valid_square_index(int index) noexcept {
  return 0 <= index && index < kSquareCount;
}

constexpr bool is_valid_file_rank(int file, int rank) noexcept {
  return 0 <= file && file < kBoardSize && 0 <= rank && rank < kBoardSize;
}

constexpr bool is_valid(Square square) noexcept {
  return square.index < kSquareCount;
}

constexpr Square square_from_index(int index) noexcept {
  return Square{is_valid_square_index(index) ? static_cast<std::uint8_t>(index)
                                             : kInvalidSquareIndex};
}

constexpr Square square_from_file_rank(int file, int rank) noexcept {
  return is_valid_file_rank(file, rank) ? square_from_index((rank * kBoardSize) + file)
                                        : Square{kInvalidSquareIndex};
}

constexpr int file_of(Square square) noexcept {
  return is_valid(square) ? square.index % kBoardSize : -1;
}

constexpr int rank_of(Square square) noexcept {
  return is_valid(square) ? square.index / kBoardSize : -1;
}

constexpr Bitboard bit(Square square) noexcept {
  return is_valid(square) ? Bitboard{1} << square.index : Bitboard{0};
}

} // namespace vibe_othello::board_core

#endif // VIBE_OTHELLO_BOARD_CORE_COORDINATES_H_
