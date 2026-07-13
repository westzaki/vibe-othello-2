#include "full_game_artifact_arena_core.h"

#include <catch2/catch_test_macros.hpp>

namespace vibe_othello::tools::full_game_arena {
namespace {

TEST_CASE("telemetry aggregation keeps candidate and baseline records separate", "[arena]") {
  const std::vector<SearchTelemetry> records{
      SearchTelemetry{.role = EngineRole::candidate,
                      .completed_depth = 3,
                      .elapsed_ns = 10'000'000,
                      .engine_elapsed_ms = 9,
                      .timer_accounting_delta_ns = 1'000'000,
                      .nodes = 100,
                      .eval_calls = 40,
                      .incremental_eval_enabled = true,
                      .incremental_state_initializations = 3,
                      .incremental_eval_calls = 30,
                      .stateless_eval_calls = 10,
                      .incremental_updates = 200,
                      .incremental_touched_instances = 500,
                      .tt_probes = 20,
                      .tt_hits = 10},
      SearchTelemetry{.role = EngineRole::baseline,
                      .completed_depth = 5,
                      .elapsed_ns = 20'000'000,
                      .engine_elapsed_ms = 19,
                      .timer_accounting_delta_ns = 1'000'000,
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
  REQUIRE(candidate.incremental_eval_enabled_searches == 1);
  REQUIRE(candidate.incremental_state_initializations == 3);
  REQUIRE(candidate.incremental_eval_calls == 30);
  REQUIRE(candidate.stateless_eval_calls == 10);
  REQUIRE(candidate.incremental_updates == 200);
  REQUIRE(candidate.incremental_touched_instances == 500);
  REQUIRE(candidate.tt_hits == 10);
  REQUIRE(baseline.search_calls == 1);
  REQUIRE(baseline.nodes == 300);
  REQUIRE(baseline.completed_depths == std::vector<std::uint64_t>{5});
}

TEST_CASE("telemetry records stopped and wall-time overshoot cases", "[arena]") {
  const std::vector<SearchTelemetry> records{
      SearchTelemetry{.role = EngineRole::candidate,
                      .elapsed_ns = 12'000'000,
                      .engine_elapsed_ms = 11,
                      .timer_accounting_delta_ns = 1'000'000,
                      .nodes = 7,
                      .exact = true,
                      .stopped = true,
                      .exact_handoff_used = true,
                      .exact_root_search = true,
                      .time_budget_applies = true,
                      .time_budget_ns = 10'000'000},
      SearchTelemetry{.role = EngineRole::candidate,
                      .elapsed_ns = 4'000'000,
                      .engine_elapsed_ms = 3,
                      .timer_accounting_delta_ns = 1'000'000,
                      .nodes = 3,
                      .time_budget_applies = true,
                      .time_budget_ns = 10'000'000},
  };

  const TelemetrySummary summary = summarize_telemetry(records);

  REQUIRE(summary.stopped_searches == 1);
  REQUIRE(summary.exact_handoff_uses == 1);
  REQUIRE(summary.exact_root_searches == 1);
  REQUIRE(summary.exact_searches == 1);
  REQUIRE(summary.elapsed_ns == 16'000'000);
  REQUIRE(summary.engine_elapsed_ms == 14);
  REQUIRE(summary.timer_accounting_delta_ns == 2'000'000);
  REQUIRE(summary.time_overshoot_ns == std::vector<std::uint64_t>{2'000'000, 0});
}

TEST_CASE("telemetry aggregates Multi-ProbCut phase and depth pairs", "[arena]") {
  const std::vector<SearchTelemetry> records{
      SearchTelemetry{
          .probcut_attempts = 2,
          .probcut_shallow_nodes = 20,
          .probcut_successes = 1,
          .probcut_by_phase_depth_pair = {ProbCutPairTelemetry{.phase = 3,
                                                               .deep_depth = 8,
                                                               .shallow_depth = 3,
                                                               .attempts = 2,
                                                               .shallow_nodes = 20,
                                                               .successes = 1}},
      },
      SearchTelemetry{
          .probcut_attempts = 3,
          .probcut_shallow_nodes = 30,
          .probcut_confidence_rejections = 2,
          .probcut_by_phase_depth_pair = {ProbCutPairTelemetry{.phase = 3,
                                                               .deep_depth = 8,
                                                               .shallow_depth = 3,
                                                               .attempts = 3,
                                                               .shallow_nodes = 30,
                                                               .confidence_rejections = 2,
                                                               .unsupported_profile = 1,
                                                               .near_exact_rejections = 2,
                                                               .pass_rejections = 3,
                                                               .pv_rejections = 4,
                                                               .root_rejections = 5}},
      },
  };

  const TelemetrySummary summary = summarize_telemetry(records);
  REQUIRE(summary.probcut_attempts == 5);
  REQUIRE(summary.probcut_shallow_nodes == 50);
  REQUIRE(summary.probcut_successes == 1);
  REQUIRE(summary.probcut_confidence_rejections == 2);
  REQUIRE(summary.probcut_by_phase_depth_pair.size() == 1);
  REQUIRE(summary.probcut_by_phase_depth_pair[0].attempts == 5);
  REQUIRE(summary.probcut_by_phase_depth_pair[0].shallow_nodes == 50);
  REQUIRE(summary.probcut_by_phase_depth_pair[0].unsupported_profile == 1);
  REQUIRE(summary.probcut_by_phase_depth_pair[0].near_exact_rejections == 2);
  REQUIRE(summary.probcut_by_phase_depth_pair[0].pass_rejections == 3);
  REQUIRE(summary.probcut_by_phase_depth_pair[0].pv_rejections == 4);
  REQUIRE(summary.probcut_by_phase_depth_pair[0].root_rejections == 5);
}

TEST_CASE("nearest rank depth percentiles return observed depths", "[arena]") {
  const std::vector<std::uint64_t> depths{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

  REQUIRE(nearest_rank_percentile(depths, 0.1) == 1.0);
  REQUIRE(nearest_rank_percentile(depths, 0.5) == 5.0);
  REQUIRE(nearest_rank_percentile(depths, 0.9) == 9.0);
  REQUIRE(nearest_rank_percentile({}, 0.5) == 0.0);
}

TEST_CASE("speed rates retain sub-millisecond timing precision", "[arena]") {
  const std::optional<double> rate = events_per_second(100, 900'000);

  REQUIRE(rate.has_value());
  REQUIRE(*rate > 111'111.0);
  REQUIRE(*rate < 111'112.0);
  REQUIRE_FALSE(events_per_second(100, 0).has_value());
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

TEST_CASE("strength gate rejects invalid games and missing telemetry", "[arena]") {
  const StrengthGateSummary eligible = evaluate_strength_gate(true, 0, 0, 0, 10, 10, true, true);
  const StrengthGateSummary invalid = evaluate_strength_gate(true, 2, 1, 0, 10, 10, true, false);

  REQUIRE(eligible.eligible);
  REQUIRE(eligible.reasons.empty());
  REQUIRE_FALSE(invalid.eligible);
  REQUIRE(invalid.reasons == std::vector<std::string>{"failed_games_nonzero",
                                                      "illegal_games_nonzero",
                                                      "baseline_telemetry_missing"});
}

} // namespace
} // namespace vibe_othello::tools::full_game_arena
