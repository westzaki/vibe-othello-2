#ifndef VIBE_OTHELLO_SEARCH_EVALUATOR_H_
#define VIBE_OTHELLO_SEARCH_EVALUATOR_H_

#include "vibe_othello/board_core/position.h"
#include "vibe_othello/search/score.h"

#include <cstdint>

namespace vibe_othello::search {

class Evaluator {
public:
  virtual ~Evaluator() = default;

  // Returned scores must be strictly inside the search sentinel range:
  // kScoreLoss < score < kScoreWin.
  virtual Score evaluate(const board_core::Position& position) const noexcept = 0;

  // Increment this value when a live evaluator object's scoring semantics are
  // changed in place. SearchSession also binds the evaluator object's address,
  // so immutable evaluators can keep the default revision.
  virtual std::uint64_t transposition_table_revision() const noexcept {
    return 0;
  }
};

} // namespace vibe_othello::search

#endif // VIBE_OTHELLO_SEARCH_EVALUATOR_H_
