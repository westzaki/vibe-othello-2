#include "../../src/search/search_options_internal.h"

#include <array>
#include <catch2/catch_test_macros.hpp>

namespace vibe_othello::search {
namespace {

void require_default_resolved_options(const internal::ResolvedSearchOptions& options) {
  REQUIRE(options.mode == SearchMode::move);

  REQUIRE_FALSE(options.midgame.use_pvs);
  REQUIRE_FALSE(options.midgame.use_aspiration);
  REQUIRE_FALSE(options.midgame.use_iid);
  REQUIRE_FALSE(options.midgame.use_midgame_tt);
  REQUIRE(options.midgame.pass_consumes_depth);

  REQUIRE_FALSE(options.ordering.use_tt_best_move_ordering);
  REQUIRE_FALSE(options.ordering.use_history);
  REQUIRE_FALSE(options.ordering.use_killers);
  REQUIRE(options.ordering.use_endgame_parity_ordering);

  REQUIRE_FALSE(options.endgame.exact_endgame);
  REQUIRE_FALSE(options.endgame.use_endgame_tt);
  REQUIRE(options.endgame.endgame_exact_empties == 0);
  REQUIRE(options.endgame.endgame_wld_empties == 0);

  REQUIRE(options.reporting.multi_pv == 0);

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
  const std::array pair{ProbCutDepthPairV1{.deep_depth = 4, .shallow_depth = 2}};
  const std::array scheduler_evidence{ProbCutSchedulerEvidenceV1{
      .pair_prefix_length = 1,
      .maximum_probes_per_node = 1,
      .phase = 0,
      .deep_depth = 4,
      .holdout_node_count = 100,
      .false_cut_count = 0,
      .cut_candidate_count = 100,
      .false_cut_rate_upper_bound = 0.05,
  }};
  const ProbCutCalibrationProfileV1 profile{
      .profile_id = "synthetic-options-v1",
      .source_calibration_report_checksum_sha256 =
          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      .evaluator_family = "synthetic",
      .artifact_family = "none",
      .node_class = ProbCutNodeClassV1::non_pv_scout_beta_only,
      .validated_pair_order = pair,
      .validated_maximum_probes_per_node = 1,
      .joint_holdout_checksum_sha256 =
          "1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      .joint_false_cut_count = 0,
      .joint_cut_candidate_count = 100,
      .joint_false_cut_rate_upper_bound = 0.05,
      .scheduler_evidence = scheduler_evidence,
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

  ProbCutCalibrationProfileV1 inconsistent_joint_rate = profile;
  inconsistent_joint_rate.joint_false_cut_count = 10;
  inconsistent_joint_rate.joint_false_cut_rate_upper_bound = 0.05;
  options.probcut_options.calibration_profile = &inconsistent_joint_rate;
  REQUIRE_FALSE(internal::normalize_search_options(options).probcut.use_probcut);

  options.probcut_options.calibration_profile = &profile;
  options.probcut_options.calibration_profile_id = "unreviewed-mismatch";
  const internal::ResolvedSearchOptions rejected = internal::normalize_search_options(options);
  REQUIRE_FALSE(rejected.probcut.use_probcut);
  REQUIRE(rejected.probcut_profile_semantic_fingerprint == 0);
}

TEST_CASE("Multi-ProbCut normalization rejects ambiguous profile domains",
          "[search][options][probcut]") {
  const std::array pairs{ProbCutDepthPairV1{.deep_depth = 8, .shallow_depth = 3}};
  const std::array scheduler_evidence{ProbCutSchedulerEvidenceV1{
      .pair_prefix_length = 1,
      .maximum_probes_per_node = 1,
      .phase = 4,
      .minimum_empties = 20,
      .maximum_empties = 30,
      .deep_depth = 8,
      .holdout_node_count = 100,
      .false_cut_count = 0,
      .cut_candidate_count = 100,
      .false_cut_rate_upper_bound = 0.05,
  }};
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
      .validated_pair_order = pairs,
      .validated_maximum_probes_per_node = 1,
      .joint_holdout_checksum_sha256 =
          "1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      .joint_false_cut_count = 0,
      .joint_cut_candidate_count = 100,
      .joint_false_cut_rate_upper_bound = 0.05,
      .scheduler_evidence = scheduler_evidence,
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
  const std::array full_scheduler_evidence{ProbCutSchedulerEvidenceV1{
      .pair_prefix_length = 2,
      .maximum_probes_per_node = 2,
      .phase = 0,
      .deep_depth = 8,
      .holdout_node_count = 100,
      .false_cut_count = 0,
      .cut_candidate_count = 100,
      .false_cut_rate_upper_bound = 0.05,
  }};
  ProbCutCalibrationProfileV1 profile{
      .profile_id = "semantic-profile",
      .source_calibration_report_checksum_sha256 =
          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      .evaluator_family = "synthetic",
      .artifact_family = "none",
      .node_class = ProbCutNodeClassV1::non_pv_scout_beta_only,
      .validated_pair_order = pairs,
      .validated_maximum_probes_per_node = 2,
      .joint_holdout_checksum_sha256 =
          "1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      .joint_false_cut_count = 0,
      .joint_cut_candidate_count = 100,
      .joint_false_cut_rate_upper_bound = 0.05,
      .scheduler_evidence = full_scheduler_evidence,
      .entries = entries,
  };
  const auto options_for_profile = [](const ProbCutCalibrationProfileV1* value) {
    return SearchOptions{.probcut_options = ProbCutOptionsV1{
                             .use_probcut = true,
                             .minimum_depth = 8,
                             .maximum_probes_per_node = 2,
                             .ordered_depth_pairs = value->validated_pair_order,
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
  reordered.validated_pair_order = reversed_pairs;
  SearchOptions reordered_options = options_for_profile(&reordered);
  reordered_options.probcut_options.ordered_depth_pairs = reversed_pairs;
  const auto reordered_resolved = internal::normalize_search_options(reordered_options);
  REQUIRE(reordered_resolved.probcut.use_probcut);
  REQUIRE(reordered_resolved.probcut_profile_semantic_fingerprint !=
          original.probcut_profile_semantic_fingerprint);

  SearchOptions unreviewed_order = options_for_profile(&profile);
  unreviewed_order.probcut_options.ordered_depth_pairs = reversed_pairs;
  REQUIRE_FALSE(internal::normalize_search_options(unreviewed_order).probcut.use_probcut);
  SearchOptions unaudited_prefix = options_for_profile(&profile);
  unaudited_prefix.probcut_options.ordered_depth_pairs = std::span{pairs}.first(1);
  unaudited_prefix.probcut_options.maximum_probes_per_node = 1;
  REQUIRE_FALSE(internal::normalize_search_options(unaudited_prefix).probcut.use_probcut);
  const std::array all_scheduler_evidence{
      ProbCutSchedulerEvidenceV1{
          .pair_prefix_length = 1,
          .maximum_probes_per_node = 1,
          .phase = 0,
          .deep_depth = 8,
          .holdout_node_count = 100,
          .false_cut_count = 0,
          .cut_candidate_count = 100,
          .false_cut_rate_upper_bound = 0.05,
      },
      full_scheduler_evidence.front(),
  };
  ProbCutCalibrationProfileV1 audited_prefix_profile = profile;
  audited_prefix_profile.scheduler_evidence = all_scheduler_evidence;
  SearchOptions reviewed_prefix = options_for_profile(&audited_prefix_profile);
  reviewed_prefix.probcut_options.ordered_depth_pairs = std::span{pairs}.first(1);
  reviewed_prefix.probcut_options.maximum_probes_per_node = 1;
  REQUIRE(internal::normalize_search_options(reviewed_prefix).probcut.use_probcut);
  SearchOptions unreviewed_suffix = options_for_profile(&profile);
  unreviewed_suffix.probcut_options.ordered_depth_pairs = std::span{pairs}.subspan(1);
  unreviewed_suffix.probcut_options.maximum_probes_per_node = 1;
  REQUIRE_FALSE(internal::normalize_search_options(unreviewed_suffix).probcut.use_probcut);
  SearchOptions unreviewed_probe_count = options_for_profile(&profile);
  unreviewed_probe_count.probcut_options.maximum_probes_per_node = 3;
  REQUIRE_FALSE(internal::normalize_search_options(unreviewed_probe_count).probcut.use_probcut);

  ProbCutCalibrationProfileV1 new_report = profile;
  new_report.source_calibration_report_checksum_sha256 =
      "1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  const auto new_report_resolved =
      internal::normalize_search_options(options_for_profile(&new_report));
  REQUIRE(new_report_resolved.probcut_profile_semantic_fingerprint !=
          original.probcut_profile_semantic_fingerprint);

  ProbCutCalibrationProfileV1 new_holdout = profile;
  new_holdout.joint_holdout_checksum_sha256 =
      "2123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  const auto new_holdout_resolved =
      internal::normalize_search_options(options_for_profile(&new_holdout));
  REQUIRE(new_holdout_resolved.probcut_profile_semantic_fingerprint !=
          original.probcut_profile_semantic_fingerprint);

  std::array changed_scheduler_evidence = full_scheduler_evidence;
  changed_scheduler_evidence[0].holdout_node_count = 101;
  ProbCutCalibrationProfileV1 new_scheduler_evidence = profile;
  new_scheduler_evidence.scheduler_evidence = changed_scheduler_evidence;
  const auto new_scheduler_resolved =
      internal::normalize_search_options(options_for_profile(&new_scheduler_evidence));
  REQUIRE(new_scheduler_resolved.probcut.use_probcut);
  REQUIRE(new_scheduler_resolved.probcut_profile_semantic_fingerprint !=
          original.probcut_profile_semantic_fingerprint);
}

TEST_CASE("Multi-ProbCut requires scheduler evidence for every enabled exact domain",
          "[search][options][probcut]") {
  const std::array pairs{
      ProbCutDepthPairV1{.deep_depth = 8, .shallow_depth = 3},
      ProbCutDepthPairV1{.deep_depth = 8, .shallow_depth = 4},
  };
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
      ProbCutCalibrationEntryV1{
          .phase = 1,
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
          .phase = 1,
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
  const std::array incomplete_prefix_evidence{
      ProbCutSchedulerEvidenceV1{
          .pair_prefix_length = 1,
          .maximum_probes_per_node = 1,
          .phase = 0,
          .deep_depth = 8,
          .holdout_node_count = 100,
          .false_cut_count = 0,
          .cut_candidate_count = 100,
          .false_cut_rate_upper_bound = 0.05,
      },
      ProbCutSchedulerEvidenceV1{
          .pair_prefix_length = 2,
          .maximum_probes_per_node = 2,
          .phase = 0,
          .deep_depth = 8,
          .holdout_node_count = 100,
          .false_cut_count = 0,
          .cut_candidate_count = 100,
          .false_cut_rate_upper_bound = 0.05,
      },
      ProbCutSchedulerEvidenceV1{
          .pair_prefix_length = 2,
          .maximum_probes_per_node = 2,
          .phase = 1,
          .deep_depth = 8,
          .holdout_node_count = 100,
          .false_cut_count = 0,
          .cut_candidate_count = 100,
          .false_cut_rate_upper_bound = 0.05,
      },
  };
  ProbCutCalibrationProfileV1 profile{
      .profile_id = "multi-domain-profile",
      .source_calibration_report_checksum_sha256 =
          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      .evaluator_family = "synthetic",
      .artifact_family = "none",
      .node_class = ProbCutNodeClassV1::non_pv_scout_beta_only,
      .validated_pair_order = pairs,
      .validated_maximum_probes_per_node = 2,
      .joint_holdout_checksum_sha256 =
          "1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      .joint_false_cut_count = 0,
      .joint_cut_candidate_count = 200,
      .joint_false_cut_rate_upper_bound = 0.05,
      .scheduler_evidence = incomplete_prefix_evidence,
      .entries = entries,
  };
  SearchOptions prefix_options{.probcut_options = ProbCutOptionsV1{
                                   .use_probcut = true,
                                   .minimum_depth = 8,
                                   .maximum_probes_per_node = 1,
                                   .ordered_depth_pairs = std::span{pairs}.first(1),
                                   .maximum_margin = 8,
                                   .evaluator_family = profile.evaluator_family,
                                   .artifact_family = profile.artifact_family,
                                   .calibration_profile_id = profile.profile_id,
                                   .calibration_profile = &profile,
                               }};
  REQUIRE_FALSE(probcut_configuration_is_reviewed(profile, std::span{pairs}.first(1), 1));
  REQUIRE_FALSE(resolve_probcut_configuration(prefix_options.probcut_options).enabled());
  REQUIRE_FALSE(internal::normalize_search_options(prefix_options).probcut.use_probcut);

  std::array complete_evidence{
      incomplete_prefix_evidence[0],
      ProbCutSchedulerEvidenceV1{
          .pair_prefix_length = 1,
          .maximum_probes_per_node = 1,
          .phase = 1,
          .deep_depth = 8,
          .holdout_node_count = 100,
          .false_cut_count = 0,
          .cut_candidate_count = 100,
          .false_cut_rate_upper_bound = 0.05,
      },
      incomplete_prefix_evidence[1],
      incomplete_prefix_evidence[2],
  };
  profile.scheduler_evidence = complete_evidence;
  REQUIRE(probcut_configuration_is_reviewed(profile, std::span{pairs}.first(1), 1));
  REQUIRE(resolve_probcut_configuration(prefix_options.probcut_options).enabled());
  REQUIRE(internal::normalize_search_options(prefix_options).probcut.use_probcut);
}

} // namespace

TEST_CASE("default search options normalize to default resolved options", "[search][options]") {
  require_default_resolved_options(internal::normalize_search_options(SearchOptions{}));
}

TEST_CASE("typed search sub-configs are the resolved source of truth", "[search][options]") {
  const internal::ResolvedSearchOptions resolved = internal::normalize_search_options(SearchOptions{
      .midgame =
          MidgameSearchOptions{
              .use_pvs = true,
              .use_aspiration = true,
              .use_iid = true,
              .use_midgame_tt = true,
              .pass_consumes_depth = false,
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
      .mode = SearchMode::exact_score,
  });

  REQUIRE(resolved.mode == SearchMode::exact_score);

  REQUIRE(resolved.midgame.use_pvs);
  REQUIRE(resolved.midgame.use_aspiration);
  REQUIRE(resolved.midgame.use_iid);
  REQUIRE(resolved.midgame.use_midgame_tt);
  REQUIRE_FALSE(resolved.midgame.pass_consumes_depth);

  REQUIRE(resolved.ordering.use_tt_best_move_ordering);
  REQUIRE(resolved.ordering.use_history);
  REQUIRE(resolved.ordering.use_killers);
  REQUIRE_FALSE(resolved.ordering.use_endgame_parity_ordering);

  REQUIRE(resolved.endgame.exact_endgame);
  REQUIRE(resolved.endgame.use_endgame_tt);
  REQUIRE(resolved.endgame.endgame_exact_empties == 10);
  REQUIRE(resolved.endgame.endgame_wld_empties == 12);

  REQUIRE(resolved.reporting.multi_pv == 2);
}

TEST_CASE("versioned selective search config resolves independently", "[search][options]") {
  constexpr std::array pairs{
      ShadowCalibrationDepthPairV1{.deep_depth = 6, .shallow_depth = 3},
      ShadowCalibrationDepthPairV1{.deep_depth = 8, .shallow_depth = 4},
  };
  const SelectiveSearchOptionsV1 selective{
      .enable_shadow_calibration = true,
      .sample_rate = 125'000,
      .max_samples_per_search = 17,
      .ordered_depth_pairs = pairs,
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
}

} // namespace vibe_othello::search
