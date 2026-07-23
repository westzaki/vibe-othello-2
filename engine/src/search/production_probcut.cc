#include "vibe_othello/search/production_probcut.h"

#include "production_probcut_profile_data.inc"

namespace vibe_othello::search {
namespace {

constexpr std::uint16_t kProductionPhaseMask = (std::uint16_t{1} << 3U) | (std::uint16_t{1} << 4U) |
                                               (std::uint16_t{1} << 6U) | (std::uint16_t{1} << 7U);
constexpr std::uint16_t kProductionScoreScale = 100;
constexpr std::uint16_t kProductionTrainedPhaseMask = kAllProbCutPhasesMask;
constexpr std::uint8_t kProductionFallbackAdditiveThroughPhase = 9;

} // namespace

ResolvedProbCutConfigurationV1
production_probcut_configuration_v1(ProbCutRuntimeIdentityV1 identity, SearchMode search_mode,
                                    std::uint8_t exact_handoff_threshold) noexcept {
#if !VIBE_OTHELLO_ENABLE_PRODUCTION_PROBCUT
  static_cast<void>(identity);
  static_cast<void>(search_mode);
  static_cast<void>(exact_handoff_threshold);
  return {};
#else
  using namespace production_internal;
  if (identity.evaluator_family != kProfile.evaluator_family ||
      identity.artifact_family != kProfile.artifact_family ||
      identity.weights_checksum != kWeightsChecksum ||
      identity.score_scale != kProductionScoreScale ||
      identity.trained_phase_mask != kProductionTrainedPhaseMask ||
      identity.fallback_additive_through_phase !=
          std::optional<std::uint8_t>{kProductionFallbackAdditiveThroughPhase} ||
      search_mode != SearchMode::move || exact_handoff_threshold != 8) {
    return {};
  }

  return resolve_probcut_configuration(ProbCutOptionsV1{
      .use_probcut = true,
      .minimum_depth = 7,
      .shallow_depth_reduction = 4,
      .maximum_probes_per_node = kProfile.validated_maximum_probes_per_node,
      .ordered_depth_pairs = kPairOrder,
      .stop_after_first_success = true,
      .confidence_multiplier = 0.0,
      .minimum_confidence = 0.0,
      .minimum_margin = 0,
      .maximum_margin = kMaximumMargin,
      .maximum_shallow_overhead_ratio = kMaximumShallowOverheadRatio,
      .minimum_official_nodes_before_probe = 0,
      .enabled_phase_mask = kProductionPhaseMask,
      .non_pv_only = true,
      .beta_only = true,
      .disable_near_exact = true,
      .near_exact_disable_empties = exact_handoff_threshold,
      .shadow_verify = false,
      .evaluator_family = identity.evaluator_family,
      .artifact_family = identity.artifact_family,
      .calibration_profile_id = kProfile.profile_id,
      .calibration_profile = &kProfile,
  });
#endif
}

} // namespace vibe_othello::search
