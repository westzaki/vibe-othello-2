#include "endgame_policy_internal.h"
#include "result_builder_internal.h"

#include <chrono>
#include <optional>

namespace vibe_othello::search::internal {
namespace {

RootResultMetadata metadata_from_context(const EndgameContext& context,
                                         std::chrono::steady_clock::time_point start) {
  return RootResultMetadata{
      .stats = context.stats,
      .start = start,
  };
}

bool use_best_only_root_reporting(ResolvedSearchOptions options) noexcept {
  return options.reporting.multi_pv == 1;
}

Score best_only_root_alpha(board_core::Move move, Score best_score,
                           std::optional<board_core::Move> best_move) noexcept {
  if (!best_move.has_value()) {
    return kScoreLoss;
  }
  if (move.kind == board_core::MoveKind::normal &&
      best_move->kind == board_core::MoveKind::normal &&
      move.square.index < best_move->square.index && best_score > kScoreLoss) {
    return static_cast<Score>(best_score - 1);
  }
  return best_score;
}

void publish_best_only_root_move(SearchResult* result, RootMoveInfo root_move) {
  result->root_moves.clear();
  result->root_moves.push_back(root_move);
}

template <typename EndgamePolicy>
Score root_alpha_with_policy(bool best_only_reporting, board_core::Move move, Score best_score,
                             std::optional<board_core::Move> best_move) noexcept {
  if (!best_only_reporting) {
    return EndgamePolicy::kInitialAlpha;
  }
  if constexpr (EndgamePolicy::kUseInitialAlphaBeforeBestOnlyMove) {
    if (!best_move.has_value()) {
      return EndgamePolicy::kInitialAlpha;
    }
  }
  return best_only_root_alpha(move, best_score, best_move);
}

template <typename EndgamePolicy>
std::optional<board_core::Move> probe_root_tt_best_move_with_policy(EndgameContext* context,
                                                                    Depth completed_depth) {
  if constexpr (EndgamePolicy::kUsesSmallEmpty) {
    return probe_exact_score_endgame_root_tt_best_move(context, completed_depth);
  }
  return probe_wld_endgame_root_tt_best_move(context, completed_depth);
}

template <typename EndgamePolicy>
MoveList root_endgame_moves_with_policy(EndgameContext* context, Depth completed_depth,
                                        std::uint8_t root_empties,
                                        SmallEndgamePolicy small_endgame_policy) {
  if constexpr (EndgamePolicy::kUsesSmallEmpty) {
    if (small_endgame_policy == SmallEndgamePolicy::enabled && root_empties == 1) {
      MoveList root_moves{};
      const board_core::Bitboard legal_moves = board_core::legal_moves(context->position);
      if (legal_moves != 0) {
        root_moves.moves[0] = first_legal_move(legal_moves);
        root_moves.size = 1;
      }
      return root_moves;
    }

    std::optional<board_core::Move> root_tt_best_move;
    if (!(small_endgame_policy == SmallEndgamePolicy::enabled && root_empties <= 4)) {
      root_tt_best_move =
          probe_root_tt_best_move_with_policy<EndgamePolicy>(context, completed_depth);
    }
    return small_endgame_policy == SmallEndgamePolicy::enabled && root_empties <= 4
               ? small_empty_move_list(context->position)
               : order_endgame_moves(
                     context->position,
                     EndgameOrderingHints{
                         .tt_best_move = root_tt_best_move,
                         .use_parity_ordering =
                             context->options.ordering.use_endgame_parity_ordering});
  }

  const std::optional<board_core::Move> root_tt_best_move =
      probe_root_tt_best_move_with_policy<EndgamePolicy>(context, completed_depth);
  return order_endgame_moves(
      context->position,
      EndgameOrderingHints{
          .tt_best_move = root_tt_best_move,
          .use_parity_ordering = context->options.ordering.use_endgame_parity_ordering,
      });
}

template <typename EndgamePolicy>
SearchNodeResult search_root_child_with_policy(EndgameContext* context, board_core::Move move,
                                               Score alpha, Score beta, std::uint8_t empties,
                                               Ply ply, SmallEndgamePolicy small_endgame_policy) {
  if constexpr (EndgamePolicy::kUsesSmallEmpty) {
    return search_exact_score_endgame_child(context, move, alpha, beta, empties, ply,
                                            small_endgame_policy);
  }
  return search_wld_endgame_child(context, move, alpha, beta, empties, ply);
}

template <typename EndgamePolicy>
SearchResult solve_root_endgame_with_policy(board_core::Position position, SearchLimits limits,
                                            SearchOptions options, TranspositionTable* tt,
                                            SmallEndgamePolicy small_endgame_policy,
                                            SearchLimitState* limit_state) {
  const auto start = std::chrono::steady_clock::now();
  const ResolvedSearchOptions resolved_options = normalize_search_options(options);
  SearchLimitState local_limit_state =
      limit_state == nullptr ? initialize_limit_state(limits) : SearchLimitState{};
  SearchLimitState* active_limit_state = limit_state == nullptr ? &local_limit_state : limit_state;
  const std::uint8_t root_empties = empty_count(position);
  const Depth completed_depth = static_cast<Depth>(root_empties);
  EndgameContext context{
      .position = position,
      .limits = limits,
      .options = resolved_options,
      .transposition_table = tt,
      .limit_state = active_limit_state,
  };

  SearchResult result{
      .score_kind = EndgamePolicy::kScoreKind,
      .completed_depth = 0,
  };

  if (note_endgame_node_visited(&context)) {
    publish_stopped_result(&result, StoppedRootResult{}, metadata_from_context(context, start));
    return result;
  }

  if (board_core::is_terminal(context.position)) {
    ++context.stats.terminal_nodes;
    const Score terminal = EndgamePolicy::terminal_score(context.position);
    if (should_stop_endgame(&context)) {
      publish_stopped_result(&result,
                             StoppedRootResult{
                                 .score = terminal,
                                 .score_kind = EndgamePolicy::kScoreKind,
                                 .bound = BoundType::lower,
                                 .completed_depth = completed_depth,
                                 .has_completed_score = true,
                             },
                             metadata_from_context(context, start));
    } else {
      publish_terminal_result(&result, EndgamePolicy::kScoreKind, terminal, completed_depth,
                              metadata_from_context(context, start));
    }
    return result;
  }

  if (should_stop_endgame(&context)) {
    publish_stopped_result(&result, StoppedRootResult{}, metadata_from_context(context, start));
    return result;
  }

  StackFrame& root_frame = context.stack[0];
  root_frame = StackFrame{};
  root_frame.moves = root_endgame_moves_with_policy<EndgamePolicy>(
      &context, completed_depth, root_empties, small_endgame_policy);
  const MoveList root_moves = root_frame.moves;

  if (root_moves.size == 0) {
    ++context.stats.pass_nodes;
    const NodeCount before_nodes = context.stats.nodes;
    const SearchNodeResult pass = search_root_child_with_policy<EndgamePolicy>(
        &context, board_core::make_pass(), EndgamePolicy::kInitialAlpha,
        EndgamePolicy::kInitialBeta, root_empties, Ply{0}, small_endgame_policy);
    if (pass.is_stopped()) {
      publish_stopped_result(&result, StoppedRootResult{}, metadata_from_context(context, start));
      return result;
    }
    const SearchValue& pass_value = pass.value();

    result.root_moves.push_back(make_root_move_info(
        board_core::make_pass(), pass_value.score, EndgamePolicy::kScoreKind, BoundType::exact,
        completed_depth, context.stats.nodes - before_nodes, pass_value.pv, true, false));
    context.stats.root_moves_searched = 1;
    if (should_stop_endgame(&context)) {
      publish_stopped_result(&result,
                             StoppedRootResult{
                                 .best_move = board_core::make_pass(),
                                 .score = pass_value.score,
                                 .score_kind = EndgamePolicy::kScoreKind,
                                 .bound = BoundType::lower,
                                 .completed_depth = completed_depth,
                                 .pv = pass_value.pv,
                                 .has_completed_score = true,
                             },
                             metadata_from_context(context, start));
    } else {
      publish_completed_root_result(&result,
                                    CompletedRootResult{
                                        .best_move = board_core::make_pass(),
                                        .score = pass_value.score,
                                        .score_kind = EndgamePolicy::kScoreKind,
                                        .bound = BoundType::exact,
                                        .completed_depth = completed_depth,
                                        .pv = pass_value.pv,
                                        .exact = true,
                                    },
                                    metadata_from_context(context, start));
    }
    return result;
  }

  Score best_score = EndgamePolicy::kWorstScore;
  Line best_line{};
  std::optional<RootMoveInfo> best_root_move;
  const bool best_only_reporting = use_best_only_root_reporting(context.options);
  for (std::uint8_t move_index = 0; move_index < root_moves.size; ++move_index) {
    if (should_stop_endgame(&context)) {
      result.stopped = true;
      break;
    }

    const board_core::Move move = root_moves.moves[move_index];
    const NodeCount before_nodes = context.stats.nodes;
    const Score root_alpha = root_alpha_with_policy<EndgamePolicy>(best_only_reporting, move,
                                                                   best_score, result.best_move);
    const SearchNodeResult child = search_root_child_with_policy<EndgamePolicy>(
        &context, move, root_alpha, EndgamePolicy::kInitialBeta, root_empties, Ply{0},
        small_endgame_policy);
    if (child.is_stopped()) {
      result.stopped = true;
      break;
    }
    const SearchValue& child_value = child.value();

    RootMoveInfo root_move = make_root_move_info(
        move, child_value.score, EndgamePolicy::kScoreKind, BoundType::exact, completed_depth,
        context.stats.nodes - before_nodes, child_value.pv, true, false);
    ++context.stats.root_moves_searched;
    const bool improves_root =
        is_better_root_move(child_value.score, move, best_score, result.best_move);
    if (!best_only_reporting) {
      result.root_moves.push_back(root_move);
    }
    if (improves_root) {
      if (child_value.score > best_score) {
        ++context.stats.alpha_updates;
      }
      result.best_move = move;
      best_score = child_value.score;
      best_line = child_value.pv;
      best_root_move = root_move;
      if (best_only_reporting) {
        publish_best_only_root_move(&result, root_move);
      }
    }
  }

  if (should_stop_endgame(&context)) {
    result.stopped = true;
  }
  if (result.stopped) {
    if (best_only_reporting && best_root_move.has_value()) {
      publish_best_only_root_move(&result, *best_root_move);
    }
    publish_stopped_result(
        &result,
        StoppedRootResult{
            .best_move = result.best_move,
            .score = best_score,
            .score_kind = EndgamePolicy::kScoreKind,
            .bound = BoundType::lower,
            .completed_depth = result.best_move.has_value() ? completed_depth : Depth{0},
            .pv = best_line,
            .has_completed_score = result.best_move.has_value(),
        },
        metadata_from_context(context, start));
    return result;
  }

  publish_completed_root_result(&result,
                                CompletedRootResult{
                                    .best_move = result.best_move,
                                    .score = best_score,
                                    .score_kind = EndgamePolicy::kScoreKind,
                                    .bound = BoundType::exact,
                                    .completed_depth = completed_depth,
                                    .pv = best_line,
                                    .exact = true,
                                },
                                metadata_from_context(context, start));
  return result;
}

} // namespace

bool should_use_exact_endgame(board_core::Position position,
                              ResolvedSearchOptions options) noexcept {
  return options.endgame.exact_endgame && options.mode != SearchMode::win_loss_draw &&
         empty_count(position) <= options.endgame.endgame_exact_empties;
}

bool should_use_wld_endgame(board_core::Position position, ResolvedSearchOptions options) noexcept {
  return options.mode == SearchMode::win_loss_draw && options.endgame.endgame_wld_empties > 0 &&
         empty_count(position) <= options.endgame.endgame_wld_empties;
}

SearchResult solve_exact_endgame(board_core::Position position, SearchLimits limits,
                                 SearchOptions options, TranspositionTable* tt,
                                 SearchLimitState* limit_state) {
  return solve_exact_endgame_with_small_endgame_policy(position, limits, options, tt,
                                                       SmallEndgamePolicy::enabled, limit_state);
}

SearchResult solve_wld_endgame(board_core::Position position, SearchLimits limits,
                               SearchOptions options, TranspositionTable* tt,
                               SearchLimitState* limit_state) {
  return solve_root_endgame_with_policy<WldEndgamePolicy>(
      position, limits, options, tt, SmallEndgamePolicy::generic_only, limit_state);
}

SearchResult solve_exact_endgame_with_small_endgame_policy(board_core::Position position,
                                                           SearchLimits limits,
                                                           SearchOptions options,
                                                           TranspositionTable* tt,
                                                           SmallEndgamePolicy small_endgame_policy,
                                                           SearchLimitState* limit_state) {
  return solve_root_endgame_with_policy<ExactScoreEndgamePolicy>(position, limits, options, tt,
                                                                 small_endgame_policy, limit_state);
}

} // namespace vibe_othello::search::internal
