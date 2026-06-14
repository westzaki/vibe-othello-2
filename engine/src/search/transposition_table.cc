#include "search_internal.h"
#include "vibe_othello/board_core/hash.h"

#include <limits>

namespace vibe_othello::search::internal {

namespace {

std::size_t bucket_count_for(std::size_t requested_entries) noexcept {
  std::size_t bucket_count = requested_entries / TranspositionTable::kBucketWidth;
  if (requested_entries % TranspositionTable::kBucketWidth != 0) {
    ++bucket_count;
  }
  if (bucket_count == 0) {
    bucket_count = 1;
  }
  if (bucket_count > TranspositionTable::kMaxBucketCount) {
    return TranspositionTable::kMaxBucketCount;
  }

  std::size_t power_of_two = 1;
  while (power_of_two < bucket_count) {
    power_of_two <<= 1;
  }
  return power_of_two;
}

TTEntry make_entry(board_core::PositionHash key, Depth depth, Score score, BoundType bound,
                   std::optional<board_core::Move> best_move, TTEntryKind kind,
                   std::uint8_t generation) noexcept {
  return TTEntry{
      .key = key,
      .depth = depth,
      .score = score,
      .bound = bound,
      .best_move = best_move.value_or(board_core::make_pass()),
      .has_best_move = best_move.has_value(),
      .generation = generation,
      .kind = kind,
      .occupied = true,
  };
}

} // namespace

TranspositionTable::TranspositionTable(std::size_t requested_entries)
    : buckets_(bucket_count_for(requested_entries)) {}

void TranspositionTable::clear() noexcept {
  for (TTBucket& bucket : buckets_) {
    bucket = TTBucket{};
  }
}

void TranspositionTable::new_generation() noexcept {
  if (generation_ == std::numeric_limits<std::uint8_t>::max()) {
    clear();
    generation_ = 1;
    return;
  }
  ++generation_;
}

std::size_t TranspositionTable::index_for(board_core::PositionHash key) const noexcept {
  return static_cast<std::size_t>(key) & (buckets_.size() - 1);
}

std::optional<TTEntry> TranspositionTable::probe(board_core::Position position,
                                                 SearchStats* stats) const noexcept {
  ++stats->tt_probes;

  const board_core::PositionHash key = board_core::hash_position(position);
  const TTBucket& bucket = buckets_[index_for(key)];
  for (const TTEntry& entry : bucket.entries) {
    if (entry.occupied && entry.key == key) {
      ++stats->tt_hits;
      return entry;
    }
  }

  return std::nullopt;
}

void TranspositionTable::store(board_core::Position position, Depth depth, Score score,
                               BoundType bound, board_core::Move best_move, TTEntryKind kind,
                               SearchStats* stats) noexcept {
  if (best_move.kind != board_core::MoveKind::normal ||
      (board_core::legal_moves(position) & board_core::bit(best_move.square)) == 0) {
    ++stats->tt_invalid_best_move_stores;
    return;
  }

  store_entry(position, depth, score, bound, best_move, kind, stats);
}

void TranspositionTable::store_value(board_core::Position position, Depth depth, Score score,
                                     BoundType bound, TTEntryKind kind,
                                     SearchStats* stats) noexcept {
  store_entry(position, depth, score, bound, std::nullopt, kind, stats);
}

void TranspositionTable::store_entry(board_core::Position position, Depth depth, Score score,
                                     BoundType bound, std::optional<board_core::Move> best_move,
                                     TTEntryKind kind, SearchStats* stats) noexcept {
  const board_core::PositionHash key = board_core::hash_position(position);
  TTBucket& bucket = buckets_[index_for(key)];
  const TTEntry incoming = make_entry(key, depth, score, bound, best_move, kind, generation_);

  bool saw_occupied_different_key = false;
  for (TTEntry& entry : bucket.entries) {
    if (!entry.occupied) {
      continue;
    }
    if (entry.key == key) {
      entry = incoming;
      ++stats->tt_stores;
      return;
    }
    saw_occupied_different_key = true;
  }
  if (saw_occupied_different_key) {
    ++stats->tt_collisions;
  }

  for (TTEntry& entry : bucket.entries) {
    if (!entry.occupied) {
      entry = incoming;
      ++stats->tt_stores;
      return;
    }
  }

  TTEntry* victim = &bucket.entries[0];
  for (TTEntry& entry : bucket.entries) {
    if (entry.generation != generation_) {
      if (victim->generation == generation_ || entry.depth < victim->depth) {
        victim = &entry;
      }
      continue;
    }
    if (victim->generation == generation_ && entry.depth < victim->depth) {
      victim = &entry;
    }
  }

  if (victim->generation == generation_ && depth < victim->depth) {
    ++stats->tt_rejected_stores;
    return;
  }

  *victim = incoming;
  ++stats->tt_stores;
  ++stats->tt_overwrites;
}

} // namespace vibe_othello::search::internal
