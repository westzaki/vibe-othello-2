#pragma once

#include "search_internal.h"

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
  static constexpr bool kUsesSmallEmpty = false;
  static constexpr bool kUseInitialAlphaBeforeBestOnlyMove = true;

  static Score terminal_score(board_core::Position position) noexcept {
    return wld_score_from_exact_score(vibe_othello::search::internal::terminal_score(position));
  }
};

ExactEndgameTtProbe probe_exact_score_endgame_tt(EndgameContext* context, Depth remaining_empties,
                                                 Score alpha, Score beta);
ExactEndgameTtProbe probe_wld_endgame_tt(EndgameContext* context, Depth remaining_empties,
                                         Score alpha, Score beta);
std::optional<board_core::Move>
probe_exact_score_endgame_root_tt_best_move(EndgameContext* context, Depth remaining_empties);
std::optional<board_core::Move> probe_wld_endgame_root_tt_best_move(EndgameContext* context,
                                                                    Depth remaining_empties);
void store_exact_score_endgame_tt(EndgameContext* context, Depth remaining_empties, Score score,
                                  BoundType bound,
                                  std::optional<board_core::Move> best_move = std::nullopt);
void store_wld_endgame_tt(EndgameContext* context, Depth remaining_empties, Score score,
                          BoundType bound,
                          std::optional<board_core::Move> best_move = std::nullopt);

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
std::optional<SearchNodeResult>
try_exact_score_small_empty(EndgameContext* context, Score alpha, Score beta, std::uint8_t empties,
                            Ply ply, SmallEndgamePolicy small_endgame_policy, Score original_alpha,
                            Score original_beta);

} // namespace vibe_othello::search::internal
