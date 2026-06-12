#ifndef VIBE_OTHELLO_BOARD_CORE_TYPES_H_
#define VIBE_OTHELLO_BOARD_CORE_TYPES_H_

#include <cstdint>

namespace vibe_othello::board_core {

using Bitboard = std::uint64_t;

enum class Color : std::uint8_t {
  black,
  white,
};

struct Square {
  std::uint8_t index;

  friend constexpr bool operator==(Square, Square) noexcept = default;
};

} // namespace vibe_othello::board_core

#endif // VIBE_OTHELLO_BOARD_CORE_TYPES_H_
