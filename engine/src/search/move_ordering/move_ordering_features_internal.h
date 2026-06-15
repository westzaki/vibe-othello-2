#pragma once

#include "../move_ordering_internal.h"

#include <array>

namespace vibe_othello::search::internal {

struct MoveOrderFeatures {
  bool is_corner = false;
  bool is_edge = false;
  bool is_stable_like_edge = false;
  bool is_dangerous_x = false;
  bool is_dangerous_c = false;
  int opponent_mobility_after = board_core::kSquareCount;
  int parity_region_bonus = 0;
  bool matches_tt = false;
  bool matches_root_best = false;
  bool matches_iid = false;
  bool matches_killer = false;
  int history = 0;
};

struct MidgameOrderingWeights {
  int root_best = 2'000'000;
  int tt_best = 1'000'000;
  int iid_best = 500'000;
  int corner = 100'000;
  int dangerous_x = -50'000;
  int dangerous_c = -20'000;
  int edge = 2'000;
  int stable_like_edge = 1'000;
  int opponent_mobility = 10;
  int killer = 100;
  int history_min = -99;
  int history_max = 99;
};

struct EndgameOrderingWeights {
  int tt_best = 2'000'000;
  int root_best = 1'000'000;
  int corner = 100'000;
  int dangerous_x = -50'000;
  int dangerous_c = -20'000;
  int edge = 2'000;
  int stable_like_edge = 1'000;
  int opponent_mobility = 10;
  int parity_region = 1;
};

struct EmptyRegionMap {
  std::array<std::uint8_t, board_core::kSquareCount> region_for_square{};
  std::array<std::uint8_t, board_core::kSquareCount> region_sizes{};
  std::uint8_t size = 0;
};

constexpr std::uint8_t kNoEmptyRegion = board_core::kSquareCount;

constexpr bool is_normal_move(board_core::Move move) noexcept {
  return move.kind == board_core::MoveKind::normal;
}

bool matches_normal_move(board_core::Move candidate, board_core::Move move) noexcept;

MoveList move_list_from_legal_mask(board_core::Bitboard legal_moves) noexcept;
MoveList
sorted_move_list_from_scores(MoveList list,
                             const std::array<int, board_core::kSquareCount>& scores) noexcept;

void add_static_othello_features(board_core::Position position, board_core::Move move,
                                 MoveOrderFeatures* features) noexcept;

int opponent_mobility_after(board_core::Position position, board_core::Move move) noexcept;

EmptyRegionMap build_empty_region_map(board_core::Position position) noexcept;
int parity_region_order_score(board_core::Move move, const EmptyRegionMap& regions) noexcept;

int score_midgame_move(MoveOrderFeatures features, MidgameOrderingWeights weights) noexcept;
int score_endgame_move(MoveOrderFeatures features, EndgameOrderingWeights weights) noexcept;

} // namespace vibe_othello::search::internal
