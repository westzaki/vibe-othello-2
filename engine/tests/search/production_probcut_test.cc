#include "vibe_othello/search/production_probcut.h"

#include <catch2/catch_test_macros.hpp>

namespace vibe_othello::search {
namespace {

constexpr ProbCutRuntimeIdentityV1 kReviewedIdentity{
    .evaluator_family = "pattern-v2-endgame-lite",
    .artifact_family = "pattern-v2-egaroucid-lv17-full-value-v1",
    .weights_checksum = "0xfe3d38f9",
    .score_scale = 100,
    .trained_phase_mask = kAllProbCutPhasesMask,
    .fallback_additive_through_phase = 9,
};

TEST_CASE("production ProbCut profile resolves only for its reviewed runtime identity",
          "[search][probcut][production]") {
  const ResolvedProbCutConfigurationV1 resolved =
      production_probcut_configuration_v1(kReviewedIdentity, SearchMode::move, 8);
  REQUIRE(resolved.enabled());
  REQUIRE(resolved.semantic_fingerprint != 0);
  REQUIRE(resolved.options.maximum_probes_per_node == 1);
  REQUIRE(resolved.options.ordered_depth_pairs.size() == 1);
  REQUIRE(resolved.options.ordered_depth_pairs[0] ==
          (ProbCutDepthPairV1{.deep_depth = 7, .shallow_depth = 3}));
  REQUIRE(resolved.options.maximum_margin == 22);
  REQUIRE(resolved.options.maximum_shallow_overhead_ratio == 0.20);
  REQUIRE(resolved.options.minimum_official_nodes_before_probe == 0);
  REQUIRE(resolved.options.enabled_phase_mask ==
          ((std::uint16_t{1} << 3U) | (std::uint16_t{1} << 4U) | (std::uint16_t{1} << 6U) |
           (std::uint16_t{1} << 7U)));
  REQUIRE(resolved.options.calibration_profile != nullptr);
  REQUIRE(resolved.options.calibration_profile->profile_id ==
          "pattern-v2-egaroucid-lv17-full-value-mpc-d7-fast-v3");
  REQUIRE(resolved.options.calibration_profile->entries.size() == 5);
  REQUIRE(resolved.options.calibration_profile->scheduler_evidence.size() == 5);
  REQUIRE(resolved.options.calibration_profile->joint_false_cut_count == 0);
  REQUIRE(resolved.options.calibration_profile->joint_cut_candidate_count == 631);

  ProbCutRuntimeIdentityV1 mismatch = kReviewedIdentity;
  mismatch.weights_checksum = "0x00000000";
  REQUIRE_FALSE(production_probcut_configuration_v1(mismatch, SearchMode::move, 8).enabled());
  mismatch = kReviewedIdentity;
  mismatch.score_scale = 101;
  REQUIRE_FALSE(production_probcut_configuration_v1(mismatch, SearchMode::move, 8).enabled());
  mismatch = kReviewedIdentity;
  mismatch.trained_phase_mask &= static_cast<std::uint16_t>(~(std::uint16_t{1} << 12U));
  REQUIRE_FALSE(production_probcut_configuration_v1(mismatch, SearchMode::move, 8).enabled());
  mismatch = kReviewedIdentity;
  mismatch.fallback_additive_through_phase = 8;
  REQUIRE_FALSE(production_probcut_configuration_v1(mismatch, SearchMode::move, 8).enabled());
  mismatch = kReviewedIdentity;
  mismatch.fallback_additive_through_phase = std::nullopt;
  REQUIRE_FALSE(production_probcut_configuration_v1(mismatch, SearchMode::move, 8).enabled());
  REQUIRE_FALSE(
      production_probcut_configuration_v1(kReviewedIdentity, SearchMode::analyze, 8).enabled());
  REQUIRE_FALSE(
      production_probcut_configuration_v1(kReviewedIdentity, SearchMode::move, 0).enabled());
  REQUIRE_FALSE(
      production_probcut_configuration_v1(kReviewedIdentity, SearchMode::move, 10).enabled());
}

} // namespace
} // namespace vibe_othello::search
