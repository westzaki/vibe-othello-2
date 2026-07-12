#include "../../src/search/endgame_tt_internal.h"
#include "../../src/search/search_internal.h"
#include "vibe_othello/board_core/board.h"

#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <initializer_list>

namespace vibe_othello::search::internal {
namespace {

constexpr board_core::Square square(int file, int rank) noexcept {
  return board_core::square_from_file_rank(file, rank);
}

board_core::Move select_legal_move(board_core::Position position, std::size_t choice) {
  const board_core::Bitboard legal_moves = board_core::legal_moves(position);
  REQUIRE(legal_moves != 0);

  std::array<board_core::Move, board_core::kSquareCount> moves{};
  std::size_t move_count = 0;
  for (int square_index = 0; square_index < board_core::kSquareCount; ++square_index) {
    const board_core::Square move_square = board_core::square_from_index(square_index);
    if ((legal_moves & board_core::bit(move_square)) != 0) {
      moves[move_count] = board_core::make_move(move_square);
      ++move_count;
    }
  }

  REQUIRE(move_count > 0);
  return moves[choice % move_count];
}

board_core::Position position_after_fixed_choices(std::initializer_list<std::size_t> choices) {
  board_core::Position position = board_core::initial_position();

  for (const std::size_t choice : choices) {
    board_core::MoveDelta delta{};
    REQUIRE(board_core::apply_move(&position, select_legal_move(position, choice), &delta));
  }

  return position;
}

std::array<board_core::Position, 6> shared_bucket_positions() {
  return {
      board_core::initial_position(),
      position_after_fixed_choices({0}),
      position_after_fixed_choices({1, 0}),
      position_after_fixed_choices({2, 1, 0}),
      position_after_fixed_choices({3, 2, 1, 0}),
      position_after_fixed_choices({0, 1, 2, 3, 1}),
  };
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
  REQUIRE(entry->has_best_move);
  REQUIRE(entry->kind == TTEntryKind::midgame);
  REQUIRE(stats.tt_probes == 2);
  REQUIRE(stats.tt_hits == 1);
}

TEST_CASE("transposition table updates same-position entries without a bucket conflict",
          "[search][tt]") {
  TranspositionTable table{4};
  SearchStats stats{};
  const board_core::Position position = board_core::initial_position();
  const board_core::Move best_move = select_legal_move(position, 0);

  table.store(position, Depth{2}, Score{11}, BoundType::upper, best_move, TTEntryKind::midgame,
              &stats);
  table.store(position, Depth{5}, Score{23}, BoundType::exact, best_move, TTEntryKind::midgame,
              &stats);

  const std::optional<TTEntry> entry = table.probe(position, &stats);
  REQUIRE(entry.has_value());
  REQUIRE(entry->depth == Depth{5});
  REQUIRE(entry->score == Score{23});
  REQUIRE(entry->bound == BoundType::exact);
  REQUIRE(stats.tt_stores == 2);
  REQUIRE(stats.tt_bucket_conflicts == 0);
  REQUIRE(stats.tt_replacements == 0);
  REQUIRE(stats.tt_rejected_stores == 0);
  REQUIRE(stats.tt_invalid_best_move_stores == 0);
}

TEST_CASE("transposition table bucket keeps different keys up to bucket width", "[search][tt]") {
  TranspositionTable table{4};
  SearchStats stats{};
  const std::array<board_core::Position, 6> positions = shared_bucket_positions();

  for (std::size_t index = 0; index < TranspositionTable::kBucketWidth; ++index) {
    table.store(positions[index], static_cast<Depth>(index + 1), static_cast<Score>(10 + index),
                BoundType::exact, select_legal_move(positions[index], 0), TTEntryKind::midgame,
                &stats);
  }

  for (std::size_t index = 0; index < TranspositionTable::kBucketWidth; ++index) {
    const std::optional<TTEntry> entry = table.probe(positions[index], &stats);
    REQUIRE(entry.has_value());
    REQUIRE(entry->score == static_cast<Score>(10 + index));
  }
  REQUIRE(stats.tt_stores == TranspositionTable::kBucketWidth);
  REQUIRE(stats.tt_replacements == 0);
  REQUIRE(stats.tt_rejected_stores == 0);
  REQUIRE(stats.tt_invalid_best_move_stores == 0);
}

TEST_CASE("transposition table rejects shallow current-generation replacement", "[search][tt]") {
  TranspositionTable table{4};
  SearchStats stats{};
  const std::array<board_core::Position, 6> positions = shared_bucket_positions();

  for (std::size_t index = 0; index < TranspositionTable::kBucketWidth; ++index) {
    table.store(positions[index], static_cast<Depth>(4 + index), static_cast<Score>(index),
                BoundType::exact, select_legal_move(positions[index], 0), TTEntryKind::midgame,
                &stats);
  }

  table.store(positions[4], Depth{1}, Score{99}, BoundType::exact,
              select_legal_move(positions[4], 0), TTEntryKind::midgame, &stats);

  REQUIRE_FALSE(table.probe(positions[4], &stats).has_value());
  REQUIRE(table.probe(positions[0], &stats).has_value());
  REQUIRE(stats.tt_rejected_stores == 1);
  REQUIRE(stats.tt_replacements == 0);
  REQUIRE(stats.tt_invalid_best_move_stores == 0);
}

TEST_CASE("transposition table prefers deeper current-generation replacement", "[search][tt]") {
  TranspositionTable table{4};
  SearchStats stats{};
  const std::array<board_core::Position, 6> positions = shared_bucket_positions();

  for (std::size_t index = 0; index < TranspositionTable::kBucketWidth; ++index) {
    table.store(positions[index], static_cast<Depth>(4 + index), static_cast<Score>(index),
                BoundType::exact, select_legal_move(positions[index], 0), TTEntryKind::midgame,
                &stats);
  }

  table.store(positions[4], Depth{8}, Score{99}, BoundType::exact,
              select_legal_move(positions[4], 0), TTEntryKind::midgame, &stats);

  const std::optional<TTEntry> incoming = table.probe(positions[4], &stats);
  REQUIRE(incoming.has_value());
  REQUIRE(incoming->depth == Depth{8});
  REQUIRE(incoming->score == Score{99});
  REQUIRE_FALSE(table.probe(positions[0], &stats).has_value());
  REQUIRE(stats.tt_replacements == 1);
  REQUIRE(stats.tt_rejected_stores == 0);
  REQUIRE(stats.tt_invalid_best_move_stores == 0);
}

TEST_CASE("transposition table replaces old-generation entries more readily", "[search][tt]") {
  TranspositionTable table{4};
  SearchStats stats{};
  const std::array<board_core::Position, 6> positions = shared_bucket_positions();

  for (std::size_t index = 0; index < TranspositionTable::kBucketWidth; ++index) {
    table.store(positions[index], Depth{8}, static_cast<Score>(index), BoundType::exact,
                select_legal_move(positions[index], 0), TTEntryKind::midgame, &stats);
  }

  table.new_generation();
  table.store(positions[4], Depth{1}, Score{99}, BoundType::exact,
              select_legal_move(positions[4], 0), TTEntryKind::midgame, &stats);

  const std::optional<TTEntry> incoming = table.probe(positions[4], &stats);
  REQUIRE(incoming.has_value());
  REQUIRE(incoming->depth == Depth{1});
  REQUIRE(incoming->score == Score{99});
  REQUIRE(incoming->generation == 2);
  REQUIRE(stats.tt_replacements == 1);
  REQUIRE(stats.tt_rejected_stores == 0);
  REQUIRE(stats.tt_invalid_best_move_stores == 0);
}

TEST_CASE("transposition table rejects entries without legal normal best moves", "[search][tt]") {
  TranspositionTable table{4};
  SearchStats stats{};
  const board_core::Position position = board_core::initial_position();

  table.store(position, Depth{3}, Score{17}, BoundType::exact, board_core::make_move(square(0, 0)),
              TTEntryKind::midgame, &stats);
  table.store(position, Depth{3}, Score{17}, BoundType::exact, board_core::make_pass(),
              TTEntryKind::midgame, &stats);

  REQUIRE_FALSE(table.probe(position, &stats).has_value());
  REQUIRE(stats.tt_stores == 0);
  REQUIRE(stats.tt_rejected_stores == 0);
  REQUIRE(stats.tt_invalid_best_move_stores == 2);
}

TEST_CASE("transposition table stores value entries without best moves", "[search][tt]") {
  TranspositionTable table{4};
  SearchStats stats{};
  const board_core::Position position = board_core::initial_position();

  table.store_value(position, Depth{6}, Score{8}, BoundType::exact,
                    TTEntryKind::exact_endgame_score, &stats);

  const std::optional<TTEntry> entry = table.probe(position, &stats);
  REQUIRE(entry.has_value());
  REQUIRE(entry->depth == Depth{6});
  REQUIRE(entry->score == Score{8});
  REQUIRE(entry->bound == BoundType::exact);
  REQUIRE_FALSE(entry->has_best_move);
  REQUIRE(entry->best_move == board_core::make_pass());
  REQUIRE(entry->kind == TTEntryKind::exact_endgame_score);
  REQUIRE(stats.tt_stores == 1);
  REQUIRE(stats.tt_invalid_best_move_stores == 0);
}

TEST_CASE("transposition table keeps different entry kinds independent", "[search][tt]") {
  TranspositionTable table{4};
  SearchStats stats{};
  const board_core::Position position = board_core::initial_position();
  const board_core::Move best_move = select_legal_move(position, 0);

  table.store(position, Depth{3}, Score{17}, BoundType::lower, best_move, TTEntryKind::midgame,
              &stats);
  table.store_value(position, Depth{4}, Score{12}, BoundType::exact,
                    TTEntryKind::exact_endgame_score, &stats);

  const std::optional<TTEntry> entry =
      table.probe(board_core::hash_position(position), TTEntryKind::exact_endgame_score, &stats);
  REQUIRE(entry.has_value());
  REQUIRE(entry->depth == Depth{4});
  REQUIRE(entry->score == Score{12});
  REQUIRE(entry->kind == TTEntryKind::exact_endgame_score);
  REQUIRE_FALSE(entry->has_best_move);
  REQUIRE(entry->best_move == board_core::make_pass());
  REQUIRE(stats.tt_stores == 2);
  REQUIRE(stats.tt_invalid_best_move_stores == 0);
}

TEST_CASE("typed TT probes never return another entry kind", "[search][tt]") {
  TranspositionTable table{16};
  SearchStats stats{};
  const board_core::Position position = board_core::initial_position();
  const board_core::PositionHash key = board_core::hash_position(position);
  const board_core::Move best_move = select_legal_move(position, 0);

  table.store(key, Depth{5}, Score{7}, BoundType::exact, best_move, TTEntryKind::midgame, &stats);
  table.store_value(key, Depth{60}, Score{12}, BoundType::lower, TTEntryKind::exact_endgame_score,
                    &stats);
  table.store_value(key, Depth{60}, Score{1}, BoundType::upper, TTEntryKind::exact_endgame_wld,
                    &stats);

  REQUIRE(table.probe(key, TTEntryKind::midgame, &stats)->score == Score{7});
  REQUIRE(table.probe(key, TTEntryKind::exact_endgame_score, &stats)->score == Score{12});
  REQUIRE(table.probe(key, TTEntryKind::exact_endgame_wld, &stats)->score == Score{1});
}

TEST_CASE("same-key TT replacement protects deeper and exact information", "[search][tt]") {
  TranspositionTable table{16};
  SearchStats stats{};
  const auto key = board_core::hash_position(board_core::initial_position());
  const board_core::Move move = select_legal_move(board_core::initial_position(), 0);

  table.store(key, Depth{8}, Score{21}, BoundType::exact, move, TTEntryKind::midgame, &stats);
  table.store(key, Depth{4}, Score{99}, BoundType::lower, move, TTEntryKind::midgame, &stats);
  REQUIRE(table.probe(key, TTEntryKind::midgame, &stats)->score == Score{21});

  table.store(key, Depth{8}, Score{17}, BoundType::lower, move, TTEntryKind::midgame, &stats);
  REQUIRE(table.probe(key, TTEntryKind::midgame, &stats)->bound == BoundType::exact);
  REQUIRE(stats.tt_same_key_updates == 2);
  REQUIRE(stats.tt_rejected_stores == 2);
}

TEST_CASE("TT allocation reports entries bytes disabled and power-of-two buckets", "[search][tt]") {
  const TranspositionTable disabled{TranspositionTableConfig{.capacity = 0}};
  REQUIRE_FALSE(disabled.enabled());
  REQUIRE(disabled.allocation().actual_bytes == 0);

  const TranspositionTable by_entries{TranspositionTableConfig{.capacity = 1000}};
  const auto entries = by_entries.allocation();
  REQUIRE(entries.enabled);
  REQUIRE(std::has_single_bit(entries.bucket_count));
  REQUIRE(entries.entry_count >= 1000);
  REQUIRE(entries.actual_bytes > 0);

  const TranspositionTable by_bytes{TranspositionTableConfig{
      .capacity = 64 * 1024,
      .unit = TranspositionTableCapacityUnit::bytes,
  }};
  const auto bytes = by_bytes.allocation();
  REQUIRE(bytes.enabled);
  REQUIRE(std::has_single_bit(bytes.bucket_count));
  REQUIRE(bytes.actual_bytes <= bytes.requested_bytes);
}

TEST_CASE("TT cutoff score respects kind depth and bounds", "[search][tt]") {
  TTEntry entry{
      .depth = Depth{4},
      .score = Score{10},
      .bound = BoundType::exact,
      .best_move = board_core::make_move(square(2, 3)),
      .has_best_move = true,
      .kind = TTEntryKind::midgame,
      .occupied = true,
  };

  REQUIRE(midgame_tt_cutoff_score(entry, Depth{4}, Score{-5}, Score{5}) == Score{10});
  REQUIRE(midgame_tt_cutoff_score(entry, Depth{5}, Score{-5}, Score{5}) == std::nullopt);

  entry.depth = Depth{5};
  entry.kind = TTEntryKind::exact_endgame_score;
  REQUIRE(midgame_tt_cutoff_score(entry, Depth{4}, Score{-5}, Score{5}) == std::nullopt);

  entry.kind = TTEntryKind::midgame;
  entry.bound = BoundType::lower;
  entry.score = Score{5};
  REQUIRE(midgame_tt_cutoff_score(entry, Depth{4}, Score{-5}, Score{5}) == Score{5});
  entry.score = Score{4};
  REQUIRE(midgame_tt_cutoff_score(entry, Depth{4}, Score{-5}, Score{5}) == std::nullopt);

  entry.bound = BoundType::upper;
  entry.score = Score{-5};
  REQUIRE(midgame_tt_cutoff_score(entry, Depth{4}, Score{-5}, Score{5}) == Score{-5});
  entry.score = Score{-4};
  REQUIRE(midgame_tt_cutoff_score(entry, Depth{4}, Score{-5}, Score{5}) == std::nullopt);

  entry.kind = TTEntryKind::exact_endgame_wld;
  entry.bound = BoundType::exact;
  entry.score = Score{10};
  REQUIRE(midgame_tt_cutoff_score(entry, Depth{4}, Score{-5}, Score{5}) == std::nullopt);
}

TEST_CASE("exact endgame score TT cutoff respects kind depth and bounds", "[search][tt]") {
  TTEntry entry{
      .depth = Depth{4},
      .score = Score{6},
      .bound = BoundType::exact,
      .kind = TTEntryKind::exact_endgame_score,
      .occupied = true,
  };

  REQUIRE(exact_endgame_score_tt_cutoff_score(entry, Depth{4}, Score{-5}, Score{5}) == Score{6});
  REQUIRE(exact_endgame_score_tt_cutoff_score(entry, Depth{5}, Score{-5}, Score{5}) ==
          std::nullopt);

  entry.depth = Depth{5};
  entry.kind = TTEntryKind::midgame;
  REQUIRE(exact_endgame_score_tt_cutoff_score(entry, Depth{4}, Score{-5}, Score{5}) ==
          std::nullopt);

  entry.kind = TTEntryKind::exact_endgame_score;
  entry.bound = BoundType::lower;
  entry.score = Score{5};
  REQUIRE(exact_endgame_score_tt_cutoff_score(entry, Depth{4}, Score{-5}, Score{5}) == Score{5});
  entry.score = Score{4};
  REQUIRE(exact_endgame_score_tt_cutoff_score(entry, Depth{4}, Score{-5}, Score{5}) ==
          std::nullopt);

  entry.bound = BoundType::upper;
  entry.score = Score{-5};
  REQUIRE(exact_endgame_score_tt_cutoff_score(entry, Depth{4}, Score{-5}, Score{5}) == Score{-5});
  entry.score = Score{-4};
  REQUIRE(exact_endgame_score_tt_cutoff_score(entry, Depth{4}, Score{-5}, Score{5}) ==
          std::nullopt);

  entry.kind = TTEntryKind::exact_endgame_wld;
  entry.bound = BoundType::exact;
  entry.score = Score{1};
  REQUIRE(exact_endgame_score_tt_cutoff_score(entry, Depth{4}, Score{-5}, Score{5}) ==
          std::nullopt);
}

TEST_CASE("exact endgame score TT probe returns only compatible legal best move hints",
          "[search][tt]") {
  const board_core::Position position = board_core::initial_position();
  const board_core::Move legal_best_move = select_legal_move(position, 0);
  TTEntry entry{
      .depth = Depth{4},
      .score = Score{4},
      .bound = BoundType::lower,
      .best_move = legal_best_move,
      .has_best_move = true,
      .kind = TTEntryKind::exact_endgame_score,
      .occupied = true,
  };

  ExactEndgameTtProbe probe =
      exact_endgame_score_tt_probe(entry, position, Depth{4}, Score{-5}, Score{5});
  REQUIRE_FALSE(probe.cutoff_score.has_value());
  REQUIRE(probe.best_move == legal_best_move);

  entry.score = Score{5};
  probe = exact_endgame_score_tt_probe(entry, position, Depth{4}, Score{-5}, Score{5});
  REQUIRE(probe.cutoff_score == Score{5});
  REQUIRE(probe.best_move == legal_best_move);

  entry.depth = Depth{3};
  probe = exact_endgame_score_tt_probe(entry, position, Depth{4}, Score{-5}, Score{5});
  REQUIRE_FALSE(probe.cutoff_score.has_value());
  REQUIRE(probe.best_move == legal_best_move);

  entry.depth = Depth{4};
  entry.score = Score{4};
  entry.best_move = board_core::make_pass();
  probe = exact_endgame_score_tt_probe(entry, position, Depth{4}, Score{-5}, Score{5});
  REQUIRE_FALSE(probe.cutoff_score.has_value());
  REQUIRE_FALSE(probe.best_move.has_value());

  entry.best_move = board_core::make_move(square(0, 0));
  probe = exact_endgame_score_tt_probe(entry, position, Depth{4}, Score{-5}, Score{5});
  REQUIRE_FALSE(probe.cutoff_score.has_value());
  REQUIRE_FALSE(probe.best_move.has_value());

  entry.best_move = legal_best_move;
  entry.has_best_move = false;
  probe = exact_endgame_score_tt_probe(entry, position, Depth{4}, Score{-5}, Score{5});
  REQUIRE_FALSE(probe.cutoff_score.has_value());
  REQUIRE_FALSE(probe.best_move.has_value());

  entry.has_best_move = true;
  entry.kind = TTEntryKind::midgame;
  probe = exact_endgame_score_tt_probe(entry, position, Depth{4}, Score{-5}, Score{5});
  REQUIRE_FALSE(probe.cutoff_score.has_value());
  REQUIRE_FALSE(probe.best_move.has_value());

  entry.kind = TTEntryKind::exact_endgame_wld;
  probe = exact_endgame_score_tt_probe(entry, position, Depth{4}, Score{-5}, Score{5});
  REQUIRE_FALSE(probe.cutoff_score.has_value());
  REQUIRE_FALSE(probe.best_move.has_value());
}

TEST_CASE("exact endgame WLD TT probe respects kind depth bounds and legal hints", "[search][tt]") {
  const board_core::Position position = board_core::initial_position();
  const board_core::Move legal_best_move = select_legal_move(position, 0);
  TTEntry entry{
      .depth = Depth{4},
      .score = Score{0},
      .bound = BoundType::exact,
      .best_move = legal_best_move,
      .has_best_move = true,
      .kind = TTEntryKind::exact_endgame_wld,
      .occupied = true,
  };

  ExactEndgameTtProbe probe =
      exact_endgame_wld_tt_probe(entry, position, Depth{4}, Score{-1}, Score{1});
  REQUIRE(probe.cutoff_score == Score{0});
  REQUIRE(probe.best_move == legal_best_move);

  entry.depth = Depth{3};
  probe = exact_endgame_wld_tt_probe(entry, position, Depth{4}, Score{-1}, Score{1});
  REQUIRE_FALSE(probe.cutoff_score.has_value());
  REQUIRE(probe.best_move == legal_best_move);

  entry.depth = Depth{4};
  entry.bound = BoundType::lower;
  entry.score = Score{1};
  probe = exact_endgame_wld_tt_probe(entry, position, Depth{4}, Score{-1}, Score{1});
  REQUIRE(probe.cutoff_score == Score{1});

  entry.score = Score{0};
  probe = exact_endgame_wld_tt_probe(entry, position, Depth{4}, Score{-1}, Score{1});
  REQUIRE_FALSE(probe.cutoff_score.has_value());

  entry.bound = BoundType::upper;
  entry.score = Score{-1};
  probe = exact_endgame_wld_tt_probe(entry, position, Depth{4}, Score{-1}, Score{1});
  REQUIRE(probe.cutoff_score == Score{-1});

  entry.score = Score{0};
  probe = exact_endgame_wld_tt_probe(entry, position, Depth{4}, Score{-1}, Score{1});
  REQUIRE_FALSE(probe.cutoff_score.has_value());

  entry.bound = BoundType::exact;
  entry.score = Score{1};
  entry.best_move = board_core::make_pass();
  probe = exact_endgame_wld_tt_probe(entry, position, Depth{4}, Score{-1}, Score{1});
  REQUIRE(probe.cutoff_score == Score{1});
  REQUIRE_FALSE(probe.best_move.has_value());

  entry.best_move = legal_best_move;
  entry.has_best_move = false;
  probe = exact_endgame_wld_tt_probe(entry, position, Depth{4}, Score{-1}, Score{1});
  REQUIRE(probe.cutoff_score == Score{1});
  REQUIRE_FALSE(probe.best_move.has_value());
}

TEST_CASE("exact score and WLD TT probes do not satisfy each other", "[search][tt]") {
  const board_core::Position position = board_core::initial_position();
  const board_core::Move legal_best_move = select_legal_move(position, 0);
  TTEntry entry{
      .depth = Depth{4},
      .score = Score{1},
      .bound = BoundType::exact,
      .best_move = legal_best_move,
      .has_best_move = true,
      .kind = TTEntryKind::exact_endgame_wld,
      .occupied = true,
  };

  ExactEndgameTtProbe score_probe =
      exact_endgame_score_tt_probe(entry, position, Depth{4}, Score{-5}, Score{5});
  REQUIRE_FALSE(score_probe.cutoff_score.has_value());
  REQUIRE_FALSE(score_probe.best_move.has_value());

  entry.kind = TTEntryKind::exact_endgame_score;
  entry.score = Score{12};
  ExactEndgameTtProbe wld_probe =
      exact_endgame_wld_tt_probe(entry, position, Depth{4}, Score{-1}, Score{1});
  REQUIRE_FALSE(wld_probe.cutoff_score.has_value());
  REQUIRE_FALSE(wld_probe.best_move.has_value());
}

} // namespace
} // namespace vibe_othello::search::internal
