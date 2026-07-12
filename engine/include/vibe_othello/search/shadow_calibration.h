#ifndef VIBE_OTHELLO_SEARCH_SHADOW_CALIBRATION_H_
#define VIBE_OTHELLO_SEARCH_SHADOW_CALIBRATION_H_

#include "vibe_othello/board_core/board.h"
#include "vibe_othello/search/score.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vibe_othello::search {

inline constexpr std::uint32_t kShadowCalibrationSchemaVersion = 1;
inline constexpr std::uint32_t kShadowCalibrationSampleRateScale = 1'000'000;

enum class ShadowNodeType : std::uint8_t {
  pv,
  cut,
  all,
};

enum class ShadowDeepResult : std::uint8_t {
  fail_low,
  exact,
  fail_high,
};

struct ShadowCalibrationSample {
  std::uint32_t schema_version = kShadowCalibrationSchemaVersion;
  std::string repo_sha;
  std::string search_config_id;
  std::string evaluator_id;
  std::string artifact_id;
  std::uint64_t canonical_position_hash = 0;
  std::uint8_t phase = 0;
  std::uint8_t occupied_count = 0;
  std::uint8_t empties = 0;
  Ply ply = 0;
  ShadowNodeType node_type = ShadowNodeType::all;
  bool pv_node = false;
  bool cut_node = false;
  bool all_node = false;
  Depth deep_depth = 0;
  Depth shallow_depth = 0;
  Score alpha = 0;
  Score beta = 0;
  Score shallow_score = 0;
  Score deep_score = 0;
  BoundType deep_bound = BoundType::exact;
  std::optional<board_core::Move> shallow_best_move;
  std::optional<board_core::Move> deep_best_move;
  bool best_move_agreement = false;
  bool pass_state = false;
  bool terminal_state = false;
  bool exact_handoff_eligible = false;
  ShadowDeepResult actual_deep_result = ShadowDeepResult::exact;
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
  Depth minimum_deep_depth = 4;
  Depth shallow_depth_reduction = 2;
  bool include_pv_nodes = false;
  bool include_pass_nodes = false;
  bool include_near_exact_nodes = false;
  std::uint64_t sampling_seed = 0;
  std::string_view repo_sha;
  std::string_view search_config_id;
  std::string_view evaluator_id;
  std::string_view artifact_id;
  ShadowCalibrationSink* sink = nullptr;

  friend constexpr bool operator==(const SelectiveSearchOptionsV1&,
                                   const SelectiveSearchOptionsV1&) = default;
};

// Shadow work is deliberately excluded from SearchStats and official nodes.
struct ShadowCalibrationStats {
  NodeCount shadow_candidates = 0;
  NodeCount shadow_samples = 0;
  NodeCount shadow_shallow_nodes = 0;
  NodeCount shadow_best_move_agreements = 0;
  NodeCount hypothetical_cut_highs = 0;
  NodeCount hypothetical_cut_lows = 0;
  NodeCount false_cut_high_candidates = 0;
  NodeCount false_cut_low_candidates = 0;

  friend bool operator==(const ShadowCalibrationStats&, const ShadowCalibrationStats&) = default;
};

} // namespace vibe_othello::search

#endif // VIBE_OTHELLO_SEARCH_SHADOW_CALIBRATION_H_
