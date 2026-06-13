#ifndef VIBE_OTHELLO_SRC_BOARD_CORE_DIRECTIONS_H_
#define VIBE_OTHELLO_SRC_BOARD_CORE_DIRECTIONS_H_

#include "vibe_othello/board_core/coordinates.h"

namespace vibe_othello::board_core {
namespace detail {

inline constexpr Bitboard kFileA = 0x0101010101010101ULL;
inline constexpr Bitboard kFileH = 0x8080808080808080ULL;

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

} // namespace detail
} // namespace vibe_othello::board_core

#endif // VIBE_OTHELLO_SRC_BOARD_CORE_DIRECTIONS_H_
