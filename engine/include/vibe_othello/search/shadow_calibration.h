#ifndef VIBE_OTHELLO_SEARCH_SHADOW_CALIBRATION_H_
#define VIBE_OTHELLO_SEARCH_SHADOW_CALIBRATION_H_

#include "vibe_othello/board_core/board.h"
#include "vibe_othello/search/score.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace vibe_othello::search {

inline constexpr std::uint32_t kShadowCalibrationSchemaVersion = 5;
inline constexpr std::uint32_t kShadowCalibrationSampleRateScale = 1'000'000;

enum class ShadowNodeType : std::uint8_t {
  pv,
  cut,
  all,
};

// Result-independent role assigned when the official node is entered. This is
// the population key for ProbCut calibration; ShadowNodeType remains a
// post-result diagnostic classification.
enum class ShadowSearchRole : std::uint8_t {
  pv,
  non_pv_scout,
  other,
};

enum class ShadowWindowResult : std::uint8_t {
  fail_low,
  exact,
  fail_high,
};

struct ShadowCalibrationDepthPairV1 {
  Depth deep_depth = 0;
  Depth shallow_depth = 0;

  friend constexpr bool operator==(const ShadowCalibrationDepthPairV1&,
                                   const ShadowCalibrationDepthPairV1&) = default;
};

struct ShadowCalibrationSample {
  std::uint32_t schema_version = kShadowCalibrationSchemaVersion;
  std::string repo_sha;
  std::string search_config_id;
  std::string evaluator_id;
  std::string artifact_id;
  std::string collection_config_id;
  std::uint64_t canonical_position_hash = 0;
  std::uint8_t phase = 0;
  std::uint8_t occupied_count = 0;
  std::uint8_t empties = 0;
  Ply ply = 0;
  ShadowSearchRole search_role = ShadowSearchRole::other;
  ShadowNodeType node_type = ShadowNodeType::all;
  bool pv_node = false;
  bool cut_node = false;
  bool all_node = false;
  std::uint16_t collection_pair_index = 0;
  std::uint16_t collection_pair_count = 0;
  std::uint16_t same_deep_pair_index = 0;
  std::uint16_t same_deep_pair_count = 0;
  Depth deep_depth = 0;
  Depth shallow_depth = 0;
  Score official_alpha = 0;
  Score official_beta = 0;
  Score official_deep_score = 0;
  BoundType official_deep_bound = BoundType::exact;
  Score shallow_verification_score = 0;
  Score deep_verification_score = 0;
  BoundType shallow_verification_bound = BoundType::exact;
  BoundType deep_verification_bound = BoundType::exact;
  std::optional<board_core::Move> shallow_verification_best_move;
  std::optional<board_core::Move> deep_verification_best_move;
  bool verification_best_move_agreement = false;
  bool pass_state = false;
  bool terminal_state = false;
  SearchMode search_mode = SearchMode::move;
  bool exact_handoff_enabled = false;
  std::uint8_t exact_handoff_threshold = 0;
  std::uint8_t exact_handoff_distance = 0;
  bool exact_handoff_eligible = false;
  ShadowWindowResult actual_official_deep_result = ShadowWindowResult::exact;
  bool hypothetical_cut_high = false;
  bool hypothetical_cut_low = false;
  bool false_cut_high_candidate = false;
  bool false_cut_low_candidate = false;
  std::uint64_t sampling_seed = 0;
  std::string search_identity;

  friend bool operator==(const ShadowCalibrationSample&, const ShadowCalibrationSample&) = default;
};

class ShadowCalibrationSink {
public:
  ShadowCalibrationSink() = default;
  virtual ~ShadowCalibrationSink() = default;
  ShadowCalibrationSink(const ShadowCalibrationSink&) = delete;
  ShadowCalibrationSink& operator=(const ShadowCalibrationSink&) = delete;
  ShadowCalibrationSink(ShadowCalibrationSink&&) = delete;
  ShadowCalibrationSink& operator=(ShadowCalibrationSink&&) = delete;

  virtual void record(const ShadowCalibrationSample& sample) noexcept = 0;
};

// Versioned, diagnostics-only selective-search configuration. sample_rate is a
// deterministic integer rate in [0, kShadowCalibrationSampleRateScale]. The
// metadata string views and sink must remain alive for the search call.
struct SelectiveSearchOptionsV1 {
  bool enable_shadow_calibration = false;
  std::uint32_t sample_rate = 0;
  std::uint32_t max_samples_per_search = 0;
  // The reviewed order is preserved in collection_config_id and in every
  // sample's pair indices. Storage must outlive the search call.
  std::span<const ShadowCalibrationDepthPairV1> ordered_depth_pairs;
  bool include_pv_nodes = false;
  bool include_pass_nodes = false;
  bool include_near_exact_nodes = false;
  std::uint64_t sampling_seed = 0;
  std::string_view repo_sha;
  std::string_view search_config_id;
  std::string_view evaluator_id;
  std::string_view artifact_id;
  ShadowCalibrationSink* sink = nullptr;

  friend constexpr bool operator==(const SelectiveSearchOptionsV1& lhs,
                                   const SelectiveSearchOptionsV1& rhs) noexcept {
    if (lhs.enable_shadow_calibration != rhs.enable_shadow_calibration ||
        lhs.sample_rate != rhs.sample_rate ||
        lhs.max_samples_per_search != rhs.max_samples_per_search ||
        lhs.ordered_depth_pairs.size() != rhs.ordered_depth_pairs.size() ||
        lhs.include_pv_nodes != rhs.include_pv_nodes ||
        lhs.include_pass_nodes != rhs.include_pass_nodes ||
        lhs.include_near_exact_nodes != rhs.include_near_exact_nodes ||
        lhs.sampling_seed != rhs.sampling_seed || lhs.repo_sha != rhs.repo_sha ||
        lhs.search_config_id != rhs.search_config_id || lhs.evaluator_id != rhs.evaluator_id ||
        lhs.artifact_id != rhs.artifact_id || lhs.sink != rhs.sink) {
      return false;
    }
    for (std::size_t index = 0; index < lhs.ordered_depth_pairs.size(); ++index) {
      if (lhs.ordered_depth_pairs[index] != rhs.ordered_depth_pairs[index]) {
        return false;
      }
    }
    return true;
  }
};

// Shadow work is deliberately excluded from SearchStats and official nodes.
struct ShadowCalibrationStats {
  NodeCount shadow_candidates = 0;
  NodeCount shadow_samples = 0;
  NodeCount shadow_shallow_nodes = 0;
  NodeCount shadow_deep_verification_nodes = 0;
  NodeCount shadow_shallow_verification_searches = 0;
  NodeCount shadow_deep_verification_searches = 0;
  NodeCount shadow_verification_probcut_attempts = 0;
  NodeCount shadow_verification_probcut_beta_cutoffs = 0;
  NodeCount shadow_best_move_agreements = 0;
  NodeCount hypothetical_cut_highs = 0;
  NodeCount hypothetical_cut_lows = 0;
  NodeCount false_cut_high_candidates = 0;
  NodeCount false_cut_low_candidates = 0;

  friend bool operator==(const ShadowCalibrationStats&, const ShadowCalibrationStats&) = default;
};

} // namespace vibe_othello::search

#endif // VIBE_OTHELLO_SEARCH_SHADOW_CALIBRATION_H_
