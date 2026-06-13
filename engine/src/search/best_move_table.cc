#include "search_internal.h"
#include "vibe_othello/board_core/hash.h"

namespace vibe_othello::search::internal {

std::optional<board_core::Move> TTBestMoveTable::probe(board_core::Position position,
                                                       SearchStats* stats) const noexcept {
  ++stats->tt_probes;

  const board_core::PositionHash key = board_core::hash_position(position);
  const Entry& entry = entries_[index_for(key)];
  if (!entry.occupied || entry.key != key) {
    return std::nullopt;
  }

  ++stats->tt_hits;
  return entry.best_move;
}

void TTBestMoveTable::store(board_core::Position position, board_core::Move best_move,
                            SearchStats* stats) noexcept {
  if (best_move.kind != board_core::MoveKind::normal) {
    return;
  }

  const board_core::PositionHash key = board_core::hash_position(position);
  entries_[index_for(key)] = Entry{
      .key = key,
      .best_move = best_move,
      .occupied = true,
  };
  ++stats->tt_stores;
}

} // namespace vibe_othello::search::internal
