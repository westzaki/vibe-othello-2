#include "search_internal.h"

namespace vibe_othello::search::internal {

namespace {

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

void move_to_front(MoveList* list, board_core::Move move) noexcept {
  for (std::uint8_t index = 0; index < list->size; ++index) {
    if (list->moves[index] != move) {
      continue;
    }

    for (std::uint8_t shift = index; shift > 0; --shift) {
      list->moves[shift] = list->moves[shift - 1];
    }
    list->moves[0] = move;
    return;
  }
}

} // namespace

MoveList ordered_moves(board_core::Position position, MoveOrderingHints hints) noexcept {
  MoveList list = move_list_from_legal_mask(board_core::legal_moves(position));
  if (hints.first_move.has_value() && hints.first_move->kind == board_core::MoveKind::normal) {
    move_to_front(&list, *hints.first_move);
  }
  return list;
}

} // namespace vibe_othello::search::internal
