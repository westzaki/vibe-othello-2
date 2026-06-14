#ifndef VIBE_OTHELLO_TEST_SUPPORT_SEARCH_REFERENCE_SEARCH_H_
#define VIBE_OTHELLO_TEST_SUPPORT_SEARCH_REFERENCE_SEARCH_H_

#include "vibe_othello/search/search.h"

namespace vibe_othello::search::test_support {

SearchResult reference_negamax_fixed_depth(board_core::Position position,
                                           const Evaluator& evaluator, Depth depth);

} // namespace vibe_othello::search::test_support

#endif // VIBE_OTHELLO_TEST_SUPPORT_SEARCH_REFERENCE_SEARCH_H_
