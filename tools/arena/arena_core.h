#ifndef VIBE_OTHELLO_TOOLS_ARENA_CORE_H_
#define VIBE_OTHELLO_TOOLS_ARENA_CORE_H_

#include "vibe_othello/board_core/board.h"
#include "vibe_othello/search/score.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vibe_othello::tools::arena {

struct BestMoveResponse {
  std::optional<board_core::Move> move;
  search::Score score = 0;
  search::Depth depth = 0;
};

struct Opening {
  std::string id;
  std::vector<board_core::Move> moves;
};

struct GameRecord {
  int game_id = 0;
  std::string opening;
  std::string black;
  std::string white;
  std::string moves;
  int black_discs = 0;
  int white_discs = 0;
  std::string winner;
  std::string candidate_result;
  int candidate_disc_diff = 0;
  std::string reason;
};

struct Summary {
  int games = 0;
  int candidate_wins = 0;
  int candidate_draws = 0;
  int candidate_losses = 0;
  double candidate_score = 0.0;
  double candidate_win_rate = 0.0;
  double candidate_avg_disc_diff = 0.0;
  int invalid_games = 0;
};

[[nodiscard]] std::optional<board_core::Square> parse_square(std::string_view text) noexcept;
[[nodiscard]] std::optional<board_core::Move> parse_move_token(std::string_view text) noexcept;
[[nodiscard]] std::string format_move(board_core::Move move);
[[nodiscard]] std::string format_moves(std::span<const board_core::Move> moves);
[[nodiscard]] std::optional<BestMoveResponse> parse_bestmove_response(std::string_view line);
[[nodiscard]] bool replay_moves(std::span<const board_core::Move> moves,
                                board_core::Position* position, std::string* error);
[[nodiscard]] std::optional<std::vector<Opening>> parse_openings_file(std::string_view content,
                                                                      std::string* error);
[[nodiscard]] Summary summarize(std::span<const GameRecord> games) noexcept;
[[nodiscard]] std::string json_escape(std::string_view text);

} // namespace vibe_othello::tools::arena

#endif // VIBE_OTHELLO_TOOLS_ARENA_CORE_H_
