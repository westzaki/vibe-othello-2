#ifndef VIBE_OTHELLO_SEARCH_SEARCH_H_
#define VIBE_OTHELLO_SEARCH_SEARCH_H_

#include "vibe_othello/board_core/position.h"
#include "vibe_othello/search/evaluator.h"
#include "vibe_othello/search/result.h"
#include "vibe_othello/search/score.h"

namespace vibe_othello::search {

SearchResult search_fixed_depth(board_core::Position position, const Evaluator& evaluator,
                                Depth depth);

} // namespace vibe_othello::search

#endif // VIBE_OTHELLO_SEARCH_SEARCH_H_
