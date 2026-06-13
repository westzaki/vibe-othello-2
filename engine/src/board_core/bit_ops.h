#ifndef VIBE_OTHELLO_SRC_BOARD_CORE_BIT_OPS_H_
#define VIBE_OTHELLO_SRC_BOARD_CORE_BIT_OPS_H_

#include "vibe_othello/board_core/types.h"

#include <bit>

namespace vibe_othello::board_core {
namespace detail {

// Precondition: bits != 0.
[[nodiscard]] inline int pop_lsb_index(Bitboard& bits) noexcept {
  const int index = std::countr_zero(bits);
  bits &= bits - 1;
  return index;
}

} // namespace detail
} // namespace vibe_othello::board_core

#endif // VIBE_OTHELLO_SRC_BOARD_CORE_BIT_OPS_H_
