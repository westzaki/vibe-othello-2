#ifndef VIBE_OTHELLO_SEARCH_RESULT_H_
#define VIBE_OTHELLO_SEARCH_RESULT_H_

#include "vibe_othello/search/score.h"
#include "vibe_othello/search/shadow_calibration.h"

#include <chrono>
#include <optional>
#include <vector>

namespace vibe_othello::search {

struct ProbCutDepthPairStats {
  std::uint8_t phase = 0;
  Depth deep_depth = 0;
  Depth shallow_depth = 0;
  NodeCount attempts = 0;
  NodeCount shallow_nodes = 0;
  NodeCount successes = 0;
  NodeCount confidence_rejections = 0;
  NodeCount unsupported_profile = 0;
  NodeCount near_exact_rejections = 0;
  NodeCount pass_rejections = 0;
  NodeCount pv_rejections = 0;
  NodeCount root_rejections = 0;
  NodeCount beta_cuts = 0;
  NodeCount cut_low_attempts = 0;
  NodeCount shadow_candidates = 0;
  NodeCount shadow_verifications = 0;
  NodeCount shadow_false_cuts = 0;

  friend bool operator==(const ProbCutDepthPairStats&, const ProbCutDepthPairStats&) = default;
};

struct SearchStats {
  NodeCount nodes = 0;
  NodeCount leaf_nodes = 0;
  NodeCount eval_calls = 0;
  bool incremental_eval_enabled = false;
  NodeCount incremental_state_initializations = 0;
  NodeCount incremental_eval_calls = 0;
  NodeCount stateless_eval_calls = 0;
  NodeCount incremental_updates = 0;
  NodeCount terminal_nodes = 0;
  NodeCount pass_nodes = 0;
  NodeCount beta_cutoffs = 0;
  NodeCount alpha_updates = 0;
  NodeCount root_moves_searched = 0;
  NodeCount tt_probes = 0;
  NodeCount tt_hits = 0;
  NodeCount tt_stores = 0;
  NodeCount tt_cutoffs = 0;
  NodeCount tt_replacements = 0;
  NodeCount tt_bucket_conflicts = 0;
  NodeCount tt_same_key_updates = 0;
  NodeCount tt_probe_slots = 0;
  NodeCount tt_generation_age_hits = 0;
  NodeCount tt_rejected_stores = 0;
  NodeCount tt_invalid_best_move_stores = 0;
  NodeCount pvs_researches = 0;
  NodeCount aspiration_fail_lows = 0;
  NodeCount aspiration_fail_highs = 0;
  NodeCount iid_searches = 0;
  NodeCount endgame_nodes = 0;
  NodeCount endgame_last_flip_solved = 0;
  NodeCount endgame_stability_probes = 0;
  NodeCount endgame_stability_lower_candidates = 0;
  NodeCount endgame_stability_upper_candidates = 0;
  NodeCount endgame_stability_cutoffs = 0;
  NodeCount endgame_stability_shadow_verifications = 0;
  NodeCount endgame_stability_shadow_false_cutoffs = 0;
  NodeCount selective_cuts = 0;
  NodeCount probcut_attempts = 0;
  NodeCount probcut_shallow_nodes = 0;
  NodeCount probcut_successes = 0;
  NodeCount probcut_unsupported_profile = 0;
  NodeCount probcut_rejected_by_phase = 0;
  NodeCount probcut_rejected_by_depth = 0;
  NodeCount probcut_rejected_near_exact = 0;
  NodeCount probcut_rejected_pass = 0;
  NodeCount probcut_rejected_pv = 0;
  NodeCount probcut_rejected_root = 0;
  NodeCount probcut_rejected_overhead = 0;
  NodeCount probcut_probe_limit_reached = 0;
  NodeCount probcut_rejected_confidence = 0;
  NodeCount probcut_beta_cutoffs = 0;
  NodeCount probcut_cut_low_attempts = 0;
  NodeCount probcut_shadow_candidates = 0;
  NodeCount probcut_shadow_verifications = 0;
  NodeCount probcut_shadow_false_cuts = 0;
  NodeCount probcut_estimated_saved_nodes = 0;
  bool probcut_estimated_saved_nodes_available = false;
  std::vector<ProbCutDepthPairStats> probcut_by_phase_depth_pair;

  friend bool operator==(const SearchStats&, const SearchStats&) = default;
};

struct RootMoveInfo {
  board_core::Move move = board_core::make_pass();
  Score score = 0;
  ScoreKind score_kind = ScoreKind::heuristic;
  BoundType bound = BoundType::exact;
  Depth depth = 0;
  NodeCount nodes = 0;
  Line pv{};
  bool exact = false;
  bool selective = false;

  friend bool operator==(const RootMoveInfo&, const RootMoveInfo&) = default;
};

struct SearchResult {
  std::optional<board_core::Move> best_move;
  Score score = 0;
  ScoreKind score_kind = ScoreKind::heuristic;
  BoundType bound = BoundType::exact;
  Depth completed_depth = 0;
  NodeCount nodes = 0;
  SearchStats stats{};
  ShadowCalibrationStats shadow_calibration{};
  std::chrono::milliseconds elapsed{0};
  Line pv{};
  std::vector<RootMoveInfo> root_moves;
  bool exact = false;
  bool stopped = false;
};

} // namespace vibe_othello::search

#endif // VIBE_OTHELLO_SEARCH_RESULT_H_
