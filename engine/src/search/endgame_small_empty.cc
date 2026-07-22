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
  (void)small_endgame_policy;
  require_invariant(empties == 1);
  EndgameStackFrame& frame = context->stack[ply];
  const Depth remaining_empties = static_cast<Depth>(empties);
  if (should_stop_endgame(context)) {
    return SearchNodeResult::stopped();
  }

  const board_core::Bitboard legal_moves = frame.legal_moves;
  if (legal_moves == 0) {
    ++context->stats.pass_nodes;
    board_core::Position opponent_position = context->position_state.position;
    board_core::MoveDelta pass_delta{};
    const bool made_pass =
        board_core::make_move_delta(opponent_position, board_core::make_pass(), &pass_delta);
    require_invariant(made_pass);
    board_core::apply_move_delta(&opponent_position, pass_delta);

    const board_core::Bitboard opponent_legal_moves = board_core::legal_moves(opponent_position);
    require_invariant(std::popcount(opponent_legal_moves) == 1);
    const board_core::Move opponent_move = first_legal_move(opponent_legal_moves);
    const int flipped =
        std::popcount(board_core::flips_for_move(opponent_position, opponent_move.square));
    const int opponent_disc_difference =
        std::popcount(opponent_position.player) - std::popcount(opponent_position.opponent);
    const Score score = static_cast<Score>(-(opponent_disc_difference + 1 + (2 * flipped)));

    frame.pv.moves[0] = board_core::make_pass();
    frame.pv.moves[1] = opponent_move;
    frame.pv.size = 2;
    ++context->stats.endgame_last_flip_solved;
    ++context->stats.terminal_nodes;
    store_exact_score_endgame_tt(context, remaining_empties, score,
                                 classify_bound(score, original_alpha, original_beta));
    return SearchNodeResult::completed(SearchValue{.score = score});
  }

  const board_core::Move move = first_legal_move(legal_moves);
  const int flipped =
      std::popcount(board_core::flips_for_move(context->position_state.position, move.square));
  const int disc_difference = std::popcount(context->position_state.position.player) -
                              std::popcount(context->position_state.position.opponent);
  const Score score = static_cast<Score>(disc_difference + 1 + (2 * flipped));

  frame.pv.moves[0] = move;
  frame.pv.size = 1;
  ++context->stats.endgame_last_flip_solved;
  ++context->stats.terminal_nodes;
  update_endgame_alpha_and_check_cutoff(context, score, &alpha, beta);
  store_exact_score_endgame_tt(context, remaining_empties, score,
                               classify_bound(score, original_alpha, original_beta), move);
  return SearchNodeResult::completed(SearchValue{.score = score});
}

SearchNodeResult exact_score_direct_small_empty(EndgameContext* context, Score alpha, Score beta,
                                                std::uint8_t empties, Ply ply,
                                                SmallEndgamePolicy small_endgame_policy,
                                                Score original_alpha, Score original_beta) {
  EndgameStackFrame& frame = context->stack[ply];
  const Depth remaining_empties = static_cast<Depth>(empties);
  if (should_stop_endgame(context)) {
    return SearchNodeResult::stopped();
  }

  const board_core::Bitboard legal_moves = frame.legal_moves;
  if (legal_moves == 0) {
    ++context->stats.pass_nodes;
    const SearchNodeResult pass = search_exact_score_endgame_child(
        context, board_core::make_pass(), alpha, beta, empties, ply, small_endgame_policy);
    if (pass.is_complete()) {
      prepend_move(board_core::make_pass(), context->stack[ply + 1].pv, &frame.pv);
      store_exact_score_endgame_tt(
          context, remaining_empties, pass.value().score,
          classify_bound(pass.value().score, original_alpha, original_beta));
    }
    return pass;
  }

  Score best_score = kScoreLoss;
  std::optional<board_core::Move> best_move;
  const MoveList moves =
      empties >= 5 && context->options.ordering.use_endgame_parity_ordering
          ? order_endgame_moves_by_parity(context->position_state.position, legal_moves)
          : move_list_from_legal_mask(legal_moves);

  for (std::uint8_t move_index = 0; move_index < moves.size; ++move_index) {
    if (should_stop_endgame(context)) {
      return SearchNodeResult::stopped();
    }

    const board_core::Move move = moves.moves[move_index];
    const SearchNodeResult child = search_exact_score_endgame_child(
        context, move, alpha, beta, empties, ply, small_endgame_policy);
    if (child.is_stopped()) {
      return SearchNodeResult::stopped();
    }

    const SearchValue& child_value = child.value();
    update_best_line_and_move(child_value.score, context->stack[ply + 1].pv, move, &best_score,
                              &best_move, &frame);
    if (update_endgame_alpha_and_check_cutoff(context, child_value.score, &alpha, beta)) {
      break;
    }
  }

  store_exact_score_endgame_tt(context, remaining_empties, best_score,
                               classify_bound(best_score, original_alpha, original_beta),
                               best_move);
  return SearchNodeResult::completed(SearchValue{.score = best_score});
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

SearchNodeResult exact_score_5_to_8_empty(EndgameContext* context, Score alpha, Score beta,
                                          std::uint8_t empties, Ply ply,
                                          SmallEndgamePolicy small_endgame_policy,
                                          Score original_alpha, Score original_beta) {
  require_invariant(empties >= 5 && empties <= 8);
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
  if (small_endgame_policy == SmallEndgamePolicy::generic_only || empties > 8) {
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
  case 5:
  case 6:
  case 7:
  case 8:
    return exact_score_5_to_8_empty(context, alpha, beta, empties, ply, small_endgame_policy,
                                    original_alpha, original_beta);
  default:
    return std::nullopt;
  }
}

} // namespace vibe_othello::search::internal
