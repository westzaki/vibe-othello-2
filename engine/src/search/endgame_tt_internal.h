#pragma once

#include "endgame_context_internal.h"

#include <optional>

namespace vibe_othello::search::internal {

struct ExactEndgameTtProbe {
  std::optional<Score> cutoff_score;
  std::optional<board_core::Move> best_move;
};

ExactEndgameTtProbe exact_endgame_score_tt_probe(const TTEntry& entry,
                                                 board_core::Position position,
                                                 Depth remaining_empties, Score alpha,
                                                 Score beta) noexcept;
ExactEndgameTtProbe exact_endgame_wld_tt_probe(const TTEntry& entry, board_core::Position position,
                                               Depth remaining_empties, Score alpha,
                                               Score beta) noexcept;

ExactEndgameTtProbe probe_exact_score_endgame_tt(EndgameContext* context, Depth remaining_empties,
                                                 Score alpha, Score beta);
ExactEndgameTtProbe probe_wld_endgame_tt(EndgameContext* context, Depth remaining_empties,
                                         Score alpha, Score beta);
std::optional<board_core::Move>
probe_exact_score_endgame_root_tt_best_move(EndgameContext* context, Depth remaining_empties);
std::optional<board_core::Move> probe_wld_endgame_root_tt_best_move(EndgameContext* context,
                                                                    Depth remaining_empties);
void store_exact_score_endgame_tt(EndgameContext* context, Depth remaining_empties, Score score,
                                  BoundType bound,
                                  std::optional<board_core::Move> best_move = std::nullopt);
void store_wld_endgame_tt(EndgameContext* context, Depth remaining_empties, Score score,
                          BoundType bound,
                          std::optional<board_core::Move> best_move = std::nullopt);

} // namespace vibe_othello::search::internal
