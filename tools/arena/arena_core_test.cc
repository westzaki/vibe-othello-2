#include "arena_core.h"

#include <catch2/catch_test_macros.hpp>

namespace vibe_othello::tools::arena {
namespace {

TEST_CASE("coordinate parser accepts board coordinates", "[arena]") {
  const std::optional<board_core::Square> square = parse_square("d3");

  REQUIRE(square.has_value());
  REQUIRE(board_core::file_of(*square) == 3);
  REQUIRE(board_core::rank_of(*square) == 2);
  REQUIRE_FALSE(parse_square("i3").has_value());
  REQUIRE_FALSE(parse_square("d9").has_value());
}

TEST_CASE("bestmove parser accepts machine readable output", "[arena]") {
  const std::optional<BestMoveResponse> move =
      parse_bestmove_response("bestmove e6 score 12 depth 4");

  REQUIRE(move.has_value());
  REQUIRE(move->move == board_core::make_move(*parse_square("e6")));
  REQUIRE(move->score == 12);
  REQUIRE(move->depth == 4);

  const std::optional<BestMoveResponse> pass =
      parse_bestmove_response("bestmove pass score 0 depth 4");
  REQUIRE(pass.has_value());
  REQUIRE(pass->move == board_core::make_pass());
  const std::optional<BestMoveResponse> none =
      parse_bestmove_response("bestmove none score 58 depth 4");
  REQUIRE(none.has_value());
  REQUIRE_FALSE(none->move.has_value());
  REQUIRE_FALSE(parse_bestmove_response("info bestmove e6").has_value());
}

TEST_CASE("opening parser accepts comments ids and shorthand", "[arena]") {
  std::string error;
  const std::optional<std::vector<Opening>> openings =
      parse_openings_file("# comment\nstart:\nf5:\nd3 c3:\ncustom: c4 c3\n", &error);

  REQUIRE(openings.has_value());
  REQUIRE(openings->size() == 4);
  REQUIRE((*openings)[0].id == "start");
  REQUIRE((*openings)[0].moves.empty());
  REQUIRE((*openings)[1].id == "f5");
  REQUIRE((*openings)[1].moves.size() == 1);
  REQUIRE((*openings)[2].moves.size() == 2);
  REQUIRE((*openings)[3].id == "custom");
}

TEST_CASE("opening parser rejects illegal openings", "[arena]") {
  std::string error;
  const std::optional<std::vector<Opening>> openings = parse_openings_file("bad: a1\n", &error);

  REQUIRE_FALSE(openings.has_value());
  REQUIRE_FALSE(error.empty());
}

TEST_CASE("summary aggregates candidate results", "[arena]") {
  const std::vector<GameRecord> games{
      GameRecord{.candidate_result = "win", .candidate_disc_diff = 6, .reason = "terminal"},
      GameRecord{.candidate_result = "draw", .candidate_disc_diff = 0, .reason = "terminal"},
      GameRecord{.candidate_result = "loss", .candidate_disc_diff = -64, .reason = "timeout"},
  };

  const Summary summary = summarize(games);

  REQUIRE(summary.games == 3);
  REQUIRE(summary.candidate_wins == 1);
  REQUIRE(summary.candidate_draws == 1);
  REQUIRE(summary.candidate_losses == 1);
  REQUIRE(summary.candidate_score == 1.5);
  REQUIRE(summary.candidate_win_rate == 1.0 / 3.0);
  REQUIRE(summary.invalid_games == 1);
}

} // namespace
} // namespace vibe_othello::tools::arena
