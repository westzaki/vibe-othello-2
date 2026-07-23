#include "move_ordering_features_internal.h"

#include <bit>

namespace vibe_othello::search::internal {
namespace {

constexpr board_core::Bitboard kFileA = 0x0101010101010101ULL;
constexpr board_core::Bitboard kFileH = 0x8080808080808080ULL;

constexpr board_core::Bitboard orthogonal_neighbors(board_core::Bitboard squares) noexcept {
  return ((squares & ~kFileH) << 1) | ((squares & ~kFileA) >> 1) | (squares << 8) | (squares >> 8);
}

} // namespace

EmptyRegionMap build_empty_region_map(board_core::Position position) noexcept {
  EmptyRegionMap map{};
  map.region_for_square.fill(kNoEmptyRegion);

  board_core::Bitboard remaining_empties = ~board_core::occupied(position);
  while (remaining_empties != 0) {
    board_core::Bitboard frontier = remaining_empties & -remaining_empties;
    board_core::Bitboard region = 0;
    while (frontier != 0) {
      region |= frontier;
      remaining_empties &= ~frontier;
      frontier = orthogonal_neighbors(frontier) & remaining_empties;
    }

    const std::uint8_t region_size = static_cast<std::uint8_t>(std::popcount(region));
    const std::uint8_t region_id = map.size;
    map.region_sizes[region_id] = region_size;
    while (region != 0) {
      const int square_index = std::countr_zero(region);
      region &= region - 1;
      map.region_for_square[square_index] = region_id;
    }
    ++map.size;
  }

  return map;
}

int parity_region_order_score(board_core::Move move, const EmptyRegionMap& regions) noexcept {
  if (!is_normal_move(move)) {
    return 0;
  }

  const std::uint8_t region_id = regions.region_for_square[move.square.index];
  if (region_id == kNoEmptyRegion) {
    return 0;
  }

  // First policy: with fixed 4-neighbor regions, odd empty regions are treated
  // as favorable ordering hints and even regions receive no bonus.
  return (regions.region_sizes[region_id] % 2) == 1 ? 20'000 : 0;
}

MoveList order_endgame_moves_by_parity(board_core::Position position,
                                       board_core::Bitboard legal_moves) noexcept {
  const EmptyRegionMap regions = build_empty_region_map(position);
  MoveList list{};
  for (int parity_pass = 0; parity_pass < 2; ++parity_pass) {
    const bool odd_region = parity_pass == 0;
    board_core::Bitboard remaining = legal_moves;
    while (remaining != 0) {
      const int square_index = std::countr_zero(remaining);
      remaining &= remaining - 1;
      const std::uint8_t region_id = regions.region_for_square[square_index];
      if (region_id == kNoEmptyRegion) {
        if (!odd_region) {
          list.moves[list.size] =
              board_core::make_move(board_core::square_from_index(square_index));
          ++list.size;
        }
        continue;
      }
      if (((regions.region_sizes[region_id] % 2) == 1) != odd_region) {
        continue;
      }
      list.moves[list.size] = board_core::make_move(board_core::square_from_index(square_index));
      ++list.size;
    }
  }
  return list;
}

} // namespace vibe_othello::search::internal
