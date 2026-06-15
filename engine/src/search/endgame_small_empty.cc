#include "endgame_policy_internal.h"

#include <bit>

namespace vibe_othello::search::internal {
namespace {

SearchNodeResult exact_score_0_empty(EndgameContext* context, Score alpha, Score beta,
                                     std::uint8_t empties, Ply ply,
                                     SmallEndgamePolicy small_endgame_policy, Score original_alpha,
                                     Score original_beta) {
  (void)alpha;
  (void)beta;
  (void)ply;
  (void)small_endgame_policy;
  (void)original_alpha;
  (void)original_beta;
  require_invariant(empties == 0);
  return exact_score_terminal(context, empties);
}

SearchNodeResult exact_score_1_empty(EndgameContext* context, Score alpha, Score beta,
                                     std::uint8_t empties, Ply ply,
                                     SmallEndgamePolicy small_endgame_policy, Score original_alpha,
                                     Score original_beta) {
  require_invariant(empties == 1);
  StackFrame& frame = context->stack[ply];
  const Depth remaining_empties = static_cast<Depth>(empties);
  if (board_core::is_terminal(context->position)) {
    return exact_score_terminal(context, empties);
  }

  if (should_stop_endgame(context)) {
    return SearchNodeResult::stopped();
  }

  const board_core::Bitboard legal_moves = board_core::legal_moves(context->position);
  if (legal_moves == 0) {
    ++context->stats.pass_nodes;
    const SearchNodeResult pass = search_exact_score_endgame_child(
        context, board_core::make_pass(), alpha, beta, empties, ply, small_endgame_policy);
    if (pass.is_complete()) {
      frame.pv = pass.value().pv;
      store_exact_score_endgame_tt(
          context, remaining_empties, pass.value().score,
          classify_bound(pass.value().score, original_alpha, original_beta));
    }
    return pass;
  }

  const board_core::Move move = first_legal_move(legal_moves);
  const SearchNodeResult child = search_exact_score_endgame_child(
      context, move, alpha, beta, empties, ply, small_endgame_policy);
  if (child.is_stopped()) {
    return SearchNodeResult::stopped();
  }

  const SearchValue& child_value = child.value();
  frame.pv = child_value.pv;
  update_endgame_alpha_and_check_cutoff(context, child_value.score, &alpha, beta);
  store_exact_score_endgame_tt(context, remaining_empties, child_value.score,
                               classify_bound(child_value.score, original_alpha, original_beta),
                               move);
  return child;
}

SearchNodeResult exact_score_direct_small_empty(EndgameContext* context, Score alpha, Score beta,
                                                std::uint8_t empties, Ply ply,
                                                SmallEndgamePolicy small_endgame_policy,
                                                Score original_alpha, Score original_beta) {
  StackFrame& frame = context->stack[ply];
  const Depth remaining_empties = static_cast<Depth>(empties);
  if (board_core::is_terminal(context->position)) {
    return exact_score_terminal(context, empties);
  }

  if (should_stop_endgame(context)) {
    return SearchNodeResult::stopped();
  }

  board_core::Bitboard legal_moves = board_core::legal_moves(context->position);
  if (legal_moves == 0) {
    ++context->stats.pass_nodes;
    const SearchNodeResult pass = search_exact_score_endgame_child(
        context, board_core::make_pass(), alpha, beta, empties, ply, small_endgame_policy);
    if (pass.is_complete()) {
      frame.pv = pass.value().pv;
      store_exact_score_endgame_tt(
          context, remaining_empties, pass.value().score,
          classify_bound(pass.value().score, original_alpha, original_beta));
    }
    return pass;
  }

  SearchValue best{
      .score = kScoreLoss,
      .pv = {},
  };
  std::optional<board_core::Move> best_move;

  while (legal_moves != 0) {
    if (should_stop_endgame(context)) {
      return SearchNodeResult::stopped();
    }

    const board_core::Move move = first_legal_move(legal_moves);
    legal_moves &= legal_moves - 1;
    const SearchNodeResult child = search_exact_score_endgame_child(
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

  store_exact_score_endgame_tt(context, remaining_empties, best.score,
                               classify_bound(best.score, original_alpha, original_beta),
                               best_move);
  return SearchNodeResult::completed(best);
}

SearchNodeResult exact_score_2_empty(EndgameContext* context, Score alpha, Score beta,
                                     std::uint8_t empties, Ply ply,
                                     SmallEndgamePolicy small_endgame_policy, Score original_alpha,
                                     Score original_beta) {
  require_invariant(empties == 2);
  return exact_score_direct_small_empty(context, alpha, beta, empties, ply, small_endgame_policy,
                                        original_alpha, original_beta);
}

SearchNodeResult exact_score_3_empty(EndgameContext* context, Score alpha, Score beta,
                                     std::uint8_t empties, Ply ply,
                                     SmallEndgamePolicy small_endgame_policy, Score original_alpha,
                                     Score original_beta) {
  require_invariant(empties == 3);
  return exact_score_direct_small_empty(context, alpha, beta, empties, ply, small_endgame_policy,
                                        original_alpha, original_beta);
}

SearchNodeResult exact_score_4_empty(EndgameContext* context, Score alpha, Score beta,
                                     std::uint8_t empties, Ply ply,
                                     SmallEndgamePolicy small_endgame_policy, Score original_alpha,
                                     Score original_beta) {
  require_invariant(empties == 4);
  return exact_score_direct_small_empty(context, alpha, beta, empties, ply, small_endgame_policy,
                                        original_alpha, original_beta);
}

} // namespace

MoveList small_empty_move_list(board_core::Position position) noexcept {
  MoveList list{};
  const board_core::Bitboard legal_moves = board_core::legal_moves(position);
  for (int square_index = 0; square_index < board_core::kSquareCount; ++square_index) {
    const board_core::Square square = board_core::square_from_index(square_index);
    if ((legal_moves & board_core::bit(square)) != 0) {
      list.moves[list.size] = board_core::make_move(square);
      ++list.size;
    }
  }
  return list;
}

board_core::Move first_legal_move(board_core::Bitboard legal_moves) noexcept {
  const int square_index = std::countr_zero(legal_moves);
  return board_core::make_move(board_core::square_from_index(square_index));
}

std::optional<SearchNodeResult>
try_exact_score_small_empty(EndgameContext* context, Score alpha, Score beta, std::uint8_t empties,
                            Ply ply, SmallEndgamePolicy small_endgame_policy, Score original_alpha,
                            Score original_beta) {
  if (small_endgame_policy == SmallEndgamePolicy::generic_only || empties > 4) {
    return std::nullopt;
  }

  switch (empties) {
  case 0:
    return exact_score_0_empty(context, alpha, beta, empties, ply, small_endgame_policy,
                               original_alpha, original_beta);
  case 1:
    return exact_score_1_empty(context, alpha, beta, empties, ply, small_endgame_policy,
                               original_alpha, original_beta);
  case 2:
    return exact_score_2_empty(context, alpha, beta, empties, ply, small_endgame_policy,
                               original_alpha, original_beta);
  case 3:
    return exact_score_3_empty(context, alpha, beta, empties, ply, small_endgame_policy,
                               original_alpha, original_beta);
  case 4:
    return exact_score_4_empty(context, alpha, beta, empties, ply, small_endgame_policy,
                               original_alpha, original_beta);
  default:
    return std::nullopt;
  }
}

} // namespace vibe_othello::search::internal
