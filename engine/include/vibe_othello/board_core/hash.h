#ifndef VIBE_OTHELLO_BOARD_CORE_HASH_H_
#define VIBE_OTHELLO_BOARD_CORE_HASH_H_

#include "vibe_othello/board_core/position.h"

#include <cstdint>

namespace vibe_othello::board_core {

using PositionHash = std::uint64_t;

// Precondition: position must be valid.
PositionHash hash_position(Position position) noexcept;

} // namespace vibe_othello::board_core

#endif // VIBE_OTHELLO_BOARD_CORE_HASH_H_
