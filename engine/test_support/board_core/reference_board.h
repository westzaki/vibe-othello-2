#ifndef VIBE_OTHELLO_TEST_SUPPORT_BOARD_CORE_REFERENCE_BOARD_H_
#define VIBE_OTHELLO_TEST_SUPPORT_BOARD_CORE_REFERENCE_BOARD_H_

#include "vibe_othello/board_core/board.h"

namespace vibe_othello::board_core::test_support {

bool reference_apply_move(Position* position, Move move, MoveDelta* delta) noexcept;
bool reference_apply_pass(Position* position, MoveDelta* delta) noexcept;
Bitboard reference_flips_for_move(Position position, Square move) noexcept;
bool reference_has_legal_move(Position position) noexcept;
bool reference_is_terminal(Position position) noexcept;
Bitboard reference_legal_moves(Position position) noexcept;

} // namespace vibe_othello::board_core::test_support

#endif // VIBE_OTHELLO_TEST_SUPPORT_BOARD_CORE_REFERENCE_BOARD_H_
