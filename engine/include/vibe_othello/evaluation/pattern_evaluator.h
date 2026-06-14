#ifndef VIBE_OTHELLO_EVALUATION_PATTERN_EVALUATOR_H_
#define VIBE_OTHELLO_EVALUATION_PATTERN_EVALUATOR_H_

#include "vibe_othello/evaluation/pattern_feature_set.h"
#include "vibe_othello/evaluation/pattern_weights.h"
#include "vibe_othello/search/evaluator.h"

namespace vibe_othello::evaluation {

class PatternEvaluator final : public search::Evaluator {
public:
  PatternEvaluator(PatternWeights weights, PatternFeatureSet feature_set);

  search::Score evaluate(const board_core::Position& position) const noexcept override;

private:
  PatternWeights weights_;
  PatternFeatureSet feature_set_;
};

} // namespace vibe_othello::evaluation

#endif // VIBE_OTHELLO_EVALUATION_PATTERN_EVALUATOR_H_
