#include "vibe_othello/evaluation/phase_aware_evaluator.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
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

std::array<bool, PatternWeights::kDiscCountEntries>
PhaseAwareEvaluator::fallback_additive_by_disc_count(
    const PatternWeights& weights,
    const std::array<bool, PatternWeights::kDiscCountEntries>& learned_by_disc_count,
    std::optional<std::uint8_t> fallback_additive_through_phase) {
  if (fallback_additive_through_phase.has_value() &&
      *fallback_additive_through_phase >= weights.phase_count()) {
    throw std::invalid_argument("fallback additive phase is out of range");
  }

  std::array<bool, PatternWeights::kDiscCountEntries> result{};
  if (!fallback_additive_through_phase.has_value()) {
    return result;
  }
  for (std::uint8_t disc_count = 0; disc_count < result.size(); ++disc_count) {
    result[disc_count] =
        learned_by_disc_count[disc_count] &&
        weights.phase_for_disc_count(disc_count) <= *fallback_additive_through_phase;
  }
  return result;
}

PhaseAwareEvaluator::PhaseAwareEvaluator(
    PatternWeights weights, PatternFeatureSet feature_set,
    std::optional<std::vector<std::uint8_t>> trained_phases,
    std::optional<std::uint8_t> fallback_additive_through_phase)
    : learned_by_disc_count_(learned_by_disc_count(weights, trained_phases)),
      fallback_additive_by_disc_count_(fallback_additive_by_disc_count(
          weights, learned_by_disc_count_, fallback_additive_through_phase)),
      learned_(std::move(weights), std::move(feature_set)) {}

search::Score PhaseAwareEvaluator::finish_routed_score(search::Score learned_score,
                                                       const board_core::Position& position,
                                                       int discs) const noexcept {
  if (!fallback_additive_by_disc_count_[static_cast<std::size_t>(discs)]) {
    return learned_score;
  }
  const std::int64_t combined =
      static_cast<std::int64_t>(fallback_.evaluate(position)) + learned_score;
  return static_cast<search::Score>(std::clamp(combined,
                                               static_cast<std::int64_t>(search::kScoreLoss + 1),
                                               static_cast<std::int64_t>(search::kScoreWin - 1)));
}

search::Score PhaseAwareEvaluator::evaluate(const board_core::Position& position) const noexcept {
  const int discs = std::popcount(board_core::occupied(position));
  if (!learned_by_disc_count_[static_cast<std::size_t>(discs)]) {
    return fallback_.evaluate(position);
  }
  return finish_routed_score(learned_.evaluate(position), position, discs);
}

search::Score
PhaseAwareEvaluator::evaluate_reference(const board_core::Position& position) const noexcept {
  const int discs = std::popcount(board_core::occupied(position));
  if (!learned_by_disc_count_[static_cast<std::size_t>(discs)]) {
    return fallback_.evaluate(position);
  }
  return finish_routed_score(learned_.evaluate_reference(position), position, discs);
}

PhaseAwareEvaluator::IncrementalState
PhaseAwareEvaluator::make_incremental_state(const board_core::Position& position) const {
  return learned_.make_incremental_state(position);
}

bool PhaseAwareEvaluator::uses_learned_patterns(const board_core::Position& position,
                                                int max_normal_moves) const noexcept {
  const int discs = std::popcount(board_core::occupied(position));
  const int last_disc_count =
      std::min(board_core::kSquareCount, discs + std::max(0, max_normal_moves));
  for (int disc_count = discs; disc_count <= last_disc_count; ++disc_count) {
    if (learned_by_disc_count_[static_cast<std::size_t>(disc_count)]) {
      return true;
    }
  }
  return false;
}

search::Score
PhaseAwareEvaluator::evaluate_incremental(const IncrementalState& state,
                                          const board_core::Position& position) const noexcept {
  const int discs = state.occupied_count();
  assert(discs == std::popcount(board_core::occupied(position)));
  assert(state.side_to_move() == position.side_to_move);
  if (!learned_by_disc_count_[static_cast<std::size_t>(discs)]) {
    return fallback_.evaluate(position);
  }
  return finish_routed_score(state.evaluate(), position, discs);
}

} // namespace vibe_othello::evaluation
