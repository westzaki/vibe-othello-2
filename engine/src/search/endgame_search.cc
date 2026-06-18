#include "endgame_policy_internal.h"

#include <bit>
#include <optional>

namespace vibe_othello::search::internal {
namespace {

template <typename EndgamePolicy>
ExactEndgameTtProbe probe_tt_with_policy(EndgameContext* context, Depth remaining_empties,
                                         Score alpha, Score beta) {
  if constexpr (EndgamePolicy::kUsesSmallEmpty) {
    return probe_exact_score_endgame_tt(context, remaining_empties, alpha, beta);
  }
  return probe_wld_endgame_tt(context, remaining_empties, alpha, beta);
}

template <typename EndgamePolicy>
void store_tt_with_policy(EndgameContext* context, Depth remaining_empties, Score score,
                          BoundType bound,
                          std::optional<board_core::Move> best_move = std::nullopt) {
  if constexpr (EndgamePolicy::kUsesSmallEmpty) {
    store_exact_score_endgame_tt(context, remaining_empties, score, bound, best_move);
  } else {
    store_wld_endgame_tt(context, remaining_empties, score, bound, best_move);
  }
}

template <typename EndgamePolicy>
SearchNodeResult endgame_search_with_policy(EndgameContext* context, Score alpha, Score beta,
                                            std::uint8_t empties, Ply ply,
                                            SmallEndgamePolicy small_endgame_policy);

template <typename EndgamePolicy>
SearchNodeResult search_endgame_child_with_policy(EndgameContext* context, board_core::Move move,
                                                  Score alpha, Score beta, std::uint8_t empties,
                                                  Ply ply,
                                                  SmallEndgamePolicy small_endgame_policy) {
  EndgameStackFrame& frame = context->stack[ply];
  frame.current_move = move;
  const bool made_delta = board_core::make_move_delta(context->position, move, &frame.delta);
  require_invariant(made_delta);
  board_core::apply_move_delta(&context->position, frame.delta);

  const std::uint8_t child_empties =
      move.kind == board_core::MoveKind::pass ? empties : static_cast<std::uint8_t>(empties - 1);
  const SearchNodeResult child = endgame_search_with_policy<EndgamePolicy>(
      context, static_cast<Score>(-beta), static_cast<Score>(-alpha), child_empties,
      static_cast<Ply>(ply + 1), small_endgame_policy);
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

template <typename EndgamePolicy>
SearchNodeResult endgame_terminal(EndgameContext* context, std::uint8_t empties) {
  ++context->stats.terminal_nodes;
  const Score score = EndgamePolicy::terminal_score(context->position);
  store_tt_with_policy<EndgamePolicy>(context, static_cast<Depth>(empties), score,
                                      BoundType::exact);
  return SearchNodeResult::completed(SearchValue{
      .score = score,
      .pv = {},
  });
}

template <typename EndgamePolicy>
SearchNodeResult endgame_search_with_policy(EndgameContext* context, Score alpha, Score beta,
                                            std::uint8_t empties, Ply ply,
                                            SmallEndgamePolicy small_endgame_policy) {
  require_invariant(alpha < beta);
  require_invariant(ply < kMaxPly);
  const Score original_alpha = alpha;
  const Score original_beta = beta;
  const Depth remaining_empties = static_cast<Depth>(empties);

  EndgameStackFrame& frame = context->stack[ply];
  frame = EndgameStackFrame{};

  if (note_endgame_node_visited(context)) {
    return SearchNodeResult::stopped();
  }

  const ExactEndgameTtProbe tt_probe =
      probe_tt_with_policy<EndgamePolicy>(context, remaining_empties, alpha, beta);
  if (tt_probe.cutoff_score.has_value()) {
    return SearchNodeResult::completed(SearchValue{
        .score = *tt_probe.cutoff_score,
        .pv = {},
    });
  }

  if constexpr (EndgamePolicy::kUsesSmallEmpty) {
    if (std::optional<SearchNodeResult> small_empty =
            try_exact_score_small_empty(context, alpha, beta, empties, ply, small_endgame_policy,
                                        original_alpha, original_beta)) {
      return *small_empty;
    }
  }

  if (board_core::is_terminal(context->position)) {
    return endgame_terminal<EndgamePolicy>(context, empties);
  }

  if (should_stop_endgame(context)) {
    return SearchNodeResult::stopped();
  }

  frame.moves = order_endgame_moves(
      context->position,
      EndgameOrderingHints{
          .tt_best_move = tt_probe.best_move,
          .use_parity_ordering = context->options.ordering.use_endgame_parity_ordering,
      });
  if (frame.moves.size == 0) {
    ++context->stats.pass_nodes;
    const SearchNodeResult pass = search_endgame_child_with_policy<EndgamePolicy>(
        context, board_core::make_pass(), alpha, beta, empties, ply, small_endgame_policy);
    if (pass.is_complete()) {
      frame.pv = pass.value().pv;
      store_tt_with_policy<EndgamePolicy>(
          context, remaining_empties, pass.value().score,
          classify_bound(pass.value().score, original_alpha, original_beta));
    }
    return pass;
  }

  SearchValue best{
      .score = EndgamePolicy::kWorstScore,
      .pv = {},
  };
  std::optional<board_core::Move> best_move;

  for (std::uint8_t move_index = 0; move_index < frame.moves.size; ++move_index) {
    if (should_stop_endgame(context)) {
      return SearchNodeResult::stopped();
    }

    const board_core::Move move = frame.moves.moves[move_index];
    const SearchNodeResult child = search_endgame_child_with_policy<EndgamePolicy>(
        context, move, alpha, beta, empties, ply, small_endgame_policy);
    if (child.is_stopped()) {
      return SearchNodeResult::stopped();
    }

    const SearchValue& child_value = child.value();
    update_best_line_and_move(child_value, move, &best, &best_move, &frame);
    if (update_endgame_alpha_and_check_cutoff(context, child_value.score, &alpha, beta)) {
      break;
    }
  }

  store_tt_with_policy<EndgamePolicy>(context, remaining_empties, best.score,
                                      classify_bound(best.score, original_alpha, original_beta),
                                      best_move);

  return SearchNodeResult::completed(best);
}

} // namespace

bool should_stop_endgame(EndgameContext* context) {
  return context != nullptr && should_stop(context->limit_state);
}

bool note_endgame_node_visited(EndgameContext* context) {
  if (context == nullptr) {
    return false;
  }

  return note_node_visited(context->limit_state, &context->stats, SearchNodeAccounting::endgame);
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

Score wld_score_from_exact_score(Score score) noexcept {
  if (score > 0) {
    return kWldWinScore;
  }
  if (score < 0) {
    return kWldLossScore;
  }
  return kWldDrawScore;
}

SearchNodeResult exact_score_terminal(EndgameContext* context, std::uint8_t empties) {
  return endgame_terminal<ExactScoreEndgamePolicy>(context, empties);
}

SearchNodeResult search_exact_score_endgame_child(EndgameContext* context, board_core::Move move,
                                                  Score alpha, Score beta, std::uint8_t empties,
                                                  Ply ply,
                                                  SmallEndgamePolicy small_endgame_policy) {
  return search_endgame_child_with_policy<ExactScoreEndgamePolicy>(
      context, move, alpha, beta, empties, ply, small_endgame_policy);
}

SearchNodeResult search_wld_endgame_child(EndgameContext* context, board_core::Move move,
                                          Score alpha, Score beta, std::uint8_t empties, Ply ply) {
  return search_endgame_child_with_policy<WldEndgamePolicy>(context, move, alpha, beta, empties,
                                                            ply, SmallEndgamePolicy::generic_only);
}

SearchNodeResult exact_score_search_with_policy(EndgameContext* context, Score alpha, Score beta,
                                                std::uint8_t empties, Ply ply,
                                                SmallEndgamePolicy small_endgame_policy) {
  return endgame_search_with_policy<ExactScoreEndgamePolicy>(context, alpha, beta, empties, ply,
                                                             small_endgame_policy);
}

std::uint8_t empty_count(board_core::Position position) noexcept {
  return static_cast<std::uint8_t>(std::popcount(~board_core::occupied(position)));
}

SearchNodeResult exact_score_search(EndgameContext* context, Score alpha, Score beta,
                                    std::uint8_t empties, Ply ply) {
  return exact_score_search_with_policy(context, alpha, beta, empties, ply,
                                        SmallEndgamePolicy::enabled);
}

SearchNodeResult wld_search(EndgameContext* context, Score alpha, Score beta, std::uint8_t empties,
                            Ply ply) {
  return endgame_search_with_policy<WldEndgamePolicy>(context, alpha, beta, empties, ply,
                                                      SmallEndgamePolicy::generic_only);
}

} // namespace vibe_othello::search::internal
