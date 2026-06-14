#ifndef VIBE_OTHELLO_TEST_SUPPORT_BOARD_CORE_PERFT_H_
#define VIBE_OTHELLO_TEST_SUPPORT_BOARD_CORE_PERFT_H_

#include "vibe_othello/board_core/board.h"

#include <cstdint>

namespace vibe_othello::board_core::test_support {

using PerftCount = std::uint64_t;

PerftCount perft(Position position, int depth) noexcept;
PerftCount reference_perft(Position position, int depth) noexcept;

} // namespace vibe_othello::board_core::test_support

#endif // VIBE_OTHELLO_TEST_SUPPORT_BOARD_CORE_PERFT_H_
