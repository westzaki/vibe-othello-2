#include "move_ordering_features_internal.h"

#include <array>
#include <bit>
#include <cstdint>

namespace vibe_othello::search::internal {
namespace {

constexpr int kSquareIndexBits = 6;
static_assert(board_core::kSquareCount == (1 << kSquareIndexBits));

std::uint64_t ordering_key(board_core::Move move, int score) noexcept {
  constexpr std::uint32_t kSignedOrderBias = std::uint32_t{1} << 31;
  const std::uint64_t ordered_score = static_cast<std::uint32_t>(score) ^ kSignedOrderBias;
  const std::uint64_t square_tiebreak = board_core::kSquareCount - 1 - move.square.index;
  return (ordered_score << kSquareIndexBits) | square_tiebreak;
}

} // namespace

MoveList move_list_from_legal_mask(board_core::Bitboard legal_moves) noexcept {
  MoveList list;
  list.size = 0;
  while (legal_moves != 0) {
    const int square_index = std::countr_zero(legal_moves);
    list.moves[list.size] = board_core::make_move(board_core::square_from_index(square_index));
    ++list.size;
    legal_moves &= legal_moves - 1;
  }
  return list;
}

void sort_move_list_from_scores(MoveList* list,
                                const std::array<int, board_core::kSquareCount>& scores) noexcept {
  std::array<std::uint64_t, board_core::kSquareCount> keys;
  for (std::uint8_t index = 0; index < list->size; ++index) {
    keys[index] = ordering_key(list->moves[index], scores[index]);
  }

  for (std::uint8_t index = 1; index < list->size; ++index) {
    const std::uint64_t current = keys[index];
    std::uint8_t shift = index;
    while (shift > 0 && current > keys[shift - 1]) {
      keys[shift] = keys[shift - 1];
      --shift;
    }
    keys[shift] = current;
  }

  constexpr std::uint64_t kSquareMask = board_core::kSquareCount - 1;
  for (std::uint8_t index = 0; index < list->size; ++index) {
    const auto square_index =
        static_cast<int>(board_core::kSquareCount - 1 - (keys[index] & kSquareMask));
    list->moves[index] = board_core::make_move(board_core::square_from_index(square_index));
  }
}

} // namespace vibe_othello::search::internal
