#include "search_internal.h"

#include <chrono>
#include <optional>

namespace vibe_othello::search {
namespace internal {

namespace {

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

void discard_incomplete_result(SearchResult* result) {
  result->best_move.reset();
  result->score = 0;
  result->bound = BoundType::exact;
  result->completed_depth = 0;
  result->pv = Line{};
  result->root_moves.clear();
  result->exact = false;
  result->stopped = true;
}

bool search_root_move(SearchContext* context, Depth depth, board_core::Move move, Score* best_score,
                      Line* best_line, SearchResult* result) {
  if (should_stop(context)) {
    result->stopped = true;
    return false;
  }

  StackFrame& frame = context->stack[0];
  frame = StackFrame{};
  frame.current_move = move;
  const bool made_delta = board_core::make_move_delta(context->position, move, &frame.delta);
  require_invariant(made_delta);
  board_core::apply_move_delta(&context->position, frame.delta);

  const NodeCount before_nodes = context->stats.nodes;
  const SearchValue child =
      alphabeta(context, kScoreLoss, kScoreWin, static_cast<Depth>(depth - 1), Ply{1});
  board_core::undo_move(&context->position, frame.delta);
  if (child.stopped) {
    result->stopped = true;
    return false;
  }

  const Score score = static_cast<Score>(-child.score);
  Line line{};
  prepend_move(move, child.pv, &line);
  frame.pv = line;

  result->root_moves.push_back(RootMoveInfo{
      .move = move,
      .score = score,
      .bound = BoundType::exact,
      .depth = depth,
      .nodes = context->stats.nodes - before_nodes,
      .pv = line,
      .exact = false,
      .selective = false,
  });

  if (is_better_root_move(score, move, *best_score, result->best_move)) {
    result->best_move = move;
    *best_score = score;
    *best_line = line;
  }
  return true;
}

} // namespace

SearchResult search_fixed_depth_with_hint(board_core::Position position, const Evaluator& evaluator,
                                          Depth depth, MoveOrderingHints root_hints,
                                          SearchLimits limits, SearchOptions options,
                                          TTBestMoveTable* tt,
                                          std::chrono::steady_clock::time_point start_time) {
  const Depth completed_depth = depth < 0 ? Depth{0} : depth;
  limits.max_depth = completed_depth;
  SearchContext context{
      .position = position,
      .evaluator = evaluator,
      .limits = limits,
      .options = options,
      .best_move_table = tt,
      .start_time = start_time,
  };

  SearchResult result{
      .completed_depth = completed_depth,
  };

  if (should_stop(&context)) {
    result.stopped = true;
    result.completed_depth = 0;
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    return result;
  }

  if (board_core::is_terminal(context.position) || completed_depth == 0) {
    const SearchValue root = alphabeta(&context, kScoreLoss, kScoreWin, completed_depth, Ply{0});
    if (root.stopped) {
      result.nodes = context.stats.nodes;
      result.stats = context.stats;
      result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start_time);
      discard_incomplete_result(&result);
      return result;
    }

    result.score = root.score;
    result.nodes = context.stats.nodes;
    result.stats = context.stats;
    result.pv = root.pv;
    result.exact = board_core::is_terminal(context.position);
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    return result;
  }

  context.stats.nodes = 1;
  if (should_stop(&context)) {
    result.stopped = true;
    result.completed_depth = 0;
    result.nodes = context.stats.nodes;
    result.stats = context.stats;
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    return result;
  }

  if (!root_hints.first_move.has_value() && context.best_move_table != nullptr) {
    root_hints.first_move = context.best_move_table->probe(context.position, &context.stats);
  }
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
    const SearchValue child =
        alphabeta(&context, kScoreLoss, kScoreWin, static_cast<Depth>(completed_depth - 1), Ply{1});
    board_core::undo_move(&context.position, root_frame.delta);
    if (child.stopped) {
      result.nodes = context.stats.nodes;
      result.stats = context.stats;
      result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start_time);
      discard_incomplete_result(&result);
      return result;
    }

    result.best_move = board_core::make_pass();
    result.score = static_cast<Score>(-child.score);
    prepend_move(board_core::make_pass(), child.pv, &result.pv);
    root_frame.pv = result.pv;
    result.root_moves.push_back(RootMoveInfo{
        .move = board_core::make_pass(),
        .score = result.score,
        .bound = BoundType::exact,
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
        std::chrono::steady_clock::now() - start_time);
    return result;
  }

  Score best_score = kScoreLoss;
  Line best_line{};
  for (std::uint8_t move_index = 0; move_index < root_moves.size; ++move_index) {
    const board_core::Move move = root_moves.moves[move_index];
    if (!search_root_move(&context, completed_depth, move, &best_score, &best_line, &result)) {
      result.completed_depth = 0;
      break;
    }
    ++context.stats.root_moves_searched;
  }

  if (!result.stopped && context.best_move_table != nullptr && result.best_move.has_value()) {
    context.best_move_table->store(context.position, *result.best_move, &context.stats);
  }

  result.score = result.best_move.has_value() ? best_score : Score{0};
  result.nodes = context.stats.nodes;
  result.stats = context.stats;
  result.pv = best_line;
  result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start_time);
  if (result.stopped) {
    discard_incomplete_result(&result);
  }
  return result;
}

} // namespace internal

SearchResult search_fixed_depth(board_core::Position position, const Evaluator& evaluator,
                                Depth depth) {
  const auto start = std::chrono::steady_clock::now();
  return internal::search_fixed_depth_with_hint(
      position, evaluator, depth, internal::MoveOrderingHints{}, SearchLimits{.max_depth = depth},
      SearchOptions{}, nullptr, start);
}

SearchResult search_iterative(board_core::Position position, const Evaluator& evaluator,
                              SearchLimits limits) {
  return search_iterative(position, evaluator, limits, SearchOptions{});
}

SearchResult search_iterative(board_core::Position position, const Evaluator& evaluator,
                              SearchLimits limits, SearchOptions options) {
  const auto start = std::chrono::steady_clock::now();
  const Depth max_depth = limits.infinite ? static_cast<Depth>(kMaxPly - 1)
                                          : (limits.max_depth < 0 ? Depth{0} : limits.max_depth);

  if (internal::limits_reached(limits, start, NodeCount{0})) {
    SearchResult result{};
    result.stopped = true;
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
  }

  if (!limits.infinite && max_depth == 0) {
    SearchResult result = search_fixed_depth(position, evaluator, Depth{0});
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
  }

  SearchResult best_completed{};
  SearchStats total_stats{};
  internal::TTBestMoveTable tt_storage{};
  internal::TTBestMoveTable* tt = options.use_tt_best_move_ordering ? &tt_storage : nullptr;
  std::optional<board_core::Move> previous_best_move;
  for (Depth depth = 1; depth <= max_depth; ++depth) {
    if (internal::limits_reached(limits, start, total_stats.nodes)) {
      best_completed.stopped = true;
      break;
    }

    SearchResult current = internal::search_fixed_depth_with_hint(
        position, evaluator, depth, internal::MoveOrderingHints{.first_move = previous_best_move},
        limits, options, tt, start);
    internal::add_stats(&total_stats, current.stats);
    if (current.stopped) {
      if (!best_completed.best_move.has_value()) {
        best_completed = current;
      }
      best_completed.stopped = true;
      break;
    }
    previous_best_move = current.best_move;
    best_completed = current;
  }

  best_completed.nodes = total_stats.nodes;
  best_completed.stats = total_stats;
  best_completed.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  return best_completed;
}

} // namespace vibe_othello::search
