#ifndef VIBE_OTHELLO_SEARCH_SEARCH_H_
#define VIBE_OTHELLO_SEARCH_SEARCH_H_

#include "vibe_othello/board_core/position.h"
#include "vibe_othello/search/evaluator.h"
#include "vibe_othello/search/result.h"
#include "vibe_othello/search/score.h"

#include <atomic>
#include <chrono>
#include <cstdint>

namespace vibe_othello::search {

struct SearchLimits {
  Depth max_depth = 0;
  NodeCount max_nodes = 0;
  std::chrono::milliseconds max_time{0};
  bool infinite = false;
  const std::atomic_bool* stop_requested = nullptr;
};

struct SearchOptions {
  bool use_pvs = false;
  bool use_aspiration = false;
  bool use_iid = false;
  bool use_history = false;
  bool use_killers = false;
  bool use_midgame_tt = false;
  bool use_endgame_tt = false;
  bool exact_endgame = false;
  bool probcut = false;
  bool use_pv_table = false;
  bool use_parallel = false;
  bool use_tt_best_move_ordering = false;
  bool use_endgame_parity_ordering = true;
  std::uint8_t multi_pv = 0;
  std::uint8_t endgame_exact_empties = 0;
  std::uint8_t endgame_wld_empties = 0;
  std::uint8_t selectivity_level = 0;
};

SearchResult search_fixed_depth(board_core::Position position, const Evaluator& evaluator,
                                Depth depth);
SearchResult search_iterative(board_core::Position position, const Evaluator& evaluator,
                              SearchLimits limits);
SearchResult search_iterative(board_core::Position position, const Evaluator& evaluator,
                              SearchLimits limits, SearchOptions options);

} // namespace vibe_othello::search

#endif // VIBE_OTHELLO_SEARCH_SEARCH_H_
