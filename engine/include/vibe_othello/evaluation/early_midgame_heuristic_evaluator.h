#ifndef VIBE_OTHELLO_EVALUATION_EARLY_MIDGAME_HEURISTIC_EVALUATOR_H_
#define VIBE_OTHELLO_EVALUATION_EARLY_MIDGAME_HEURISTIC_EVALUATOR_H_

#include "vibe_othello/search/evaluator.h"

namespace vibe_othello::evaluation {

// A small deterministic fallback for phases without reviewed learned coverage.
// It is intentionally a baseline heuristic, not a production-strength claim.
class EarlyMidgameHeuristicEvaluator final : public search::Evaluator {
public:
  search::Score evaluate(const board_core::Position& position) const noexcept override;
};

} // namespace vibe_othello::evaluation

#endif // VIBE_OTHELLO_EVALUATION_EARLY_MIDGAME_HEURISTIC_EVALUATOR_H_
