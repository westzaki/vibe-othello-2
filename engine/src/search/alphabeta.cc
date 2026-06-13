#include "search_internal.h"

namespace vibe_othello::search::internal {

SearchValue alphabeta(SearchContext* context, Score alpha, Score beta, Depth depth, Ply ply) {
  require_invariant(alpha < beta);
  require_invariant(ply < kMaxPly);
  StackFrame& frame = context->stack[ply];
  frame = StackFrame{};
  frame.key = board_core::hash_position(context->position);

  ++context->stats.nodes;

  if (board_core::is_terminal(context->position)) {
    ++context->stats.terminal_nodes;
    return SearchValue{
        .score = terminal_score(context->position),
        .pv = {},
    };
  }

  if (depth <= 0) {
    ++context->stats.leaf_nodes;
    ++context->stats.eval_calls;
    const Score score = context->evaluator.evaluate(context->position);
    require_invariant(is_valid_evaluator_score(score));
    return SearchValue{
        .score = score,
        .pv = {},
    };
  }

  const MoveOrderingHints hints{
      .first_move = context->best_move_table == nullptr
                        ? std::nullopt
                        : context->best_move_table->probe(context->position, &context->stats),
  };
  frame.moves = ordered_moves(context->position, hints);
  if (frame.moves.size == 0) {
    ++context->stats.pass_nodes;
    frame.current_move = board_core::make_pass();
    const bool made_delta =
        board_core::make_move_delta(context->position, frame.current_move, &frame.delta);
    require_invariant(made_delta);
    board_core::apply_move_delta(&context->position, frame.delta);

    const SearchValue child = alphabeta(context, static_cast<Score>(-beta),
                                        static_cast<Score>(-alpha),
                                        static_cast<Depth>(depth - 1),
                                        static_cast<Ply>(ply + 1));
    board_core::undo_move(&context->position, frame.delta);

    SearchValue result{
        .score = static_cast<Score>(-child.score),
        .pv = {},
    };
    prepend_move(board_core::make_pass(), child.pv, &result.pv);
    frame.pv = result.pv;
    return result;
  }

  SearchValue best{
      .score = kScoreLoss,
      .pv = {},
  };
  std::optional<board_core::Move> best_move;

  for (std::uint8_t move_index = 0; move_index < frame.moves.size; ++move_index) {
    const board_core::Move move = frame.moves.moves[move_index];
    frame.current_move = move;
    const bool made_delta = board_core::make_move_delta(context->position, move, &frame.delta);
    require_invariant(made_delta);
    board_core::apply_move_delta(&context->position, frame.delta);

    const SearchValue child = alphabeta(context, static_cast<Score>(-beta),
                                        static_cast<Score>(-alpha),
                                        static_cast<Depth>(depth - 1),
                                        static_cast<Ply>(ply + 1));
    board_core::undo_move(&context->position, frame.delta);

    const Score score = static_cast<Score>(-child.score);
    if (!best_move.has_value() || score > best.score ||
        (score == best.score && move.square.index < best_move->square.index)) {
      best.score = score;
      best_move = move;
      prepend_move(move, child.pv, &best.pv);
      frame.pv = best.pv;
    }

    if (score > alpha) {
      ++context->stats.alpha_updates;
      alpha = score;
    }
    if (alpha >= beta) {
      ++context->stats.beta_cutoffs;
      break;
    }
  }

  if (context->best_move_table != nullptr && best_move.has_value()) {
    context->best_move_table->store(context->position, *best_move, &context->stats);
  }

  return best;
}

} // namespace vibe_othello::search::internal
