#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/serialization.h"
#include "vibe_othello/evaluation/early_midgame_heuristic_evaluator.h"
#include "vibe_othello/search/score.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>

namespace vibe_othello::evaluation {
namespace {

constexpr board_core::Square square(int file, int rank) noexcept {
  return board_core::square_from_file_rank(file, rank);
}

constexpr board_core::Position swap_sides(board_core::Position position) noexcept {
  return board_core::Position{
      .player = position.opponent,
      .opponent = position.player,
      .side_to_move = board_core::opposite(position.side_to_move),
  };
}

TEST_CASE("early-midgame heuristic is deterministic and side-to-move relative",
          "[evaluation][heuristic]") {
  const EarlyMidgameHeuristicEvaluator evaluator;
  constexpr board_core::Position position{
      .player = board_core::bit(square(0, 0)) | board_core::bit(square(3, 3)) |
                board_core::bit(square(5, 4)),
      .opponent = board_core::bit(square(1, 0)) | board_core::bit(square(2, 2)) |
                  board_core::bit(square(6, 5)),
      .side_to_move = board_core::Color::black,
  };

  const search::Score first = evaluator.evaluate(position);
  const search::Score second = evaluator.evaluate(position);

  REQUIRE(first == second);
  REQUIRE(first != 0);
  REQUIRE(evaluator.evaluate(swap_sides(position)) == -first);
}

TEST_CASE("early-midgame heuristic penalizes X and C squares beside an empty corner",
          "[evaluation][heuristic]") {
  const EarlyMidgameHeuristicEvaluator evaluator;
  constexpr board_core::Position player_danger{
      .player = board_core::bit(square(1, 1)) | board_core::bit(square(1, 0)) |
                board_core::bit(square(0, 1)),
      .opponent = 0,
      .side_to_move = board_core::Color::black,
  };

  const search::Score score = evaluator.evaluate(player_danger);

  REQUIRE(score < 0);
  REQUIRE(evaluator.evaluate(swap_sides(player_danger)) == -score);
}

TEST_CASE("early-midgame heuristic does not collapse the checked-in midgame to zero",
          "[evaluation][heuristic]") {
  const EarlyMidgameHeuristicEvaluator evaluator;
  const std::optional<board_core::Position> position = board_core::parse_position(
      "......../......../..W.W.../...WWWBW/.BBWWBW./BBBWWWW./...BB.W./....B... b");
  REQUIRE(position.has_value());

  REQUIRE(evaluator.evaluate(*position) != 0);
}

TEST_CASE("early-midgame heuristic remains inside search sentinels", "[evaluation][heuristic]") {
  const EarlyMidgameHeuristicEvaluator evaluator;
  constexpr std::array<board_core::Position, 3> kPositions{
      board_core::initial_position(),
      board_core::Position{
          .player = UINT64_C(0x5555555555555555),
          .opponent = UINT64_C(0xaaaaaaaaaaaaaaaa),
          .side_to_move = board_core::Color::black,
      },
      board_core::Position{
          .player = UINT64_C(0x0000000003ffffff),
          .opponent = UINT64_C(0x000ffffffc000000),
          .side_to_move = board_core::Color::white,
      },
  };

  for (const board_core::Position position : kPositions) {
    const search::Score score = evaluator.evaluate(position);
    REQUIRE(score > search::kScoreLoss);
    REQUIRE(score < search::kScoreWin);
  }
}

} // namespace
} // namespace vibe_othello::evaluation
