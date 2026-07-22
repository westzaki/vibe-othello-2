#include "vibe_othello/search/production_probcut.h"

#include <catch2/catch_test_macros.hpp>

namespace vibe_othello::search {
namespace {

constexpr ProbCutRuntimeIdentityV1 kReviewedIdentity{
    .evaluator_family = "pattern-v2-endgame-lite",
    .artifact_family = "pattern-v2-egaroucid-lv17-full-value-v1",
    .weights_checksum = "0xfe3d38f9",
};

TEST_CASE("production ProbCut profile resolves only for its reviewed runtime identity",
          "[search][probcut][production]") {
  const ResolvedProbCutConfigurationV1 resolved =
      production_probcut_configuration_v1(kReviewedIdentity, SearchMode::move, 8);
  REQUIRE(resolved.enabled());
  REQUIRE(resolved.semantic_fingerprint != 0);
  REQUIRE(resolved.options.maximum_probes_per_node == 2);
  REQUIRE(resolved.options.ordered_depth_pairs.size() == 2);
  REQUIRE(resolved.options.maximum_margin == 22);
  REQUIRE(resolved.options.maximum_shallow_overhead_ratio == 0.005);
  REQUIRE(resolved.options.minimum_official_nodes_before_probe == 25'000);
  REQUIRE(resolved.options.enabled_phase_mask ==
          ((std::uint16_t{1} << 2U) | (std::uint16_t{1} << 3U)));
  REQUIRE(resolved.options.calibration_profile != nullptr);
  REQUIRE(resolved.options.calibration_profile->entries.size() == 24);
  REQUIRE(resolved.options.calibration_profile->scheduler_evidence.size() == 24);
  REQUIRE(resolved.options.calibration_profile->joint_false_cut_count == 3);
  REQUIRE(resolved.options.calibration_profile->joint_cut_candidate_count == 1690);

  ProbCutRuntimeIdentityV1 mismatch = kReviewedIdentity;
  mismatch.weights_checksum = "0x00000000";
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
