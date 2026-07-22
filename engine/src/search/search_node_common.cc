#include "endgame_policy_internal.h"
#include "search_internal.h"

#include <cstdlib>

namespace vibe_othello::search::internal {

namespace {

constexpr std::uint8_t kInternalExactEndgameMaxEmpties = 8;
constexpr int kHistoryScoreLimit = 1'000'000;
constexpr Depth kIidMinDepth = 4;
constexpr Depth kIidDepthReduction = 2;
constexpr Depth kMidgameMobilityOrderingMinDepth = 5;

bool is_legal_normal_move(board_core::Bitboard legal_moves, board_core::Move move) noexcept {
  return move.kind == board_core::MoveKind::normal &&
         (legal_moves & board_core::bit(move.square)) != 0;
}

std::optional<board_core::Move>
legal_midgame_tt_best_move(board_core::Bitboard legal_moves,
                           const std::optional<TTEntry>& tt_entry) noexcept {
  if (!tt_entry.has_value() || tt_entry->kind != TTEntryKind::midgame || !tt_entry->has_best_move) {
    return std::nullopt;
  }

  if (!is_legal_normal_move(legal_moves, tt_entry->best_move)) {
    return std::nullopt;
  }
  return tt_entry->best_move;
}

struct ScopedIidFlag {
  explicit ScopedIidFlag(SearchContext* search_context) noexcept
      : context(search_context), previous(search_context->in_iid) {
    context->in_iid = true;
  }

  ~ScopedIidFlag() noexcept {
    context->in_iid = previous;
  }

  SearchContext* context;
  bool previous;
};

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

SearchNodeResult child_result(const SearchNodeResult& child) noexcept {
  if (child.is_stopped()) {
    return SearchNodeResult::stopped();
  }

  const SearchValue& child_value = child.value();
  return SearchNodeResult::completed(
      SearchValue{
          .score = static_cast<Score>(-child_value.score),
      },
      child.is_selective());
}

std::uint8_t internal_exact_endgame_threshold(ResolvedSearchOptions options) noexcept {
  return options.endgame.endgame_exact_empties < kInternalExactEndgameMaxEmpties
             ? options.endgame.endgame_exact_empties
             : kInternalExactEndgameMaxEmpties;
}

bool should_use_internal_exact_endgame(board_core::Position position,
                                       ResolvedSearchOptions options) noexcept {
  return options.endgame.exact_endgame && options.mode != SearchMode::win_loss_draw &&
         empty_count(position) <= internal_exact_endgame_threshold(options);
}

SearchNodeResult search_internal_exact_endgame(SearchContext* context, Ply ply) {
  EndgameContext endgame_context{
      .position_state =
          SearchPositionState{
              .position = context->position_state.position,
              .key = context->position_state.key,
              .legal_mask = context->position_state.legal_mask,
              .legal_mask_valid = context->position_state.legal_mask_valid,
          },
      .limits = context->limits,
      .options = context->options,
      .transposition_table = context->transposition_table,
      .limit_state = context->limit_state,
  };
  const SearchNodeResult result =
      exact_score_search(&endgame_context, kScoreLoss, kScoreWin,
                         empty_count(endgame_context.position_state.position), Ply{0});
  add_stats(&context->stats, endgame_context.stats);
  if (result.is_complete()) {
    context->stack[ply].pv = endgame_context.stack[0].pv;
  }
  return result;
}

} // namespace

SearchNodeResult SearchNodeResult::completed(SearchValue value, bool selective) noexcept {
  SearchNodeResult result;
  result.status_ = SearchNodeStatus::complete;
  result.value_ = value;
  result.selective_ = selective;
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

bool SearchNodeResult::is_selective() const noexcept {
  return is_complete() && selective_;
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

std::optional<SearchNodeResult> prepare_search_node(SearchContext* context, Score alpha, Score beta,
                                                    Depth depth, Ply ply,
                                                    std::optional<TTEntry>* tt_entry) {
  require_invariant(alpha < beta);
  require_invariant(ply < kMaxPly);
  StackFrame& frame = context->stack[ply];
  frame.pv.size = 0;

  frame.legal_moves = legal_moves(&context->position_state);
  if (frame.legal_moves == 0 && opponent_legal_moves(context->position_state) == 0) {
    if (note_node_visited(context)) {
      return SearchNodeResult::stopped();
    }
    ++context->stats.terminal_nodes;
    return SearchNodeResult::completed(SearchValue{
        .score = terminal_score(context->position_state.position),
    });
  }

  if (depth <= 0 &&
      should_use_internal_exact_endgame(context->position_state.position, context->options)) {
    if (should_stop_search(context)) {
      return SearchNodeResult::stopped();
    }
    return search_internal_exact_endgame(context, ply);
  }

  if (note_node_visited(context)) {
    return SearchNodeResult::stopped();
  }

  if (depth <= 0) {
    ++context->stats.leaf_nodes;
    ++context->stats.eval_calls;
    if (uses_incremental_evaluation(context->position_state)) {
      ++context->stats.incremental_eval_calls;
    } else {
      ++context->stats.stateless_eval_calls;
    }
    const Score score = evaluate_position(context->position_state, context->evaluator);
    if (context->position_state.evaluation_state.has_value() &&
        context->incremental_eval_verify_interval != 0 &&
        ++context->incremental_eval_count % context->incremental_eval_verify_interval == 0 &&
        score != evaluate_position_reference(context->position_state, context->evaluator)) {
      std::abort();
    }
    require_invariant(is_valid_evaluator_score(score));
    return SearchNodeResult::completed(SearchValue{
        .score = score,
    });
  }

  *tt_entry = context->transposition_table == nullptr
                  ? std::nullopt
                  : context->transposition_table->probe(context->position_state.key,
                                                        TTEntryKind::midgame, &context->stats);
  if (context->options.midgame.use_midgame_tt && tt_entry->has_value()) {
    const std::optional<Score> cutoff = midgame_tt_cutoff_score(**tt_entry, depth, alpha, beta);
    if (cutoff.has_value()) {
      ++context->stats.tt_cutoffs;
      if ((*tt_entry)->selective) {
        ++context->stats.selective_cuts;
      }
      return SearchNodeResult::completed(
          SearchValue{
              .score = *cutoff,
          },
          (*tt_entry)->selective);
    }
  }

  if (should_stop_search(context)) {
    return SearchNodeResult::stopped();
  }

  return std::nullopt;
}

MoveOrderingHints build_midgame_ordering_hints(const SearchContext& context,
                                               const std::optional<TTEntry>& tt_entry,
                                               std::optional<board_core::Move> iid_best_move,
                                               Depth depth, Ply ply) noexcept {
  MoveOrderingHints hints{
      .tt_best_move = context.options.ordering.use_tt_best_move_ordering
                          ? legal_midgame_tt_best_move(context.stack[ply].legal_moves, tt_entry)
                          : std::nullopt,
      .iid_best_move = iid_best_move,
      .use_opponent_mobility = context.options.ordering.use_midgame_mobility_ordering &&
                               depth >= kMidgameMobilityOrderingMinDepth,
  };

  if (context.ordering_state != nullptr && context.options.ordering.use_killers) {
    hints.killer_moves = context.ordering_state->killer_moves[ply];
  }
  if (context.ordering_state != nullptr && context.options.ordering.use_history) {
    hints.history = &context.ordering_state->history;
  }
  return hints;
}

std::optional<board_core::Move> maybe_find_iid_best_move(SearchContext* context, Score alpha,
                                                         Score beta, Depth depth, Ply ply,
                                                         const std::optional<TTEntry>& tt_entry,
                                                         bool* stopped) {
  *stopped = false;
  if (!context->options.midgame.use_iid || context->in_iid || context->in_probcut_shallow ||
      depth < kIidMinDepth || alpha + 1 >= beta || ply >= kMaxPly) {
    return std::nullopt;
  }

  const board_core::Bitboard legal_mask = legal_moves(&context->position_state);
  if (legal_mask == 0 || legal_midgame_tt_best_move(legal_mask, tt_entry).has_value()) {
    return std::nullopt;
  }

  if (should_stop_search(context)) {
    *stopped = true;
    return std::nullopt;
  }

  ++context->stats.iid_searches;
  const Depth shallow_depth = static_cast<Depth>(depth - kIidDepthReduction);
  SearchNodeResult shallow;
  {
    const ScopedIidFlag guard{context};
    shallow = full_window_search(context, alpha, beta, shallow_depth, ply);
  }
  const Line& shallow_pv = context->stack[ply].pv;
  const std::optional<board_core::Move> shallow_best_move =
      shallow_pv.size != 0 && is_legal_normal_move(legal_mask, shallow_pv.moves[0])
          ? std::optional<board_core::Move>{shallow_pv.moves[0]}
          : std::nullopt;
  context->stack[ply].pv.size = 0;
  context->stack[ply].legal_moves = legal_mask;

  if (shallow.is_stopped()) {
    *stopped = true;
    return std::nullopt;
  }

  return shallow_best_move;
}

SearchNodeResult search_full_window_child(SearchContext* context, board_core::Move move,
                                          Score alpha, Score beta, Depth depth, Ply ply,
                                          SearchDispatch dispatch) {
  StackFrame& frame = context->stack[ply];
  frame.current_move = move;
  if (move.kind == board_core::MoveKind::pass) {
    frame.delta = board_core::MoveDelta{.move = board_core::make_pass(), .flipped = 0};
  } else {
    const bool made_delta =
        board_core::make_move_delta(context->position_state.position, move, &frame.delta);
    require_invariant(made_delta);
  }
  apply_move(&context->position_state, frame.delta, &frame.position_undo, &context->stats);

  const Depth child_depth =
      move.kind == board_core::MoveKind::pass && !context->options.midgame.pass_consumes_depth
          ? depth
          : static_cast<Depth>(depth - 1);

  const SearchNodeResult child =
      dispatch_search(context, dispatch, static_cast<Score>(-beta), static_cast<Score>(-alpha),
                      child_depth, static_cast<Ply>(ply + 1));
  undo_move(&context->position_state, frame.delta, frame.position_undo, &context->stats);

  return child_result(child);
}

SearchNodeResult search_null_window_child(SearchContext* context, board_core::Move move, Score beta,
                                          Depth depth, Ply ply) {
  StackFrame& frame = context->stack[ply];
  frame.current_move = move;
  const bool made_delta =
      board_core::make_move_delta(context->position_state.position, move, &frame.delta);
  require_invariant(made_delta);
  apply_move(&context->position_state, frame.delta, &frame.position_undo, &context->stats);

  const SearchNodeResult child =
      null_window_search(context, beta, static_cast<Depth>(depth - 1), static_cast<Ply>(ply + 1));
  undo_move(&context->position_state, frame.delta, frame.position_undo, &context->stats);

  return child_result(child);
}

SearchNodeResult search_pass_child(SearchContext* context, Score alpha, Score beta, Depth depth,
                                   Ply ply, SearchDispatch dispatch) {
  ++context->stats.pass_nodes;
  SearchNodeResult result =
      search_full_window_child(context, board_core::make_pass(), alpha, beta, depth, ply, dispatch);
  if (result.is_complete()) {
    prepend_move(board_core::make_pass(), context->stack[ply + 1].pv, &context->stack[ply].pv);
  }
  return result;
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

void update_midgame_ordering_on_beta_cutoff(SearchContext* context, board_core::Move move,
                                            Depth depth, Ply ply) noexcept {
  if (context->ordering_state == nullptr || move.kind != board_core::MoveKind::normal) {
    return;
  }

  if (context->options.ordering.use_history) {
    int& history_score = context->ordering_state->history[move.square.index];
    const int bonus = static_cast<int>(depth * depth);
    history_score =
        history_score > kHistoryScoreLimit - bonus ? kHistoryScoreLimit : history_score + bonus;
  }

  if (context->options.ordering.use_killers) {
    std::array<board_core::Move, 2>& killers = context->ordering_state->killer_moves[ply];
    if (killers[0] != move) {
      killers[1] = killers[0];
      killers[0] = move;
    }
  }
}

void maybe_store_midgame_tt(SearchContext* context, Depth depth, Score score, BoundType bound,
                            std::optional<board_core::Move> best_move,
                            bool subtree_selective) noexcept {
  if (context->transposition_table != nullptr && best_move.has_value() &&
      !context->in_probcut_shallow && !subtree_selective) {
    context->transposition_table->store(context->position_state.key, depth, score, bound,
                                        *best_move, TTEntryKind::midgame, &context->stats);
  }
}

} // namespace vibe_othello::search::internal
