#include "search_internal.h"

namespace vibe_othello::search::internal {

SearchNodeResult pvs(SearchContext* context, Score alpha, Score beta, Depth depth, Ply ply) {
  const Score original_alpha = alpha;
  const Score original_beta = beta;
  std::optional<TTEntry> tt_entry;
  const std::optional<SearchNodeResult> node_result =
      prepare_search_node(context, alpha, beta, depth, ply, &tt_entry);
  if (node_result.has_value()) {
    return *node_result;
  }

  StackFrame& frame = context->stack[ply];
  const MoveOrderingHints hints = build_ordering_hints_from_tt(*context, tt_entry);
  frame.moves = order_midgame_moves(context->position, hints);
  if (frame.moves.size == 0) {
    return search_pass_child(context, alpha, beta, depth, ply, SearchDispatch::pvs);
  }

  SearchValue best{
      .score = kScoreLoss,
      .pv = {},
  };
  std::optional<board_core::Move> best_move;

  for (std::uint8_t move_index = 0; move_index < frame.moves.size; ++move_index) {
    const board_core::Move move = frame.moves.moves[move_index];

    SearchNodeResult child;
    if (move_index == 0) {
      child = search_full_window_child(context, move, alpha, beta, depth, ply, SearchDispatch::pvs);
    } else {
      child = search_null_window_child(context, move, static_cast<Score>(-alpha), depth, ply);
      if (child.is_stopped()) {
        return SearchNodeResult::stopped();
      }
      const Score score = child.value().score;
      if (score > alpha && score < beta) {
        ++context->stats.pvs_researches;
        child =
            search_full_window_child(context, move, alpha, beta, depth, ply, SearchDispatch::pvs);
      }
    }
    if (child.is_stopped()) {
      return SearchNodeResult::stopped();
    }
    const SearchValue& child_value = child.value();
    update_best_line_and_move(child_value, move, &best, &best_move, &frame);

    if (update_alpha_and_check_cutoff(context, child_value.score, &alpha, beta)) {
      break;
    }
  }

  maybe_store_midgame_tt(context, depth, best.score,
                         classify_bound(best.score, original_alpha, original_beta), best_move);

  return SearchNodeResult::completed(best);
}

SearchNodeResult full_window_search(SearchContext* context, Score alpha, Score beta, Depth depth,
                                    Ply ply) {
  if (context->options.use_pvs) {
    return pvs(context, alpha, beta, depth, ply);
  }
  return alphabeta(context, alpha, beta, depth, ply);
}

} // namespace vibe_othello::search::internal
