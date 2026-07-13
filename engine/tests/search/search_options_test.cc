#include "../../src/search/search_options_internal.h"

#include <catch2/catch_test_macros.hpp>

namespace vibe_othello::search {
namespace {

void require_default_resolved_options(const internal::ResolvedSearchOptions& options) {
  REQUIRE(options.mode == SearchMode::move);

  REQUIRE_FALSE(options.midgame.use_pvs);
  REQUIRE_FALSE(options.midgame.use_aspiration);
  REQUIRE_FALSE(options.midgame.use_iid);
  REQUIRE_FALSE(options.midgame.use_midgame_tt);

  REQUIRE_FALSE(options.ordering.use_tt_best_move_ordering);
  REQUIRE_FALSE(options.ordering.use_history);
  REQUIRE_FALSE(options.ordering.use_killers);
  REQUIRE(options.ordering.use_endgame_parity_ordering);

  REQUIRE_FALSE(options.endgame.exact_endgame);
  REQUIRE_FALSE(options.endgame.use_endgame_tt);
  REQUIRE(options.endgame.endgame_exact_empties == 0);
  REQUIRE(options.endgame.endgame_wld_empties == 0);

  REQUIRE(options.reporting.multi_pv == 0);

  REQUIRE_FALSE(options.experimental.probcut);
  REQUIRE_FALSE(options.experimental.use_pv_table);
  REQUIRE_FALSE(options.experimental.use_parallel);
  REQUIRE(options.experimental.selectivity_level == 0);

  REQUIRE(options.selective == SelectiveSearchOptionsV1{});
  REQUIRE(options.probcut == ProbCutOptionsV1{});
  REQUIRE(options.probcut_profile_semantic_fingerprint == 0);
}

TEST_CASE("ProbCut normalization requires a complete reviewed profile identity",
          "[search][options][probcut]") {
  const ProbCutCalibrationEntryV1 entry{
      .phase = 0,
      .deep_depth = 4,
      .shallow_depth = 2,
      .regression_slope = 1.0,
      .intercept = 0.0,
      .residual_sigma = 1.0,
      .confidence_multiplier = 3.0,
      .minimum_shallow_score = -100,
      .maximum_shallow_score = 100,
      .minimum_beta = -100,
      .maximum_beta = 100,
  };
  const ProbCutCalibrationProfileV1 profile{
      .profile_id = "synthetic-options-v1",
      .source_calibration_report_checksum_sha256 =
          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      .evaluator_family = "synthetic",
      .artifact_family = "none",
      .node_class = ProbCutNodeClassV1::non_pv_scout_beta_only,
      .entries = std::span<const ProbCutCalibrationEntryV1>{&entry, 1},
  };
  SearchOptions options{};
  options.probcut_options = ProbCutOptionsV1{
      .use_probcut = true,
      .minimum_depth = 4,
      .shallow_depth_reduction = 2,
      .confidence_multiplier = 3.0,
      .minimum_margin = 2,
      .maximum_margin = 8,
      .evaluator_family = profile.evaluator_family,
      .artifact_family = profile.artifact_family,
      .calibration_profile_id = profile.profile_id,
      .calibration_profile = &profile,
  };

  const internal::ResolvedSearchOptions resolved = internal::normalize_search_options(options);
  REQUIRE(resolved.probcut == options.probcut_options);
  REQUIRE(resolved.probcut_profile_semantic_fingerprint != 0);

  SearchOptions wrong_family = options;
  wrong_family.probcut_options.artifact_family = "another-artifact";
  REQUIRE_FALSE(internal::normalize_search_options(wrong_family).probcut.use_probcut);

  ProbCutCalibrationProfileV1 wrong_node_class = profile;
  wrong_node_class.node_class = ProbCutNodeClassV1::unspecified;
  options.probcut_options.calibration_profile = &wrong_node_class;
  const internal::ResolvedSearchOptions wrong_population =
      internal::normalize_search_options(options);
  REQUIRE_FALSE(wrong_population.probcut.use_probcut);
  REQUIRE(wrong_population.probcut_profile_semantic_fingerprint == 0);

  options.probcut_options.calibration_profile = &profile;
  options.probcut_options.calibration_profile_id = "unreviewed-mismatch";
  const internal::ResolvedSearchOptions rejected = internal::normalize_search_options(options);
  REQUIRE_FALSE(rejected.probcut.use_probcut);
  REQUIRE(rejected.probcut_profile_semantic_fingerprint == 0);
}

TEST_CASE("Multi-ProbCut normalization rejects ambiguous profile domains",
          "[search][options][probcut]") {
  const std::array pairs{ProbCutDepthPairV1{.deep_depth = 8, .shallow_depth = 3}};
  const std::array entries{
      ProbCutCalibrationEntryV1{
          .phase = 4,
          .minimum_empties = 20,
          .maximum_empties = 30,
          .deep_depth = 8,
          .shallow_depth = 3,
          .regression_slope = 1.0,
          .residual_sigma = 1.0,
          .confidence_multiplier = 3.0,
          .minimum_shallow_score = -100,
          .maximum_shallow_score = 100,
          .minimum_beta = -100,
          .maximum_beta = 100,
      },
      ProbCutCalibrationEntryV1{
          .phase = 4,
          .minimum_empties = 25,
          .maximum_empties = 35,
          .deep_depth = 8,
          .shallow_depth = 3,
          .regression_slope = 1.0,
          .residual_sigma = 1.0,
          .confidence_multiplier = 3.0,
          .minimum_shallow_score = -100,
          .maximum_shallow_score = 100,
          .minimum_beta = -100,
          .maximum_beta = 100,
      },
  };
  const ProbCutCalibrationProfileV1 profile{
      .profile_id = "ambiguous-profile",
      .source_calibration_report_checksum_sha256 =
          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      .evaluator_family = "synthetic",
      .artifact_family = "none",
      .node_class = ProbCutNodeClassV1::non_pv_scout_beta_only,
      .ordered_depth_pairs = pairs,
      .entries = entries,
  };
  const SearchOptions options{.probcut_options = ProbCutOptionsV1{
                                  .use_probcut = true,
                                  .minimum_depth = 8,
                                  .maximum_probes_per_node = 1,
                                  .ordered_depth_pairs = pairs,
                                  .maximum_margin = 8,
                                  .evaluator_family = profile.evaluator_family,
                                  .artifact_family = profile.artifact_family,
                                  .calibration_profile_id = profile.profile_id,
                                  .calibration_profile = &profile,
                              }};
  REQUIRE_FALSE(internal::normalize_search_options(options).probcut.use_probcut);
}

TEST_CASE("Multi-ProbCut semantic identity includes pair order and report checksum",
          "[search][options][probcut]") {
  const std::array pairs{
      ProbCutDepthPairV1{.deep_depth = 8, .shallow_depth = 3},
      ProbCutDepthPairV1{.deep_depth = 8, .shallow_depth = 4},
  };
  const std::array reversed_pairs{pairs[1], pairs[0]};
  const std::array entries{
      ProbCutCalibrationEntryV1{
          .phase = 0,
          .deep_depth = 8,
          .shallow_depth = 3,
          .regression_slope = 1.0,
          .residual_sigma = 1.0,
          .confidence_multiplier = 3.0,
          .minimum_shallow_score = -100,
          .maximum_shallow_score = 100,
          .minimum_beta = -100,
          .maximum_beta = 100,
      },
      ProbCutCalibrationEntryV1{
          .phase = 0,
          .deep_depth = 8,
          .shallow_depth = 4,
          .regression_slope = 1.0,
          .residual_sigma = 1.0,
          .confidence_multiplier = 3.0,
          .minimum_shallow_score = -100,
          .maximum_shallow_score = 100,
          .minimum_beta = -100,
          .maximum_beta = 100,
      },
  };
  ProbCutCalibrationProfileV1 profile{
      .profile_id = "semantic-profile",
      .source_calibration_report_checksum_sha256 =
          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      .evaluator_family = "synthetic",
      .artifact_family = "none",
      .node_class = ProbCutNodeClassV1::non_pv_scout_beta_only,
      .ordered_depth_pairs = pairs,
      .entries = entries,
  };
  const auto options_for_profile = [](const ProbCutCalibrationProfileV1* value) {
    return SearchOptions{.probcut_options = ProbCutOptionsV1{
                             .use_probcut = true,
                             .minimum_depth = 8,
                             .maximum_probes_per_node = 2,
                             .ordered_depth_pairs = value->ordered_depth_pairs,
                             .minimum_margin = 0,
                             .maximum_margin = 8,
                             .evaluator_family = value->evaluator_family,
                             .artifact_family = value->artifact_family,
                             .calibration_profile_id = value->profile_id,
                             .calibration_profile = value,
                         }};
  };
  const auto original = internal::normalize_search_options(options_for_profile(&profile));
  REQUIRE(original.probcut.use_probcut);

  ProbCutCalibrationProfileV1 reordered = profile;
  reordered.ordered_depth_pairs = reversed_pairs;
  SearchOptions reordered_options = options_for_profile(&reordered);
  reordered_options.probcut_options.ordered_depth_pairs = reversed_pairs;
  const auto reordered_resolved = internal::normalize_search_options(reordered_options);
  REQUIRE(reordered_resolved.probcut.use_probcut);
  REQUIRE(reordered_resolved.probcut_profile_semantic_fingerprint !=
          original.probcut_profile_semantic_fingerprint);

  ProbCutCalibrationProfileV1 new_report = profile;
  new_report.source_calibration_report_checksum_sha256 =
      "1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  const auto new_report_resolved =
      internal::normalize_search_options(options_for_profile(&new_report));
  REQUIRE(new_report_resolved.probcut_profile_semantic_fingerprint !=
          original.probcut_profile_semantic_fingerprint);
}

} // namespace

TEST_CASE("default search options normalize to default resolved options", "[search][options]") {
  require_default_resolved_options(internal::normalize_search_options(SearchOptions{}));
}

TEST_CASE("legacy flat search options normalize into typed groups", "[search][options]") {
  const internal::ResolvedSearchOptions resolved = internal::normalize_search_options(SearchOptions{
      .use_pvs = true,
      .use_aspiration = true,
      .use_iid = true,
      .use_history = true,
      .use_killers = true,
      .use_midgame_tt = true,
      .use_endgame_tt = true,
      .exact_endgame = true,
      .probcut = true,
      .use_pv_table = true,
      .use_parallel = true,
      .use_tt_best_move_ordering = true,
      .use_endgame_parity_ordering = false,
      .multi_pv = 2,
      .endgame_exact_empties = 10,
      .endgame_wld_empties = 12,
      .selectivity_level = 3,
      .mode = SearchMode::win_loss_draw,
  });

  REQUIRE(resolved.mode == SearchMode::win_loss_draw);

  REQUIRE(resolved.midgame.use_pvs);
  REQUIRE(resolved.midgame.use_aspiration);
  REQUIRE(resolved.midgame.use_iid);
  REQUIRE(resolved.midgame.use_midgame_tt);

  REQUIRE(resolved.ordering.use_tt_best_move_ordering);
  REQUIRE(resolved.ordering.use_history);
  REQUIRE(resolved.ordering.use_killers);
  REQUIRE_FALSE(resolved.ordering.use_endgame_parity_ordering);

  REQUIRE(resolved.endgame.exact_endgame);
  REQUIRE(resolved.endgame.use_endgame_tt);
  REQUIRE(resolved.endgame.endgame_exact_empties == 10);
  REQUIRE(resolved.endgame.endgame_wld_empties == 12);

  REQUIRE(resolved.reporting.multi_pv == 2);

  REQUIRE(resolved.experimental.probcut);
  REQUIRE(resolved.experimental.use_pv_table);
  REQUIRE(resolved.experimental.use_parallel);
  REQUIRE(resolved.experimental.selectivity_level == 3);
}

TEST_CASE("typed search sub-configs normalize without legacy flat fields", "[search][options]") {
  const internal::ResolvedSearchOptions resolved = internal::normalize_search_options(SearchOptions{
      .midgame =
          MidgameSearchOptions{
              .use_pvs = true,
              .use_aspiration = true,
              .use_iid = true,
              .use_midgame_tt = true,
          },
      .ordering =
          MoveOrderingOptions{
              .use_tt_best_move_ordering = true,
              .use_history = true,
              .use_killers = true,
              .use_endgame_parity_ordering = false,
          },
      .endgame =
          EndgameSearchOptions{
              .exact_endgame = true,
              .use_endgame_tt = true,
              .endgame_exact_empties = 10,
              .endgame_wld_empties = 12,
          },
      .reporting = SearchReportingOptions{.multi_pv = 2},
      .experimental =
          ExperimentalSearchOptions{
              .probcut = true,
              .use_pv_table = true,
              .use_parallel = true,
              .selectivity_level = 3,
          },
      .mode = SearchMode::exact_score,
  });

  REQUIRE(resolved.mode == SearchMode::exact_score);

  REQUIRE(resolved.midgame.use_pvs);
  REQUIRE(resolved.midgame.use_aspiration);
  REQUIRE(resolved.midgame.use_iid);
  REQUIRE(resolved.midgame.use_midgame_tt);

  REQUIRE(resolved.ordering.use_tt_best_move_ordering);
  REQUIRE(resolved.ordering.use_history);
  REQUIRE(resolved.ordering.use_killers);
  REQUIRE_FALSE(resolved.ordering.use_endgame_parity_ordering);

  REQUIRE(resolved.endgame.exact_endgame);
  REQUIRE(resolved.endgame.use_endgame_tt);
  REQUIRE(resolved.endgame.endgame_exact_empties == 10);
  REQUIRE(resolved.endgame.endgame_wld_empties == 12);

  REQUIRE(resolved.reporting.multi_pv == 2);

  REQUIRE(resolved.experimental.probcut);
  REQUIRE(resolved.experimental.use_pv_table);
  REQUIRE(resolved.experimental.use_parallel);
  REQUIRE(resolved.experimental.selectivity_level == 3);
}

TEST_CASE("conflicting legacy and typed search options use compatibility rules",
          "[search][options]") {
  const internal::ResolvedSearchOptions resolved = internal::normalize_search_options(SearchOptions{
      .use_pvs = true,
      .use_aspiration = true,
      .use_iid = true,
      .use_history = true,
      .use_killers = true,
      .use_midgame_tt = true,
      .use_endgame_tt = true,
      .exact_endgame = true,
      .probcut = true,
      .use_pv_table = true,
      .use_parallel = true,
      .use_tt_best_move_ordering = true,
      .use_endgame_parity_ordering = true,
      .multi_pv = 1,
      .endgame_exact_empties = 8,
      .endgame_wld_empties = 9,
      .selectivity_level = 2,
      .midgame =
          MidgameSearchOptions{
              .use_pvs = false,
              .use_aspiration = false,
              .use_iid = false,
              .use_midgame_tt = false,
          },
      .ordering =
          MoveOrderingOptions{
              .use_tt_best_move_ordering = false,
              .use_history = false,
              .use_killers = false,
              .use_endgame_parity_ordering = false,
          },
      .endgame =
          EndgameSearchOptions{
              .exact_endgame = false,
              .use_endgame_tt = false,
              .endgame_exact_empties = 10,
              .endgame_wld_empties = 12,
          },
      .reporting = SearchReportingOptions{.multi_pv = 3},
      .experimental =
          ExperimentalSearchOptions{
              .probcut = false,
              .use_pv_table = false,
              .use_parallel = false,
              .selectivity_level = 4,
          },
  });

  REQUIRE(resolved.midgame.use_pvs);
  REQUIRE(resolved.midgame.use_aspiration);
  REQUIRE(resolved.midgame.use_iid);
  REQUIRE(resolved.midgame.use_midgame_tt);

  REQUIRE(resolved.ordering.use_tt_best_move_ordering);
  REQUIRE(resolved.ordering.use_history);
  REQUIRE(resolved.ordering.use_killers);
  REQUIRE_FALSE(resolved.ordering.use_endgame_parity_ordering);

  REQUIRE(resolved.endgame.exact_endgame);
  REQUIRE(resolved.endgame.use_endgame_tt);
  REQUIRE(resolved.endgame.endgame_exact_empties == 8);
  REQUIRE(resolved.endgame.endgame_wld_empties == 9);

  REQUIRE(resolved.reporting.multi_pv == 1);

  REQUIRE(resolved.experimental.probcut);
  REQUIRE(resolved.experimental.use_pv_table);
  REQUIRE(resolved.experimental.use_parallel);
  REQUIRE(resolved.experimental.selectivity_level == 2);
}

TEST_CASE("zero-valued legacy numeric search options defer to typed values", "[search][options]") {
  const internal::ResolvedSearchOptions resolved = internal::normalize_search_options(SearchOptions{
      .multi_pv = 0,
      .endgame_exact_empties = 0,
      .endgame_wld_empties = 0,
      .selectivity_level = 0,
      .endgame =
          EndgameSearchOptions{
              .endgame_exact_empties = 10,
              .endgame_wld_empties = 12,
          },
      .reporting = SearchReportingOptions{.multi_pv = 3},
      .experimental = ExperimentalSearchOptions{.selectivity_level = 4},
  });

  REQUIRE(resolved.endgame.endgame_exact_empties == 10);
  REQUIRE(resolved.endgame.endgame_wld_empties == 12);
  REQUIRE(resolved.reporting.multi_pv == 3);
  REQUIRE(resolved.experimental.selectivity_level == 4);
}

TEST_CASE("typed true bool search options enable features when legacy fields are false",
          "[search][options]") {
  const internal::ResolvedSearchOptions resolved = internal::normalize_search_options(SearchOptions{
      .midgame =
          MidgameSearchOptions{
              .use_pvs = true,
              .use_aspiration = true,
              .use_iid = true,
              .use_midgame_tt = true,
          },
      .ordering =
          MoveOrderingOptions{
              .use_tt_best_move_ordering = true,
              .use_history = true,
              .use_killers = true,
              .use_endgame_parity_ordering = true,
          },
      .endgame =
          EndgameSearchOptions{
              .exact_endgame = true,
              .use_endgame_tt = true,
          },
      .experimental =
          ExperimentalSearchOptions{
              .probcut = true,
              .use_pv_table = true,
              .use_parallel = true,
          },
  });

  REQUIRE(resolved.midgame.use_pvs);
  REQUIRE(resolved.midgame.use_aspiration);
  REQUIRE(resolved.midgame.use_iid);
  REQUIRE(resolved.midgame.use_midgame_tt);

  REQUIRE(resolved.ordering.use_tt_best_move_ordering);
  REQUIRE(resolved.ordering.use_history);
  REQUIRE(resolved.ordering.use_killers);
  REQUIRE(resolved.ordering.use_endgame_parity_ordering);

  REQUIRE(resolved.endgame.exact_endgame);
  REQUIRE(resolved.endgame.use_endgame_tt);

  REQUIRE(resolved.experimental.probcut);
  REQUIRE(resolved.experimental.use_pv_table);
  REQUIRE(resolved.experimental.use_parallel);
}

TEST_CASE("versioned selective search config normalizes without widening legacy fields",
          "[search][options]") {
  const SelectiveSearchOptionsV1 selective{
      .enable_shadow_calibration = true,
      .sample_rate = 125'000,
      .max_samples_per_search = 17,
      .minimum_deep_depth = 6,
      .shallow_depth_reduction = 3,
      .include_pv_nodes = true,
      .include_pass_nodes = true,
      .include_near_exact_nodes = true,
      .sampling_seed = 99,
      .repo_sha = "repo",
      .search_config_id = "config",
      .evaluator_id = "eval",
      .artifact_id = "artifact",
  };

  const internal::ResolvedSearchOptions resolved =
      internal::normalize_search_options(SearchOptions{.selective = selective});

  REQUIRE(resolved.selective == selective);
  REQUIRE_FALSE(resolved.experimental.probcut);
}

} // namespace vibe_othello::search
