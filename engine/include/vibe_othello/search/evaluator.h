#ifndef VIBE_OTHELLO_SEARCH_EVALUATOR_H_
#define VIBE_OTHELLO_SEARCH_EVALUATOR_H_

#include "vibe_othello/board_core/position.h"
#include "vibe_othello/search/score.h"

namespace vibe_othello::search {

class Evaluator {
public:
  virtual ~Evaluator() = default;

  // Returned scores must be strictly inside the search sentinel range:
  // kScoreLoss < score < kScoreWin.
  virtual Score evaluate(const board_core::Position& position) const noexcept = 0;
};

} // namespace vibe_othello::search

#endif // VIBE_OTHELLO_SEARCH_EVALUATOR_H_
