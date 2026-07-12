#include "search_internal.h"

namespace vibe_othello::search::internal {

SearchNodeResult alphabeta(SearchContext* context, Score alpha, Score beta, Depth depth, Ply ply) {
  const Score original_alpha = alpha;
  const Score original_beta = beta;
  std::optional<TTEntry> tt_entry;
  const std::optional<SearchNodeResult> node_result =
      prepare_search_node(context, alpha, beta, depth, ply, &tt_entry);
  if (node_result.has_value()) {
    return *node_result;
  }

  StackFrame& frame = context->stack[ply];
  bool iid_stopped = false;
  const std::optional<board_core::Move> iid_best_move =
      maybe_find_iid_best_move(context, alpha, beta, depth, ply, tt_entry, &iid_stopped);
  if (iid_stopped) {
    return SearchNodeResult::stopped();
  }

  std::optional<ShadowCandidate> shadow_candidate;
  if (context->shadow_calibration != nullptr) {
    shadow_candidate = begin_shadow_candidate(context, original_alpha, original_beta, depth, ply);
  }

  const MoveOrderingHints hints =
      build_midgame_ordering_hints(*context, tt_entry, iid_best_move, ply);
  frame.moves = order_midgame_moves(context->position_state.position, frame.legal_moves, hints);
  if (frame.moves.size == 0) {
    const SearchNodeResult result =
        search_pass_child(context, alpha, beta, depth, ply, SearchDispatch::alphabeta);
    if (shadow_candidate.has_value()) {
      complete_shadow_candidate(context, *shadow_candidate, result);
    }
    return result;
  }

  SearchValue best{
      .score = kScoreLoss,
      .pv = {},
  };
  std::optional<board_core::Move> best_move;

  for (std::uint8_t move_index = 0; move_index < frame.moves.size; ++move_index) {
    const board_core::Move move = frame.moves.moves[move_index];
    const SearchNodeResult child =
        search_full_window_child(context, move, alpha, beta, depth, ply, SearchDispatch::alphabeta);
    if (child.is_stopped()) {
      return SearchNodeResult::stopped();
    }
    const SearchValue& child_value = child.value();
    update_best_line_and_move(child_value, move, &best, &best_move, &frame);

    if (update_alpha_and_check_cutoff(context, child_value.score, &alpha, beta)) {
      update_midgame_ordering_on_beta_cutoff(context, move, depth, ply);
      break;
    }
  }

  maybe_store_midgame_tt(context, depth, best.score,
                         classify_bound(best.score, original_alpha, original_beta), best_move);

  const SearchNodeResult result = SearchNodeResult::completed(best);
  if (shadow_candidate.has_value()) {
    complete_shadow_candidate(context, *shadow_candidate, result);
  }
  return result;
}

SearchNodeResult null_window_search(SearchContext* context, Score beta, Depth depth, Ply ply) {
  require_invariant(beta > kScoreLoss);
  require_invariant(beta <= kScoreWin);
  return alphabeta(context, static_cast<Score>(beta - 1), beta, depth, ply);
}

} // namespace vibe_othello::search::internal
