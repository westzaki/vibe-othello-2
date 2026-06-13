#include "search_internal.h"

#include <chrono>
#include <optional>

namespace vibe_othello::search {
namespace internal {

namespace {

constexpr Score kAspirationInitialWindow = 4;
constexpr Score kFullScoreRange = kScoreWin - kScoreLoss;

bool is_better_root_move(Score score, board_core::Move move, Score best_score,
                         std::optional<board_core::Move> best_move) noexcept {
  if (!best_move.has_value() || score > best_score) {
    return true;
  }
  if (score < best_score || move.kind != board_core::MoveKind::normal ||
      best_move->kind != board_core::MoveKind::normal) {
    return false;
  }
  return move.square.index < best_move->square.index;
}

Score clamp_score(Score score) noexcept {
  if (score < kScoreLoss) {
    return kScoreLoss;
  }
  if (score > kScoreWin) {
    return kScoreWin;
  }
  return score;
}

Score next_aspiration_margin(Score margin) noexcept {
  if (margin >= kFullScoreRange / 2) {
    return kFullScoreRange;
  }
  return static_cast<Score>(margin * 2);
}

RootSearchWindow aspiration_window(Score previous_score, Score low_margin,
                                   Score high_margin) noexcept {
  RootSearchWindow window{
      .alpha = clamp_score(static_cast<Score>(previous_score - low_margin)),
      .beta = clamp_score(static_cast<Score>(previous_score + high_margin)),
      .enabled = true,
  };
  if (window.alpha >= window.beta) {
    window.alpha = kScoreLoss;
    window.beta = kScoreWin;
  }
  return window;
}

bool is_full_score_range(RootSearchWindow window) noexcept {
  return window.alpha == kScoreLoss && window.beta == kScoreWin;
}

BoundType bound_for_score(Score score, RootSearchWindow window) noexcept {
  if (!window.enabled) {
    return BoundType::exact;
  }
  if (score <= window.alpha) {
    return BoundType::upper;
  }
  if (score >= window.beta) {
    return BoundType::lower;
  }
  return BoundType::exact;
}

RootMoveInfo evaluate_root_move(SearchContext* context, Depth depth, board_core::Move move,
                                RootSearchWindow move_window) {
  StackFrame& frame = context->stack[0];
  frame = StackFrame{};
  frame.current_move = move;
  const bool made_delta = board_core::make_move_delta(context->position, move, &frame.delta);
  require_invariant(made_delta);
  board_core::apply_move_delta(&context->position, frame.delta);

  const NodeCount before_nodes = context->stats.nodes;
  const Score child_alpha = static_cast<Score>(-move_window.beta);
  const Score child_beta = static_cast<Score>(-move_window.alpha);
  const SearchValue child =
      full_window_search(context, child_alpha, child_beta, static_cast<Depth>(depth - 1), Ply{1});
  board_core::undo_move(&context->position, frame.delta);

  const Score score = static_cast<Score>(-child.score);
  Line line{};
  prepend_move(move, child.pv, &line);
  frame.pv = line;

  return RootMoveInfo{
      .move = move,
      .score = score,
      .bound = bound_for_score(score, move_window),
      .depth = depth,
      .nodes = context->stats.nodes - before_nodes,
      .pv = line,
      .exact = false,
      .selective = false,
  };
}

void update_best_root_move(const RootMoveInfo& root_move, Score* best_score, Line* best_line,
                           SearchResult* result) {
  if (is_better_root_move(root_move.score, root_move.move, *best_score, result->best_move)) {
    result->best_move = root_move.move;
    *best_score = root_move.score;
    *best_line = root_move.pv;
  }
}

void recompute_best_root_move(SearchResult* result, Score* best_score, Line* best_line) {
  result->best_move = std::nullopt;
  *best_score = kScoreLoss;
  *best_line = {};
  for (const RootMoveInfo& root_move : result->root_moves) {
    update_best_root_move(root_move, best_score, best_line, result);
  }
}

void complete_exact_root_move_reports(SearchContext* context, Depth depth, SearchResult* result,
                                      Score* best_score, Line* best_line) {
  bool updated = false;
  for (RootMoveInfo& root_move : result->root_moves) {
    if (root_move.bound == BoundType::exact) {
      continue;
    }
    root_move = evaluate_root_move(context, depth, root_move.move, RootSearchWindow{});
    ++context->stats.root_moves_searched;
    updated = true;
  }
  if (updated) {
    recompute_best_root_move(result, best_score, best_line);
  }
}

SearchResult search_depth_with_aspiration(board_core::Position position, const Evaluator& evaluator,
                                          Depth depth, MoveOrderingHints root_hints,
                                          SearchOptions options, TranspositionTable* tt,
                                          Score previous_score) {
  SearchStats depth_stats{};
  Score low_margin = kAspirationInitialWindow;
  Score high_margin = kAspirationInitialWindow;
  RootSearchWindow window = aspiration_window(previous_score, low_margin, high_margin);

  SearchResult current{};
  for (;;) {
    current =
        search_fixed_depth_with_hint(position, evaluator, depth, root_hints, options, tt, window);
    add_stats(&depth_stats, current.stats);

    if (current.score <= window.alpha) {
      ++depth_stats.aspiration_fail_lows;
      if (is_full_score_range(window)) {
        current.nodes = depth_stats.nodes;
        current.stats = depth_stats;
        return current;
      }
      low_margin = next_aspiration_margin(low_margin);
      if (window.alpha == kScoreLoss) {
        high_margin = kFullScoreRange;
      }
      window = aspiration_window(previous_score, low_margin, high_margin);
      continue;
    }

    if (current.score >= window.beta) {
      ++depth_stats.aspiration_fail_highs;
      if (is_full_score_range(window)) {
        current.nodes = depth_stats.nodes;
        current.stats = depth_stats;
        return current;
      }
      high_margin = next_aspiration_margin(high_margin);
      if (window.beta == kScoreWin) {
        low_margin = kFullScoreRange;
      }
      window = aspiration_window(previous_score, low_margin, high_margin);
      continue;
    }

    current.nodes = depth_stats.nodes;
    current.stats = depth_stats;
    return current;
  }
}

} // namespace

SearchResult search_fixed_depth_with_hint(board_core::Position position, const Evaluator& evaluator,
                                          Depth depth, MoveOrderingHints root_hints,
                                          SearchOptions options, TranspositionTable* tt,
                                          RootSearchWindow root_window) {
  require_invariant(!root_window.enabled || root_window.alpha < root_window.beta);
  const auto start = std::chrono::steady_clock::now();
  const Depth completed_depth = depth < 0 ? Depth{0} : depth;
  SearchContext context{
      .position = position,
      .evaluator = evaluator,
      .limits = SearchLimits{.max_depth = completed_depth},
      .options = options,
      .transposition_table = tt,
  };

  SearchResult result{
      .completed_depth = completed_depth,
  };

  if (board_core::is_terminal(context.position) || completed_depth == 0) {
    const Score alpha = root_window.enabled ? root_window.alpha : kScoreLoss;
    const Score beta = root_window.enabled ? root_window.beta : kScoreWin;
    const SearchValue root = full_window_search(&context, alpha, beta, completed_depth, Ply{0});
    result.score = root.score;
    result.bound = bound_for_score(root.score, root_window);
    result.nodes = context.stats.nodes;
    result.stats = context.stats;
    result.pv = root.pv;
    result.exact = board_core::is_terminal(context.position);
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
  }

  context.stats.nodes = 1;
  if (context.options.use_tt_best_move_ordering && !root_hints.tt_best_move.has_value() &&
      context.transposition_table != nullptr) {
    const std::optional<TTEntry> root_tt_entry =
        context.transposition_table->probe(context.position, &context.stats);
    if (root_tt_entry.has_value()) {
      root_hints.tt_best_move = root_tt_entry->best_move;
    }
  }
  root_hints.use_opponent_mobility = true;
  StackFrame& root_frame = context.stack[0];
  root_frame = StackFrame{};
  root_frame.moves = ordered_moves(context.position, root_hints);
  const MoveList root_moves = root_frame.moves;
  if (root_moves.size == 0) {
    ++context.stats.pass_nodes;
    root_frame.current_move = board_core::make_pass();
    const bool made_delta =
        board_core::make_move_delta(context.position, root_frame.current_move, &root_frame.delta);
    require_invariant(made_delta);
    board_core::apply_move_delta(&context.position, root_frame.delta);

    const NodeCount before_nodes = context.stats.nodes;
    const Score alpha = root_window.enabled ? root_window.alpha : kScoreLoss;
    const Score beta = root_window.enabled ? root_window.beta : kScoreWin;
    const SearchValue child =
        full_window_search(&context, static_cast<Score>(-beta), static_cast<Score>(-alpha),
                           static_cast<Depth>(completed_depth - 1), Ply{1});
    board_core::undo_move(&context.position, root_frame.delta);

    result.best_move = board_core::make_pass();
    result.score = static_cast<Score>(-child.score);
    result.bound = bound_for_score(result.score, root_window);
    prepend_move(board_core::make_pass(), child.pv, &result.pv);
    root_frame.pv = result.pv;
    result.root_moves.push_back(RootMoveInfo{
        .move = board_core::make_pass(),
        .score = result.score,
        .bound = bound_for_score(result.score, root_window),
        .depth = completed_depth,
        .nodes = context.stats.nodes - before_nodes,
        .pv = result.pv,
        .exact = false,
        .selective = false,
    });
    context.stats.root_moves_searched = 1;

    result.nodes = context.stats.nodes;
    result.stats = context.stats;
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
  }

  Score best_score = kScoreLoss;
  Line best_line{};
  Score alpha = root_window.alpha;
  for (std::uint8_t move_index = 0; move_index < root_moves.size; ++move_index) {
    const board_core::Move move = root_moves.moves[move_index];
    const RootSearchWindow move_window =
        root_window.enabled
            ? RootSearchWindow{.alpha = alpha, .beta = root_window.beta, .enabled = true}
            : RootSearchWindow{};
    result.root_moves.push_back(evaluate_root_move(&context, completed_depth, move, move_window));
    const Score score = result.root_moves.back().score;
    ++context.stats.root_moves_searched;
    update_best_root_move(result.root_moves.back(), &best_score, &best_line, &result);
    if (root_window.enabled && score > alpha) {
      ++context.stats.alpha_updates;
      alpha = score;
    }
    if (root_window.enabled && alpha >= root_window.beta) {
      ++context.stats.beta_cutoffs;
      break;
    }
  }

  const BoundType result_bound = bound_for_score(best_score, root_window);
  if (root_window.enabled && result_bound == BoundType::exact &&
      !is_full_score_range(root_window)) {
    complete_exact_root_move_reports(&context, completed_depth, &result, &best_score, &best_line);
  }

  if (context.transposition_table != nullptr && result.best_move.has_value()) {
    const BoundType bound = bound_for_score(best_score, root_window);
    context.transposition_table->store(context.position, completed_depth, best_score, bound,
                                       *result.best_move, TTEntryKind::midgame, &context.stats);
  }

  result.score = best_score;
  result.bound = bound_for_score(best_score, root_window);
  result.nodes = context.stats.nodes;
  result.stats = context.stats;
  result.pv = best_line;
  result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  return result;
}

} // namespace internal

SearchResult search_fixed_depth(board_core::Position position, const Evaluator& evaluator,
                                Depth depth) {
  return internal::search_fixed_depth_with_hint(
      position, evaluator, depth, internal::MoveOrderingHints{}, SearchOptions{}, nullptr);
}

SearchResult search_iterative(board_core::Position position, const Evaluator& evaluator,
                              SearchLimits limits) {
  return search_iterative(position, evaluator, limits, SearchOptions{});
}

SearchResult search_iterative(board_core::Position position, const Evaluator& evaluator,
                              SearchLimits limits, SearchOptions options) {
  const auto start = std::chrono::steady_clock::now();
  const Depth max_depth = limits.max_depth < 0 ? Depth{0} : limits.max_depth;

  if (max_depth == 0) {
    SearchResult result = search_fixed_depth(position, evaluator, Depth{0});
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
  }

  SearchResult best_completed{};
  SearchStats total_stats{};
  internal::TranspositionTable tt_storage{};
  internal::TranspositionTable* tt =
      (options.use_tt_best_move_ordering || options.use_midgame_tt) ? &tt_storage : nullptr;
  std::optional<board_core::Move> previous_best_move;
  std::optional<Score> previous_score;
  for (Depth depth = 1; depth <= max_depth; ++depth) {
    const internal::MoveOrderingHints root_hints{.root_best_move = previous_best_move};
    SearchResult current =
        options.use_aspiration && depth >= 2 && previous_score.has_value()
            ? internal::search_depth_with_aspiration(position, evaluator, depth, root_hints,
                                                     options, tt, *previous_score)
            : internal::search_fixed_depth_with_hint(position, evaluator, depth, root_hints,
                                                     options, tt);
    internal::add_stats(&total_stats, current.stats);
    previous_best_move = current.best_move;
    previous_score = current.score;
    best_completed = current;
  }

  best_completed.nodes = total_stats.nodes;
  best_completed.stats = total_stats;
  best_completed.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  return best_completed;
}

} // namespace vibe_othello::search
