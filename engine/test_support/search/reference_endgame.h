#ifndef VIBE_OTHELLO_TEST_SUPPORT_SEARCH_REFERENCE_ENDGAME_H_
#define VIBE_OTHELLO_TEST_SUPPORT_SEARCH_REFERENCE_ENDGAME_H_

#include "vibe_othello/search/search.h"

namespace vibe_othello::search::test_support {

SearchResult reference_exact_endgame(board_core::Position position);

} // namespace vibe_othello::search::test_support

#endif // VIBE_OTHELLO_TEST_SUPPORT_SEARCH_REFERENCE_ENDGAME_H_
