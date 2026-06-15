#include "search_internal.h"

#include <bit>
#include <chrono>
#include <optional>

namespace vibe_othello::search::internal {
namespace {

constexpr Score kWldLossScore = static_cast<Score>(WldResult::loss);
constexpr Score kWldDrawScore = static_cast<Score>(WldResult::draw);
constexpr Score kWldWinScore = static_cast<Score>(WldResult::win);
constexpr Score kWldAlpha = static_cast<Score>(kWldLossScore - 1);
constexpr Score kWldBeta = static_cast<Score>(kWldWinScore + 1);

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

bool should_use_endgame_tt(const EndgameContext& context) noexcept {
  return context.options.use_endgame_tt && context.transposition_table != nullptr;
}

struct ExactScoreEndgamePolicy {
  static constexpr Score kInitialAlpha = kScoreLoss;
  static constexpr Score kInitialBeta = kScoreWin;
  static constexpr Score kWorstScore = kScoreLoss;
  static constexpr TTEntryKind kTtEntryKind = TTEntryKind::exact_endgame_score;
  static constexpr bool kUsesSmallEmpty = true;
  static constexpr bool kUseInitialAlphaBeforeBestOnlyMove = false;

  static Score terminal_score(board_core::Position position) noexcept {
    return vibe_othello::search::internal::terminal_score(position);
  }

  static ExactEndgameTtProbe probe_entry(const TTEntry& entry, board_core::Position position,
                                         Depth remaining_empties, Score alpha,
                                         Score beta) noexcept {
    return exact_endgame_score_tt_probe(entry, position, remaining_empties, alpha, beta);
  }
};

struct WldEndgamePolicy {
  static constexpr Score kInitialAlpha = kWldAlpha;
  static constexpr Score kInitialBeta = kWldBeta;
  static constexpr Score kWorstScore = kWldLossScore;
  static constexpr TTEntryKind kTtEntryKind = TTEntryKind::exact_endgame_wld;
  static constexpr bool kUsesSmallEmpty = false;
  static constexpr bool kUseInitialAlphaBeforeBestOnlyMove = true;

  static Score terminal_score(board_core::Position position) noexcept {
    return wld_score_from_exact_score(vibe_othello::search::internal::terminal_score(position));
  }

  static ExactEndgameTtProbe probe_entry(const TTEntry& entry, board_core::Position position,
                                         Depth remaining_empties, Score alpha,
                                         Score beta) noexcept {
    return exact_endgame_wld_tt_probe(entry, position, remaining_empties, alpha, beta);
  }
};

template <typename EndgamePolicy>
SearchNodeResult endgame_search_with_policy(EndgameContext* context, Score alpha, Score beta,
                                            std::uint8_t empties, Ply ply,
                                            SmallEndgamePolicy small_endgame_policy);

template <typename EndgamePolicy>
ExactEndgameTtProbe probe_endgame_tt(EndgameContext* context, Depth remaining_empties, Score alpha,
                                     Score beta) {
  if (!should_use_endgame_tt(*context)) {
    return {};
  }

  const std::optional<TTEntry> entry =
      context->transposition_table->probe(context->position, &context->stats);
  if (!entry.has_value()) {
    return {};
  }

  ExactEndgameTtProbe probe =
      EndgamePolicy::probe_entry(*entry, context->position, remaining_empties, alpha, beta);
  if (probe.cutoff_score.has_value()) {
    ++context->stats.tt_cutoffs;
  }
  return probe;
}

template <typename EndgamePolicy>
std::optional<board_core::Move> probe_endgame_root_tt_best_move(EndgameContext* context,
                                                                Depth remaining_empties) {
  return probe_endgame_tt<EndgamePolicy>(context, remaining_empties, EndgamePolicy::kInitialAlpha,
                                         EndgamePolicy::kInitialBeta)
      .best_move;
}

template <typename EndgamePolicy>
void store_endgame_tt(EndgameContext* context, Depth remaining_empties, Score score,
                      BoundType bound,
                      std::optional<board_core::Move> best_move = std::nullopt) noexcept {
  if (!should_use_endgame_tt(*context) || should_stop_endgame(context)) {
    return;
  }

  if (best_move.has_value()) {
    context->transposition_table->store(context->position, remaining_empties, score, bound,
                                        *best_move, EndgamePolicy::kTtEntryKind, &context->stats);
    return;
  }

  context->transposition_table->store_value(context->position, remaining_empties, score, bound,
                                            EndgamePolicy::kTtEntryKind, &context->stats);
}

template <typename EndgamePolicy>
SearchNodeResult search_endgame_child_with_policy(EndgameContext* context, board_core::Move move,
                                                  Score alpha, Score beta, std::uint8_t empties,
                                                  Ply ply,
                                                  SmallEndgamePolicy small_endgame_policy) {
  StackFrame& frame = context->stack[ply];
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

RootMoveInfo make_endgame_root_move(board_core::Move move, Score score, Depth depth,
                                    NodeCount nodes, Line pv) noexcept {
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
  result->bound = BoundType::lower;
  result->score = kScoreLoss;
}

bool use_best_only_root_reporting(SearchOptions options) noexcept {
  return options.multi_pv == 1;
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

template <typename EndgamePolicy>
SearchNodeResult endgame_terminal(EndgameContext* context, std::uint8_t empties) {
  ++context->stats.terminal_nodes;
  const Score score = EndgamePolicy::terminal_score(context->position);
  store_endgame_tt<EndgamePolicy>(context, static_cast<Depth>(empties), score, BoundType::exact);
  return SearchNodeResult::completed(SearchValue{
      .score = score,
      .pv = {},
  });
}

SearchNodeResult exact_score_terminal(EndgameContext* context, std::uint8_t empties) {
  return endgame_terminal<ExactScoreEndgamePolicy>(context, empties);
}

SearchNodeResult exact_score_small_empty(EndgameContext* context, Score alpha, Score beta,
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

  const MoveList moves = small_empty_move_list(context->position);
  if (moves.size == 0) {
    ++context->stats.pass_nodes;
    const SearchNodeResult pass = search_endgame_child_with_policy<ExactScoreEndgamePolicy>(
        context, board_core::make_pass(), alpha, beta, empties, ply, small_endgame_policy);
    if (pass.is_complete()) {
      frame.pv = pass.value().pv;
      store_endgame_tt<ExactScoreEndgamePolicy>(
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

  for (std::uint8_t move_index = 0; move_index < moves.size; ++move_index) {
    if (should_stop_endgame(context)) {
      return SearchNodeResult::stopped();
    }

    const board_core::Move move = moves.moves[move_index];
    const SearchNodeResult child = search_endgame_child_with_policy<ExactScoreEndgamePolicy>(
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

  store_endgame_tt<ExactScoreEndgamePolicy>(
      context, remaining_empties, best.score,
      classify_bound(best.score, original_alpha, original_beta), best_move);
  return SearchNodeResult::completed(best);
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
    const SearchNodeResult pass = search_endgame_child_with_policy<ExactScoreEndgamePolicy>(
        context, board_core::make_pass(), alpha, beta, empties, ply, small_endgame_policy);
    if (pass.is_complete()) {
      frame.pv = pass.value().pv;
      store_endgame_tt<ExactScoreEndgamePolicy>(
          context, remaining_empties, pass.value().score,
          classify_bound(pass.value().score, original_alpha, original_beta));
    }
    return pass;
  }

  const board_core::Move move = first_legal_move(legal_moves);
  const SearchNodeResult child = search_endgame_child_with_policy<ExactScoreEndgamePolicy>(
      context, move, alpha, beta, empties, ply, small_endgame_policy);
  if (child.is_stopped()) {
    return SearchNodeResult::stopped();
  }

  const SearchValue& child_value = child.value();
  frame.pv = child_value.pv;
  update_endgame_alpha_and_check_cutoff(context, child_value.score, &alpha, beta);
  store_endgame_tt<ExactScoreEndgamePolicy>(
      context, remaining_empties, child_value.score,
      classify_bound(child_value.score, original_alpha, original_beta), move);
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
    const SearchNodeResult pass = search_endgame_child_with_policy<ExactScoreEndgamePolicy>(
        context, board_core::make_pass(), alpha, beta, empties, ply, small_endgame_policy);
    if (pass.is_complete()) {
      frame.pv = pass.value().pv;
      store_endgame_tt<ExactScoreEndgamePolicy>(
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
    const SearchNodeResult child = search_endgame_child_with_policy<ExactScoreEndgamePolicy>(
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

  store_endgame_tt<ExactScoreEndgamePolicy>(
      context, remaining_empties, best.score,
      classify_bound(best.score, original_alpha, original_beta), best_move);
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

template <typename EndgamePolicy>
SearchNodeResult endgame_search_with_policy(EndgameContext* context, Score alpha, Score beta,
                                            std::uint8_t empties, Ply ply,
                                            SmallEndgamePolicy small_endgame_policy) {
  require_invariant(alpha < beta);
  require_invariant(ply < kMaxPly);
  const Score original_alpha = alpha;
  const Score original_beta = beta;
  const Depth remaining_empties = static_cast<Depth>(empties);

  StackFrame& frame = context->stack[ply];
  frame = StackFrame{};

  if (note_endgame_node_visited(context)) {
    return SearchNodeResult::stopped();
  }

  const ExactEndgameTtProbe tt_probe =
      probe_endgame_tt<EndgamePolicy>(context, remaining_empties, alpha, beta);
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
      context->position, EndgameOrderingHints{
                             .tt_best_move = tt_probe.best_move,
                             .use_parity_ordering = context->options.use_endgame_parity_ordering,
                         });
  if (frame.moves.size == 0) {
    ++context->stats.pass_nodes;
    const SearchNodeResult pass = search_endgame_child_with_policy<EndgamePolicy>(
        context, board_core::make_pass(), alpha, beta, empties, ply, small_endgame_policy);
    if (pass.is_complete()) {
      frame.pv = pass.value().pv;
      store_endgame_tt<EndgamePolicy>(
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

  store_endgame_tt<EndgamePolicy>(context, remaining_empties, best.score,
                                  classify_bound(best.score, original_alpha, original_beta),
                                  best_move);

  return SearchNodeResult::completed(best);
}

SearchNodeResult exact_score_search_with_policy(EndgameContext* context, Score alpha, Score beta,
                                                std::uint8_t empties, Ply ply,
                                                SmallEndgamePolicy small_endgame_policy) {
  return endgame_search_with_policy<ExactScoreEndgamePolicy>(context, alpha, beta, empties, ply,
                                                             small_endgame_policy);
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
      root_tt_best_move = probe_endgame_root_tt_best_move<EndgamePolicy>(context, completed_depth);
    }
    return small_endgame_policy == SmallEndgamePolicy::enabled && root_empties <= 4
               ? small_empty_move_list(context->position)
               : order_endgame_moves(
                     context->position,
                     EndgameOrderingHints{.tt_best_move = root_tt_best_move,
                                          .use_parity_ordering =
                                              context->options.use_endgame_parity_ordering});
  }

  const std::optional<board_core::Move> root_tt_best_move =
      probe_endgame_root_tt_best_move<EndgamePolicy>(context, completed_depth);
  return order_endgame_moves(
      context->position, EndgameOrderingHints{
                             .tt_best_move = root_tt_best_move,
                             .use_parity_ordering = context->options.use_endgame_parity_ordering,
                         });
}

template <typename EndgamePolicy>
SearchResult solve_root_endgame_with_policy(board_core::Position position, SearchLimits limits,
                                            SearchOptions options, TranspositionTable* tt,
                                            SmallEndgamePolicy small_endgame_policy,
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
      .completed_depth = 0,
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
    result.completed_depth = completed_depth;
    result.score = EndgamePolicy::terminal_score(context.position);
    result.nodes = context.stats.nodes;
    result.stats = context.stats;
    if (should_stop_endgame(&context)) {
      mark_stopped_non_exact(&result);
      result.completed_depth = completed_depth;
      result.score = EndgamePolicy::terminal_score(context.position);
    } else {
      result.exact = true;
    }
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
  root_frame.moves = root_endgame_moves_with_policy<EndgamePolicy>(
      &context, completed_depth, root_empties, small_endgame_policy);
  const MoveList root_moves = root_frame.moves;

  if (root_moves.size == 0) {
    ++context.stats.pass_nodes;
    const NodeCount before_nodes = context.stats.nodes;
    const SearchNodeResult pass = search_endgame_child_with_policy<EndgamePolicy>(
        &context, board_core::make_pass(), EndgamePolicy::kInitialAlpha,
        EndgamePolicy::kInitialBeta, root_empties, Ply{0}, small_endgame_policy);
    if (pass.is_stopped()) {
      mark_stopped_non_exact(&result);
      result.nodes = context.stats.nodes;
      result.stats = context.stats;
      publish_elapsed(&result, start);
      return result;
    }
    const SearchValue& pass_value = pass.value();

    result.best_move = board_core::make_pass();
    result.completed_depth = completed_depth;
    result.score = pass_value.score;
    result.pv = pass_value.pv;
    result.root_moves.push_back(
        make_endgame_root_move(board_core::make_pass(), pass_value.score, completed_depth,
                               context.stats.nodes - before_nodes, pass_value.pv));
    context.stats.root_moves_searched = 1;
    result.nodes = context.stats.nodes;
    result.stats = context.stats;
    if (should_stop_endgame(&context)) {
      mark_stopped_non_exact(&result);
      result.best_move = board_core::make_pass();
      result.completed_depth = completed_depth;
      result.score = pass_value.score;
      result.pv = pass_value.pv;
    } else {
      result.exact = true;
    }
    publish_elapsed(&result, start);
    return result;
  }

  Score best_score = EndgamePolicy::kWorstScore;
  Line best_line{};
  std::optional<RootMoveInfo> best_root_move;
  const bool best_only_reporting = use_best_only_root_reporting(context.options);
  for (std::uint8_t move_index = 0; move_index < root_moves.size; ++move_index) {
    if (should_stop_endgame(&context)) {
      mark_stopped_non_exact(&result);
      break;
    }

    const board_core::Move move = root_moves.moves[move_index];
    const NodeCount before_nodes = context.stats.nodes;
    const Score root_alpha = root_alpha_with_policy<EndgamePolicy>(best_only_reporting, move,
                                                                   best_score, result.best_move);
    const SearchNodeResult child = search_endgame_child_with_policy<EndgamePolicy>(
        &context, move, root_alpha, EndgamePolicy::kInitialBeta, root_empties, Ply{0},
        small_endgame_policy);
    if (child.is_stopped()) {
      mark_stopped_non_exact(&result);
      break;
    }
    const SearchValue& child_value = child.value();

    RootMoveInfo root_move =
        make_endgame_root_move(move, child_value.score, completed_depth,
                               context.stats.nodes - before_nodes, child_value.pv);
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
    mark_stopped_non_exact(&result);
  }
  result.nodes = context.stats.nodes;
  result.stats = context.stats;
  publish_elapsed(&result, start);
  if (result.stopped) {
    if (result.best_move.has_value()) {
      result.bound = BoundType::lower;
      result.completed_depth = completed_depth;
      result.score = best_score;
      result.pv = best_line;
      if (best_only_reporting && best_root_move.has_value()) {
        publish_best_only_root_move(&result, *best_root_move);
      }
    }
    return result;
  }

  result.completed_depth = completed_depth;
  result.score = best_score;
  result.pv = best_line;
  result.exact = true;
  return result;
}

} // namespace

std::uint8_t empty_count(board_core::Position position) noexcept {
  return static_cast<std::uint8_t>(std::popcount(~board_core::occupied(position)));
}

bool should_use_exact_endgame(board_core::Position position, SearchOptions options) noexcept {
  return options.exact_endgame && options.mode != SearchMode::win_loss_draw &&
         empty_count(position) <= options.endgame_exact_empties;
}

bool should_use_wld_endgame(board_core::Position position, SearchOptions options) noexcept {
  return options.mode == SearchMode::win_loss_draw && options.endgame_wld_empties > 0 &&
         empty_count(position) <= options.endgame_wld_empties;
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
