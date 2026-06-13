#include "search_internal.h"

namespace vibe_othello::search::internal {

std::optional<Score> tt_cutoff_score(const TTEntry& entry, Depth depth, Score alpha,
                                     Score beta) noexcept {
  if (entry.kind != TTEntryKind::midgame || entry.depth < depth) {
    return std::nullopt;
  }
  if (entry.bound == BoundType::exact) {
    return entry.score;
  }
  if (entry.bound == BoundType::lower && entry.score >= beta) {
    return entry.score;
  }
  if (entry.bound == BoundType::upper && entry.score <= alpha) {
    return entry.score;
  }
  return std::nullopt;
}

SearchValue alphabeta(SearchContext* context, Score alpha, Score beta, Depth depth, Ply ply) {
  require_invariant(alpha < beta);
  require_invariant(ply < kMaxPly);
  const Score original_alpha = alpha;
  const Score original_beta = beta;
  StackFrame& frame = context->stack[ply];
  frame = StackFrame{};

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

  const std::optional<TTEntry> tt_entry =
      context->transposition_table == nullptr
          ? std::nullopt
          : context->transposition_table->probe(context->position, &context->stats);
  if (context->options.use_midgame_tt && tt_entry.has_value()) {
    const std::optional<Score> cutoff = tt_cutoff_score(*tt_entry, depth, alpha, beta);
    if (cutoff.has_value()) {
      ++context->stats.tt_cutoffs;
      return SearchValue{
          .score = *cutoff,
          .pv = {},
      };
    }
  }

  const MoveOrderingHints hints{
      .tt_best_move = context->options.use_tt_best_move_ordering && tt_entry.has_value()
                          ? std::optional<board_core::Move>{tt_entry->best_move}
                          : std::nullopt,
  };
  frame.moves = ordered_moves(context->position, hints);
  if (frame.moves.size == 0) {
    ++context->stats.pass_nodes;
    frame.current_move = board_core::make_pass();
    const bool made_delta =
        board_core::make_move_delta(context->position, frame.current_move, &frame.delta);
    require_invariant(made_delta);
    board_core::apply_move_delta(&context->position, frame.delta);

    const SearchValue child =
        alphabeta(context, static_cast<Score>(-beta), static_cast<Score>(-alpha),
                  static_cast<Depth>(depth - 1), static_cast<Ply>(ply + 1));
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

    const SearchValue child =
        alphabeta(context, static_cast<Score>(-beta), static_cast<Score>(-alpha),
                  static_cast<Depth>(depth - 1), static_cast<Ply>(ply + 1));
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

  if (context->transposition_table != nullptr && best_move.has_value()) {
    BoundType bound = BoundType::exact;
    if (best.score <= original_alpha) {
      bound = BoundType::upper;
    } else if (best.score >= original_beta) {
      bound = BoundType::lower;
    }
    context->transposition_table->store(context->position, depth, best.score, bound, *best_move,
                                        TTEntryKind::midgame, &context->stats);
  }

  return best;
}

SearchValue null_window_search(SearchContext* context, Score beta, Depth depth, Ply ply) {
  require_invariant(beta > kScoreLoss);
  require_invariant(beta <= kScoreWin);
  return alphabeta(context, static_cast<Score>(beta - 1), beta, depth, ply);
}

} // namespace vibe_othello::search::internal
