#pragma once

#include "search_node_internal.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/search/result.h"

#include <optional>

namespace vibe_othello::search::internal {

Score terminal_score(board_core::Position position) noexcept;
bool is_valid_evaluator_score(Score score) noexcept;
void require_invariant(bool condition) noexcept;
void prepend_move(board_core::Move move, const Line& child, Line* line) noexcept;
void add_stats(SearchStats* total, const SearchStats& delta);
BoundType classify_bound(Score score, Score original_alpha, Score original_beta) noexcept;
bool is_better_root_move(Score score, board_core::Move move, Score best_score,
                         std::optional<board_core::Move> best_move) noexcept;

template <typename StackFrameT>
void update_best_line_and_move(Score child_score, const Line& child_pv, board_core::Move move,
                               Score* best_score, std::optional<board_core::Move>* best_move,
                               StackFrameT* frame) {
  if (!best_move->has_value() || child_score > *best_score ||
      (child_score == *best_score && move.square.index < (*best_move)->square.index)) {
    *best_score = child_score;
    *best_move = move;
    prepend_move(move, child_pv, &frame->pv);
  }
}

} // namespace vibe_othello::search::internal
