#include "move_ordering_features_internal.h"

#include <array>

namespace vibe_othello::search::internal {
namespace {

MoveOrderFeatures extract_endgame_features(board_core::Position position, board_core::Move move,
                                           EndgameOrderingHints hints,
                                           const EmptyRegionMap* regions) noexcept {
  MoveOrderFeatures features{};
  add_static_othello_features(position, move, &features);
  features.opponent_mobility_after = opponent_mobility_after(position, move);
  features.matches_tt = hints.tt_best_move.has_value() && move == *hints.tt_best_move;
  features.matches_root_best = hints.root_best_move.has_value() && move == *hints.root_best_move;
  if (hints.use_parity_ordering && regions != nullptr) {
    features.parity_region_bonus = parity_region_order_score(move, *regions);
  }
  return features;
}

} // namespace

int score_endgame_move(MoveOrderFeatures features, EndgameOrderingWeights weights) noexcept {
  int score = 0;

  if (features.matches_tt) {
    score += weights.tt_best;
  }
  if (features.matches_root_best) {
    score += weights.root_best;
  }

  if (features.is_corner) {
    score += weights.corner;
  }
  if (features.is_dangerous_x) {
    score += weights.dangerous_x;
  }
  if (features.is_dangerous_c) {
    score += weights.dangerous_c;
  }
  if (features.is_edge) {
    score += weights.edge;
  }
  if (features.is_stable_like_edge) {
    score += weights.stable_like_edge;
  }

  score +=
      (board_core::kSquareCount - features.opponent_mobility_after) * weights.opponent_mobility;
  score += features.parity_region_bonus * weights.parity_region;

  return score;
}

MoveList order_endgame_moves(board_core::Position position, EndgameOrderingHints hints) noexcept {
  return order_endgame_moves(position, board_core::legal_moves(position), hints);
}

MoveList order_endgame_moves(board_core::Position position, board_core::Bitboard legal_moves,
                             EndgameOrderingHints hints) noexcept {
  const EmptyRegionMap regions =
      hints.use_parity_ordering ? build_empty_region_map(position) : EmptyRegionMap{};
  MoveList list = move_list_from_legal_mask(legal_moves);
  std::array<int, board_core::kSquareCount> scores;
  const EndgameOrderingWeights weights{};
  for (std::uint8_t index = 0; index < list.size; ++index) {
    const board_core::Move move = list.moves[index];
    scores[index] =
        score_endgame_move(extract_endgame_features(position, move, hints,
                                                    hints.use_parity_ordering ? &regions : nullptr),
                           weights);
  }
  sort_move_list_from_scores(&list, scores);
  return list;
}

} // namespace vibe_othello::search::internal
