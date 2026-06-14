#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/tiny_pattern_evaluator.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vibe_othello::evaluation {
namespace {

using board_core::bit;
using board_core::Color;
using board_core::Position;
using board_core::square_from_file_rank;

constexpr board_core::Square square(int file, int rank) noexcept {
  return square_from_file_rank(file, rank);
}

constexpr board_core::Bitboard center_filler() noexcept {
  return bit(square(3, 1)) | bit(square(4, 1)) | bit(square(3, 2)) | bit(square(4, 2)) |
         bit(square(1, 3)) | bit(square(2, 3)) | bit(square(3, 3)) | bit(square(4, 3)) |
         bit(square(5, 3)) | bit(square(6, 3)) | bit(square(1, 4)) | bit(square(2, 4)) |
         bit(square(3, 4)) | bit(square(4, 4)) | bit(square(5, 4)) | bit(square(6, 4)) |
         bit(square(3, 5)) | bit(square(4, 5)) | bit(square(3, 6));
}

constexpr std::uint8_t kFixturePhaseCount = 2;
constexpr int kFixtureLatePhaseDiscBoundary = 20;
constexpr search::Score kFixtureLatePhaseScale = 2;

std::array<std::uint8_t, PatternWeights::kDiscCountEntries> fixture_phase_by_disc_count() {
  std::array<std::uint8_t, PatternWeights::kDiscCountEntries> phases{};
  for (std::uint8_t disc_count = 0; disc_count < phases.size(); ++disc_count) {
    phases[disc_count] = disc_count < kFixtureLatePhaseDiscBoundary ? 0 : 1;
  }
  return phases;
}

search::Score fixture_weight(std::uint32_t index, std::uint8_t length,
                             std::uint8_t phase) noexcept {
  search::Score score = 0;
  for (std::uint8_t digit = 0; digit < length; ++digit) {
    const std::uint32_t cell = index % 3;
    index /= 3;
    const search::Score positional_weight = static_cast<search::Score>(digit + 1);
    if (cell == static_cast<std::uint32_t>(PatternCell::player)) {
      score += positional_weight;
    } else if (cell == static_cast<std::uint32_t>(PatternCell::opponent)) {
      score -= positional_weight;
    }
  }
  return phase == 0 ? score : static_cast<search::Score>(score * kFixtureLatePhaseScale);
}

PatternWeightTable make_fixture_table(std::string_view pattern_id, std::uint8_t pattern_length) {
  std::vector<search::Score> weights;
  weights.reserve(static_cast<std::size_t>(kFixturePhaseCount) * pattern_size(pattern_length));
  for (std::uint8_t phase = 0; phase < kFixturePhaseCount; ++phase) {
    for (std::uint32_t index = 0; index < pattern_size(pattern_length); ++index) {
      weights.push_back(fixture_weight(index, pattern_length, phase));
    }
  }
  return PatternWeightTable{
      .pattern_id = std::string{pattern_id},
      .pattern_length = pattern_length,
      .weights = std::move(weights),
  };
}

PatternWeights make_tiny_pattern_fixture_weights() {
  return PatternWeights{
      kFixturePhaseCount,
      fixture_phase_by_disc_count(),
      {
          make_fixture_table("edge-8", 8),
          make_fixture_table("corner-3x3", 9),
      },
  };
}

PatternWeightTable make_uniform_table(std::string_view pattern_id, std::uint8_t pattern_length,
                                      search::Score weight) {
  return PatternWeightTable{
      .pattern_id = std::string{pattern_id},
      .pattern_length = pattern_length,
      .weights = std::vector<search::Score>(
          static_cast<std::size_t>(kFixturePhaseCount) * pattern_size(pattern_length), weight),
  };
}

PatternWeights make_uniform_weights(search::Score edge_weight, search::Score corner_weight) {
  return PatternWeights{
      kFixturePhaseCount,
      fixture_phase_by_disc_count(),
      {
          make_uniform_table("edge-8", 8, edge_weight),
          make_uniform_table("corner-3x3", 9, corner_weight),
      },
  };
}

TEST_CASE("ternary pattern index uses empty player opponent digits", "[evaluation][tiny_pattern]") {
  constexpr std::array<PatternCell, 3> cells{
      PatternCell::empty,
      PatternCell::player,
      PatternCell::opponent,
  };

  STATIC_REQUIRE(ternary_pattern_index(cells) == 21);
}

TEST_CASE("ternary pattern index reads position from side to move perspective",
          "[evaluation][tiny_pattern]") {
  constexpr Position position{
      .player = bit(square(1, 0)),
      .opponent = bit(square(2, 0)),
      .side_to_move = Color::black,
  };
  constexpr std::array<board_core::Square, 3> squares{
      square(0, 0),
      square(1, 0),
      square(2, 0),
  };

  REQUIRE(ternary_pattern_index(position, squares) == 21);
}

TEST_CASE("tiny pattern evaluator is deterministic", "[evaluation][tiny_pattern]") {
  const TinyPatternEvaluator evaluator{make_tiny_pattern_fixture_weights()};
  constexpr Position position{
      .player = bit(square(0, 0)) | bit(square(1, 0)),
      .opponent = bit(square(7, 7)),
      .side_to_move = Color::black,
  };

  const search::Score first = evaluator.evaluate(position);
  const search::Score second = evaluator.evaluate(position);

  REQUIRE(first == second);
}

TEST_CASE("tiny pattern evaluator scores are side to move relative", "[evaluation][tiny_pattern]") {
  const TinyPatternEvaluator evaluator{make_tiny_pattern_fixture_weights()};
  constexpr Position player_corner{
      .player = bit(square(0, 0)),
      .opponent = 0,
      .side_to_move = Color::black,
  };
  constexpr Position opponent_corner{
      .player = 0,
      .opponent = bit(square(0, 0)),
      .side_to_move = Color::white,
  };

  REQUIRE(evaluator.evaluate(player_corner) == 3);
  REQUIRE(evaluator.evaluate(opponent_corner) == -3);
}

TEST_CASE("tiny pattern fixture weights preserve previous scores", "[evaluation][tiny_pattern]") {
  const TinyPatternEvaluator evaluator{make_tiny_pattern_fixture_weights()};
  constexpr Position edge_and_corner{
      .player = bit(square(0, 0)) | bit(square(1, 0)),
      .opponent = bit(square(7, 7)),
      .side_to_move = Color::black,
  };

  REQUIRE(evaluator.evaluate(edge_and_corner) == -10);
}

TEST_CASE("tiny pattern evaluator phase boundary changes weights", "[evaluation][tiny_pattern]") {
  const TinyPatternEvaluator evaluator{make_tiny_pattern_fixture_weights()};
  constexpr Position early{
      .player = bit(square(0, 0)),
      .opponent = 0,
      .side_to_move = Color::black,
  };
  constexpr Position late{
      .player = bit(square(0, 0)) | center_filler(),
      .opponent = 0,
      .side_to_move = Color::black,
  };

  REQUIRE(evaluator.evaluate(early) == 3);
  REQUIRE(evaluator.evaluate(late) == 6);
}

TEST_CASE("tiny pattern evaluator reads explicit pattern weights", "[evaluation][tiny_pattern]") {
  const TinyPatternEvaluator evaluator{make_uniform_weights(1, 0)};
  constexpr Position position{
      .player = bit(square(0, 0)),
      .opponent = bit(square(7, 7)),
      .side_to_move = Color::black,
  };

  REQUIRE(evaluator.evaluate(position) == 4);
}

TEST_CASE("tiny pattern evaluator rejects corrupted weights", "[evaluation][tiny_pattern]") {
  PatternWeightTable edge_table = make_fixture_table("edge-8", 8);
  edge_table.weights.pop_back();
  PatternWeights weights{
      kFixturePhaseCount,
      fixture_phase_by_disc_count(),
      {
          std::move(edge_table),
          make_fixture_table("corner-3x3", 9),
      },
  };

  REQUIRE_THROWS_AS(TinyPatternEvaluator{std::move(weights)}, std::invalid_argument);
}

TEST_CASE("tiny pattern evaluator rejects incompatible weights", "[evaluation][tiny_pattern]") {
  PatternWeightTable edge_table = make_fixture_table("edge-8", 8);
  edge_table.pattern_id = "edge-7";
  PatternWeights wrong_schema{
      kFixturePhaseCount,
      fixture_phase_by_disc_count(),
      {
          std::move(edge_table),
          make_fixture_table("corner-3x3", 9),
      },
  };

  REQUIRE_THROWS_AS(TinyPatternEvaluator{std::move(wrong_schema)}, std::invalid_argument);

  std::array<std::uint8_t, PatternWeights::kDiscCountEntries> invalid_phases =
      fixture_phase_by_disc_count();
  invalid_phases[0] = kFixturePhaseCount;
  PatternWeights wrong_phase_map{
      kFixturePhaseCount,
      invalid_phases,
      {
          make_fixture_table("edge-8", 8),
          make_fixture_table("corner-3x3", 9),
      },
  };

  REQUIRE_THROWS_AS(TinyPatternEvaluator{std::move(wrong_phase_map)}, std::invalid_argument);
}

TEST_CASE("tiny pattern evaluator rejects sentinel-reaching weights",
          "[evaluation][tiny_pattern]") {
  REQUIRE_THROWS_AS(TinyPatternEvaluator{make_uniform_weights(4'000, 4'000)},
                    std::invalid_argument);
}

TEST_CASE("tiny pattern evaluator stays inside search sentinels", "[evaluation][tiny_pattern]") {
  const TinyPatternEvaluator evaluator{make_tiny_pattern_fixture_weights()};
  constexpr Position dense_position{
      .player = UINT64_C(0x5555555555555555),
      .opponent = UINT64_C(0xaaaaaaaaaaaaaaaa),
      .side_to_move = Color::black,
  };

  const search::Score score = evaluator.evaluate(dense_position);

  REQUIRE(score > search::kScoreLoss);
  REQUIRE(score < search::kScoreWin);
}

} // namespace
} // namespace vibe_othello::evaluation
