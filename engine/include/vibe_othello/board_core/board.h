#ifndef VIBE_OTHELLO_BOARD_CORE_BOARD_H_
#define VIBE_OTHELLO_BOARD_CORE_BOARD_H_

#include "vibe_othello/board_core/position.h"

namespace vibe_othello::board_core {

Bitboard flips_for_move(Position position, Square move) noexcept;
Bitboard legal_moves(Position position) noexcept;

} // namespace vibe_othello::board_core

#endif // VIBE_OTHELLO_BOARD_CORE_BOARD_H_
