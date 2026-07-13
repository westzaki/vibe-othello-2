#ifndef VIBE_OTHELLO_SEARCH_PROBCUT_H_
#define VIBE_OTHELLO_SEARCH_PROBCUT_H_

#include "vibe_othello/search/score.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace vibe_othello::search {

inline constexpr std::uint32_t kProbCutCalibrationProfileSchemaVersion = 1;

// One reviewed regression group. Ranges are inclusive and are part of the
// calibrated domain; runtime code never extrapolates to another phase, depth
// pair, shallow score, or beta.
struct ProbCutCalibrationEntryV1 {
  std::uint8_t phase = 0;
  Depth deep_depth = 0;
  Depth shallow_depth = 0;
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

// Profile storage and every referenced string/entry must outlive the search
// call. No production profile is built into the engine without reviewed local
// calibration evidence.
struct ProbCutCalibrationProfileV1 {
  std::uint32_t schema_version = kProbCutCalibrationProfileSchemaVersion;
  std::string_view profile_id;
  std::string_view source_calibration_report_checksum_sha256;
  std::string_view evaluator_family;
  std::string_view artifact_family;
  std::span<const ProbCutCalibrationEntryV1> entries;
};

struct ProbCutOptionsV1 {
  bool use_probcut = false;
  Depth minimum_depth = 0;
  Depth shallow_depth_reduction = 0;
  double confidence_multiplier = 0.0;
  Score minimum_margin = 0;
  Score maximum_margin = 0;
  bool non_pv_only = true;
  bool beta_only = true;
  bool disable_near_exact = true;
  bool shadow_verify = false;
  std::string_view calibration_profile_id;
  const ProbCutCalibrationProfileV1* calibration_profile = nullptr;

  friend constexpr bool operator==(const ProbCutOptionsV1& lhs,
                                   const ProbCutOptionsV1& rhs) noexcept {
    return lhs.use_probcut == rhs.use_probcut && lhs.minimum_depth == rhs.minimum_depth &&
           lhs.shallow_depth_reduction == rhs.shallow_depth_reduction &&
           lhs.confidence_multiplier == rhs.confidence_multiplier &&
           lhs.minimum_margin == rhs.minimum_margin && lhs.maximum_margin == rhs.maximum_margin &&
           lhs.non_pv_only == rhs.non_pv_only && lhs.beta_only == rhs.beta_only &&
           lhs.disable_near_exact == rhs.disable_near_exact &&
           lhs.shadow_verify == rhs.shadow_verify &&
           lhs.calibration_profile_id == rhs.calibration_profile_id &&
           lhs.calibration_profile == rhs.calibration_profile;
  }
};

} // namespace vibe_othello::search

#endif // VIBE_OTHELLO_SEARCH_PROBCUT_H_
