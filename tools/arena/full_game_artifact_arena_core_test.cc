#include "full_game_artifact_arena_core.h"

#include <catch2/catch_test_macros.hpp>

namespace vibe_othello::tools::full_game_arena {
namespace {

TEST_CASE("telemetry aggregation keeps candidate and baseline records separate", "[arena]") {
  const std::vector<SearchTelemetry> records{
      SearchTelemetry{.role = EngineRole::candidate,
                      .completed_depth = 3,
                      .elapsed_ms = 10,
                      .nodes = 100,
                      .eval_calls = 40,
                      .tt_probes = 20,
                      .tt_hits = 10},
      SearchTelemetry{.role = EngineRole::baseline,
                      .completed_depth = 5,
                      .elapsed_ms = 20,
                      .nodes = 300,
                      .eval_calls = 80,
                      .tt_probes = 30,
                      .tt_hits = 15},
  };

  const TelemetrySummary candidate = summarize_telemetry({records.data(), 1});
  const TelemetrySummary baseline = summarize_telemetry({records.data() + 1, 1});

  REQUIRE(candidate.search_calls == 1);
  REQUIRE(candidate.nodes == 100);
  REQUIRE(candidate.eval_calls == 40);
  REQUIRE(candidate.tt_hits == 10);
  REQUIRE(baseline.search_calls == 1);
  REQUIRE(baseline.nodes == 300);
  REQUIRE(baseline.completed_depths == std::vector<std::uint64_t>{5});
}

TEST_CASE("telemetry records stopped and wall-time overshoot cases", "[arena]") {
  const std::vector<SearchTelemetry> records{
      SearchTelemetry{.role = EngineRole::candidate,
                      .elapsed_ms = 12,
                      .nodes = 7,
                      .stopped = true,
                      .exact = true,
                      .exact_handoff_attempted = true,
                      .time_budget_applies = true,
                      .time_budget_ms = 10},
      SearchTelemetry{.role = EngineRole::candidate,
                      .elapsed_ms = 4,
                      .nodes = 3,
                      .time_budget_applies = true,
                      .time_budget_ms = 10},
  };

  const TelemetrySummary summary = summarize_telemetry(records);

  REQUIRE(summary.stopped_searches == 1);
  REQUIRE(summary.exact_handoff_attempts == 1);
  REQUIRE(summary.exact_searches == 1);
  REQUIRE(summary.time_overshoot_ms == std::vector<std::uint64_t>{2, 0});
}

TEST_CASE("nearest rank depth percentiles return observed depths", "[arena]") {
  const std::vector<std::uint64_t> depths{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

  REQUIRE(nearest_rank_percentile(depths, 0.1) == 1.0);
  REQUIRE(nearest_rank_percentile(depths, 0.5) == 5.0);
  REQUIRE(nearest_rank_percentile(depths, 0.9) == 9.0);
  REQUIRE(nearest_rank_percentile({}, 0.5) == 0.0);
}

TEST_CASE("paired bootstrap is deterministic and clusters opening pairs", "[arena]") {
  const std::vector<PairObservation> pairs{
      PairObservation{.opening_key = "a",
                      .score = 0.0,
                      .disc_diff_sum = -10,
                      .games = 2,
                      .complete_color_swap = true},
      PairObservation{.opening_key = "b",
                      .score = 1.0,
                      .disc_diff_sum = 10,
                      .games = 2,
                      .complete_color_swap = true},
  };

  const BootstrapInterval first = paired_cluster_bootstrap(pairs, 17, 1000);
  const BootstrapInterval second = paired_cluster_bootstrap(pairs, 17, 1000);

  REQUIRE(first.point_estimate == 0.5);
  REQUIRE(first.lower_95 == second.lower_95);
  REQUIRE(first.upper_95 == second.upper_95);
  REQUIRE(first.opening_pair_count == 2);
  REQUIRE(first.game_count == 4);
}

TEST_CASE("same artifact sanity requires neutral complete pairs", "[arena]") {
  const std::vector<PairGameResult> games{
      PairGameResult{.opening_key = "start",
                     .side_assignment = "candidate_black",
                     .candidate_score = 1.0,
                     .candidate_disc_diff = 8},
      PairGameResult{.opening_key = "start",
                     .side_assignment = "candidate_white",
                     .candidate_score = 0.0,
                     .candidate_disc_diff = -8},
  };
  const std::vector<PairObservation> pairs = make_pair_observations(games);
  const SanitySummary sanity = summarize_sanity(pairs, true);

  REQUIRE(sanity.paired_color_swap_complete);
  REQUIRE(sanity.same_artifact_neutral);
  REQUIRE(sanity.paired_disc_diff_sum == 0);
}

TEST_CASE("incomplete and malformed outcomes fail pair sanity", "[arena]") {
  const std::vector<PairGameResult> games{
      PairGameResult{.opening_key = "start",
                     .side_assignment = "candidate_black",
                     .candidate_score = 0.0,
                     .candidate_disc_diff = -64,
                     .failed = true},
  };
  const std::vector<PairObservation> pairs = make_pair_observations(games);
  const SanitySummary sanity = summarize_sanity(pairs, true);

  REQUIRE_FALSE(sanity.paired_color_swap_complete);
  REQUIRE_FALSE(sanity.same_artifact_neutral);
  REQUIRE(sanity.incomplete_pairs == 1);
}

TEST_CASE("argument-order sanity requires complementary scores and disc differences", "[arena]") {
  const BootstrapInterval forward{.point_estimate = 0.75, .opening_pair_count = 2, .game_count = 4};
  const BootstrapInterval reverse{.point_estimate = 0.25, .opening_pair_count = 2, .game_count = 4};

  REQUIRE(argument_order_is_complementary(forward, 12, reverse, -12));
  REQUIRE_FALSE(argument_order_is_complementary(forward, 12, reverse, 12));
}

} // namespace
} // namespace vibe_othello::tools::full_game_arena
