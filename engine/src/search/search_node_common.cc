#include "search_internal.h"

namespace vibe_othello::search::internal {

namespace {

constexpr std::uint8_t kInternalExactEndgameMaxEmpties = 4;
constexpr int kHistoryScoreLimit = 1'000'000;
constexpr Depth kIidMinDepth = 4;
constexpr Depth kIidDepthReduction = 2;

bool is_legal_normal_move(board_core::Bitboard legal_moves, board_core::Move move) noexcept {
  return move.kind == board_core::MoveKind::normal &&
         (legal_moves & board_core::bit(move.square)) != 0;
}

std::optional<board_core::Move>
legal_midgame_tt_best_move(board_core::Position position,
                           const std::optional<TTEntry>& tt_entry) noexcept {
  if (!tt_entry.has_value() || tt_entry->kind != TTEntryKind::midgame || !tt_entry->has_best_move) {
    return std::nullopt;
  }

  const board_core::Bitboard legal_moves = board_core::legal_moves(position);
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

std::uint8_t internal_exact_endgame_threshold(SearchOptions options) noexcept {
  return options.endgame_exact_empties < kInternalExactEndgameMaxEmpties
             ? options.endgame_exact_empties
             : kInternalExactEndgameMaxEmpties;
}

bool should_use_internal_exact_endgame(board_core::Position position,
                                       SearchOptions options) noexcept {
  return options.exact_endgame &&
         empty_count(position) <= internal_exact_endgame_threshold(options);
}

SearchNodeResult search_internal_exact_endgame(SearchContext* context) {
  EndgameContext endgame_context{
      .position = context->position,
      .limits = context->limits,
      .options = context->options,
      .transposition_table = context->transposition_table,
      .limit_state = context->limit_state,
  };
  const SearchNodeResult result = exact_score_search(&endgame_context, kScoreLoss, kScoreWin,
                                                     empty_count(endgame_context.position), Ply{0});
  add_stats(&context->stats, endgame_context.stats);
  return result;
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

ExactEndgameTtProbe exact_endgame_score_tt_probe(const TTEntry& entry,
                                                 board_core::Position position,
                                                 Depth remaining_empties, Score alpha,
                                                 Score beta) noexcept {
  ExactEndgameTtProbe probe{};
  if (entry.kind != TTEntryKind::exact_endgame_score) {
    return probe;
  }

  if (entry.has_best_move && entry.best_move.kind == board_core::MoveKind::normal &&
      (board_core::legal_moves(position) & board_core::bit(entry.best_move.square)) != 0) {
    probe.best_move = entry.best_move;
  }

  if (entry.depth < remaining_empties) {
    // Best-move hints are ordering-only: a shallow exact-endgame entry cannot
    // cut off this node, but its legal normal best move may still order moves.
    return probe;
  }
  if (entry.bound == BoundType::exact || (entry.bound == BoundType::lower && entry.score >= beta) ||
      (entry.bound == BoundType::upper && entry.score <= alpha)) {
    probe.cutoff_score = entry.score;
  }
  return probe;
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

  if (board_core::is_terminal(context->position)) {
    if (note_node_visited(context)) {
      return SearchNodeResult::stopped();
    }
    ++context->stats.terminal_nodes;
    return SearchNodeResult::completed(SearchValue{
        .score = terminal_score(context->position),
        .pv = {},
    });
  }

  if (depth <= 0 && should_use_internal_exact_endgame(context->position, context->options)) {
    if (should_stop_search(context)) {
      return SearchNodeResult::stopped();
    }
    return search_internal_exact_endgame(context);
  }

  if (note_node_visited(context)) {
    return SearchNodeResult::stopped();
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

MoveOrderingHints build_midgame_ordering_hints(const SearchContext& context,
                                               const std::optional<TTEntry>& tt_entry,
                                               std::optional<board_core::Move> iid_best_move,
                                               Ply ply) noexcept {
  MoveOrderingHints hints{
      .tt_best_move = context.options.use_tt_best_move_ordering
                          ? legal_midgame_tt_best_move(context.position, tt_entry)
                          : std::nullopt,
      .iid_best_move = iid_best_move,
  };

  if (context.ordering_state != nullptr && context.options.use_killers) {
    hints.killer_moves = context.ordering_state->killer_moves[ply];
  }
  if (context.ordering_state != nullptr && context.options.use_history) {
    hints.history = &context.ordering_state->history;
  }
  return hints;
}

std::optional<board_core::Move> maybe_find_iid_best_move(SearchContext* context, Score alpha,
                                                         Score beta, Depth depth, Ply ply,
                                                         const std::optional<TTEntry>& tt_entry,
                                                         bool* stopped) {
  *stopped = false;
  if (!context->options.use_iid || context->in_iid || depth < kIidMinDepth || alpha + 1 >= beta ||
      ply >= kMaxPly) {
    return std::nullopt;
  }

  const board_core::Bitboard legal_moves = board_core::legal_moves(context->position);
  if (legal_moves == 0 || legal_midgame_tt_best_move(context->position, tt_entry).has_value()) {
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
  context->stack[ply] = StackFrame{};

  if (shallow.is_stopped()) {
    *stopped = true;
    return std::nullopt;
  }

  const Line& shallow_pv = shallow.value().pv;
  if (shallow_pv.size == 0 || !is_legal_normal_move(legal_moves, shallow_pv.moves[0])) {
    return std::nullopt;
  }
  return shallow_pv.moves[0];
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

void update_midgame_ordering_on_beta_cutoff(SearchContext* context, board_core::Move move,
                                            Depth depth, Ply ply) noexcept {
  if (context->ordering_state == nullptr || move.kind != board_core::MoveKind::normal) {
    return;
  }

  if (context->options.use_history) {
    int& history_score = context->ordering_state->history[move.square.index];
    const int bonus = static_cast<int>(depth * depth);
    history_score =
        history_score > kHistoryScoreLimit - bonus ? kHistoryScoreLimit : history_score + bonus;
  }

  if (context->options.use_killers) {
    std::array<board_core::Move, 2>& killers = context->ordering_state->killer_moves[ply];
    if (killers[0] != move) {
      killers[1] = killers[0];
      killers[0] = move;
    }
  }
}

void maybe_store_midgame_tt(SearchContext* context, Depth depth, Score score, BoundType bound,
                            std::optional<board_core::Move> best_move) noexcept {
  if (context->transposition_table != nullptr && best_move.has_value()) {
    context->transposition_table->store(context->position, depth, score, bound, *best_move,
                                        TTEntryKind::midgame, &context->stats);
  }
}

} // namespace vibe_othello::search::internal
