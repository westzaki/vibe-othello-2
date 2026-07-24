#pragma once

#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/hash.h"
#include "vibe_othello/evaluation/pattern_evaluator.h"
#include "vibe_othello/evaluation/phase_aware_evaluator.h"
#include "vibe_othello/search/evaluator.h"
#include "vibe_othello/search/result.h"

#include <optional>

namespace vibe_othello::search::internal {

struct SearchPositionState {
  board_core::Position position{};
  board_core::PositionHash key = 0;
  board_core::Bitboard legal_mask = 0;
  bool legal_mask_valid = false;
  const evaluation::PatternEvaluator* pattern_evaluator = nullptr;
  const evaluation::PhaseAwareEvaluator* phase_aware_evaluator = nullptr;
  std::optional<evaluation::PatternEvaluator::IncrementalState> evaluation_state;
};

struct SearchPositionUndo {
  board_core::PositionHash key = 0;
  board_core::Bitboard legal_mask = 0;
  bool legal_mask_valid = false;
};

inline SearchPositionState make_search_position(board_core::Position position,
                                                const Evaluator* evaluator = nullptr,
                                                int max_normal_moves = 0) {
  SearchPositionState state{
      .position = position,
      .key = board_core::hash_position(position),
  };
  if (evaluator == nullptr) {
    return state;
  }
  state.phase_aware_evaluator = dynamic_cast<const evaluation::PhaseAwareEvaluator*>(evaluator);
  if (state.phase_aware_evaluator != nullptr) {
    if (!state.phase_aware_evaluator->uses_learned_patterns(position, max_normal_moves)) {
      state.phase_aware_evaluator = nullptr;
      return state;
    }
    state.evaluation_state.emplace(state.phase_aware_evaluator->make_incremental_state(position));
    return state;
  }
  state.pattern_evaluator = dynamic_cast<const evaluation::PatternEvaluator*>(evaluator);
  if (state.pattern_evaluator != nullptr) {
    state.evaluation_state.emplace(state.pattern_evaluator->make_incremental_state(position));
  }
  return state;
}

inline Score evaluate_position(const SearchPositionState& state,
                               const Evaluator& evaluator) noexcept {
  if (state.phase_aware_evaluator != nullptr) {
    return state.phase_aware_evaluator->evaluate_incremental(*state.evaluation_state,
                                                             state.position);
  }
  if (state.pattern_evaluator != nullptr) {
    return state.evaluation_state->evaluate();
  }
  return evaluator.evaluate(state.position);
}

inline bool uses_incremental_evaluation(const SearchPositionState& state) noexcept {
  if (state.pattern_evaluator != nullptr) {
    return true;
  }
  return state.phase_aware_evaluator != nullptr &&
         state.phase_aware_evaluator->uses_learned_patterns(state.position, 0);
}

inline Score evaluate_position_reference(const SearchPositionState& state,
                                         const Evaluator& evaluator) noexcept {
  if (state.phase_aware_evaluator != nullptr) {
    return state.phase_aware_evaluator->evaluate_reference(state.position);
  }
  if (state.pattern_evaluator != nullptr) {
    return state.pattern_evaluator->evaluate_reference(state.position);
  }
  return evaluator.evaluate(state.position);
}

inline board_core::Bitboard legal_moves(SearchPositionState* state) noexcept {
  if (!state->legal_mask_valid) {
    state->legal_mask = board_core::legal_moves(state->position);
    state->legal_mask_valid = true;
  }
  return state->legal_mask;
}

inline board_core::Bitboard opponent_legal_moves(const SearchPositionState& state) noexcept {
  return board_core::legal_moves(board_core::Position{
      .player = state.position.opponent,
      .opponent = state.position.player,
      .side_to_move = board_core::opposite(state.position.side_to_move),
  });
}

inline void apply_move(SearchPositionState* state, board_core::MoveDelta delta,
                       SearchPositionUndo* undo, SearchStats* stats = nullptr) noexcept {
  *undo = SearchPositionUndo{
      .key = state->key,
      .legal_mask = state->legal_mask,
      .legal_mask_valid = state->legal_mask_valid,
  };
  if (state->evaluation_state.has_value()) {
    state->evaluation_state->apply_move(delta);
    if (stats != nullptr) {
      ++stats->incremental_updates;
    }
  }
  state->key = board_core::hash_after_move(state->position, state->key, delta);
  board_core::apply_move_delta(&state->position, delta);
  state->legal_mask = 0;
  state->legal_mask_valid = false;
}

inline void undo_move(SearchPositionState* state, board_core::MoveDelta delta,
                      SearchPositionUndo undo, SearchStats* stats = nullptr) noexcept {
  board_core::undo_move(&state->position, delta);
  if (state->evaluation_state.has_value()) {
    state->evaluation_state->undo_move(delta);
    if (stats != nullptr) {
      ++stats->incremental_updates;
    }
  }
  state->key = undo.key;
  state->legal_mask = undo.legal_mask;
  state->legal_mask_valid = undo.legal_mask_valid;
}

} // namespace vibe_othello::search::internal
