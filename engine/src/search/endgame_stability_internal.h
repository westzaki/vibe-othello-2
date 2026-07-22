#pragma once

#include "endgame_context_internal.h"
#include "search_node_internal.h"
#include "vibe_othello/board_core/position.h"

#include <optional>

namespace vibe_othello::search::internal {

struct EndgameStabilityBounds {
  Score lower = kScoreLoss;
  Score upper = kScoreWin;
  board_core::Bitboard stable_player = 0;
  board_core::Bitboard stable_opponent = 0;
};

enum class EndgameStabilityCutoffKind : std::uint8_t {
  lower,
  upper,
};

struct EndgameStabilityCutoffCandidate {
  EndgameStabilityCutoffKind kind = EndgameStabilityCutoffKind::lower;
  Score score = 0;
  Score threshold = 0;
};

board_core::Bitboard stable_discs(board_core::Bitboard discs,
                                  board_core::Bitboard occupied) noexcept;
EndgameStabilityBounds endgame_stability_bounds(board_core::Position position) noexcept;
std::optional<EndgameStabilityCutoffCandidate>
probe_endgame_stability(EndgameContext* context, Score alpha, Score beta,
                        std::uint8_t empties) noexcept;
void verify_endgame_stability_shadow(EndgameContext* context,
                                     std::optional<EndgameStabilityCutoffCandidate> candidate,
                                     const SearchNodeResult& result) noexcept;

} // namespace vibe_othello::search::internal
