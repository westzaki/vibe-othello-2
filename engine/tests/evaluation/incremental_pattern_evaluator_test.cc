#include "vibe_othello/board_core/board.h"
#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/pattern_artifact.h"
#include "vibe_othello/evaluation/pattern_evaluator.h"
#include "vibe_othello/evaluation/phase_aware_evaluator.h"
#include "vibe_othello/search/search.h"

#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

namespace vibe_othello::evaluation {
namespace {

constexpr std::uint8_t kPhaseCount = 13;

std::array<std::uint8_t, PatternWeights::kDiscCountEntries> phase_by_disc_count() {
  std::array<std::uint8_t, PatternWeights::kDiscCountEntries> phases{};
  for (std::uint8_t discs = 0; discs < phases.size(); ++discs) {
    const int normalized = discs < 4 ? 0 : static_cast<int>(discs) - 4;
    phases[discs] = static_cast<std::uint8_t>(std::min(12, (normalized * 13) / 60));
  }
  return phases;
}

PatternFeatureSet single_square_feature_set() {
  return PatternFeatureSet{
      .id = "incremental-single-square-fixture-v1",
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

PatternWeights make_phase_weights(std::uint16_t score_scale) {
  std::vector<search::Score> biases;
  std::vector<search::Score> table_weights;
  biases.reserve(kPhaseCount);
  table_weights.reserve(static_cast<std::size_t>(kPhaseCount) * pattern_size(1));
  for (std::uint8_t phase = 0; phase < kPhaseCount; ++phase) {
    biases.push_back(static_cast<search::Score>(phase * score_scale));
    table_weights.push_back(0);
    table_weights.push_back(score_scale);
    table_weights.push_back(-static_cast<search::Score>(score_scale));
  }
  return PatternWeights{
      kPhaseCount,
      phase_by_disc_count(),
      std::move(biases),
      {
          PatternWeightTable{
              .pattern_id = "single-square",
              .pattern_length = 1,
              .weights = std::move(table_weights),
          },
      },
      score_scale,
  };
}

board_core::Position position_with_discs(std::uint8_t discs, board_core::Color side_to_move) {
  const board_core::Bitboard occupied = discs == board_core::kSquareCount
                                            ? ~board_core::Bitboard{0}
                                            : (board_core::Bitboard{1} << discs) - 1;
  if (side_to_move == board_core::Color::black) {
    return board_core::Position{
        .player = occupied,
        .opponent = 0,
        .side_to_move = side_to_move,
    };
  }
  return board_core::Position{
      .player = 0,
      .opponent = occupied,
      .side_to_move = side_to_move,
  };
}

std::filesystem::path source_root() {
  return std::filesystem::path{VIBE_OTHELLO_SOURCE_DIR};
}

board_core::Move choose_move(board_core::Bitboard legal, std::uint64_t* random_state) {
  *random_state ^= *random_state << 13;
  *random_state ^= *random_state >> 7;
  *random_state ^= *random_state << 17;
  std::uint8_t choice =
      static_cast<std::uint8_t>(*random_state % static_cast<std::uint64_t>(std::popcount(legal)));
  while (choice > 0) {
    legal &= legal - 1;
    --choice;
  }
  return board_core::make_move(
      board_core::Square{static_cast<std::uint8_t>(std::countr_zero(legal))});
}

void require_score_parity(const PhaseAwareEvaluator& evaluator,
                          const PhaseAwareEvaluator::IncrementalState& state,
                          const board_core::Position& position) {
  const search::Score reference = evaluator.evaluate_reference(position);
  REQUIRE(evaluator.evaluate(position) == reference);
  REQUIRE(evaluator.evaluate_incremental(state, position) == reference);
}

class DelegatingEvaluator final : public search::Evaluator {
public:
  explicit DelegatingEvaluator(const PhaseAwareEvaluator* evaluator) : evaluator_(evaluator) {}

  search::Score evaluate(const board_core::Position& position) const noexcept override {
    return evaluator_->evaluate_reference(position);
  }

private:
  const PhaseAwareEvaluator* evaluator_;
};

TEST_CASE("incremental pattern evaluation covers every phase and fixed-point scale",
          "[evaluation][pattern][incremental]") {
  for (const std::uint16_t score_scale : {std::uint16_t{1}, std::uint16_t{100}}) {
    const PatternEvaluator evaluator{make_phase_weights(score_scale), single_square_feature_set()};
    std::array<bool, kPhaseCount> saw_phase{};
    for (std::uint8_t discs = 4; discs <= board_core::kSquareCount; ++discs) {
      const board_core::Position position = position_with_discs(discs, board_core::Color::black);
      const PatternEvaluator::IncrementalState state = evaluator.make_incremental_state(position);
      const std::uint8_t phase = phase_by_disc_count()[discs];
      saw_phase[phase] = true;
      REQUIRE(evaluator.evaluate_reference(position) == evaluator.evaluate(position));
      REQUIRE(state.evaluate() == evaluator.evaluate(position));
      REQUIRE(state.evaluate() == static_cast<search::Score>(phase + 1));
    }
    REQUIRE(std::all_of(saw_phase.begin(), saw_phase.end(), [](bool saw) { return saw; }));
  }
}

TEST_CASE("incremental pattern evaluation preserves half-away-from-zero rounding",
          "[evaluation][pattern][incremental]") {
  const auto make_rounding_weights = [](search::Score bias) {
    return PatternWeights{
        1,
        std::array<std::uint8_t, PatternWeights::kDiscCountEntries>{},
        {bias},
        {
            PatternWeightTable{
                .pattern_id = "single-square",
                .pattern_length = 1,
                .weights = std::vector<search::Score>(pattern_size(1), 0),
            },
        },
        100,
    };
  };
  const board_core::Position empty{
      .player = 0, .opponent = 0, .side_to_move = board_core::Color::black};
  const PatternEvaluator positive{make_rounding_weights(50), single_square_feature_set()};
  const PatternEvaluator negative{make_rounding_weights(-50), single_square_feature_set()};

  REQUIRE(positive.evaluate(empty) == 1);
  REQUIRE(positive.make_incremental_state(empty).evaluate() == 1);
  REQUIRE(negative.evaluate(empty) == -1);
  REQUIRE(negative.make_incremental_state(empty).evaluate() == -1);
}

TEST_CASE("incremental paired indices preserve perspective antisymmetry",
          "[evaluation][pattern][incremental]") {
  const PatternWeights weights{
      1,
      std::array<std::uint8_t, PatternWeights::kDiscCountEntries>{},
      {0},
      {
          PatternWeightTable{
              .pattern_id = "single-square",
              .pattern_length = 1,
              .weights = {0, 100, -100},
          },
      },
      100,
  };
  const PatternEvaluator evaluator{weights, single_square_feature_set()};
  const board_core::Position position{
      .player = board_core::bit(board_core::square_from_file_rank(0, 0)),
      .opponent = board_core::bit(board_core::square_from_file_rank(1, 0)),
      .side_to_move = board_core::Color::black,
  };
  const board_core::Position opposite_perspective{
      .player = position.opponent,
      .opponent = position.player,
      .side_to_move = board_core::Color::white,
  };

  REQUIRE(evaluator.evaluate(position) == -evaluator.evaluate(opposite_perspective));
  REQUIRE(evaluator.make_incremental_state(position).evaluate() == evaluator.evaluate(position));
  REQUIRE(evaluator.make_incremental_state(opposite_perspective).evaluate() ==
          evaluator.evaluate(opposite_perspective));
}

TEST_CASE("phase-aware incremental routing preserves fallback replacement and residual modes",
          "[evaluation][phase_aware][incremental]") {
  const PhaseAwareEvaluator evaluator{make_phase_weights(100), single_square_feature_set(),
                                      std::vector<std::uint8_t>{5, 10}, 5};
  for (const std::uint8_t phase : {std::uint8_t{4}, std::uint8_t{5}, std::uint8_t{10}}) {
    std::uint8_t discs = 4;
    while (phase_by_disc_count()[discs] != phase) {
      ++discs;
    }
    const board_core::Position position = position_with_discs(discs, board_core::Color::black);
    const PhaseAwareEvaluator::IncrementalState state = evaluator.make_incremental_state(position);
    REQUIRE(evaluator.evaluate_incremental(state, position) == evaluator.evaluate(position));
    REQUIRE(evaluator.evaluate(position) == evaluator.evaluate_reference(position));
  }
}

TEST_CASE(
    "committed artifact incremental state matches flat and reference evaluation through games",
    "[evaluation][artifact][incremental]") {
  PatternArtifactLoadResult load_result =
      load_default_pattern_artifact(default_eval_root(source_root()));
  REQUIRE(load_result.ok());
  LoadedPatternArtifact artifact = std::move(*load_result.artifact);
  REQUIRE(artifact.feature_set.tables.size() == 11);
  std::size_t instance_count = 0;
  for (const PatternFeatureTable& table : artifact.feature_set.tables) {
    instance_count += table.instances.size();
  }
  REQUIRE(instance_count == 58);

  std::array<std::uint8_t, PatternWeights::kDiscCountEntries> phase_for_discs{};
  for (std::uint8_t discs = 0; discs < phase_for_discs.size(); ++discs) {
    phase_for_discs[discs] = artifact.weights.phase_for_disc_count(discs);
  }
  const PhaseAwareEvaluator evaluator{std::move(artifact.weights), std::move(artifact.feature_set),
                                      std::move(artifact.trained_phases),
                                      artifact.fallback_additive_through_phase};

  std::array<bool, kPhaseCount> saw_phase{};
  bool saw_pass = false;
  std::uint64_t random_state = UINT64_C(0x7d4e9a13c652b08f);
  for (int game = 0; game < 12; ++game) {
    board_core::Position position = board_core::initial_position();
    PhaseAwareEvaluator::IncrementalState state = evaluator.make_incremental_state(position);
    std::vector<board_core::MoveDelta> history;
    require_score_parity(evaluator, state, position);

    for (;;) {
      const std::uint8_t discs =
          static_cast<std::uint8_t>(std::popcount(board_core::occupied(position)));
      saw_phase[phase_for_discs[discs]] = true;
      const board_core::Bitboard legal = board_core::legal_moves(position);
      board_core::MoveDelta delta{};
      if (legal == 0) {
        if (!board_core::has_legal_move(board_core::Position{
                .player = position.opponent,
                .opponent = position.player,
                .side_to_move = board_core::opposite(position.side_to_move),
            })) {
          break;
        }
        REQUIRE(board_core::apply_pass(&position, &delta));
        saw_pass = true;
      } else {
        REQUIRE(board_core::apply_move(&position, choose_move(legal, &random_state), &delta));
      }
      state.apply_move(delta);
      history.push_back(delta);
      require_score_parity(evaluator, state, position);
    }

    while (!history.empty()) {
      const board_core::MoveDelta delta = history.back();
      history.pop_back();
      state.undo_move(delta);
      board_core::undo_move(&position, delta);
      require_score_parity(evaluator, state, position);
    }
    REQUIRE(position == board_core::initial_position());
  }

  REQUIRE(saw_pass);
  REQUIRE(std::all_of(saw_phase.begin(), saw_phase.end(), [](bool saw) { return saw; }));
}

TEST_CASE("search session incremental backend matches the generic stateless path",
          "[evaluation][artifact][incremental][search]") {
  PatternArtifactLoadResult load_result =
      load_default_pattern_artifact(default_eval_root(source_root()));
  REQUIRE(load_result.ok());
  LoadedPatternArtifact artifact = std::move(*load_result.artifact);
  const PhaseAwareEvaluator evaluator{std::move(artifact.weights), std::move(artifact.feature_set),
                                      std::move(artifact.trained_phases),
                                      artifact.fallback_additive_through_phase};
  const DelegatingEvaluator stateless{&evaluator};
  search::SearchSession incremental_session{search::SearchSessionConfig{
      .incremental_eval_verify_interval = 1,
  }};
  search::SearchSession stateless_session;

  const search::SearchResult incremental =
      search::search_fixed_depth(incremental_session, board_core::initial_position(), evaluator, 4);
  const search::SearchResult reference =
      search::search_fixed_depth(stateless_session, board_core::initial_position(), stateless, 4);

  REQUIRE(incremental.score == reference.score);
  REQUIRE(incremental.best_move == reference.best_move);
  REQUIRE(incremental.nodes == reference.nodes);
  REQUIRE_FALSE(incremental.stats.incremental_eval_enabled);
  REQUIRE(incremental.stats.incremental_state_initializations == 0);
  REQUIRE(incremental.stats.incremental_eval_calls == 0);
  REQUIRE(incremental.stats.stateless_eval_calls == incremental.stats.eval_calls);
  REQUIRE(incremental.stats.incremental_updates == 0);
  REQUIRE(incremental.stats.incremental_touched_instances == 0);
  REQUIRE_FALSE(reference.stats.incremental_eval_enabled);
  REQUIRE(reference.stats.stateless_eval_calls == reference.stats.eval_calls);

  const std::vector<std::uint8_t> all_phases{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  const PhaseAwareEvaluator fully_learned{make_phase_weights(100), single_square_feature_set(),
                                          all_phases};
  const DelegatingEvaluator fully_learned_stateless{&fully_learned};
  search::SearchSession verified_session{search::SearchSessionConfig{
      .incremental_eval_verify_interval = 1,
  }};
  search::SearchSession fully_stateless_session;
  const search::SearchResult verified = search::search_fixed_depth(
      verified_session, board_core::initial_position(), fully_learned, 4);
  const search::SearchResult fully_stateless = search::search_fixed_depth(
      fully_stateless_session, board_core::initial_position(), fully_learned_stateless, 4);
  REQUIRE(verified.score == fully_stateless.score);
  REQUIRE(verified.best_move == fully_stateless.best_move);
  REQUIRE(verified.nodes == fully_stateless.nodes);
  REQUIRE(verified.stats.incremental_eval_enabled);
  REQUIRE(verified.stats.incremental_state_initializations == 1);
  REQUIRE(verified.stats.incremental_eval_calls == verified.stats.eval_calls);
  REQUIRE(verified.stats.stateless_eval_calls == 0);
  REQUIRE(verified.stats.incremental_updates > 0);
  REQUIRE_FALSE(fully_stateless.stats.incremental_eval_enabled);
  REQUIRE(fully_stateless.stats.stateless_eval_calls == fully_stateless.stats.eval_calls);

  const search::SearchResult iterative = search::search_iterative(
      board_core::initial_position(), fully_learned, search::SearchLimits{.max_depth = 3});
  REQUIRE(iterative.stats.incremental_eval_enabled);
  REQUIRE(iterative.stats.incremental_state_initializations == 3);
  REQUIRE(iterative.stats.incremental_eval_calls == iterative.stats.eval_calls);
  REQUIRE(iterative.stats.stateless_eval_calls == 0);

  PatternArtifactLoadResult direct_load_result =
      load_default_pattern_artifact(default_eval_root(source_root()));
  REQUIRE(direct_load_result.ok());
  LoadedPatternArtifact direct_artifact = std::move(*direct_load_result.artifact);
  const PatternEvaluator direct_pattern{std::move(direct_artifact.weights),
                                        std::move(direct_artifact.feature_set)};
  const search::SearchResult direct =
      search::search_fixed_depth(board_core::initial_position(), direct_pattern, 1);
  REQUIRE(direct.stats.incremental_eval_enabled);
  REQUIRE(direct.stats.incremental_state_initializations == 1);
  REQUIRE(direct.stats.incremental_eval_calls == direct.stats.eval_calls);
  REQUIRE(direct.stats.stateless_eval_calls == 0);
  REQUIRE(direct.stats.incremental_updates > 0);
  REQUIRE(direct.stats.incremental_touched_instances > 0);
}

} // namespace
} // namespace vibe_othello::evaluation
