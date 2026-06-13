#include "search_internal.h"
#include "vibe_othello/board_core/hash.h"

namespace vibe_othello::search::internal {

std::optional<board_core::Move> TranspositionTable::probe(board_core::Position position,
                                                          SearchStats* stats) const noexcept {
  ++stats->tt_probes;

  const board_core::PositionHash key = board_core::hash_position(position);
  const TTEntry& entry = entries_[index_for(key)];
  if (!entry.occupied || entry.key != key) {
    return std::nullopt;
  }

  ++stats->tt_hits;
  return entry.best_move;
}

void TranspositionTable::store(board_core::Position position, Depth depth, Score score,
                               BoundType bound, board_core::Move best_move, TTEntryKind kind,
                               SearchStats* stats) noexcept {
  if (best_move.kind != board_core::MoveKind::normal) {
    return;
  }

  const board_core::PositionHash key = board_core::hash_position(position);
  entries_[index_for(key)] = TTEntry{
      .key = key,
      .depth = depth,
      .score = score,
      .bound = bound,
      .best_move = best_move,
      .generation = 0,
      .kind = kind,
      .occupied = true,
  };
  ++stats->tt_stores;
}

} // namespace vibe_othello::search::internal
