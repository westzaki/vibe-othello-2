#ifndef VIBE_OTHELLO_EVALUATION_PHASE_AWARE_EVALUATOR_H_
#define VIBE_OTHELLO_EVALUATION_PHASE_AWARE_EVALUATOR_H_

#include "vibe_othello/evaluation/early_midgame_heuristic_evaluator.h"
#include "vibe_othello/evaluation/pattern_evaluator.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace vibe_othello::evaluation {

class PhaseAwareEvaluator final : public search::Evaluator {
public:
  PhaseAwareEvaluator(PatternWeights weights, PatternFeatureSet feature_set,
                      std::optional<std::vector<std::uint8_t>> trained_phases);

  search::Score evaluate(const board_core::Position& position) const noexcept override;

private:
  static std::array<bool, PatternWeights::kDiscCountEntries>
  learned_by_disc_count(const PatternWeights& weights,
                        const std::optional<std::vector<std::uint8_t>>& trained_phases);

  std::array<bool, PatternWeights::kDiscCountEntries> learned_by_disc_count_{};
  PatternEvaluator learned_;
  EarlyMidgameHeuristicEvaluator fallback_;
};

} // namespace vibe_othello::evaluation

#endif // VIBE_OTHELLO_EVALUATION_PHASE_AWARE_EVALUATOR_H_
