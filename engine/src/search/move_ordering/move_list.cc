#include "move_ordering_features_internal.h"

#include <array>

namespace vibe_othello::search::internal {
namespace {

struct ScoredMove {
  board_core::Move move;
  int score = 0;
};

bool comes_before(ScoredMove left, ScoredMove right) noexcept {
  if (left.score != right.score) {
    return left.score > right.score;
  }
  return left.move.square.index < right.move.square.index;
}

void insertion_sort_scored_moves(std::array<ScoredMove, board_core::kSquareCount>* moves,
                                 std::uint8_t size) noexcept {
  for (std::uint8_t index = 1; index < size; ++index) {
    const ScoredMove current = (*moves)[index];
    std::uint8_t shift = index;
    while (shift > 0 && comes_before(current, (*moves)[shift - 1])) {
      (*moves)[shift] = (*moves)[shift - 1];
      --shift;
    }
    (*moves)[shift] = current;
  }
}

} // namespace

bool matches_normal_move(board_core::Move candidate, board_core::Move move) noexcept {
  return is_normal_move(candidate) && candidate == move;
}

MoveList move_list_from_legal_mask(board_core::Bitboard legal_moves) noexcept {
  MoveList list{};
  for (int square_index = 0; square_index < board_core::kSquareCount; ++square_index) {
    const board_core::Square square = board_core::square_from_index(square_index);
    if ((legal_moves & board_core::bit(square)) != 0) {
      list.moves[list.size] = board_core::make_move(square);
      ++list.size;
    }
  }
  return list;
}

MoveList
sorted_move_list_from_scores(MoveList list,
                             const std::array<int, board_core::kSquareCount>& scores) noexcept {
  std::array<ScoredMove, board_core::kSquareCount> scored_moves{};
  for (std::uint8_t index = 0; index < list.size; ++index) {
    const board_core::Move move = list.moves[index];
    scored_moves[index] = ScoredMove{
        .move = move,
        .score = scores[move.square.index],
    };
  }

  insertion_sort_scored_moves(&scored_moves, list.size);

  for (std::uint8_t index = 0; index < list.size; ++index) {
    list.moves[index] = scored_moves[index].move;
  }
  return list;
}

} // namespace vibe_othello::search::internal
