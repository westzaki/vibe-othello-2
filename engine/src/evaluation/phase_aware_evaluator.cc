#include "vibe_othello/evaluation/phase_aware_evaluator.h"

#include <array>
#include <bit>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vibe_othello::evaluation {

std::array<bool, PatternWeights::kDiscCountEntries> PhaseAwareEvaluator::learned_by_disc_count(
    const PatternWeights& weights, const std::optional<std::vector<std::uint8_t>>& trained_phases) {
  if (weights.phase_count() == 0) {
    throw std::invalid_argument("phase-aware evaluator requires at least one pattern phase");
  }

  std::array<bool, std::numeric_limits<std::uint8_t>::max() + 1> learned_by_phase{};
  if (!trained_phases.has_value()) {
    // A missing field is legacy/unreported coverage. Preserve the historic
    // all-phase learned routing without treating it as a coverage claim.
    learned_by_phase.fill(true);
  } else {
    if (trained_phases->empty()) {
      throw std::invalid_argument("phase-aware evaluator requires non-empty trained phases");
    }
    for (const std::uint8_t phase : *trained_phases) {
      if (phase >= weights.phase_count()) {
        throw std::invalid_argument("phase-aware evaluator trained phase is out of range");
      }
      if (learned_by_phase[phase]) {
        throw std::invalid_argument("phase-aware evaluator trained phases must be unique");
      }
      learned_by_phase[phase] = true;
    }
  }

  std::array<bool, PatternWeights::kDiscCountEntries> result{};
  for (std::uint8_t disc_count = 0; disc_count < result.size(); ++disc_count) {
    const std::uint8_t phase = weights.phase_for_disc_count(disc_count);
    if (phase >= weights.phase_count()) {
      throw std::invalid_argument("phase-aware evaluator weights use an invalid phase mapping");
    }
    result[disc_count] = learned_by_phase[phase];
  }
  return result;
}

PhaseAwareEvaluator::PhaseAwareEvaluator(PatternWeights weights, PatternFeatureSet feature_set,
                                         std::optional<std::vector<std::uint8_t>> trained_phases)
    : learned_by_disc_count_(learned_by_disc_count(weights, trained_phases)),
      learned_(std::move(weights), std::move(feature_set)) {}

search::Score PhaseAwareEvaluator::evaluate(const board_core::Position& position) const noexcept {
  const int discs = std::popcount(board_core::occupied(position));
  if (learned_by_disc_count_[static_cast<std::size_t>(discs)]) {
    return learned_.evaluate(position);
  }
  return fallback_.evaluate(position);
}

} // namespace vibe_othello::evaluation
