#include "search_internal.h"

#include <atomic>
#include <bit>
#include <chrono>
#include <optional>

namespace vibe_othello::search::internal {
namespace {

constexpr NodeCount kEndgameTimeCheckNodeInterval = 256;

bool external_stop_requested(const SearchLimitState& state) noexcept {
  return state.stop_requested != nullptr && state.stop_requested->load(std::memory_order_acquire);
}

bool time_limit_reached(const SearchLimitState& state) {
  return state.has_deadline && std::chrono::steady_clock::now() >= state.deadline;
}

bool should_stop_endgame(EndgameContext* context) {
  if (context == nullptr || context->limit_state == nullptr) {
    return false;
  }

  SearchLimitState* state = context->limit_state;
  if (state->stopped) {
    return true;
  }
  if (external_stop_requested(*state) || time_limit_reached(*state)) {
    state->stopped = true;
  }
  return state->stopped;
}

bool note_endgame_node_visited(EndgameContext* context) {
  if (context == nullptr) {
    return false;
  }

  SearchLimitState* state = context->limit_state;
  if (state != nullptr) {
    if (state->stopped || external_stop_requested(*state)) {
      state->stopped = true;
      return true;
    }
    if (state->max_nodes != 0 && state->nodes >= state->max_nodes) {
      state->stopped = true;
      return true;
    }
  }

  ++context->stats.nodes;
  ++context->stats.endgame_nodes;

  if (state == nullptr) {
    return false;
  }

  ++state->nodes;
  if (state->max_nodes != 0 && state->nodes >= state->max_nodes) {
    state->stopped = true;
    return false;
  }

  if (state->nodes_until_next_time_check == 0) {
    state->nodes_until_next_time_check = kEndgameTimeCheckNodeInterval;
    if (time_limit_reached(*state)) {
      state->stopped = true;
    }
  } else {
    --state->nodes_until_next_time_check;
  }

  return state->stopped;
}

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

bool update_endgame_alpha_and_check_cutoff(EndgameContext* context, Score score, Score* alpha,
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

SearchNodeResult search_endgame_child(EndgameContext* context, board_core::Move move, Score alpha,
                                      Score beta, std::uint8_t empties, Ply ply) {
  StackFrame& frame = context->stack[ply];
  frame.current_move = move;
  const bool made_delta = board_core::make_move_delta(context->position, move, &frame.delta);
  require_invariant(made_delta);
  board_core::apply_move_delta(&context->position, frame.delta);

  const std::uint8_t child_empties =
      move.kind == board_core::MoveKind::pass ? empties : static_cast<std::uint8_t>(empties - 1);
  const SearchNodeResult child =
      exact_score_search(context, static_cast<Score>(-beta), static_cast<Score>(-alpha),
                         child_empties, static_cast<Ply>(ply + 1));
  board_core::undo_move(&context->position, frame.delta);

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

RootMoveInfo make_exact_root_move(board_core::Move move, Score score, Depth depth, NodeCount nodes,
                                  Line pv) noexcept {
  return RootMoveInfo{
      .move = move,
      .score = score,
      .bound = BoundType::exact,
      .depth = depth,
      .nodes = nodes,
      .pv = pv,
      .exact = true,
      .selective = false,
  };
}

void publish_elapsed(SearchResult* result, std::chrono::steady_clock::time_point start) {
  result->elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
}

void mark_stopped_non_exact(SearchResult* result) noexcept {
  result->stopped = true;
  result->exact = false;
}

} // namespace

std::uint8_t empty_count(board_core::Position position) noexcept {
  return static_cast<std::uint8_t>(std::popcount(~board_core::occupied(position)));
}

bool should_use_exact_endgame(board_core::Position position, SearchOptions options) noexcept {
  return options.exact_endgame && empty_count(position) <= options.endgame_exact_empties;
}

SearchNodeResult exact_score_search(EndgameContext* context, Score alpha, Score beta,
                                    std::uint8_t empties, Ply ply) {
  require_invariant(alpha < beta);
  require_invariant(ply < kMaxPly);

  StackFrame& frame = context->stack[ply];
  frame = StackFrame{};

  if (note_endgame_node_visited(context)) {
    return SearchNodeResult::stopped();
  }

  if (board_core::is_terminal(context->position)) {
    ++context->stats.terminal_nodes;
    return SearchNodeResult::completed(SearchValue{
        .score = terminal_score(context->position),
        .pv = {},
    });
  }

  if (should_stop_endgame(context)) {
    return SearchNodeResult::stopped();
  }

  frame.moves = ordered_moves(context->position, MoveOrderingHints{});
  if (frame.moves.size == 0) {
    ++context->stats.pass_nodes;
    const SearchNodeResult pass =
        search_endgame_child(context, board_core::make_pass(), alpha, beta, empties, ply);
    if (pass.is_complete()) {
      frame.pv = pass.value().pv;
    }
    return pass;
  }

  SearchValue best{
      .score = kScoreLoss,
      .pv = {},
  };
  std::optional<board_core::Move> best_move;

  for (std::uint8_t move_index = 0; move_index < frame.moves.size; ++move_index) {
    if (should_stop_endgame(context)) {
      return SearchNodeResult::stopped();
    }

    const board_core::Move move = frame.moves.moves[move_index];
    const SearchNodeResult child = search_endgame_child(context, move, alpha, beta, empties, ply);
    if (child.is_stopped()) {
      return SearchNodeResult::stopped();
    }

    const SearchValue& child_value = child.value();
    update_best_line_and_move(child_value, move, &best, &best_move, &frame);
    if (update_endgame_alpha_and_check_cutoff(context, child_value.score, &alpha, beta)) {
      break;
    }
  }

  return SearchNodeResult::completed(best);
}

SearchResult solve_exact_endgame(board_core::Position position, SearchLimits limits,
                                 SearchOptions options, TranspositionTable* tt,
                                 SearchLimitState* limit_state) {
  const auto start = std::chrono::steady_clock::now();
  SearchLimitState local_limit_state =
      limit_state == nullptr ? initialize_limit_state(limits) : SearchLimitState{};
  SearchLimitState* active_limit_state = limit_state == nullptr ? &local_limit_state : limit_state;
  const std::uint8_t root_empties = empty_count(position);
  const Depth completed_depth = static_cast<Depth>(root_empties);
  EndgameContext context{
      .position = position,
      .limits = limits,
      .options = options,
      .transposition_table = tt,
      .limit_state = active_limit_state,
  };

  SearchResult result{
      .completed_depth = completed_depth,
  };

  if (note_endgame_node_visited(&context)) {
    mark_stopped_non_exact(&result);
    result.nodes = context.stats.nodes;
    result.stats = context.stats;
    publish_elapsed(&result, start);
    return result;
  }

  if (board_core::is_terminal(context.position)) {
    ++context.stats.terminal_nodes;
    result.score = terminal_score(context.position);
    result.nodes = context.stats.nodes;
    result.stats = context.stats;
    result.exact = true;
    publish_elapsed(&result, start);
    return result;
  }

  if (should_stop_endgame(&context)) {
    mark_stopped_non_exact(&result);
    result.nodes = context.stats.nodes;
    result.stats = context.stats;
    publish_elapsed(&result, start);
    return result;
  }

  StackFrame& root_frame = context.stack[0];
  root_frame = StackFrame{};
  root_frame.moves = ordered_moves(context.position, MoveOrderingHints{});
  const MoveList root_moves = root_frame.moves;

  if (root_moves.size == 0) {
    ++context.stats.pass_nodes;
    const NodeCount before_nodes = context.stats.nodes;
    const SearchNodeResult pass = search_endgame_child(&context, board_core::make_pass(),
                                                       kScoreLoss, kScoreWin, root_empties, Ply{0});
    if (pass.is_stopped()) {
      mark_stopped_non_exact(&result);
      result.nodes = context.stats.nodes;
      result.stats = context.stats;
      publish_elapsed(&result, start);
      return result;
    }
    const SearchValue& pass_value = pass.value();

    result.best_move = board_core::make_pass();
    result.score = pass_value.score;
    result.pv = pass_value.pv;
    result.root_moves.push_back(
        make_exact_root_move(board_core::make_pass(), pass_value.score, completed_depth,
                             context.stats.nodes - before_nodes, pass_value.pv));
    context.stats.root_moves_searched = 1;
    result.nodes = context.stats.nodes;
    result.stats = context.stats;
    result.exact = true;
    publish_elapsed(&result, start);
    return result;
  }

  Score best_score = kScoreLoss;
  Line best_line{};
  for (std::uint8_t move_index = 0; move_index < root_moves.size; ++move_index) {
    if (should_stop_endgame(&context)) {
      mark_stopped_non_exact(&result);
      break;
    }

    const board_core::Move move = root_moves.moves[move_index];
    const NodeCount before_nodes = context.stats.nodes;
    const SearchNodeResult child =
        search_endgame_child(&context, move, kScoreLoss, kScoreWin, root_empties, Ply{0});
    if (child.is_stopped()) {
      mark_stopped_non_exact(&result);
      break;
    }
    const SearchValue& child_value = child.value();

    result.root_moves.push_back(make_exact_root_move(move, child_value.score, completed_depth,
                                                     context.stats.nodes - before_nodes,
                                                     child_value.pv));
    ++context.stats.root_moves_searched;
    const bool improves_root =
        is_better_root_move(child_value.score, move, best_score, result.best_move);
    if (improves_root) {
      if (child_value.score > best_score) {
        ++context.stats.alpha_updates;
      }
      result.best_move = move;
      best_score = child_value.score;
      best_line = child_value.pv;
    }
  }

  result.nodes = context.stats.nodes;
  result.stats = context.stats;
  publish_elapsed(&result, start);
  if (result.stopped) {
    if (result.best_move.has_value()) {
      result.score = best_score;
      result.pv = best_line;
    }
    return result;
  }

  result.score = best_score;
  result.pv = best_line;
  result.exact = true;
  return result;
}

} // namespace vibe_othello::search::internal
