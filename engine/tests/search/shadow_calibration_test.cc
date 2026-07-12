#include "../../src/search/search_internal.h"
#include "search/endgame_positions.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/search/search.h"

#include <algorithm>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <utility>
#include <vector>

namespace vibe_othello::search {
namespace {

class DiscDifferenceEvaluator final : public Evaluator {
public:
  Score evaluate(const board_core::Position& position) const noexcept override {
    return static_cast<Score>(std::popcount(position.player)) -
           static_cast<Score>(std::popcount(position.opponent));
  }
};

class Collector final : public ShadowCalibrationSink {
public:
  void record(const ShadowCalibrationSample& sample) noexcept override {
    samples.push_back(sample);
  }

  std::vector<ShadowCalibrationSample> samples;
};

SelectiveSearchOptionsV1 enabled_shadow(Collector* collector) {
  return SelectiveSearchOptionsV1{
      .enable_shadow_calibration = true,
      .sample_rate = kShadowCalibrationSampleRateScale,
      .max_samples_per_search = 64,
      .minimum_deep_depth = 2,
      .shallow_depth_reduction = 1,
      .include_pv_nodes = true,
      .include_pass_nodes = true,
      .include_near_exact_nodes = false,
      .sampling_seed = 42,
      .repo_sha = "0123456789abcdef",
      .search_config_id = "test-search-v1",
      .evaluator_id = "disc-difference-test",
      .artifact_id = "none",
      .sink = collector,
  };
}

SearchResult run_fixed(board_core::Position position, Depth depth, SearchOptions options) {
  SearchSession session;
  DiscDifferenceEvaluator evaluator;
  return search_fixed_depth(session, position, evaluator, depth, options);
}

void require_same_official_result(const SearchResult& actual, const SearchResult& expected) {
  REQUIRE(actual.best_move == expected.best_move);
  REQUIRE(actual.score == expected.score);
  REQUIRE(actual.score_kind == expected.score_kind);
  REQUIRE(actual.bound == expected.bound);
  REQUIRE(actual.completed_depth == expected.completed_depth);
  REQUIRE(actual.nodes == expected.nodes);
  REQUIRE(actual.stats == expected.stats);
  REQUIRE(actual.pv == expected.pv);
  REQUIRE(actual.root_moves == expected.root_moves);
  REQUIRE(actual.exact == expected.exact);
  REQUIRE(actual.stopped == expected.stopped);
}

TEST_CASE("disabled shadow calibration has bit-exact official parity",
          "[search][shadow_calibration]") {
  Collector collector;
  SearchOptions disabled{};
  disabled.midgame.use_pvs = true;
  disabled.selective = enabled_shadow(&collector);
  disabled.selective.enable_shadow_calibration = false;

  SearchOptions baseline = disabled;
  baseline.selective = {};
  const SearchResult actual = run_fixed(board_core::initial_position(), Depth{5}, disabled);
  const SearchResult expected = run_fixed(board_core::initial_position(), Depth{5}, baseline);

  require_same_official_result(actual, expected);
  REQUIRE(actual.shadow_calibration == ShadowCalibrationStats{});
  REQUIRE(collector.samples.empty());
}

TEST_CASE("shadow mode preserves official result nodes and PV", "[search][shadow_calibration]") {
  Collector collector;
  SearchOptions shadow{};
  shadow.midgame.use_pvs = true;
  shadow.midgame.use_iid = true;
  shadow.ordering.use_history = true;
  shadow.ordering.use_killers = true;
  shadow.selective = enabled_shadow(&collector);
  shadow.selective.max_samples_per_search = 12;

  SearchOptions baseline = shadow;
  baseline.selective = {};
  const SearchResult actual = run_fixed(board_core::initial_position(), Depth{5}, shadow);
  const SearchResult expected = run_fixed(board_core::initial_position(), Depth{5}, baseline);

  require_same_official_result(actual, expected);
  REQUIRE(actual.shadow_calibration.shadow_samples == collector.samples.size());
  REQUIRE(actual.shadow_calibration.shadow_samples == 12);
  REQUIRE(actual.shadow_calibration.shadow_shallow_nodes > 0);
  REQUIRE(actual.stats.selective_cuts == 0);
}

TEST_CASE("shadow nodes do not consume the official iterative node limit",
          "[search][shadow_calibration]") {
  Collector collector;
  SearchOptions shadow{};
  shadow.midgame.use_pvs = true;
  shadow.selective = enabled_shadow(&collector);
  shadow.selective.max_samples_per_search = 8;
  SearchOptions baseline = shadow;
  baseline.selective = {};
  const SearchLimits limits{.max_depth = Depth{7}, .max_nodes = 1'000};
  SearchSession shadow_session;
  SearchSession baseline_session;
  DiscDifferenceEvaluator shadow_evaluator;
  DiscDifferenceEvaluator baseline_evaluator;

  const SearchResult actual = search_iterative(shadow_session, board_core::initial_position(),
                                               shadow_evaluator, limits, shadow);
  const SearchResult expected = search_iterative(baseline_session, board_core::initial_position(),
                                                 baseline_evaluator, limits, baseline);

  require_same_official_result(actual, expected);
  REQUIRE(actual.shadow_calibration.shadow_shallow_nodes > 0);
  REQUIRE(actual.shadow_calibration.shadow_samples == collector.samples.size());
}

TEST_CASE("shadow sampling is deterministic and metadata is complete",
          "[search][shadow_calibration]") {
  Collector first;
  Collector second;
  SearchOptions first_options{};
  first_options.midgame.use_pvs = true;
  first_options.selective = enabled_shadow(&first);
  first_options.selective.sample_rate = 500'000;
  SearchOptions second_options = first_options;
  second_options.selective.sink = &second;

  const SearchResult first_result =
      run_fixed(board_core::initial_position(), Depth{6}, first_options);
  const SearchResult second_result =
      run_fixed(board_core::initial_position(), Depth{6}, second_options);

  require_same_official_result(first_result, second_result);
  REQUIRE_FALSE(first.samples.empty());
  REQUIRE(first.samples == second.samples);
  for (const ShadowCalibrationSample& sample : first.samples) {
    REQUIRE(sample.schema_version == kShadowCalibrationSchemaVersion);
    REQUIRE(sample.repo_sha == "0123456789abcdef");
    REQUIRE(sample.search_config_id == "test-search-v1");
    REQUIRE(sample.evaluator_id == "disc-difference-test");
    REQUIRE(sample.artifact_id == "none");
    REQUIRE(sample.canonical_position_hash != 0);
    REQUIRE(sample.occupied_count + sample.empties == board_core::kSquareCount);
    const int normalized_count =
        sample.occupied_count < 4 ? 0 : static_cast<int>(sample.occupied_count) - 4;
    REQUIRE(sample.phase == std::min(12, (normalized_count * 13) / 60));
    REQUIRE(sample.deep_depth >= 2);
    REQUIRE(sample.shallow_depth == sample.deep_depth - 1);
    REQUIRE(sample.alpha < sample.beta);
    REQUIRE(sample.sampling_seed == 42);
    REQUIRE(sample.search_identity.size() == 16);
    REQUIRE((sample.node_type == ShadowNodeType::pv || sample.node_type == ShadowNodeType::cut ||
             sample.node_type == ShadowNodeType::all));
    REQUIRE(sample.cut_node == (sample.node_type == ShadowNodeType::cut));
    REQUIRE(sample.all_node == (sample.node_type == ShadowNodeType::all));
  }
}

TEST_CASE("shadow sampling enforces its per-search cap", "[search][shadow_calibration]") {
  Collector collector;
  SearchOptions options{};
  options.selective = enabled_shadow(&collector);
  options.selective.max_samples_per_search = 3;

  const SearchResult result = run_fixed(board_core::initial_position(), Depth{5}, options);

  REQUIRE(collector.samples.size() == 3);
  REQUIRE(result.shadow_calibration.shadow_samples == 3);
  REQUIRE(result.shadow_calibration.shadow_candidates >= 3);
}

TEST_CASE("PV nodes are excluded or included by policy", "[search][shadow_calibration]") {
  Collector excluded;
  SearchOptions excluded_options{};
  excluded_options.selective = enabled_shadow(&excluded);
  excluded_options.selective.include_pv_nodes = false;
  const SearchResult excluded_result =
      run_fixed(board_core::initial_position(), Depth{4}, excluded_options);

  Collector included;
  SearchOptions included_options = excluded_options;
  included_options.selective.include_pv_nodes = true;
  included_options.selective.sink = &included;
  const SearchResult included_result =
      run_fixed(board_core::initial_position(), Depth{4}, included_options);

  REQUIRE(excluded.samples.empty());
  REQUIRE(excluded_result.shadow_calibration.shadow_samples == 0);
  REQUIRE_FALSE(included.samples.empty());
  REQUIRE(included_result.shadow_calibration.shadow_samples == included.samples.size());
  for (const ShadowCalibrationSample& sample : included.samples) {
    REQUIRE(sample.pv_node);
    REQUIRE(sample.node_type == ShadowNodeType::pv);
  }
}

std::pair<ShadowCalibrationStats, std::vector<ShadowCalibrationSample>>
run_pass_node_shadow(bool include_pass_nodes) {
  constexpr board_core::Position pass_position{
      .player = board_core::bit(board_core::square_from_file_rank(1, 0)),
      .opponent = board_core::bit(board_core::square_from_file_rank(0, 0)),
      .side_to_move = board_core::Color::black,
  };
  Collector collector;
  SelectiveSearchOptionsV1 selective = enabled_shadow(&collector);
  selective.include_pass_nodes = include_pass_nodes;
  std::optional<internal::ShadowCalibrationRun> run =
      internal::make_shadow_calibration_run(pass_position, selective);
  REQUIRE(run.has_value());
  DiscDifferenceEvaluator evaluator;
  SearchOptions options{};
  options.selective = selective;
  internal::SearchContext context{
      .position_state = internal::make_search_position(pass_position),
      .evaluator = evaluator,
      .limits = SearchLimits{.max_depth = Depth{3}},
      .options = internal::normalize_search_options(options),
      .shadow_calibration = &*run,
  };
  const internal::SearchNodeResult result =
      internal::alphabeta(&context, kScoreLoss, kScoreWin, Depth{3}, Ply{0});
  REQUIRE(result.is_complete());
  return {run->stats, std::move(collector.samples)};
}

TEST_CASE("pass nodes are excluded or included by policy", "[search][shadow_calibration]") {
  const auto [excluded_stats, excluded] = run_pass_node_shadow(false);
  const auto [included_stats, included] = run_pass_node_shadow(true);

  for (const ShadowCalibrationSample& sample : excluded) {
    REQUIRE_FALSE(sample.pass_state);
  }
  REQUIRE_FALSE(included.empty());
  REQUIRE(included_stats.shadow_samples > excluded_stats.shadow_samples);
  bool saw_pass = false;
  for (const ShadowCalibrationSample& sample : included) {
    saw_pass = saw_pass || sample.pass_state;
    REQUIRE_FALSE(sample.terminal_state);
  }
  REQUIRE(saw_pass);
}

TEST_CASE("near exact nodes are excluded unless explicitly included",
          "[search][shadow_calibration]") {
  const board_core::Position position = test_support::generated_endgame_position(12);
  Collector excluded;
  SearchOptions excluded_options{};
  excluded_options.endgame.exact_endgame = true;
  excluded_options.endgame.endgame_exact_empties = 12;
  excluded_options.selective = enabled_shadow(&excluded);
  excluded_options.selective.include_near_exact_nodes = false;
  const SearchResult excluded_result = run_fixed(position, Depth{4}, excluded_options);

  Collector included;
  SearchOptions included_options = excluded_options;
  included_options.selective.include_near_exact_nodes = true;
  included_options.selective.sink = &included;
  const SearchResult included_result = run_fixed(position, Depth{4}, included_options);

  require_same_official_result(included_result, excluded_result);
  REQUIRE(excluded.samples.empty());
  REQUIRE_FALSE(included.samples.empty());
  for (const ShadowCalibrationSample& sample : included.samples) {
    REQUIRE(sample.exact_handoff_eligible);
  }
}

} // namespace
} // namespace vibe_othello::search
