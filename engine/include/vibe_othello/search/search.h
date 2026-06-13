#ifndef VIBE_OTHELLO_SEARCH_SEARCH_H_
#define VIBE_OTHELLO_SEARCH_SEARCH_H_

#include "vibe_othello/board_core/position.h"
#include "vibe_othello/search/evaluator.h"
#include "vibe_othello/search/result.h"
#include "vibe_othello/search/score.h"

namespace vibe_othello::search {

struct SearchLimits {
  Depth max_depth = 0;
};

struct SearchOptions {
  bool use_tt_best_move_ordering = false;
};

SearchResult search_fixed_depth(board_core::Position position, const Evaluator& evaluator,
                                Depth depth);
SearchResult search_iterative(board_core::Position position, const Evaluator& evaluator,
                              SearchLimits limits);
SearchResult search_iterative(board_core::Position position, const Evaluator& evaluator,
                              SearchLimits limits, SearchOptions options);

} // namespace vibe_othello::search

#endif // VIBE_OTHELLO_SEARCH_SEARCH_H_
