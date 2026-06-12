#ifndef VIBE_OTHELLO_BOARD_CORE_COORDINATES_H_
#define VIBE_OTHELLO_BOARD_CORE_COORDINATES_H_

#include "vibe_othello/board_core/types.h"

namespace vibe_othello::board_core {

inline constexpr int kBoardSize = 8;
inline constexpr int kSquareCount = kBoardSize * kBoardSize;

inline constexpr Bitboard kFileA = 0x0101010101010101ULL;
inline constexpr Bitboard kFileH = 0x8080808080808080ULL;
inline constexpr Bitboard kRank1 = 0x00000000000000FFULL;
inline constexpr Bitboard kRank8 = 0xFF00000000000000ULL;

constexpr Square square_from_index(int index) noexcept {
  return Square{static_cast<std::uint8_t>(index)};
}

constexpr Square square_from_file_rank(int file, int rank) noexcept {
  return square_from_index((rank * kBoardSize) + file);
}

constexpr int file_of(Square square) noexcept {
  return square.index % kBoardSize;
}

constexpr int rank_of(Square square) noexcept {
  return square.index / kBoardSize;
}

constexpr Bitboard bit(Square square) noexcept {
  return Bitboard{1} << square.index;
}

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

} // namespace vibe_othello::board_core

#endif // VIBE_OTHELLO_BOARD_CORE_COORDINATES_H_
