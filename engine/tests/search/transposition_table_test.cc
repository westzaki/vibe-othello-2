#include "../../src/search/search_internal.h"

#include <catch2/catch_test_macros.hpp>

namespace vibe_othello::search::internal {
namespace {

constexpr board_core::Square square(int file, int rank) noexcept {
  return board_core::square_from_file_rank(file, rank);
}

TEST_CASE("transposition table probe returns stored entries", "[search][tt]") {
  TranspositionTable table;
  SearchStats stats{};
  const board_core::Position position = board_core::initial_position();
  const board_core::Move best_move = board_core::make_move(square(2, 3));

  REQUIRE_FALSE(table.probe(position, &stats).has_value());
  REQUIRE(stats.tt_probes == 1);
  REQUIRE(stats.tt_hits == 0);

  table.store(position, Depth{3}, Score{17}, BoundType::lower, best_move, TTEntryKind::midgame,
              &stats);
  REQUIRE(stats.tt_stores == 1);

  const std::optional<TTEntry> entry = table.probe(position, &stats);
  REQUIRE(entry.has_value());
  REQUIRE(entry->depth == Depth{3});
  REQUIRE(entry->score == Score{17});
  REQUIRE(entry->bound == BoundType::lower);
  REQUIRE(entry->best_move == best_move);
  REQUIRE(entry->kind == TTEntryKind::midgame);
  REQUIRE(stats.tt_probes == 2);
  REQUIRE(stats.tt_hits == 1);
}

TEST_CASE("TT cutoff score respects kind depth and bounds", "[search][tt]") {
  TTEntry entry{
      .depth = Depth{4},
      .score = Score{10},
      .bound = BoundType::exact,
      .best_move = board_core::make_move(square(2, 3)),
      .kind = TTEntryKind::midgame,
      .occupied = true,
  };

  REQUIRE(tt_cutoff_score(entry, Depth{4}, Score{-5}, Score{5}) == Score{10});
  REQUIRE(tt_cutoff_score(entry, Depth{5}, Score{-5}, Score{5}) == std::nullopt);

  entry.depth = Depth{5};
  entry.kind = TTEntryKind::exact_endgame_score;
  REQUIRE(tt_cutoff_score(entry, Depth{4}, Score{-5}, Score{5}) == std::nullopt);

  entry.kind = TTEntryKind::midgame;
  entry.bound = BoundType::lower;
  entry.score = Score{5};
  REQUIRE(tt_cutoff_score(entry, Depth{4}, Score{-5}, Score{5}) == Score{5});
  entry.score = Score{4};
  REQUIRE(tt_cutoff_score(entry, Depth{4}, Score{-5}, Score{5}) == std::nullopt);

  entry.bound = BoundType::upper;
  entry.score = Score{-5};
  REQUIRE(tt_cutoff_score(entry, Depth{4}, Score{-5}, Score{5}) == Score{-5});
  entry.score = Score{-4};
  REQUIRE(tt_cutoff_score(entry, Depth{4}, Score{-5}, Score{5}) == std::nullopt);
}

} // namespace
} // namespace vibe_othello::search::internal
