#pragma once

#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/hash.h"

namespace vibe_othello::search::internal {

struct SearchPositionState {
  board_core::Position position{};
  board_core::PositionHash key = 0;
  board_core::Bitboard legal_mask = 0;
  bool legal_mask_valid = false;
};

struct SearchPositionUndo {
  board_core::PositionHash key = 0;
  board_core::Bitboard legal_mask = 0;
  bool legal_mask_valid = false;
};

inline SearchPositionState make_search_position(board_core::Position position) noexcept {
  return SearchPositionState{
      .position = position,
      .key = board_core::hash_position(position),
  };
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
                       SearchPositionUndo* undo) noexcept {
  *undo = SearchPositionUndo{
      .key = state->key,
      .legal_mask = state->legal_mask,
      .legal_mask_valid = state->legal_mask_valid,
  };
  state->key = board_core::hash_after_move(state->position, state->key, delta);
  board_core::apply_move_delta(&state->position, delta);
  state->legal_mask = 0;
  state->legal_mask_valid = false;
}

inline void undo_move(SearchPositionState* state, board_core::MoveDelta delta,
                      SearchPositionUndo undo) noexcept {
  board_core::undo_move(&state->position, delta);
  state->key = undo.key;
  state->legal_mask = undo.legal_mask;
  state->legal_mask_valid = undo.legal_mask_valid;
}

} // namespace vibe_othello::search::internal
