#pragma once

#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/hash.h"
#include "vibe_othello/search/result.h"

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
  bool occupied = false;
};

class TranspositionTable {
public:
  static constexpr std::size_t kBucketWidth = 4;
  static constexpr std::size_t kDefaultEntryCount = 4096;
  static constexpr std::size_t kMaxBucketCount = std::size_t{1} << 20;

  explicit TranspositionTable(std::size_t requested_entries = kDefaultEntryCount);

  void clear() noexcept;
  void new_generation() noexcept;

  std::optional<TTEntry> probe(board_core::Position position, SearchStats* stats) const noexcept;

  void store(board_core::Position position, Depth depth, Score score, BoundType bound,
             board_core::Move best_move, TTEntryKind kind, SearchStats* stats) noexcept;
  void store_value(board_core::Position position, Depth depth, Score score, BoundType bound,
                   TTEntryKind kind, SearchStats* stats) noexcept;

private:
  struct TTBucket {
    std::array<TTEntry, kBucketWidth> entries{};
  };

  std::size_t index_for(board_core::PositionHash key) const noexcept;
  void store_entry(board_core::Position position, Depth depth, Score score, BoundType bound,
                   std::optional<board_core::Move> best_move, TTEntryKind kind,
                   SearchStats* stats) noexcept;

  std::vector<TTBucket> buckets_;
  std::uint8_t generation_ = 1;
};

std::optional<Score> midgame_tt_cutoff_score(const TTEntry& entry, Depth depth, Score alpha,
                                             Score beta) noexcept;
std::optional<Score> exact_endgame_score_tt_cutoff_score(const TTEntry& entry,
                                                         Depth remaining_empties, Score alpha,
                                                         Score beta) noexcept;

} // namespace vibe_othello::search::internal
