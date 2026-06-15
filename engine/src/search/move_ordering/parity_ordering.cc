#include "move_ordering_features_internal.h"

#include <array>

namespace vibe_othello::search::internal {
namespace {

void push_empty_neighbor(board_core::Bitboard* remaining_empties, int file, int rank,
                         std::array<board_core::Square, board_core::kSquareCount>* pending,
                         std::uint8_t* pending_size) noexcept {
  const board_core::Square neighbor = board_core::square_from_file_rank(file, rank);
  const board_core::Bitboard neighbor_bit = board_core::bit(neighbor);
  if ((*remaining_empties & neighbor_bit) == 0) {
    return;
  }

  (*pending)[*pending_size] = neighbor;
  ++(*pending_size);
  *remaining_empties &= ~neighbor_bit;
}

} // namespace

EmptyRegionMap build_empty_region_map(board_core::Position position) noexcept {
  EmptyRegionMap map{};
  map.region_for_square.fill(kNoEmptyRegion);

  board_core::Bitboard remaining_empties = ~board_core::occupied(position);
  for (int square_index = 0; square_index < board_core::kSquareCount; ++square_index) {
    const board_core::Square start = board_core::square_from_index(square_index);
    if ((remaining_empties & board_core::bit(start)) == 0) {
      continue;
    }

    std::array<board_core::Square, board_core::kSquareCount> pending{};
    std::array<board_core::Square, board_core::kSquareCount> region_squares{};
    std::uint8_t pending_size = 0;
    std::uint8_t region_size = 0;
    pending[pending_size] = start;
    ++pending_size;
    remaining_empties &= ~board_core::bit(start);

    while (pending_size > 0) {
      --pending_size;
      const board_core::Square square = pending[pending_size];
      region_squares[region_size] = square;
      ++region_size;

      const int file = board_core::file_of(square);
      const int rank = board_core::rank_of(square);
      push_empty_neighbor(&remaining_empties, file - 1, rank, &pending, &pending_size);
      push_empty_neighbor(&remaining_empties, file + 1, rank, &pending, &pending_size);
      push_empty_neighbor(&remaining_empties, file, rank - 1, &pending, &pending_size);
      push_empty_neighbor(&remaining_empties, file, rank + 1, &pending, &pending_size);
    }

    const std::uint8_t region_id = map.size;
    map.region_sizes[region_id] = region_size;
    for (std::uint8_t index = 0; index < region_size; ++index) {
      map.region_for_square[region_squares[index].index] = region_id;
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

} // namespace vibe_othello::search::internal
