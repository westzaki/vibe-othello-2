#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/tiny_pattern_evaluator.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace vibe_othello::evaluation {
namespace {

using board_core::bit;
using board_core::Color;
using board_core::Position;
using board_core::square_from_file_rank;

constexpr board_core::Square square(int file, int rank) noexcept {
  return square_from_file_rank(file, rank);
}

constexpr board_core::Bitboard center_filler() noexcept {
  return bit(square(3, 1)) | bit(square(4, 1)) | bit(square(3, 2)) | bit(square(4, 2)) |
         bit(square(1, 3)) | bit(square(2, 3)) | bit(square(3, 3)) | bit(square(4, 3)) |
         bit(square(5, 3)) | bit(square(6, 3)) | bit(square(1, 4)) | bit(square(2, 4)) |
         bit(square(3, 4)) | bit(square(4, 4)) | bit(square(5, 4)) | bit(square(6, 4)) |
         bit(square(3, 5)) | bit(square(4, 5)) | bit(square(3, 6));
}

TEST_CASE("ternary pattern index uses empty player opponent digits", "[evaluation][tiny_pattern]") {
  constexpr std::array<PatternCell, 3> cells{
      PatternCell::empty,
      PatternCell::player,
      PatternCell::opponent,
  };

  STATIC_REQUIRE(ternary_pattern_index(cells) == 21);
}

TEST_CASE("ternary pattern index reads position from side to move perspective",
          "[evaluation][tiny_pattern]") {
  constexpr Position position{
      .player = bit(square(1, 0)),
      .opponent = bit(square(2, 0)),
      .side_to_move = Color::black,
  };
  constexpr std::array<board_core::Square, 3> squares{
      square(0, 0),
      square(1, 0),
      square(2, 0),
  };

  REQUIRE(ternary_pattern_index(position, squares) == 21);
}

TEST_CASE("tiny pattern evaluator is deterministic", "[evaluation][tiny_pattern]") {
  const TinyPatternEvaluator evaluator;
  constexpr Position position{
      .player = bit(square(0, 0)) | bit(square(1, 0)),
      .opponent = bit(square(7, 7)),
      .side_to_move = Color::black,
  };

  const search::Score first = evaluator.evaluate(position);
  const search::Score second = evaluator.evaluate(position);

  REQUIRE(first == second);
}

TEST_CASE("tiny pattern evaluator scores are side to move relative", "[evaluation][tiny_pattern]") {
  const TinyPatternEvaluator evaluator;
  constexpr Position player_corner{
      .player = bit(square(0, 0)),
      .opponent = 0,
      .side_to_move = Color::black,
  };
  constexpr Position opponent_corner{
      .player = 0,
      .opponent = bit(square(0, 0)),
      .side_to_move = Color::white,
  };

  REQUIRE(evaluator.evaluate(player_corner) == 3);
  REQUIRE(evaluator.evaluate(opponent_corner) == -3);
}

TEST_CASE("tiny pattern evaluator phase boundary changes weights", "[evaluation][tiny_pattern]") {
  const TinyPatternEvaluator evaluator;
  constexpr Position early{
      .player = bit(square(0, 0)),
      .opponent = 0,
      .side_to_move = Color::black,
  };
  constexpr Position late{
      .player = bit(square(0, 0)) | center_filler(),
      .opponent = 0,
      .side_to_move = Color::black,
  };

  REQUIRE(evaluator.evaluate(early) == 3);
  REQUIRE(evaluator.evaluate(late) == 6);
}

TEST_CASE("tiny pattern evaluator stays inside search sentinels", "[evaluation][tiny_pattern]") {
  const TinyPatternEvaluator evaluator;
  constexpr Position dense_position{
      .player = UINT64_C(0x5555555555555555),
      .opponent = UINT64_C(0xaaaaaaaaaaaaaaaa),
      .side_to_move = Color::black,
  };

  const search::Score score = evaluator.evaluate(dense_position);

  REQUIRE(score > search::kScoreLoss);
  REQUIRE(score < search::kScoreWin);
}

} // namespace
} // namespace vibe_othello::evaluation
