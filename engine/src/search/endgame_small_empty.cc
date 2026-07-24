#include "endgame_policy_internal.h"

#include <array>
#include <bit>

namespace vibe_othello::search::internal {
namespace {

enum RayDirection : std::size_t {
  kEast,
  kWest,
  kNorth,
  kSouth,
  kNorthEast,
  kNorthWest,
  kSouthEast,
  kSouthWest,
  kDirectionCount,
};

using RayTable =
    std::array<std::array<board_core::Bitboard, board_core::kSquareCount>, kDirectionCount>;

constexpr RayTable make_ray_table() noexcept {
  constexpr std::array<int, 8> kFileSteps{1, -1, 0, 0, 1, -1, 1, -1};
  constexpr std::array<int, 8> kRankSteps{0, 0, 1, -1, 1, 1, -1, -1};
  RayTable rays{};
  for (int square_index = 0; square_index < board_core::kSquareCount; ++square_index) {
    const board_core::Square square = board_core::square_from_index(square_index);
    for (std::size_t direction = 0; direction < kFileSteps.size(); ++direction) {
      int file = board_core::file_of(square) + kFileSteps[direction];
      int rank = board_core::rank_of(square) + kRankSteps[direction];
      while (board_core::is_valid_file_rank(file, rank)) {
        rays[direction][square_index] |=
            board_core::bit(board_core::square_from_file_rank(file, rank));
        file += kFileSteps[direction];
        rank += kRankSteps[direction];
      }
    }
  }
  return rays;
}

constexpr RayTable kRays = make_ray_table();

template <int kIndexStep>
int last_move_ray_flip_count(board_core::Bitboard player, board_core::Bitboard ray,
                             int move_index) noexcept {
  const board_core::Bitboard anchors = player & ray;
  if (anchors == 0) {
    return 0;
  }
  if constexpr (kIndexStep > 0) {
    const int nearest = std::countr_zero(anchors);
    return ((nearest - move_index) / kIndexStep) - 1;
  }
  const int nearest = 63 - std::countl_zero(anchors);
  return ((move_index - nearest) / -kIndexStep) - 1;
}

template <RayDirection kDirection, int kIndexStep>
board_core::Bitboard small_empty_ray_flips(board_core::Position position,
                                           board_core::Bitboard empty_squares,
                                           int move_index) noexcept {
  const board_core::Bitboard move_ray = kRays[kDirection][move_index];
  // With at most four empties, the nearest player disc or empty square decides the ray:
  // an empty interrupts capture, while every occupied square before a player anchor is opponent.
  const board_core::Bitboard blockers = (position.player | empty_squares) & move_ray;
  if (blockers == 0) {
    return 0;
  }

  int anchor_index = 0;
  if constexpr (kIndexStep > 0) {
    anchor_index = std::countr_zero(blockers);
  } else {
    anchor_index = 63 - std::countl_zero(blockers);
  }
  const board_core::Bitboard anchor = board_core::bit(board_core::square_from_index(anchor_index));
  if ((anchor & position.player) == 0) {
    return 0;
  }

  return move_ray & ~(kRays[kDirection][anchor_index] | anchor);
}

board_core::Bitboard small_empty_flips_for_move(board_core::Position position,
                                                board_core::Bitboard empty_squares,
                                                board_core::Square move) noexcept {
  return small_empty_ray_flips<kEast, 1>(position, empty_squares, move.index) |
         small_empty_ray_flips<kWest, -1>(position, empty_squares, move.index) |
         small_empty_ray_flips<kNorth, 8>(position, empty_squares, move.index) |
         small_empty_ray_flips<kSouth, -8>(position, empty_squares, move.index) |
         small_empty_ray_flips<kNorthEast, 9>(position, empty_squares, move.index) |
         small_empty_ray_flips<kNorthWest, 7>(position, empty_squares, move.index) |
         small_empty_ray_flips<kSouthEast, -7>(position, empty_squares, move.index) |
         small_empty_ray_flips<kSouthWest, -9>(position, empty_squares, move.index);
}

SearchNodeResult search_exact_score_endgame_delta(EndgameContext* context,
                                                  board_core::MoveDelta delta, Score alpha,
                                                  Score beta, std::uint8_t empties, Ply ply,
                                                  SmallEndgamePolicy small_endgame_policy) {
  EndgameStackFrame& frame = context->stack[ply];
  frame.current_move = delta.move;
  frame.delta = delta;
  apply_move(&context->position_state, frame.delta, &frame.position_undo);

  const std::uint8_t child_empties = delta.move.kind == board_core::MoveKind::pass
                                         ? empties
                                         : static_cast<std::uint8_t>(empties - 1);
  const SearchNodeResult child = exact_score_search_with_policy(
      context, static_cast<Score>(-beta), static_cast<Score>(-alpha), child_empties,
      static_cast<Ply>(ply + 1), small_endgame_policy);
  undo_move(&context->position_state, frame.delta, frame.position_undo);

  if (child.is_stopped()) {
    return SearchNodeResult::stopped();
  }
  return SearchNodeResult::completed(SearchValue{
      .score = static_cast<Score>(-child.value().score),
  });
}

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

  const board_core::Bitboard empty = ~board_core::occupied(context->position_state.position);
  require_invariant(std::popcount(empty) == 1);
  const board_core::Move move = first_legal_move(empty);
  const int flipped = last_move_flip_count(context->position_state.position, move.square);
  if (flipped == 0) {
    board_core::Position opponent_position = context->position_state.position;
    const board_core::MoveDelta pass_delta{
        .move = board_core::make_pass(),
        .flipped = 0,
    };
    board_core::apply_move_delta(&opponent_position, pass_delta);

    const int opponent_flipped = last_move_flip_count(opponent_position, move.square);
    if (opponent_flipped == 0) {
      frame.pv.size = 0;
      return exact_score_terminal(context, empties);
    }
    ++context->stats.pass_nodes;
    const int opponent_disc_difference =
        std::popcount(opponent_position.player) - std::popcount(opponent_position.opponent);
    const Score score =
        static_cast<Score>(-(opponent_disc_difference + 1 + (2 * opponent_flipped)));

    frame.pv.moves[0] = board_core::make_pass();
    frame.pv.moves[1] = move;
    frame.pv.size = 2;
    ++context->stats.endgame_last_flip_solved;
    ++context->stats.terminal_nodes;
    store_exact_score_endgame_tt(context, remaining_empties, score,
                                 classify_bound(score, original_alpha, original_beta));
    return SearchNodeResult::completed(SearchValue{.score = score});
  }

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

SearchNodeResult exact_score_delta_small_empty(EndgameContext* context, Score alpha, Score beta,
                                               std::uint8_t empties, Ply ply,
                                               SmallEndgamePolicy small_endgame_policy,
                                               Score original_alpha, Score original_beta) {
  require_invariant(empties >= 2 && empties <= 4);
  EndgameStackFrame& frame = context->stack[ply];
  if (should_stop_endgame(context)) {
    return SearchNodeResult::stopped();
  }

  std::array<board_core::MoveDelta, 4> legal_deltas{};
  std::uint8_t legal_count = 0;
  const board_core::Bitboard empty_squares =
      ~board_core::occupied(context->position_state.position);
  board_core::Bitboard remaining = empty_squares;
  while (remaining != 0) {
    const int square_index = std::countr_zero(remaining);
    remaining &= remaining - 1;
    const board_core::Move move =
        board_core::make_move(board_core::square_from_index(square_index));
    const board_core::Bitboard flipped =
        small_empty_flips_for_move(context->position_state.position, empty_squares, move.square);
    if (flipped != 0) {
      legal_deltas[legal_count] = board_core::MoveDelta{.move = move, .flipped = flipped};
      ++legal_count;
    }
  }

  if (legal_count == 0) {
    board_core::MoveDelta pass_delta{};
    if (!board_core::make_move_delta(context->position_state.position, board_core::make_pass(),
                                     &pass_delta)) {
      frame.pv.size = 0;
      return exact_score_terminal(context, empties);
    }
    ++context->stats.pass_nodes;
    const SearchNodeResult pass = search_exact_score_endgame_delta(
        context, pass_delta, alpha, beta, empties, ply, small_endgame_policy);
    if (pass.is_complete()) {
      prepend_move(board_core::make_pass(), context->stack[ply + 1].pv, &frame.pv);
      store_exact_score_endgame_tt(
          context, static_cast<Depth>(empties), pass.value().score,
          classify_bound(pass.value().score, original_alpha, original_beta));
    }
    return pass;
  }

  Score best_score = kScoreLoss;
  std::optional<board_core::Move> best_move;
  for (std::uint8_t index = 0; index < legal_count; ++index) {
    if (should_stop_endgame(context)) {
      return SearchNodeResult::stopped();
    }
    const board_core::MoveDelta delta = legal_deltas[index];
    const SearchNodeResult child = search_exact_score_endgame_delta(
        context, delta, alpha, beta, empties, ply, small_endgame_policy);
    if (child.is_stopped()) {
      return SearchNodeResult::stopped();
    }

    update_best_line_and_move(child.value().score, context->stack[ply + 1].pv, delta.move,
                              &best_score, &best_move, &frame);
    if (update_endgame_alpha_and_check_cutoff(context, child.value().score, &alpha, beta)) {
      break;
    }
  }

  store_exact_score_endgame_tt(context, static_cast<Depth>(empties), best_score,
                               classify_bound(best_score, original_alpha, original_beta),
                               best_move);
  return SearchNodeResult::completed(SearchValue{.score = best_score});
}

SearchNodeResult exact_score_2_empty(EndgameContext* context, Score alpha, Score beta,
                                     std::uint8_t empties, Ply ply,
                                     SmallEndgamePolicy small_endgame_policy, Score original_alpha,
                                     Score original_beta) {
  require_invariant(empties == 2);
  return exact_score_delta_small_empty(context, alpha, beta, empties, ply, small_endgame_policy,
                                       original_alpha, original_beta);
}

SearchNodeResult exact_score_3_empty(EndgameContext* context, Score alpha, Score beta,
                                     std::uint8_t empties, Ply ply,
                                     SmallEndgamePolicy small_endgame_policy, Score original_alpha,
                                     Score original_beta) {
  require_invariant(empties == 3);
  return exact_score_delta_small_empty(context, alpha, beta, empties, ply, small_endgame_policy,
                                       original_alpha, original_beta);
}

SearchNodeResult exact_score_4_empty(EndgameContext* context, Score alpha, Score beta,
                                     std::uint8_t empties, Ply ply,
                                     SmallEndgamePolicy small_endgame_policy, Score original_alpha,
                                     Score original_beta) {
  require_invariant(empties == 4);
  return exact_score_delta_small_empty(context, alpha, beta, empties, ply, small_endgame_policy,
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

int last_move_flip_count(board_core::Position position, board_core::Square move) noexcept {
  return last_move_ray_flip_count<1>(position.player, kRays[kEast][move.index], move.index) +
         last_move_ray_flip_count<-1>(position.player, kRays[kWest][move.index], move.index) +
         last_move_ray_flip_count<8>(position.player, kRays[kNorth][move.index], move.index) +
         last_move_ray_flip_count<-8>(position.player, kRays[kSouth][move.index], move.index) +
         last_move_ray_flip_count<9>(position.player, kRays[kNorthEast][move.index], move.index) +
         last_move_ray_flip_count<7>(position.player, kRays[kNorthWest][move.index], move.index) +
         last_move_ray_flip_count<-7>(position.player, kRays[kSouthEast][move.index], move.index) +
         last_move_ray_flip_count<-9>(position.player, kRays[kSouthWest][move.index], move.index);
}

board_core::Bitboard small_empty_flips_for_move(board_core::Position position,
                                                board_core::Square move) noexcept {
  return small_empty_flips_for_move(position, ~board_core::occupied(position), move);
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
