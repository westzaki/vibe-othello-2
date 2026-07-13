#pragma once

#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/hash.h"
#include "vibe_othello/search/result.h"
#include "vibe_othello/search/search_session.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace vibe_othello::search::internal {

enum class TTEntryKind : std::uint8_t {
  midgame,
  exact_endgame_score,
  exact_endgame_wld,
};

struct TTEntry {
  board_core::PositionHash key = 0;
  Depth depth = 0;
  Score score = 0;
  BoundType bound = BoundType::exact;
  board_core::Move best_move = board_core::make_pass();
  bool has_best_move = false;
  std::uint8_t generation = 0;
  TTEntryKind kind = TTEntryKind::midgame;
  bool selective = false;
  bool occupied = false;
};

class TranspositionTable {
public:
  static constexpr std::size_t kBucketWidth = 4;
  static constexpr std::size_t kDefaultEntryCount = 65'536;
  static constexpr std::size_t kMaxBucketCount = std::size_t{1} << 20;

  explicit TranspositionTable(std::size_t requested_entries = kDefaultEntryCount);
  explicit TranspositionTable(TranspositionTableConfig config) noexcept;

  void clear() noexcept;
  void new_generation() noexcept;

  std::optional<TTEntry> probe(board_core::PositionHash key, TTEntryKind kind,
                               SearchStats* stats) const noexcept;

  // Compatibility helpers for direct internal tests. Production search uses
  // precomputed-key access above and below.
  std::optional<TTEntry> probe(board_core::Position position, SearchStats* stats) const noexcept;

  void store(board_core::PositionHash key, Depth depth, Score score, BoundType bound,
             board_core::Move best_move, TTEntryKind kind, SearchStats* stats) noexcept;
  void store_value(board_core::PositionHash key, Depth depth, Score score, BoundType bound,
                   TTEntryKind kind, SearchStats* stats, bool selective = false) noexcept;

  void store(board_core::Position position, Depth depth, Score score, BoundType bound,
             board_core::Move best_move, TTEntryKind kind, SearchStats* stats) noexcept;
  void store_value(board_core::Position position, Depth depth, Score score, BoundType bound,
                   TTEntryKind kind, SearchStats* stats, bool selective = false) noexcept;

  bool enabled() const noexcept;
  std::uint8_t generation() const noexcept;
  TranspositionTableAllocation allocation() const noexcept;

private:
  struct TTBucket {
    std::array<TTEntry, kBucketWidth> entries{};
  };

  std::size_t index_for(board_core::PositionHash key) const noexcept;
  void store_entry(board_core::PositionHash key, Depth depth, Score score, BoundType bound,
                   std::optional<board_core::Move> best_move, TTEntryKind kind, SearchStats* stats,
                   bool selective = false) noexcept;

  std::vector<TTBucket> buckets_;
  std::uint8_t generation_ = 1;
  TranspositionTableAllocation allocation_{};
};

std::optional<Score> midgame_tt_cutoff_score(const TTEntry& entry, Depth depth, Score alpha,
                                             Score beta) noexcept;
std::optional<Score> exact_endgame_score_tt_cutoff_score(const TTEntry& entry,
                                                         Depth remaining_empties, Score alpha,
                                                         Score beta) noexcept;

} // namespace vibe_othello::search::internal
