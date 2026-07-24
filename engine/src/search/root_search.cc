#include "endgame_policy_internal.h"
#include "result_builder_internal.h"
#include "search_internal.h"
#include "search_session_internal.h"

#include <algorithm>
#include <chrono>
#include <optional>

namespace vibe_othello::search {
namespace internal {

namespace {

constexpr Score kAspirationInitialWindow = 8;
constexpr Score kFullScoreRange = kScoreWin - kScoreLoss;

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

RootResultMetadata metadata_from_context(const SearchContext& context,
                                         std::chrono::steady_clock::time_point start) {
  return RootResultMetadata{
      .stats = context.stats,
      .start = start,
  };
}

SearchResult interrupted_depth_zero_result(board_core::Position position, SearchStats stats,
                                           std::chrono::steady_clock::time_point start) {
  SearchResult result{
      .score_kind = ScoreKind::unavailable,
      .completed_depth = 0,
      .nodes = stats.nodes,
      .stats = stats,
      .elapsed = elapsed_since(start),
      .exact = board_core::is_terminal(position),
      .stopped = true,
  };
  if (result.exact) {
    publish_terminal_result(&result, ScoreKind::exact_disc_diff, terminal_score(position), Depth{0},
                            RootResultMetadata{.stats = stats, .start = start});
    result.stopped = true;
  }
  return result;
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

void finish_depth_zero_or_terminal_result(SearchResult* result, const SearchContext& context,
                                          Score root_score, const Line& root_pv,
                                          RootSearchWindow root_window,
                                          std::chrono::steady_clock::time_point start,
                                          bool root_terminal) {
  publish_completed_root_result(
      result,
      CompletedRootResult{
          .best_move = result->best_move,
          .score = root_score,
          .score_kind = root_terminal ? ScoreKind::exact_disc_diff : ScoreKind::heuristic,
          .bound = bound_for_score(root_score, root_window),
          .completed_depth = result->completed_depth,
          .pv = root_pv,
          .exact = root_terminal,
      },
      metadata_from_context(context, start));
}

std::optional<RootMoveInfo> evaluate_root_move(SearchContext* context, Depth depth,
                                               board_core::Move move,
                                               RootSearchWindow move_window) {
  StackFrame& frame = context->stack[0];
  frame.pv.size = 0;
  frame.current_move = move;
  const bool made_delta =
      board_core::make_move_delta(context->position_state.position, move, &frame.delta);
  require_invariant(made_delta);
  apply_move(&context->position_state, frame.delta, &frame.position_undo, &context->stats);

  const NodeCount before_nodes = context->stats.nodes;
  const Score child_alpha = static_cast<Score>(-move_window.beta);
  const Score child_beta = static_cast<Score>(-move_window.alpha);
  const SearchNodeResult child =
      full_window_search(context, child_alpha, child_beta, static_cast<Depth>(depth - 1), Ply{1});
  undo_move(&context->position_state, frame.delta, frame.position_undo, &context->stats);
  if (child.is_stopped()) {
    return std::nullopt;
  }
  const SearchValue& child_value = child.value();

  const Score score = static_cast<Score>(-child_value.score);
  Line line{};
  prepend_move(move, context->stack[1].pv, &line);
  frame.pv = line;

  return make_root_move_info(move, score, ScoreKind::heuristic, bound_for_score(score, move_window),
                             depth, context->stats.nodes - before_nodes, line, false,
                             child.is_selective());
}

std::optional<RootMoveInfo> evaluate_root_move_pvs(SearchContext* context, Depth depth,
                                                   board_core::Move move, Score alpha, Score beta) {
  const NodeCount before_nodes = context->stats.nodes;
  SearchNodeResult child =
      search_null_window_child(context, move, static_cast<Score>(-alpha), depth, Ply{0});
  if (child.is_stopped()) {
    return std::nullopt;
  }
  Score score = child.value().score;
  if (score > alpha && score < beta) {
    ++context->stats.pvs_researches;
    std::optional<RootMoveInfo> result = evaluate_root_move(
        context, depth, move, RootSearchWindow{.alpha = alpha, .beta = beta, .enabled = true});
    if (result.has_value()) {
      result->nodes = context->stats.nodes - before_nodes;
    }
    return result;
  }

  StackFrame& frame = context->stack[0];
  Line line{};
  prepend_move(move, context->stack[1].pv, &line);
  frame.pv = line;
  return make_root_move_info(move, score, ScoreKind::heuristic,
                             classify_bound(score, alpha, static_cast<Score>(alpha + 1)), depth,
                             context->stats.nodes - before_nodes, line, false,
                             child.is_selective());
}

std::optional<RootMoveInfo>
research_preferred_upper_bound_tie(SearchContext* context, Depth depth, RootMoveInfo root_move,
                                   Score best_score, std::optional<board_core::Move> best_move,
                                   Score beta) {
  if (best_score <= kScoreLoss || root_move.bound != BoundType::upper ||
      root_move.score != best_score ||
      !is_better_root_move(root_move.score, root_move.move, best_score, best_move)) {
    return root_move;
  }

  const NodeCount initial_nodes = root_move.nodes;
  std::optional<RootMoveInfo> researched =
      evaluate_root_move(context, depth, root_move.move,
                         RootSearchWindow{
                             .alpha = static_cast<Score>(best_score - 1),
                             .beta = beta,
                             .enabled = true,
                         });
  if (researched.has_value()) {
    researched->nodes += initial_nodes;
  }
  return researched;
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
    const std::optional<RootMoveInfo> exact_root_move =
        evaluate_root_move(context, depth, root_move.move, RootSearchWindow{});
    if (!exact_root_move.has_value()) {
      result->stopped = true;
      return;
    }
    root_move = *exact_root_move;
    updated = true;
  }
  if (updated) {
    recompute_best_root_move(result, best_score, best_line);
  }
}

void publish_root_move(const RootMoveInfo& root_move, SearchContext* context, Score* best_score,
                       Line* best_line, SearchResult* result) {
  result->root_moves.push_back(root_move);
  ++context->stats.root_moves_searched;
  update_best_root_move(result->root_moves.back(), best_score, best_line, result);
}

bool should_complete_exact_root_reports(ResolvedSearchOptions options) noexcept {
  return options.reporting.multi_pv != 1;
}

void maybe_store_root_midgame_tt(SearchContext* context, Depth completed_depth, Score best_score,
                                 RootSearchWindow root_window,
                                 std::optional<board_core::Move> best_move,
                                 bool subtree_selective) noexcept {
  if (context->transposition_table == nullptr || !best_move.has_value()) {
    return;
  }
  if (subtree_selective) {
    return;
  }
  const BoundType bound = bound_for_score(best_score, root_window);
  context->transposition_table->store(context->position_state.key, completed_depth, best_score,
                                      bound, *best_move, TTEntryKind::midgame, &context->stats);
}

void finalize_completed_root_result(SearchResult* result, const SearchContext& context,
                                    Score best_score, Line best_line, RootSearchWindow root_window,
                                    std::chrono::steady_clock::time_point start) {
  publish_completed_root_result(result,
                                CompletedRootResult{
                                    .best_move = result->best_move,
                                    .score = best_score,
                                    .score_kind = ScoreKind::heuristic,
                                    .bound = bound_for_score(best_score, root_window),
                                    .completed_depth = result->completed_depth,
                                    .pv = best_line,
                                    .exact = false,
                                },
                                metadata_from_context(context, start));
}

SearchResult search_depth_with_aspiration(
    board_core::Position position, const Evaluator& evaluator, Depth depth,
    MoveOrderingHints root_hints, ResolvedSearchOptions options, TranspositionTable* tt,
    Score previous_score, MidgameOrderingState* ordering_state, SearchLimitState* limit_state,
    std::uint32_t incremental_eval_verify_interval, ShadowCalibrationRun* shadow_calibration) {
  SearchStats depth_stats{};
  Score low_margin = kAspirationInitialWindow;
  Score high_margin = kAspirationInitialWindow;
  RootSearchWindow window = aspiration_window(previous_score, low_margin, high_margin);

  SearchResult current{};
  for (;;) {
    current = search_fixed_depth_with_hint(position, evaluator, depth, root_hints, options, tt,
                                           window, ordering_state, limit_state,
                                           incremental_eval_verify_interval, shadow_calibration);
    add_stats(&depth_stats, current.stats);
    if (current.stopped) {
      current.nodes = depth_stats.nodes;
      current.stats = depth_stats;
      return current;
    }

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
                                          RootSearchWindow root_window,
                                          MidgameOrderingState* ordering_state,
                                          SearchLimitState* limit_state,
                                          std::uint32_t incremental_eval_verify_interval,
                                          ShadowCalibrationRun* shadow_calibration) {
  return search_fixed_depth_with_hint(
      position, evaluator, depth, root_hints, normalize_search_options(options), tt, root_window,
      ordering_state, limit_state, incremental_eval_verify_interval, shadow_calibration);
}

SearchResult search_fixed_depth_with_hint(board_core::Position position, const Evaluator& evaluator,
                                          Depth depth, MoveOrderingHints root_hints,
                                          ResolvedSearchOptions options, TranspositionTable* tt,
                                          RootSearchWindow root_window,
                                          MidgameOrderingState* ordering_state,
                                          SearchLimitState* limit_state,
                                          std::uint32_t incremental_eval_verify_interval,
                                          ShadowCalibrationRun* shadow_calibration) {
  require_invariant(!root_window.enabled || root_window.alpha < root_window.beta);
  const auto start = std::chrono::steady_clock::now();
  const Depth completed_depth = depth < 0 ? Depth{0} : depth;
  MidgameOrderingState local_ordering_state{};
  SearchContext context{
      .position_state = make_search_position(position, &evaluator, completed_depth),
      .evaluator = evaluator,
      .limits = SearchLimits{.max_depth = completed_depth},
      .options = options,
      .transposition_table = tt,
      .ordering_state = ordering_state == nullptr ? &local_ordering_state : ordering_state,
      .limit_state = limit_state,
      .shadow_calibration = shadow_calibration,
      .incremental_eval_verify_interval = incremental_eval_verify_interval,
  };
  if (context.position_state.evaluation_state.has_value()) {
    context.stats.incremental_eval_enabled = true;
    ++context.stats.incremental_state_initializations;
  }

  SearchResult result{
      .completed_depth = completed_depth,
  };

  const board_core::Bitboard root_legal_moves = legal_moves(&context.position_state);
  const bool root_terminal =
      root_legal_moves == 0 && opponent_legal_moves(context.position_state) == 0;
  if (root_terminal || completed_depth == 0) {
    const Score alpha = root_window.enabled ? root_window.alpha : kScoreLoss;
    const Score beta = root_window.enabled ? root_window.beta : kScoreWin;
    const SearchNodeResult root =
        full_window_search(&context, alpha, beta, completed_depth, Ply{0});
    if (root.is_stopped()) {
      if (root_terminal) {
        publish_terminal_result(&result, ScoreKind::exact_disc_diff,
                                terminal_score(context.position_state.position), completed_depth,
                                metadata_from_context(context, start));
        result.stopped = true;
      } else {
        publish_stopped_result(&result, StoppedRootResult{}, metadata_from_context(context, start));
      }
      return result;
    }
    finish_depth_zero_or_terminal_result(&result, context, root.value().score, context.stack[0].pv,
                                         root_window, start, root_terminal);
    return result;
  }

  if (note_node_visited(&context)) {
    publish_stopped_result(&result, StoppedRootResult{}, metadata_from_context(context, start));
    return result;
  }
  if (context.options.ordering.use_tt_best_move_ordering && !root_hints.tt_best_move.has_value() &&
      context.transposition_table != nullptr) {
    const std::optional<TTEntry> root_tt_entry = context.transposition_table->probe(
        context.position_state.key, TTEntryKind::midgame, &context.stats);
    if (root_tt_entry.has_value() && root_tt_entry->kind == TTEntryKind::midgame &&
        root_tt_entry->has_best_move) {
      root_hints.tt_best_move = root_tt_entry->best_move;
    }
  }
  root_hints.use_opponent_mobility = true;
  if (context.ordering_state != nullptr && context.options.ordering.use_killers) {
    root_hints.killer_moves = context.ordering_state->killer_moves[0];
  }
  if (context.ordering_state != nullptr && context.options.ordering.use_history) {
    root_hints.history = &context.ordering_state->history;
  }
  StackFrame& root_frame = context.stack[0];
  root_frame.pv.size = 0;
  root_frame.legal_moves = root_legal_moves;
  root_frame.moves =
      order_midgame_moves(context.position_state.position, root_legal_moves, root_hints);
  const MoveList& root_moves = root_frame.moves;
  if (root_moves.size == 0) {
    if (should_stop_search(&context)) {
      publish_stopped_result(&result, StoppedRootResult{}, metadata_from_context(context, start));
      return result;
    }
    ++context.stats.pass_nodes;
    root_frame.current_move = board_core::make_pass();
    root_frame.delta = board_core::MoveDelta{.move = board_core::make_pass(), .flipped = 0};
    apply_move(&context.position_state, root_frame.delta, &root_frame.position_undo,
               &context.stats);

    const NodeCount before_nodes = context.stats.nodes;
    const Score alpha = root_window.enabled ? root_window.alpha : kScoreLoss;
    const Score beta = root_window.enabled ? root_window.beta : kScoreWin;
    const SearchNodeResult child = full_window_search(
        &context, static_cast<Score>(-beta), static_cast<Score>(-alpha),
        context.options.midgame.pass_consumes_depth ? static_cast<Depth>(completed_depth - 1)
                                                    : completed_depth,
        Ply{1});
    undo_move(&context.position_state, root_frame.delta, root_frame.position_undo, &context.stats);
    if (child.is_stopped()) {
      publish_stopped_result(&result, StoppedRootResult{}, metadata_from_context(context, start));
      return result;
    }
    const SearchValue& child_value = child.value();

    const Score pass_score = static_cast<Score>(-child_value.score);
    Line pass_line{};
    prepend_move(board_core::make_pass(), context.stack[1].pv, &pass_line);
    root_frame.pv = pass_line;
    result.root_moves.push_back(make_root_move_info(
        board_core::make_pass(), pass_score, ScoreKind::heuristic,
        bound_for_score(pass_score, root_window), completed_depth,
        context.stats.nodes - before_nodes, pass_line, false, child.is_selective()));
    context.stats.root_moves_searched = 1;

    publish_completed_root_result(&result,
                                  CompletedRootResult{
                                      .best_move = board_core::make_pass(),
                                      .score = pass_score,
                                      .score_kind = ScoreKind::heuristic,
                                      .bound = bound_for_score(pass_score, root_window),
                                      .completed_depth = completed_depth,
                                      .pv = pass_line,
                                      .exact = false,
                                  },
                                  metadata_from_context(context, start));
    return result;
  }

  Score best_score = kScoreLoss;
  Line best_line{};
  Score alpha = root_window.enabled ? root_window.alpha : kScoreLoss;
  const Score beta = root_window.enabled ? root_window.beta : kScoreWin;
  for (std::uint8_t move_index = 0; move_index < root_moves.size; ++move_index) {
    if (should_stop_search(&context)) {
      result.stopped = true;
      break;
    }
    const board_core::Move move = root_moves.moves[move_index];
    const RootSearchWindow move_window{.alpha = alpha, .beta = beta, .enabled = true};
    Score pvs_alpha = alpha;
    if (result.best_move.has_value() && move.kind == board_core::MoveKind::normal &&
        result.best_move->kind == board_core::MoveKind::normal &&
        move.square.index < result.best_move->square.index && best_score > kScoreLoss) {
      pvs_alpha = static_cast<Score>(best_score - 1);
    }
    std::optional<RootMoveInfo> root_move =
        context.options.midgame.use_pvs && move_index != 0
            ? evaluate_root_move_pvs(&context, completed_depth, move, pvs_alpha, beta)
            : evaluate_root_move(&context, completed_depth, move, move_window);
    if (!context.options.midgame.use_pvs && root_move.has_value()) {
      root_move = research_preferred_upper_bound_tie(&context, completed_depth, *root_move,
                                                     best_score, result.best_move, beta);
    }
    if (!root_move.has_value()) {
      result.stopped = true;
      break;
    }
    const Score score = root_move->score;
    publish_root_move(*root_move, &context, &best_score, &best_line, &result);
    if (score > alpha) {
      ++context.stats.alpha_updates;
      alpha = score;
    }
    if (alpha >= beta) {
      ++context.stats.beta_cutoffs;
      update_midgame_ordering_on_beta_cutoff(&context, move, completed_depth, Ply{0});
      break;
    }
  }

  if (result.stopped) {
    publish_stopped_result(
        &result,
        StoppedRootResult{
            .best_move = result.best_move,
            .score = best_score,
            .score_kind = ScoreKind::heuristic,
            .bound = BoundType::lower,
            .completed_depth = result.best_move.has_value() ? completed_depth : Depth{0},
            .pv = best_line,
            .has_completed_score = result.best_move.has_value(),
        },
        metadata_from_context(context, start));
    return result;
  }

  if (should_complete_exact_root_reports(context.options)) {
    complete_exact_root_move_reports(&context, completed_depth, &result, &best_score, &best_line);
  }

  if (result.stopped) {
    publish_stopped_result(
        &result,
        StoppedRootResult{
            .best_move = result.best_move,
            .score = best_score,
            .score_kind = ScoreKind::heuristic,
            .bound = BoundType::lower,
            .completed_depth = result.best_move.has_value() ? completed_depth : Depth{0},
            .pv = best_line,
            .has_completed_score = result.best_move.has_value(),
        },
        metadata_from_context(context, start));
    return result;
  }

  const bool root_selective = std::any_of(result.root_moves.begin(), result.root_moves.end(),
                                          [](const RootMoveInfo& move) { return move.selective; });
  maybe_store_root_midgame_tt(&context, completed_depth, best_score, root_window, result.best_move,
                              root_selective);

  finalize_completed_root_result(&result, context, best_score, best_line, root_window, start);
  return result;
}

} // namespace internal

SearchResult search_fixed_depth(board_core::Position position, const Evaluator& evaluator,
                                Depth depth) {
  return internal::search_fixed_depth_with_hint(
      position, evaluator, depth, internal::MoveOrderingHints{}, SearchOptions{}, nullptr);
}

SearchResult search_fixed_depth(SearchSession& session, board_core::Position position,
                                const Evaluator& evaluator, Depth depth, SearchOptions options) {
  const internal::ResolvedSearchOptions resolved = internal::normalize_search_options(options);
  internal::SearchSessionAccess::begin_root(
      session, internal::make_search_semantic_fingerprint(&evaluator, resolved,
                                                          internal::SearchSemanticDomain::midgame));
  internal::TranspositionTable* tt =
      (resolved.midgame.use_midgame_tt || resolved.ordering.use_tt_best_move_ordering ||
       resolved.endgame.use_endgame_tt)
          ? internal::SearchSessionAccess::transposition_table(session)
          : nullptr;
  internal::SearchLimitState limit_state =
      internal::initialize_limit_state(SearchLimits{.max_depth = depth});
  std::optional<internal::ShadowCalibrationRun> shadow_calibration =
      internal::make_shadow_calibration_run(position, resolved.selective);
  SearchResult result = internal::search_fixed_depth_with_hint(
      position, evaluator, depth, internal::MoveOrderingHints{}, resolved, tt,
      internal::RootSearchWindow{}, internal::SearchSessionAccess::ordering_state(session),
      &limit_state, internal::SearchSessionAccess::incremental_eval_verify_interval(session),
      shadow_calibration.has_value() ? &*shadow_calibration : nullptr);
  if (shadow_calibration.has_value()) {
    result.shadow_calibration = shadow_calibration->stats;
  }
  return result;
}

SearchResult search_iterative(board_core::Position position, const Evaluator& evaluator,
                              SearchLimits limits) {
  return search_iterative(position, evaluator, limits, SearchOptions{});
}

SearchResult search_iterative(board_core::Position position, const Evaluator& evaluator,
                              SearchLimits limits, SearchOptions options) {
  SearchSession session;
  return search_iterative(session, position, evaluator, limits, options);
}

SearchResult search_iterative(SearchSession& session, board_core::Position position,
                              const Evaluator& evaluator, SearchLimits limits,
                              SearchOptions options) {
  const auto start = std::chrono::steady_clock::now();
  const Depth max_depth = limits.max_depth < 0 ? Depth{0} : limits.max_depth;
  const internal::ResolvedSearchOptions resolved_options =
      internal::normalize_search_options(options);
  std::optional<internal::ShadowCalibrationRun> shadow_calibration =
      internal::make_shadow_calibration_run(position, resolved_options.selective);
  internal::ShadowCalibrationRun* shadow_run =
      shadow_calibration.has_value() ? &*shadow_calibration : nullptr;
  internal::SearchLimitState limit_state = internal::initialize_limit_state(limits);
  SearchStats total_stats{};
  const bool use_wld_endgame = internal::should_use_wld_endgame(position, resolved_options);
  const bool use_exact_endgame = internal::should_use_exact_endgame(position, resolved_options);
  const internal::SearchSemanticDomain semantic_domain =
      use_wld_endgame     ? internal::SearchSemanticDomain::wld_endgame
      : use_exact_endgame ? internal::SearchSemanticDomain::exact_endgame
                          : internal::SearchSemanticDomain::midgame;
  const Evaluator* semantic_evaluator =
      semantic_domain == internal::SearchSemanticDomain::midgame ? &evaluator : nullptr;
  internal::SearchSessionAccess::begin_root(
      session, internal::make_search_semantic_fingerprint(semantic_evaluator, resolved_options,
                                                          semantic_domain));
  internal::TranspositionTable* session_tt =
      internal::SearchSessionAccess::transposition_table(session);
  const std::uint32_t incremental_eval_verify_interval =
      internal::SearchSessionAccess::incremental_eval_verify_interval(session);

  if (use_wld_endgame) {
    internal::TranspositionTable* tt =
        resolved_options.endgame.use_endgame_tt ? session_tt : nullptr;
    SearchResult result = internal::solve_wld_endgame(position, limits, options, tt, &limit_state);
    if (shadow_calibration.has_value()) {
      result.shadow_calibration = shadow_calibration->stats;
    }
    return result;
  }

  if (use_exact_endgame) {
    internal::TranspositionTable* tt =
        resolved_options.endgame.use_endgame_tt ? session_tt : nullptr;
    SearchResult result =
        internal::solve_exact_endgame(position, limits, options, tt, &limit_state);
    if (shadow_calibration.has_value()) {
      result.shadow_calibration = shadow_calibration->stats;
    }
    return result;
  }

  if (!limits.infinite && max_depth == 0) {
    SearchResult result = internal::search_fixed_depth_with_hint(
        position, evaluator, Depth{0}, internal::MoveOrderingHints{}, resolved_options, nullptr,
        internal::RootSearchWindow{}, nullptr, &limit_state, incremental_eval_verify_interval,
        shadow_run);
    if (shadow_calibration.has_value()) {
      result.shadow_calibration = shadow_calibration->stats;
    }
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
  }

  SearchResult best_completed{
      .score_kind =
          board_core::is_terminal(position) ? ScoreKind::exact_disc_diff : ScoreKind::unavailable,
      .completed_depth = 0,
      .exact = board_core::is_terminal(position),
  };
  if (best_completed.exact) {
    best_completed.score = internal::terminal_score(position);
  }
  internal::TranspositionTable* tt = (resolved_options.ordering.use_tt_best_move_ordering ||
                                      resolved_options.midgame.use_midgame_tt)
                                         ? session_tt
                                         : nullptr;
  internal::MidgameOrderingState* ordering_state =
      internal::SearchSessionAccess::ordering_state(session);
  std::optional<board_core::Move> previous_best_move;
  std::optional<Score> previous_score;

  for (Depth depth = 1;; ++depth) {
    if (!limits.infinite && depth > max_depth) {
      break;
    }
    if (depth >= static_cast<Depth>(kMaxPly)) {
      break;
    }
    if (limit_state.stopped) {
      best_completed.stopped = true;
      break;
    }
    internal::SearchContext limit_check_context{
        .position_state = internal::make_search_position(position),
        .evaluator = evaluator,
        .limit_state = &limit_state,
    };
    if (internal::should_stop_search(&limit_check_context)) {
      best_completed.stopped = true;
      break;
    }

    const internal::MoveOrderingHints root_hints{.root_best_move = previous_best_move};
    SearchResult current =
        resolved_options.midgame.use_aspiration && depth >= 2 && previous_score.has_value() &&
                !internal::search_horizon_reaches_internal_exact_endgame(position, depth,
                                                                         resolved_options)
            ? internal::search_depth_with_aspiration(
                  position, evaluator, depth, root_hints, resolved_options, tt, *previous_score,
                  ordering_state, &limit_state, incremental_eval_verify_interval, shadow_run)
            : internal::search_fixed_depth_with_hint(
                  position, evaluator, depth, root_hints, resolved_options, tt,
                  internal::RootSearchWindow{}, ordering_state, &limit_state,
                  incremental_eval_verify_interval, shadow_run);
    internal::add_stats(&total_stats, current.stats);
    if (current.stopped) {
      if (best_completed.completed_depth == 0 && !best_completed.exact) {
        best_completed = internal::interrupted_depth_zero_result(position, total_stats, start);
      } else {
        best_completed.stopped = true;
      }
      break;
    }
    previous_best_move = current.best_move;
    previous_score = current.score;
    best_completed = current;
  }

  best_completed.nodes = total_stats.nodes;
  best_completed.stats = total_stats;
  if (shadow_calibration.has_value()) {
    best_completed.shadow_calibration = shadow_calibration->stats;
  }
  best_completed.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  return best_completed;
}

SearchResult solve_exact_endgame(board_core::Position position, SearchLimits limits,
                                 SearchOptions options) {
  SearchSession session;
  return solve_exact_endgame(session, position, limits, options);
}

SearchResult solve_exact_endgame(SearchSession& session, board_core::Position position,
                                 SearchLimits limits, SearchOptions options) {
  options.endgame.exact_endgame = true;
  const internal::ResolvedSearchOptions resolved_options =
      internal::normalize_search_options(options);
  internal::SearchLimitState limit_state = internal::initialize_limit_state(limits);
  internal::SearchSessionAccess::begin_root(
      session, internal::make_search_semantic_fingerprint(
                   nullptr, resolved_options, internal::SearchSemanticDomain::exact_endgame));
  internal::TranspositionTable* tt =
      resolved_options.endgame.use_endgame_tt
          ? internal::SearchSessionAccess::transposition_table(session)
          : nullptr;
  return internal::solve_exact_endgame(position, limits, options, tt, &limit_state);
}

SearchResult solve_wld_endgame(board_core::Position position, SearchLimits limits,
                               SearchOptions options) {
  SearchSession session;
  return solve_wld_endgame(session, position, limits, options);
}

SearchResult solve_wld_endgame(SearchSession& session, board_core::Position position,
                               SearchLimits limits, SearchOptions options) {
  options.endgame.exact_endgame = true;
  const internal::ResolvedSearchOptions resolved_options =
      internal::normalize_search_options(options);
  internal::SearchLimitState limit_state = internal::initialize_limit_state(limits);
  internal::SearchSessionAccess::begin_root(
      session, internal::make_search_semantic_fingerprint(
                   nullptr, resolved_options, internal::SearchSemanticDomain::wld_endgame));
  internal::TranspositionTable* tt =
      resolved_options.endgame.use_endgame_tt
          ? internal::SearchSessionAccess::transposition_table(session)
          : nullptr;
  return internal::solve_wld_endgame(position, limits, options, tt, &limit_state);
}

} // namespace vibe_othello::search
