#include "search_internal.h"

namespace vibe_othello::search::internal {

namespace {

SearchNodeResult dispatch_search(SearchContext* context, SearchDispatch dispatch, Score alpha,
                                 Score beta, Depth depth, Ply ply) {
  switch (dispatch) {
  case SearchDispatch::alphabeta:
    return alphabeta(context, alpha, beta, depth, ply);
  case SearchDispatch::pvs:
    return pvs(context, alpha, beta, depth, ply);
  }
  require_invariant(false);
  return {};
}

SearchNodeResult child_result(board_core::Move move, const SearchNodeResult& child) noexcept {
  if (child.is_stopped()) {
    return SearchNodeResult::stopped();
  }

  const SearchValue& child_value = child.value();
  SearchValue result{
      .score = static_cast<Score>(-child_value.score),
      .pv = {},
  };
  prepend_move(move, child_value.pv, &result.pv);
  return SearchNodeResult::completed(result);
}

} // namespace

SearchNodeResult SearchNodeResult::completed(SearchValue value) noexcept {
  SearchNodeResult result;
  result.status_ = SearchNodeStatus::complete;
  result.value_ = value;
  return result;
}

SearchNodeResult SearchNodeResult::stopped() noexcept {
  return SearchNodeResult{};
}

bool SearchNodeResult::is_complete() const noexcept {
  return status_ == SearchNodeStatus::complete;
}

bool SearchNodeResult::is_stopped() const noexcept {
  return status_ == SearchNodeStatus::stopped;
}

const SearchValue& SearchNodeResult::value() const noexcept {
  require_invariant(is_complete());
  return value_;
}

BoundType classify_bound(Score score, Score original_alpha, Score original_beta) noexcept {
  if (score <= original_alpha) {
    return BoundType::upper;
  }
  if (score >= original_beta) {
    return BoundType::lower;
  }
  return BoundType::exact;
}

std::optional<Score> midgame_tt_cutoff_score(const TTEntry& entry, Depth depth, Score alpha,
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

std::optional<Score> exact_endgame_score_tt_cutoff_score(const TTEntry& entry,
                                                         Depth remaining_empties, Score alpha,
                                                         Score beta) noexcept {
  if (entry.kind != TTEntryKind::exact_endgame_score || entry.depth < remaining_empties) {
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

std::optional<SearchNodeResult> prepare_search_node(SearchContext* context, Score alpha, Score beta,
                                                    Depth depth, Ply ply,
                                                    std::optional<TTEntry>* tt_entry) {
  require_invariant(alpha < beta);
  require_invariant(ply < kMaxPly);
  StackFrame& frame = context->stack[ply];
  frame = StackFrame{};

  if (note_node_visited(context)) {
    return SearchNodeResult::stopped();
  }

  if (board_core::is_terminal(context->position)) {
    ++context->stats.terminal_nodes;
    return SearchNodeResult::completed(SearchValue{
        .score = terminal_score(context->position),
        .pv = {},
    });
  }

  if (depth <= 0) {
    ++context->stats.leaf_nodes;
    ++context->stats.eval_calls;
    const Score score = context->evaluator.evaluate(context->position);
    require_invariant(is_valid_evaluator_score(score));
    return SearchNodeResult::completed(SearchValue{
        .score = score,
        .pv = {},
    });
  }

  *tt_entry = context->transposition_table == nullptr
                  ? std::nullopt
                  : context->transposition_table->probe(context->position, &context->stats);
  if (context->options.use_midgame_tt && tt_entry->has_value()) {
    const std::optional<Score> cutoff = midgame_tt_cutoff_score(**tt_entry, depth, alpha, beta);
    if (cutoff.has_value()) {
      ++context->stats.tt_cutoffs;
      return SearchNodeResult::completed(SearchValue{
          .score = *cutoff,
          .pv = {},
      });
    }
  }

  if (should_stop_search(context)) {
    return SearchNodeResult::stopped();
  }

  return std::nullopt;
}

MoveOrderingHints build_ordering_hints_from_tt(const SearchContext& context,
                                               const std::optional<TTEntry>& tt_entry) noexcept {
  return MoveOrderingHints{
      .tt_best_move = context.options.use_tt_best_move_ordering && tt_entry.has_value() &&
                              tt_entry->kind == TTEntryKind::midgame && tt_entry->has_best_move
                          ? std::optional<board_core::Move>{tt_entry->best_move}
                          : std::nullopt,
  };
}

SearchNodeResult search_full_window_child(SearchContext* context, board_core::Move move,
                                          Score alpha, Score beta, Depth depth, Ply ply,
                                          SearchDispatch dispatch) {
  StackFrame& frame = context->stack[ply];
  frame.current_move = move;
  const bool made_delta = board_core::make_move_delta(context->position, move, &frame.delta);
  require_invariant(made_delta);
  board_core::apply_move_delta(&context->position, frame.delta);

  const SearchNodeResult child =
      dispatch_search(context, dispatch, static_cast<Score>(-beta), static_cast<Score>(-alpha),
                      static_cast<Depth>(depth - 1), static_cast<Ply>(ply + 1));
  board_core::undo_move(&context->position, frame.delta);

  return child_result(move, child);
}

SearchNodeResult search_null_window_child(SearchContext* context, board_core::Move move, Score beta,
                                          Depth depth, Ply ply) {
  StackFrame& frame = context->stack[ply];
  frame.current_move = move;
  const bool made_delta = board_core::make_move_delta(context->position, move, &frame.delta);
  require_invariant(made_delta);
  board_core::apply_move_delta(&context->position, frame.delta);

  const SearchNodeResult child =
      null_window_search(context, beta, static_cast<Depth>(depth - 1), static_cast<Ply>(ply + 1));
  board_core::undo_move(&context->position, frame.delta);

  return child_result(move, child);
}

SearchNodeResult search_pass_child(SearchContext* context, Score alpha, Score beta, Depth depth,
                                   Ply ply, SearchDispatch dispatch) {
  ++context->stats.pass_nodes;
  SearchNodeResult result =
      search_full_window_child(context, board_core::make_pass(), alpha, beta, depth, ply, dispatch);
  if (result.is_complete()) {
    context->stack[ply].pv = result.value().pv;
  }
  return result;
}

void update_best_line_and_move(const SearchValue& child, board_core::Move move, SearchValue* best,
                               std::optional<board_core::Move>* best_move, StackFrame* frame) {
  if (!best_move->has_value() || child.score > best->score ||
      (child.score == best->score && move.square.index < (*best_move)->square.index)) {
    *best = child;
    *best_move = move;
    frame->pv = best->pv;
  }
}

bool update_alpha_and_check_cutoff(SearchContext* context, Score score, Score* alpha,
                                   Score beta) noexcept {
  if (score > *alpha) {
    ++context->stats.alpha_updates;
    *alpha = score;
  }
  if (*alpha >= beta) {
    ++context->stats.beta_cutoffs;
    return true;
  }
  return false;
}

void maybe_store_midgame_tt(SearchContext* context, Depth depth, Score score, BoundType bound,
                            std::optional<board_core::Move> best_move) noexcept {
  if (context->transposition_table != nullptr && best_move.has_value()) {
    context->transposition_table->store(context->position, depth, score, bound, *best_move,
                                        TTEntryKind::midgame, &context->stats);
  }
}

} // namespace vibe_othello::search::internal
