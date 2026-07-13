#include "search_internal.h"

#include <algorithm>
#include <limits>
#include <new>

namespace vibe_othello::search::internal {
namespace {

std::size_t next_power_of_two(std::size_t value) noexcept {
  std::size_t result = 1;
  while (result < value && result < TranspositionTable::kMaxBucketCount) {
    result <<= 1;
  }
  return std::min(result, TranspositionTable::kMaxBucketCount);
}

std::size_t previous_power_of_two(std::size_t value) noexcept {
  if (value == 0) {
    return 0;
  }
  std::size_t result = 1;
  while (result <= value / 2 && result < TranspositionTable::kMaxBucketCount) {
    result <<= 1;
  }
  return result;
}

int bound_quality(BoundType bound) noexcept {
  return bound == BoundType::exact ? 1 : 0;
}

bool bound_is_stronger(const TTEntry& incoming, const TTEntry& existing) noexcept {
  if (incoming.bound == BoundType::exact && existing.bound != BoundType::exact) {
    return true;
  }
  if (incoming.bound != existing.bound) {
    return false;
  }
  if (incoming.bound == BoundType::lower) {
    return incoming.score > existing.score;
  }
  if (incoming.bound == BoundType::upper) {
    return incoming.score < existing.score;
  }
  return false;
}

TTEntry make_entry(board_core::PositionHash key, Depth depth, Score score, BoundType bound,
                   std::optional<board_core::Move> best_move, TTEntryKind kind,
                   std::uint8_t generation, bool selective) noexcept {
  return TTEntry{
      .key = key,
      .depth = depth,
      .score = score,
      .bound = bound,
      .best_move = best_move.value_or(board_core::make_pass()),
      .has_best_move = best_move.has_value(),
      .generation = generation,
      .kind = kind,
      .selective = selective,
      .occupied = true,
  };
}

bool is_legal_normal_best_move(board_core::Position position, board_core::Move best_move) noexcept {
  return best_move.kind == board_core::MoveKind::normal &&
         (board_core::legal_moves(position) & board_core::bit(best_move.square)) != 0;
}

bool should_replace_same_key(const TTEntry& existing, const TTEntry& incoming) noexcept {
  if (incoming.depth > existing.depth) {
    return true;
  }
  if (incoming.depth < existing.depth) {
    return false;
  }
  if (bound_quality(incoming.bound) != bound_quality(existing.bound)) {
    return bound_quality(incoming.bound) > bound_quality(existing.bound);
  }
  return bound_is_stronger(incoming, existing);
}

} // namespace

TranspositionTable::TranspositionTable(std::size_t requested_entries)
    : TranspositionTable(TranspositionTableConfig{
          .capacity = requested_entries,
          .unit = TranspositionTableCapacityUnit::entries,
      }) {}

TranspositionTable::TranspositionTable(TranspositionTableConfig config) noexcept {
  allocation_.entry_size = sizeof(TTEntry);
  std::size_t bucket_count = 0;
  if (config.unit == TranspositionTableCapacityUnit::entries) {
    allocation_.requested_bytes =
        config.capacity > std::numeric_limits<std::size_t>::max() / sizeof(TTEntry)
            ? std::numeric_limits<std::size_t>::max()
            : config.capacity * sizeof(TTEntry);
    const std::size_t requested_buckets = (config.capacity + kBucketWidth - 1) / kBucketWidth;
    bucket_count = config.capacity == 0 ? 0 : next_power_of_two(requested_buckets);
  } else {
    allocation_.requested_bytes = config.capacity;
    const std::size_t fitting_buckets = config.capacity / sizeof(TTBucket);
    bucket_count = previous_power_of_two(std::min(fitting_buckets, kMaxBucketCount));
  }

  if (bucket_count == 0) {
    return;
  }
  try {
    buckets_.resize(bucket_count);
  } catch (const std::bad_alloc&) {
    allocation_.allocation_succeeded = false;
    return;
  } catch (...) {
    allocation_.allocation_succeeded = false;
    return;
  }
  allocation_.enabled = true;
  allocation_.bucket_count = buckets_.size();
  allocation_.entry_count = buckets_.size() * kBucketWidth;
  allocation_.actual_bytes = buckets_.size() * sizeof(TTBucket);
}

void TranspositionTable::clear() noexcept {
  for (TTBucket& bucket : buckets_) {
    bucket = TTBucket{};
  }
  generation_ = 1;
}

void TranspositionTable::new_generation() noexcept {
  if (generation_ == std::numeric_limits<std::uint8_t>::max()) {
    clear();
    return;
  }
  ++generation_;
}

bool TranspositionTable::enabled() const noexcept {
  return !buckets_.empty();
}

std::uint8_t TranspositionTable::generation() const noexcept {
  return generation_;
}

TranspositionTableAllocation TranspositionTable::allocation() const noexcept {
  return allocation_;
}

std::size_t TranspositionTable::index_for(board_core::PositionHash key) const noexcept {
  return static_cast<std::size_t>(key) & (buckets_.size() - 1);
}

std::optional<TTEntry> TranspositionTable::probe(board_core::PositionHash key, TTEntryKind kind,
                                                 SearchStats* stats) const noexcept {
  ++stats->tt_probes;
  if (buckets_.empty()) {
    return std::nullopt;
  }
  const TTBucket& bucket = buckets_[index_for(key)];
  for (std::size_t slot = 0; slot < bucket.entries.size(); ++slot) {
    ++stats->tt_probe_slots;
    const TTEntry& entry = bucket.entries[slot];
    if (entry.occupied && entry.key == key && entry.kind == kind) {
      ++stats->tt_hits;
      if (entry.generation != generation_) {
        ++stats->tt_generation_age_hits;
      }
      return entry;
    }
  }
  return std::nullopt;
}

std::optional<TTEntry> TranspositionTable::probe(board_core::Position position,
                                                 SearchStats* stats) const noexcept {
  ++stats->tt_probes;
  if (buckets_.empty()) {
    return std::nullopt;
  }
  const board_core::PositionHash key = board_core::hash_position(position);
  const TTBucket& bucket = buckets_[index_for(key)];
  for (const TTEntry& entry : bucket.entries) {
    ++stats->tt_probe_slots;
    if (entry.occupied && entry.key == key) {
      ++stats->tt_hits;
      if (entry.generation != generation_) {
        ++stats->tt_generation_age_hits;
      }
      return entry;
    }
  }
  return std::nullopt;
}

void TranspositionTable::store(board_core::PositionHash key, Depth depth, Score score,
                               BoundType bound, board_core::Move best_move, TTEntryKind kind,
                               SearchStats* stats) noexcept {
  store_entry(key, depth, score, bound, best_move, kind, stats);
}

void TranspositionTable::store_value(board_core::PositionHash key, Depth depth, Score score,
                                     BoundType bound, TTEntryKind kind, SearchStats* stats,
                                     bool selective) noexcept {
  store_entry(key, depth, score, bound, std::nullopt, kind, stats, selective);
}

void TranspositionTable::store(board_core::Position position, Depth depth, Score score,
                               BoundType bound, board_core::Move best_move, TTEntryKind kind,
                               SearchStats* stats) noexcept {
  if (!is_legal_normal_best_move(position, best_move)) {
    ++stats->tt_invalid_best_move_stores;
    return;
  }
  store_entry(board_core::hash_position(position), depth, score, bound, best_move, kind, stats);
}

void TranspositionTable::store_value(board_core::Position position, Depth depth, Score score,
                                     BoundType bound, TTEntryKind kind, SearchStats* stats,
                                     bool selective) noexcept {
  store_entry(board_core::hash_position(position), depth, score, bound, std::nullopt, kind, stats,
              selective);
}

void TranspositionTable::store_entry(board_core::PositionHash key, Depth depth, Score score,
                                     BoundType bound, std::optional<board_core::Move> best_move,
                                     TTEntryKind kind, SearchStats* stats,
                                     bool selective) noexcept {
  if (buckets_.empty()) {
    ++stats->tt_rejected_stores;
    return;
  }
  TTBucket& bucket = buckets_[index_for(key)];
  TTEntry incoming = make_entry(key, depth, score, bound, best_move, kind, generation_, selective);

  for (TTEntry& entry : bucket.entries) {
    if (!entry.occupied || entry.key != key || entry.kind != kind) {
      continue;
    }
    ++stats->tt_same_key_updates;
    if (should_replace_same_key(entry, incoming)) {
      if (!incoming.has_best_move && entry.has_best_move) {
        incoming.best_move = entry.best_move;
        incoming.has_best_move = true;
      }
      entry = incoming;
      ++stats->tt_stores;
      return;
    }
    if (!entry.has_best_move && incoming.has_best_move) {
      entry.best_move = incoming.best_move;
      entry.has_best_move = true;
      entry.generation = generation_;
      ++stats->tt_stores;
      return;
    }
    ++stats->tt_rejected_stores;
    return;
  }

  bool occupied = false;
  for (TTEntry& entry : bucket.entries) {
    if (!entry.occupied) {
      entry = incoming;
      ++stats->tt_stores;
      if (occupied) {
        ++stats->tt_bucket_conflicts;
      }
      return;
    }
    occupied = true;
  }
  ++stats->tt_bucket_conflicts;

  TTEntry* victim = &bucket.entries[0];
  for (TTEntry& candidate : bucket.entries) {
    const bool candidate_old = candidate.generation != generation_;
    const bool victim_old = victim->generation != generation_;
    if (candidate_old != victim_old) {
      if (candidate_old) {
        victim = &candidate;
      }
      continue;
    }
    if (candidate.depth != victim->depth) {
      if (candidate.depth < victim->depth) {
        victim = &candidate;
      }
      continue;
    }
    if (bound_quality(candidate.bound) < bound_quality(victim->bound)) {
      victim = &candidate;
    }
  }

  if (victim->generation == generation_ &&
      (victim->depth > depth ||
       (victim->depth == depth && bound_quality(victim->bound) > bound_quality(bound)))) {
    ++stats->tt_rejected_stores;
    return;
  }

  *victim = incoming;
  ++stats->tt_stores;
  ++stats->tt_replacements;
}

} // namespace vibe_othello::search::internal
