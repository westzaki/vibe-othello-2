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
  using IncrementalState = PatternEvaluator::IncrementalState;

  PhaseAwareEvaluator(PatternWeights weights, PatternFeatureSet feature_set,
                      std::optional<std::vector<std::uint8_t>> trained_phases,
                      std::optional<std::uint8_t> fallback_additive_through_phase = std::nullopt);

  search::Score evaluate(const board_core::Position& position) const noexcept override;
  [[nodiscard]] search::Score
  evaluate_reference(const board_core::Position& position) const noexcept;
  [[nodiscard]] IncrementalState make_incremental_state(const board_core::Position& position) const;
  [[nodiscard]] bool uses_learned_patterns(const board_core::Position& position,
                                           int max_normal_moves = 0) const noexcept;
  [[nodiscard]] search::Score
  evaluate_incremental(const IncrementalState& state,
                       const board_core::Position& position) const noexcept;

private:
  static std::array<bool, PatternWeights::kDiscCountEntries>
  learned_by_disc_count(const PatternWeights& weights,
                        const std::optional<std::vector<std::uint8_t>>& trained_phases);
  static std::array<bool, PatternWeights::kDiscCountEntries> fallback_additive_by_disc_count(
      const PatternWeights& weights,
      const std::array<bool, PatternWeights::kDiscCountEntries>& learned_by_disc_count,
      std::optional<std::uint8_t> fallback_additive_through_phase);
  [[nodiscard]] search::Score finish_routed_score(search::Score learned_score,
                                                  const board_core::Position& position,
                                                  int discs) const noexcept;

  std::array<bool, PatternWeights::kDiscCountEntries> learned_by_disc_count_{};
  std::array<bool, PatternWeights::kDiscCountEntries> fallback_additive_by_disc_count_{};
  PatternEvaluator learned_;
  EarlyMidgameHeuristicEvaluator fallback_;
};

} // namespace vibe_othello::evaluation

#endif // VIBE_OTHELLO_EVALUATION_PHASE_AWARE_EVALUATOR_H_
