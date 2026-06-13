#include "search_internal.h"

namespace vibe_othello::search::internal {

SearchValue alphabeta(board_core::Position* position, const Evaluator& evaluator, Score alpha,
                      Score beta, Depth depth, SearchStats* stats, TTBestMoveTable* tt) {
  require_invariant(alpha < beta);
  ++stats->nodes;

  if (board_core::is_terminal(*position)) {
    ++stats->terminal_nodes;
    return SearchValue{
        .score = terminal_score(*position),
        .pv = {},
    };
  }

  if (depth <= 0) {
    ++stats->leaf_nodes;
    ++stats->eval_calls;
    const Score score = evaluator.evaluate(*position);
    require_invariant(is_valid_evaluator_score(score));
    return SearchValue{
        .score = score,
        .pv = {},
    };
  }

  const MoveOrderingHints hints{
      .first_move = tt == nullptr ? std::nullopt : tt->probe(*position, stats),
  };
  const MoveList moves = ordered_moves(*position, hints);
  if (moves.size == 0) {
    ++stats->pass_nodes;
    board_core::MoveDelta delta{};
    const bool made_delta = board_core::make_move_delta(*position, board_core::make_pass(), &delta);
    require_invariant(made_delta);
    const bool applied_delta = made_delta && board_core::apply_move_delta(position, delta);
    require_invariant(applied_delta);

    const SearchValue child =
        alphabeta(position, evaluator, static_cast<Score>(-beta), static_cast<Score>(-alpha),
                  static_cast<Depth>(depth - 1), stats, tt);
    board_core::undo_move(position, delta);

    SearchValue result{
        .score = static_cast<Score>(-child.score),
        .pv = {},
    };
    prepend_move(board_core::make_pass(), child.pv, &result.pv);
    return result;
  }

  SearchValue best{
      .score = kScoreLoss,
      .pv = {},
  };
  std::optional<board_core::Move> best_move;

  for (std::uint8_t move_index = 0; move_index < moves.size; ++move_index) {
    const board_core::Move move = moves.moves[move_index];
    board_core::MoveDelta delta{};
    const bool made_delta = board_core::make_move_delta(*position, move, &delta);
    require_invariant(made_delta);
    const bool applied_delta = made_delta && board_core::apply_move_delta(position, delta);
    require_invariant(applied_delta);

    const SearchValue child =
        alphabeta(position, evaluator, static_cast<Score>(-beta), static_cast<Score>(-alpha),
                  static_cast<Depth>(depth - 1), stats, tt);
    board_core::undo_move(position, delta);

    const Score score = static_cast<Score>(-child.score);
    if (!best_move.has_value() || score > best.score ||
        (score == best.score && move.square.index < best_move->square.index)) {
      best.score = score;
      best_move = move;
      prepend_move(move, child.pv, &best.pv);
    }

    if (score > alpha) {
      ++stats->alpha_updates;
      alpha = score;
    }
    if (alpha >= beta) {
      ++stats->beta_cutoffs;
      break;
    }
  }

  if (tt != nullptr && best_move.has_value()) {
    tt->store(*position, *best_move, stats);
  }

  return best;
}

} // namespace vibe_othello::search::internal
