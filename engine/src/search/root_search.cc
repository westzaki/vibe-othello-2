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

void search_root_move(SearchContext* context, Depth depth, board_core::Move move,
                      Score* best_score, Line* best_line, SearchResult* result) {
  StackFrame& frame = context->stack[0];
  frame = StackFrame{};
  frame.current_move = move;
  const bool made_delta = board_core::make_move_delta(context->position, move, &frame.delta);
  require_invariant(made_delta);
  board_core::apply_move_delta(&context->position, frame.delta);

  const NodeCount before_nodes = context->stats.nodes;
  const SearchValue child = alphabeta(context, kScoreLoss, kScoreWin,
                                      static_cast<Depth>(depth - 1), Ply{1});
  board_core::undo_move(&context->position, frame.delta);

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
}

} // namespace

SearchResult search_fixed_depth_with_hint(board_core::Position position,
                                          const Evaluator& evaluator, Depth depth,
                                          MoveOrderingHints root_hints, SearchOptions options,
                                          TTBestMoveTable* tt) {
  const auto start = std::chrono::steady_clock::now();
  const Depth completed_depth = depth < 0 ? Depth{0} : depth;
  SearchContext context{
      .position = position,
      .evaluator = evaluator,
      .limits = SearchLimits{.max_depth = completed_depth},
      .options = options,
      .best_move_table = tt,
  };

  SearchResult result{
      .completed_depth = completed_depth,
  };

  if (board_core::is_terminal(context.position) || completed_depth == 0) {
    const SearchValue root = alphabeta(&context, kScoreLoss, kScoreWin, completed_depth, Ply{0});
    result.score = root.score;
    result.nodes = context.stats.nodes;
    result.stats = context.stats;
    result.pv = root.pv;
    result.exact = board_core::is_terminal(context.position);
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
  }

  context.stats.nodes = 1;
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
    const SearchValue child = alphabeta(&context, kScoreLoss, kScoreWin,
                                        static_cast<Depth>(completed_depth - 1), Ply{1});
    board_core::undo_move(&context.position, root_frame.delta);

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
        std::chrono::steady_clock::now() - start);
    return result;
  }

  Score best_score = kScoreLoss;
  Line best_line{};
  for (std::uint8_t move_index = 0; move_index < root_moves.size; ++move_index) {
    const board_core::Move move = root_moves.moves[move_index];
    search_root_move(&context, completed_depth, move, &best_score, &best_line, &result);
    ++context.stats.root_moves_searched;
  }

  if (context.best_move_table != nullptr && result.best_move.has_value()) {
    context.best_move_table->store(context.position, *result.best_move, &context.stats);
  }

  result.score = best_score;
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
  return internal::search_fixed_depth_with_hint(position, evaluator, depth,
                                               internal::MoveOrderingHints{}, SearchOptions{},
                                               nullptr);
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
  internal::TTBestMoveTable tt_storage{};
  internal::TTBestMoveTable* tt = options.use_tt_best_move_ordering ? &tt_storage : nullptr;
  std::optional<board_core::Move> previous_best_move;
  for (Depth depth = 1; depth <= max_depth; ++depth) {
    SearchResult current = internal::search_fixed_depth_with_hint(
        position, evaluator, depth, internal::MoveOrderingHints{.first_move = previous_best_move},
        options, tt);
    internal::add_stats(&total_stats, current.stats);
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
