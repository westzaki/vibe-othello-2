#pragma once

#include "endgame_tt_internal.h"
#include "search_node_internal.h"
#include "search_util_internal.h"

#include <optional>

namespace vibe_othello::search::internal {

constexpr Score kWldLossScore = static_cast<Score>(WldResult::loss);
constexpr Score kWldDrawScore = static_cast<Score>(WldResult::draw);
constexpr Score kWldWinScore = static_cast<Score>(WldResult::win);
constexpr Score kWldAlpha = static_cast<Score>(kWldLossScore - 1);
constexpr Score kWldBeta = static_cast<Score>(kWldWinScore + 1);

Score wld_score_from_exact_score(Score score) noexcept;
bool should_stop_endgame(EndgameContext* context);
bool note_endgame_node_visited(EndgameContext* context);
bool update_endgame_alpha_and_check_cutoff(EndgameContext* context, Score score, Score* alpha,
                                           Score beta) noexcept;

struct ExactScoreEndgamePolicy {
  static constexpr Score kInitialAlpha = kScoreLoss;
  static constexpr Score kInitialBeta = kScoreWin;
  static constexpr Score kWorstScore = kScoreLoss;
  static constexpr ScoreKind kScoreKind = ScoreKind::exact_disc_diff;
  static constexpr bool kUsesSmallEmpty = true;
  static constexpr bool kUseInitialAlphaBeforeBestOnlyMove = false;

  static Score terminal_score(board_core::Position position) noexcept {
    return vibe_othello::search::internal::terminal_score(position);
  }
};

struct WldEndgamePolicy {
  static constexpr Score kInitialAlpha = kWldAlpha;
  static constexpr Score kInitialBeta = kWldBeta;
  static constexpr Score kWorstScore = kWldLossScore;
  static constexpr ScoreKind kScoreKind = ScoreKind::win_loss_draw;
  static constexpr bool kUsesSmallEmpty = false;
  static constexpr bool kUseInitialAlphaBeforeBestOnlyMove = true;

  static Score terminal_score(board_core::Position position) noexcept {
    return wld_score_from_exact_score(vibe_othello::search::internal::terminal_score(position));
  }
};

std::uint8_t empty_count(board_core::Position position) noexcept;
bool should_use_exact_endgame(board_core::Position position,
                              ResolvedSearchOptions options) noexcept;
bool should_use_wld_endgame(board_core::Position position, ResolvedSearchOptions options) noexcept;
SearchNodeResult exact_score_search(EndgameContext* context, Score alpha, Score beta,
                                    std::uint8_t empties, Ply ply);
SearchNodeResult wld_search(EndgameContext* context, Score alpha, Score beta, std::uint8_t empties,
                            Ply ply);
SearchResult solve_exact_endgame(board_core::Position position, SearchLimits limits,
                                 SearchOptions options, TranspositionTable* tt,
                                 SearchLimitState* limit_state = nullptr);
SearchResult solve_wld_endgame(board_core::Position position, SearchLimits limits,
                               SearchOptions options, TranspositionTable* tt,
                               SearchLimitState* limit_state = nullptr);
SearchResult solve_exact_endgame_with_small_endgame_policy(board_core::Position position,
                                                           SearchLimits limits,
                                                           SearchOptions options,
                                                           TranspositionTable* tt,
                                                           SmallEndgamePolicy small_endgame_policy,
                                                           SearchLimitState* limit_state = nullptr);
SearchNodeResult exact_score_terminal(EndgameContext* context, std::uint8_t empties);
SearchNodeResult search_exact_score_endgame_child(EndgameContext* context, board_core::Move move,
                                                  Score alpha, Score beta, std::uint8_t empties,
                                                  Ply ply, SmallEndgamePolicy small_endgame_policy);
SearchNodeResult search_wld_endgame_child(EndgameContext* context, board_core::Move move,
                                          Score alpha, Score beta, std::uint8_t empties, Ply ply);
SearchNodeResult exact_score_search_with_policy(EndgameContext* context, Score alpha, Score beta,
                                                std::uint8_t empties, Ply ply,
                                                SmallEndgamePolicy small_endgame_policy);

MoveList small_empty_move_list(board_core::Position position) noexcept;
board_core::Move first_legal_move(board_core::Bitboard legal_moves) noexcept;
// Precondition: move is the only empty square in a valid position.
int last_move_flip_count(board_core::Position position, board_core::Square move) noexcept;
std::optional<SearchNodeResult>
try_exact_score_small_empty(EndgameContext* context, Score alpha, Score beta, std::uint8_t empties,
                            Ply ply, SmallEndgamePolicy small_endgame_policy, Score original_alpha,
                            Score original_beta);

} // namespace vibe_othello::search::internal
