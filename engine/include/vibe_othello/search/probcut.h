#ifndef VIBE_OTHELLO_SEARCH_PROBCUT_H_
#define VIBE_OTHELLO_SEARCH_PROBCUT_H_

#include "vibe_othello/search/score.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace vibe_othello::search {

inline constexpr std::uint32_t kProbCutCalibrationProfileSchemaVersion = 3;
inline constexpr std::uint16_t kAllProbCutPhasesMask = (std::uint16_t{1} << 13U) - 1U;

enum class ProbCutNodeClassV1 : std::uint8_t {
  unspecified,
  non_pv_scout_beta_only,
};

struct ProbCutDepthPairV1 {
  Depth deep_depth = 0;
  Depth shallow_depth = 0;

  friend constexpr bool operator==(const ProbCutDepthPairV1&, const ProbCutDepthPairV1&) = default;
};

// One reviewed regression group. Ranges are inclusive and are part of the
// calibrated domain; runtime code never extrapolates to another phase, depth
// pair, shallow score, or beta.
struct ProbCutCalibrationEntryV1 {
  std::uint8_t phase = 0;
  SearchMode search_mode = SearchMode::move;
  std::uint8_t minimum_empties = 0;
  std::uint8_t maximum_empties = 60;
  Depth deep_depth = 0;
  Depth shallow_depth = 0;
  bool exact_handoff_enabled = false;
  std::uint8_t exact_handoff_threshold = 0;
  std::uint8_t minimum_exact_handoff_distance = 0;
  std::uint8_t maximum_exact_handoff_distance = 0;
  double regression_slope = 0.0;
  double intercept = 0.0;
  double residual_sigma = 0.0;
  double confidence_multiplier = 0.0;
  Score minimum_shallow_score = 0;
  Score maximum_shallow_score = 0;
  Score minimum_beta = 0;
  Score maximum_beta = 0;

  friend constexpr bool operator==(const ProbCutCalibrationEntryV1&,
                                   const ProbCutCalibrationEntryV1&) = default;
};

// Holdout evidence for one runtime scheduler configuration and one exact
// profile domain. A prefix/probe configuration is usable only when every
// domain enabled by that configuration has a matching record.
struct ProbCutSchedulerEvidenceV1 {
  std::uint16_t pair_prefix_length = 0;
  std::uint8_t maximum_probes_per_node = 0;
  std::uint8_t phase = 0;
  SearchMode search_mode = SearchMode::move;
  std::uint8_t minimum_empties = 0;
  std::uint8_t maximum_empties = 60;
  Depth deep_depth = 0;
  bool exact_handoff_enabled = false;
  std::uint8_t exact_handoff_threshold = 0;
  std::uint8_t minimum_exact_handoff_distance = 0;
  std::uint8_t maximum_exact_handoff_distance = 0;
  NodeCount holdout_node_count = 0;
  NodeCount false_cut_count = 0;
  NodeCount cut_candidate_count = 0;
  double false_cut_rate_upper_bound = 1.0;

  friend constexpr bool operator==(const ProbCutSchedulerEvidenceV1&,
                                   const ProbCutSchedulerEvidenceV1&) = default;
};

// Profile storage and every referenced string/entry must outlive the search
// call. No production profile is built into the engine without reviewed local
// calibration evidence.
struct ProbCutCalibrationProfileV1 {
  std::uint32_t schema_version = kProbCutCalibrationProfileSchemaVersion;
  std::string_view profile_id;
  std::string_view source_calibration_report_checksum_sha256;
  std::string_view evaluator_family;
  std::string_view artifact_family;
  ProbCutNodeClassV1 node_class = ProbCutNodeClassV1::unspecified;
  // Order and maximum probe count considered by joint first-success holdout
  // replay. Runtime also requires an exact prefix/probe/domain evidence match.
  std::span<const ProbCutDepthPairV1> validated_pair_order;
  std::uint8_t validated_maximum_probes_per_node = 0;
  std::string_view joint_holdout_checksum_sha256;
  NodeCount joint_false_cut_count = 0;
  NodeCount joint_cut_candidate_count = 0;
  double joint_false_cut_rate_upper_bound = 1.0;
  std::span<const ProbCutSchedulerEvidenceV1> scheduler_evidence;
  std::span<const ProbCutCalibrationEntryV1> entries;
};

struct ProbCutOptionsV1 {
  bool use_probcut = false;
  Depth minimum_depth = 0;
  // Retained in the public option shape for disabled-option compatibility.
  // Reviewed schema-v3 profiles always use their validated pair order.
  Depth shallow_depth_reduction = 0;
  std::uint8_t maximum_probes_per_node = 1;
  std::span<const ProbCutDepthPairV1> ordered_depth_pairs;
  bool stop_after_first_success = true;
  double confidence_multiplier = 0.0;
  double minimum_confidence = 0.0;
  Score minimum_margin = 0;
  Score maximum_margin = 0;
  // Zero means no cumulative overhead gate. Otherwise a new probe is refused
  // once shallow nodes / non-shallow official nodes reaches this ratio.
  double maximum_shallow_overhead_ratio = 0.0;
  std::uint16_t enabled_phase_mask = kAllProbCutPhasesMask;
  bool non_pv_only = true;
  bool beta_only = true;
  bool disable_near_exact = true;
  std::uint8_t near_exact_disable_empties = 0;
  bool shadow_verify = false;
  // These describe the evaluator actually supplied by the caller. Runtime
  // selection requires exact equality with the reviewed profile identity.
  std::string_view evaluator_family;
  std::string_view artifact_family;
  std::string_view calibration_profile_id;
  const ProbCutCalibrationProfileV1* calibration_profile = nullptr;

  friend constexpr bool operator==(const ProbCutOptionsV1& lhs,
                                   const ProbCutOptionsV1& rhs) noexcept {
    return lhs.use_probcut == rhs.use_probcut && lhs.minimum_depth == rhs.minimum_depth &&
           lhs.shallow_depth_reduction == rhs.shallow_depth_reduction &&
           lhs.maximum_probes_per_node == rhs.maximum_probes_per_node &&
           lhs.ordered_depth_pairs.data() == rhs.ordered_depth_pairs.data() &&
           lhs.ordered_depth_pairs.size() == rhs.ordered_depth_pairs.size() &&
           lhs.stop_after_first_success == rhs.stop_after_first_success &&
           lhs.confidence_multiplier == rhs.confidence_multiplier &&
           lhs.minimum_confidence == rhs.minimum_confidence &&
           lhs.minimum_margin == rhs.minimum_margin && lhs.maximum_margin == rhs.maximum_margin &&
           lhs.maximum_shallow_overhead_ratio == rhs.maximum_shallow_overhead_ratio &&
           lhs.enabled_phase_mask == rhs.enabled_phase_mask && lhs.non_pv_only == rhs.non_pv_only &&
           lhs.beta_only == rhs.beta_only && lhs.disable_near_exact == rhs.disable_near_exact &&
           lhs.near_exact_disable_empties == rhs.near_exact_disable_empties &&
           lhs.shadow_verify == rhs.shadow_verify && lhs.evaluator_family == rhs.evaluator_family &&
           lhs.artifact_family == rhs.artifact_family &&
           lhs.calibration_profile_id == rhs.calibration_profile_id &&
           lhs.calibration_profile == rhs.calibration_profile;
  }
};

struct ResolvedProbCutConfigurationV1 {
  ProbCutOptionsV1 options{};
  std::uint64_t semantic_fingerprint = 0;

  [[nodiscard]] constexpr bool enabled() const noexcept {
    return options.use_probcut;
  }
};

// Returns true only when the exact ordered prefix and probe cap have scheduler
// evidence for every profile domain enabled by that prefix.
[[nodiscard]] bool
probcut_configuration_is_reviewed(const ProbCutCalibrationProfileV1& profile,
                                  std::span<const ProbCutDepthPairV1> ordered_depth_pairs,
                                  std::uint8_t maximum_probes_per_node) noexcept;

// Applies the same profile, identity, conservative-scope, and scheduler-evidence
// checks used by search option normalization. Invalid requested options resolve
// to a disabled default rather than retaining raw enablement fields.
[[nodiscard]] ResolvedProbCutConfigurationV1
resolve_probcut_configuration(ProbCutOptionsV1 options,
                              bool use_legacy_search_kernel = false) noexcept;

} // namespace vibe_othello::search

#endif // VIBE_OTHELLO_SEARCH_PROBCUT_H_
