#ifndef VIBE_OTHELLO_SEARCH_RESULT_H_
#define VIBE_OTHELLO_SEARCH_RESULT_H_

#include "vibe_othello/search/score.h"
#include "vibe_othello/search/shadow_calibration.h"

#include <chrono>
#include <optional>
#include <vector>

namespace vibe_othello::search {

struct SearchStats {
  NodeCount nodes = 0;
  NodeCount leaf_nodes = 0;
  NodeCount eval_calls = 0;
  bool incremental_eval_enabled = false;
  NodeCount incremental_state_initializations = 0;
  NodeCount incremental_eval_calls = 0;
  NodeCount stateless_eval_calls = 0;
  NodeCount incremental_updates = 0;
  NodeCount incremental_touched_instances = 0;
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
  NodeCount selective_cuts = 0;

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
