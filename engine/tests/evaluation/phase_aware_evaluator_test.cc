#include "vibe_othello/evaluation/early_midgame_heuristic_evaluator.h"
#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/pattern_evaluator.h"
#include "vibe_othello/evaluation/phase_aware_evaluator.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace vibe_othello::evaluation {
namespace {

constexpr std::uint8_t kPhaseCount = 13;
constexpr search::Score kLearnedBiasBase = 1'000;

std::array<std::uint8_t, PatternWeights::kDiscCountEntries> phase_by_disc_count() {
  std::array<std::uint8_t, PatternWeights::kDiscCountEntries> phases{};
  for (std::uint8_t disc_count = 0; disc_count < phases.size(); ++disc_count) {
    const int normalized_count = disc_count < 4 ? 0 : static_cast<int>(disc_count) - 4;
    phases[disc_count] = static_cast<std::uint8_t>(std::min(12, (normalized_count * 13) / 60));
  }
  return phases;
}

PatternFeatureSet single_square_feature_set() {
  return PatternFeatureSet{
      .id = "phase-aware-single-square-fixture-v1",
      .tables =
          {
              PatternFeatureTable{
                  .pattern_id = "single-square",
                  .pattern_length = 1,
                  .instances =
                      {
                          std::vector<board_core::Square>{board_core::square_from_file_rank(0, 0)},
                      },
              },
          },
  };
}

PatternWeights learned_fixture_weights() {
  std::vector<search::Score> phase_biases;
  phase_biases.reserve(kPhaseCount);
  for (std::uint8_t phase = 0; phase < kPhaseCount; ++phase) {
    phase_biases.push_back(kLearnedBiasBase + phase);
  }

  return PatternWeights{
      kPhaseCount,
      phase_by_disc_count(),
      std::move(phase_biases),
      {
          PatternWeightTable{
              .pattern_id = "single-square",
              .pattern_length = 1,
              .weights = std::vector<search::Score>(
                  static_cast<std::size_t>(kPhaseCount) * pattern_size(1), 0),
          },
      },
  };
}

board_core::Position midgame_position() {
  return board_core::Position{
      .player = UINT64_C(0x0000000000000fff),
      .opponent = UINT64_C(0x0000000000fff000),
      .side_to_move = board_core::Color::black,
  };
}

board_core::Position late_game_position() {
  return board_core::Position{
      .player = UINT64_C(0x0000000003ffffff),
      .opponent = UINT64_C(0x000ffffffc000000),
      .side_to_move = board_core::Color::black,
  };
}

PhaseAwareEvaluator make_phase_aware(std::optional<std::vector<std::uint8_t>> trained_phases) {
  return PhaseAwareEvaluator{learned_fixture_weights(), single_square_feature_set(),
                             std::move(trained_phases)};
}

PatternEvaluator make_learned() {
  return PatternEvaluator{learned_fixture_weights(), single_square_feature_set()};
}

TEST_CASE("phase-aware evaluator routes default artifact phases to fallback and learned paths",
          "[evaluation][phase_aware]") {
  const PhaseAwareEvaluator evaluator = make_phase_aware(std::vector<std::uint8_t>{10, 11, 12});
  const PatternEvaluator learned = make_learned();
  const EarlyMidgameHeuristicEvaluator fallback;
  const board_core::Position initial = board_core::initial_position();
  const board_core::Position midgame = midgame_position();
  const board_core::Position late_game = late_game_position();

  REQUIRE(evaluator.evaluate(initial) == fallback.evaluate(initial));
  REQUIRE(evaluator.evaluate(midgame) == fallback.evaluate(midgame));
  REQUIRE(evaluator.evaluate(late_game) == learned.evaluate(late_game));
  REQUIRE(evaluator.evaluate(initial) != learned.evaluate(initial));
  REQUIRE(evaluator.evaluate(midgame) != learned.evaluate(midgame));
}

TEST_CASE("phase-aware evaluator routes full coverage to learned evaluation",
          "[evaluation][phase_aware]") {
  const PhaseAwareEvaluator evaluator =
      make_phase_aware(std::vector<std::uint8_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
  const PatternEvaluator learned = make_learned();

  REQUIRE(evaluator.evaluate(board_core::initial_position()) ==
          learned.evaluate(board_core::initial_position()));
  REQUIRE(evaluator.evaluate(midgame_position()) == learned.evaluate(midgame_position()));
  REQUIRE(evaluator.evaluate(late_game_position()) == learned.evaluate(late_game_position()));
}

TEST_CASE("phase-aware evaluator preserves legacy all-phase learned routing",
          "[evaluation][phase_aware]") {
  const PhaseAwareEvaluator evaluator = make_phase_aware(std::nullopt);
  const PatternEvaluator learned = make_learned();

  REQUIRE(evaluator.evaluate(board_core::initial_position()) ==
          learned.evaluate(board_core::initial_position()));
  REQUIRE(evaluator.evaluate(midgame_position()) == learned.evaluate(midgame_position()));
}

TEST_CASE("phase-aware evaluator is deterministic and rejects invalid coverage",
          "[evaluation][phase_aware]") {
  const PhaseAwareEvaluator evaluator = make_phase_aware(std::vector<std::uint8_t>{10, 11, 12});
  const search::Score first = evaluator.evaluate(midgame_position());

  REQUIRE(first == evaluator.evaluate(midgame_position()));
  REQUIRE(first > search::kScoreLoss);
  REQUIRE(first < search::kScoreWin);
  REQUIRE_THROWS_AS(make_phase_aware(std::vector<std::uint8_t>{10, 10}), std::invalid_argument);
  REQUIRE_THROWS_AS(make_phase_aware(std::vector<std::uint8_t>{13}), std::invalid_argument);
}

} // namespace
} // namespace vibe_othello::evaluation
