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
  Depth shallow_depth = 0;
  Ply ply = 0;
  std::uint8_t occupied_count = 0;
  std::uint8_t empties = 0;
  bool pv_node = false;
  bool pass_state = false;
  bool terminal_state = false;
  bool exact_handoff_eligible = false;
};

std::optional<ShadowCalibrationRun> make_shadow_calibration_run(board_core::Position root,
                                                                SelectiveSearchOptionsV1 options);

std::optional<ShadowCandidate> begin_shadow_candidate(SearchContext* context, Score alpha,
                                                      Score beta, Depth depth, Ply ply);

void complete_shadow_candidate(SearchContext* context, const ShadowCandidate& candidate,
                               const SearchNodeResult& deep_result);

} // namespace vibe_othello::search::internal
