#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/pattern_evaluator.h"
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

std::vector<search::Score> fixture_phase_biases(search::Score early = 0, search::Score late = 0) {
  return {early, late};
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
      fixture_phase_biases(),
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
      fixture_phase_biases(),
      {
          make_uniform_table("edge-8", 8, edge_weight),
          make_uniform_table("corner-3x3", 9, corner_weight),
      },
  };
}

PatternWeights make_single_square_bias_weights(search::Score early_bias, search::Score late_bias) {
  return PatternWeights{
      kFixturePhaseCount,
      fixture_phase_by_disc_count(),
      fixture_phase_biases(early_bias, late_bias),
      {
          PatternWeightTable{
              .pattern_id = "single-square",
              .pattern_length = 1,
              .weights = std::vector<search::Score>(
                  static_cast<std::size_t>(kFixturePhaseCount) * pattern_size(1), 0),
          },
      },
  };
}

PatternWeights make_zero_weights_for_feature_set(const PatternFeatureSet& feature_set) {
  std::vector<PatternWeightTable> tables;
  tables.reserve(feature_set.tables.size());
  for (const PatternFeatureTable& table : feature_set.tables) {
    tables.push_back(PatternWeightTable{
        .pattern_id = table.pattern_id,
        .pattern_length = table.pattern_length,
        .weights = std::vector<search::Score>(
            static_cast<std::size_t>(kFixturePhaseCount) * pattern_size(table.pattern_length), 0),
    });
  }
  return PatternWeights{
      kFixturePhaseCount,
      fixture_phase_by_disc_count(),
      fixture_phase_biases(),
      std::move(tables),
  };
}

PatternFeatureSet single_square_feature_set() {
  return PatternFeatureSet{
      .id = "single-square-fixture-v1",
      .tables =
          {
              PatternFeatureTable{
                  .pattern_id = "single-square",
                  .pattern_length = 1,
                  .instances =
                      {
                          std::vector<board_core::Square>{square(0, 0)},
                      },
              },
          },
  };
}

TEST_CASE("pattern evaluator is deterministic", "[evaluation][pattern]") {
  const PatternEvaluator evaluator{make_tiny_pattern_fixture_weights(),
                                   tiny_pattern_feature_set_fixture()};
  constexpr Position position{
      .player = bit(square(0, 0)) | bit(square(1, 0)),
      .opponent = bit(square(7, 7)),
      .side_to_move = Color::black,
  };

  const search::Score first = evaluator.evaluate(position);
  const search::Score second = evaluator.evaluate(position);

  REQUIRE(first == second);
}

TEST_CASE("pattern evaluator scores are side to move relative", "[evaluation][pattern]") {
  const PatternEvaluator evaluator{make_tiny_pattern_fixture_weights(),
                                   tiny_pattern_feature_set_fixture()};
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

TEST_CASE("pattern evaluator phase boundary changes weights", "[evaluation][pattern]") {
  const PatternEvaluator evaluator{make_tiny_pattern_fixture_weights(),
                                   tiny_pattern_feature_set_fixture()};
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

TEST_CASE("pattern evaluator adds phase bias to pattern score", "[evaluation][pattern]") {
  const PatternEvaluator evaluator{make_single_square_bias_weights(7, -11),
                                   single_square_feature_set()};
  constexpr Position early{
      .player = 0,
      .opponent = 0,
      .side_to_move = Color::black,
  };
  constexpr Position late{
      .player = bit(square(0, 0)) | center_filler(),
      .opponent = 0,
      .side_to_move = Color::black,
  };

  REQUIRE(evaluator.evaluate(early) == 7);
  REQUIRE(evaluator.evaluate(late) == -11);
}

TEST_CASE("pattern evaluator accepts buro-lite feature geometry", "[evaluation][pattern]") {
  const PatternFeatureSet feature_set = buro_lite_pattern_feature_set();
  const PatternWeights weights = make_zero_weights_for_feature_set(feature_set);
  const PatternEvaluator evaluator{weights, feature_set};
  constexpr Position position{
      .player = bit(square(0, 0)) | bit(square(4, 4)) | bit(square(7, 7)),
      .opponent = bit(square(1, 0)) | bit(square(3, 3)) | bit(square(6, 7)),
      .side_to_move = Color::black,
  };

  REQUIRE(evaluator.evaluate(position) == 0);
}

TEST_CASE("pattern evaluator matches existing tiny fixture scores", "[evaluation][pattern]") {
  const PatternWeights weights = make_tiny_pattern_fixture_weights();
  const TinyPatternEvaluator tiny{weights};
  const PatternEvaluator generic{weights, tiny_pattern_feature_set_fixture()};
  constexpr Position edge_and_corner{
      .player = bit(square(0, 0)) | bit(square(1, 0)),
      .opponent = bit(square(7, 7)),
      .side_to_move = Color::black,
  };

  REQUIRE(generic.evaluate(edge_and_corner) == tiny.evaluate(edge_and_corner));
  REQUIRE(generic.evaluate(edge_and_corner) == -10);
}

TEST_CASE("pattern evaluator rejects invalid table count", "[evaluation][pattern]") {
  PatternFeatureSet feature_set = tiny_pattern_feature_set_fixture();
  feature_set.tables.pop_back();

  REQUIRE_THROWS_AS(PatternEvaluator(make_tiny_pattern_fixture_weights(), std::move(feature_set)),
                    std::invalid_argument);
}

TEST_CASE("pattern evaluator rejects table id mismatch", "[evaluation][pattern]") {
  PatternFeatureSet feature_set = tiny_pattern_feature_set_fixture();
  feature_set.tables[0].pattern_id = "other-edge";

  REQUIRE_THROWS_AS(PatternEvaluator(make_tiny_pattern_fixture_weights(), std::move(feature_set)),
                    std::invalid_argument);
}

TEST_CASE("pattern evaluator rejects table length mismatch", "[evaluation][pattern]") {
  PatternFeatureSet feature_set = tiny_pattern_feature_set_fixture();
  feature_set.tables[0].pattern_length = 7;

  REQUIRE_THROWS_AS(PatternEvaluator(make_tiny_pattern_fixture_weights(), std::move(feature_set)),
                    std::invalid_argument);
}

TEST_CASE("pattern evaluator rejects invalid squares", "[evaluation][pattern]") {
  PatternFeatureSet feature_set = tiny_pattern_feature_set_fixture();
  feature_set.tables[0].instances[0][0] = board_core::Square{board_core::kInvalidSquareIndex};

  REQUIRE_THROWS_AS(PatternEvaluator(make_tiny_pattern_fixture_weights(), std::move(feature_set)),
                    std::invalid_argument);
}

TEST_CASE("pattern evaluator rejects instance length mismatch", "[evaluation][pattern]") {
  PatternFeatureSet feature_set = tiny_pattern_feature_set_fixture();
  feature_set.tables[0].instances[0].pop_back();

  REQUIRE_THROWS_AS(PatternEvaluator(make_tiny_pattern_fixture_weights(), std::move(feature_set)),
                    std::invalid_argument);
}

TEST_CASE("pattern evaluator rejects duplicate squares in one instance", "[evaluation][pattern]") {
  PatternFeatureSet feature_set = tiny_pattern_feature_set_fixture();
  feature_set.tables[0].instances[0][1] = feature_set.tables[0].instances[0][0];

  REQUIRE_THROWS_AS(PatternEvaluator(make_tiny_pattern_fixture_weights(), std::move(feature_set)),
                    std::invalid_argument);
}

TEST_CASE("pattern evaluator rejects invalid phase mapping", "[evaluation][pattern]") {
  std::array<std::uint8_t, PatternWeights::kDiscCountEntries> invalid_phases =
      fixture_phase_by_disc_count();
  invalid_phases[0] = kFixturePhaseCount;

  PatternWeights weights{
      kFixturePhaseCount,
      invalid_phases,
      fixture_phase_biases(),
      {
          make_fixture_table("edge-8", 8),
          make_fixture_table("corner-3x3", 9),
      },
  };

  REQUIRE_THROWS_AS(PatternEvaluator(std::move(weights), tiny_pattern_feature_set_fixture()),
                    std::invalid_argument);
}

TEST_CASE("pattern evaluator rejects sentinel-reaching weights", "[evaluation][pattern]") {
  REQUIRE_THROWS_AS(
      PatternEvaluator(make_uniform_weights(4'000, 4'000), tiny_pattern_feature_set_fixture()),
      std::invalid_argument);
}

TEST_CASE("pattern evaluator rejects sentinel-reaching phase bias", "[evaluation][pattern]") {
  REQUIRE_THROWS_AS(PatternEvaluator(make_single_square_bias_weights(search::kScoreWin, 0),
                                     single_square_feature_set()),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(PatternEvaluator(make_single_square_bias_weights(0, search::kScoreLoss),
                                     single_square_feature_set()),
                    std::invalid_argument);
}

TEST_CASE("pattern evaluator stays inside search sentinels", "[evaluation][pattern]") {
  const PatternEvaluator evaluator{make_tiny_pattern_fixture_weights(),
                                   tiny_pattern_feature_set_fixture()};
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
