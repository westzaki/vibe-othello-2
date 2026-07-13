#include "search_internal.h"

namespace vibe_othello::search::internal {

SearchNodeResult pvs(SearchContext* context, Score alpha, Score beta, Depth depth, Ply ply,
                     bool cut_node) {
  const Score original_alpha = alpha;
  const Score original_beta = beta;
  std::optional<TTEntry> tt_entry;
  const std::optional<SearchNodeResult> node_result =
      prepare_search_node(context, alpha, beta, depth, ply, &tt_entry);
  if (node_result.has_value()) {
    return *node_result;
  }

  std::optional<ProbCutShadowCandidate> probcut_shadow_candidate;
  const std::optional<SearchNodeResult> probcut_result =
      maybe_probcut(context, alpha, beta, depth, ply, cut_node, &probcut_shadow_candidate);
  if (probcut_result.has_value()) {
    return *probcut_result;
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
    shadow_candidate =
        begin_shadow_candidate(context, original_alpha, original_beta, depth, ply, cut_node);
  }

  const MoveOrderingHints hints =
      build_midgame_ordering_hints(*context, tt_entry, iid_best_move, ply);
  frame.moves = order_midgame_moves(context->position_state.position, frame.legal_moves, hints);
  if (frame.moves.size == 0) {
    const SearchNodeResult result =
        search_pass_child(context, alpha, beta, depth, ply, SearchDispatch::pvs);
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
  bool subtree_selective = false;

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
    subtree_selective = subtree_selective || child.is_selective();
    update_best_line_and_move(child_value, move, &best, &best_move, &frame);

    if (update_alpha_and_check_cutoff(context, child_value.score, &alpha, beta)) {
      update_midgame_ordering_on_beta_cutoff(context, move, depth, ply);
      break;
    }
  }

  maybe_store_midgame_tt(context, depth, best.score,
                         classify_bound(best.score, original_alpha, original_beta), best_move,
                         subtree_selective);

  const SearchNodeResult result = SearchNodeResult::completed(best, subtree_selective);
  if (probcut_shadow_candidate.has_value()) {
    complete_probcut_shadow(context, *probcut_shadow_candidate, result);
  }
  if (shadow_candidate.has_value()) {
    complete_shadow_candidate(context, *shadow_candidate, result);
  }
  return result;
}

SearchNodeResult full_window_search(SearchContext* context, Score alpha, Score beta, Depth depth,
                                    Ply ply) {
  if (context->options.midgame.use_pvs) {
    return pvs(context, alpha, beta, depth, ply, false);
  }
  return alphabeta(context, alpha, beta, depth, ply, false);
}

} // namespace vibe_othello::search::internal
