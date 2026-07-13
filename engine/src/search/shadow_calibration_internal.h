#pragma once

#include "vibe_othello/board_core/position.h"
#include "vibe_othello/search/search.h"

#include <optional>
#include <string>

namespace vibe_othello::search::internal {

struct SearchContext;
class SearchNodeResult;

struct ShadowCalibrationRun {
  SelectiveSearchOptionsV1 options{};
  ShadowCalibrationStats stats{};
  NodeCount reserved_samples = 0;
  std::string collection_config_id;
  std::string search_identity;
};

struct ShadowCandidate {
  board_core::Position position{};
  std::uint64_t canonical_position_hash = 0;
  Score alpha = 0;
  Score beta = 0;
  Depth deep_depth = 0;
  Ply ply = 0;
  std::uint8_t occupied_count = 0;
  std::uint8_t empties = 0;
  ShadowSearchRole search_role = ShadowSearchRole::other;
  bool pv_node = false;
  bool pass_state = false;
  bool terminal_state = false;
  SearchMode search_mode = SearchMode::move;
  bool exact_handoff_enabled = false;
  std::uint8_t exact_handoff_threshold = 0;
  std::uint8_t exact_handoff_distance = 0;
  bool exact_handoff_eligible = false;
};

std::optional<ShadowCalibrationRun> make_shadow_calibration_run(board_core::Position root,
                                                                SelectiveSearchOptionsV1 options);

std::optional<ShadowCandidate> begin_shadow_candidate(SearchContext* context, Score alpha,
                                                      Score beta, Depth depth, Ply ply,
                                                      bool cut_node);

void complete_shadow_candidate(SearchContext* context, const ShadowCandidate& candidate,
                               const SearchNodeResult& official_deep_result);

} // namespace vibe_othello::search::internal
