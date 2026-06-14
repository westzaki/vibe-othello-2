#ifndef VIBE_OTHELLO_EVALUATION_TINY_PATTERN_EVALUATOR_H_
#define VIBE_OTHELLO_EVALUATION_TINY_PATTERN_EVALUATOR_H_

#include "vibe_othello/evaluation/pattern_weights.h"
#include "vibe_othello/search/evaluator.h"

namespace vibe_othello::evaluation {

class TinyPatternEvaluator final : public search::Evaluator {
public:
  explicit TinyPatternEvaluator(PatternWeights weights);

  search::Score evaluate(const board_core::Position& position) const noexcept override;

private:
  PatternWeights weights_;
};

} // namespace vibe_othello::evaluation

#endif // VIBE_OTHELLO_EVALUATION_TINY_PATTERN_EVALUATOR_H_
