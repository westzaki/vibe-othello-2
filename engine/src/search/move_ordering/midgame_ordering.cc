#include "move_ordering_features_internal.h"

#include <algorithm>
#include <array>

namespace vibe_othello::search::internal {
namespace {

MoveOrderFeatures extract_midgame_features(board_core::Position position, board_core::Move move,
                                           MidgameOrderingHints hints) noexcept {
  MoveOrderFeatures features{};
  add_static_othello_features(position, move, &features);

  features.matches_root_best = hints.root_best_move.has_value() && move == *hints.root_best_move;
  features.matches_tt = hints.tt_best_move.has_value() && move == *hints.tt_best_move;
  features.matches_iid = hints.iid_best_move.has_value() && move == *hints.iid_best_move;

  if (hints.use_opponent_mobility) {
    features.opponent_mobility_after = opponent_mobility_after(position, move);
  }

  for (const board_core::Move killer : hints.killer_moves) {
    if (move == killer) {
      features.matches_killer = true;
    }
  }
  if (hints.history != nullptr) {
    features.history = (*hints.history)[move.square.index];
  }

  return features;
}

} // namespace

int score_midgame_move(MoveOrderFeatures features, MidgameOrderingWeights weights) noexcept {
  int score = 0;

  if (features.matches_root_best) {
    score += weights.root_best;
  }
  if (features.matches_tt) {
    score += weights.tt_best;
  }
  if (features.matches_iid) {
    score += weights.iid_best;
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

  if (features.matches_killer) {
    score += weights.killer;
  }
  score += std::clamp(features.history, weights.history_min, weights.history_max);

  return score;
}

MoveList order_midgame_moves(board_core::Position position, MidgameOrderingHints hints) noexcept {
  return order_midgame_moves(position, board_core::legal_moves(position), hints);
}

MoveList order_midgame_moves(board_core::Position position, board_core::Bitboard legal_moves,
                             MidgameOrderingHints hints) noexcept {
  MoveList list = move_list_from_legal_mask(legal_moves);
  std::array<int, board_core::kSquareCount> scores;
  const MidgameOrderingWeights weights{};
  for (std::uint8_t index = 0; index < list.size; ++index) {
    const board_core::Move move = list.moves[index];
    scores[index] = score_midgame_move(extract_midgame_features(position, move, hints), weights);
  }
  sort_move_list_from_scores(&list, scores);
  return list;
}

MoveList ordered_moves(board_core::Position position, MoveOrderingHints hints) noexcept {
  return order_midgame_moves(position, hints);
}

} // namespace vibe_othello::search::internal
