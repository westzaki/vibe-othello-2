#include "vibe_othello/board_core/position.h"

#include <catch2/catch_test_macros.hpp>

namespace vibe_othello::board_core {
namespace {

TEST_CASE("initial position is black to move", "[board_core][position]") {
  constexpr Position position = initial_position();

  STATIC_REQUIRE(position.side_to_move == Color::black);
  STATIC_REQUIRE(position.player == initial_black_discs());
  STATIC_REQUIRE(position.opponent == initial_white_discs());
}

TEST_CASE("initial discs match canonical Othello setup", "[board_core][position]") {
  constexpr Bitboard expected_black =
      bit(square_from_file_rank(3, 4)) | bit(square_from_file_rank(4, 3));
  constexpr Bitboard expected_white =
      bit(square_from_file_rank(4, 4)) | bit(square_from_file_rank(3, 3));

  STATIC_REQUIRE(initial_black_discs() == expected_black);
  STATIC_REQUIRE(initial_white_discs() == expected_white);
  STATIC_REQUIRE(occupied(initial_position()) == (expected_black | expected_white));
}

TEST_CASE("position validity rejects overlapping bitboards", "[board_core][position]") {
  STATIC_REQUIRE(is_valid(initial_position()));

  constexpr Position overlapping{
      .player = bit(square_from_file_rank(0, 0)),
      .opponent = bit(square_from_file_rank(0, 0)),
      .side_to_move = Color::black,
  };

  STATIC_REQUIRE_FALSE(is_valid(overlapping));
}

TEST_CASE("black and white views are absolute colors", "[board_core][position]") {
  constexpr Position black_to_move = initial_position();
  constexpr Position white_to_move{
      .player = initial_white_discs(),
      .opponent = initial_black_discs(),
      .side_to_move = Color::white,
  };

  STATIC_REQUIRE(black_discs(black_to_move) == initial_black_discs());
  STATIC_REQUIRE(white_discs(black_to_move) == initial_white_discs());
  STATIC_REQUIRE(black_discs(white_to_move) == initial_black_discs());
  STATIC_REQUIRE(white_discs(white_to_move) == initial_white_discs());
}

TEST_CASE("opposite color toggles side", "[board_core][position]") {
  STATIC_REQUIRE(opposite(Color::black) == Color::white);
  STATIC_REQUIRE(opposite(Color::white) == Color::black);
}

} // namespace
} // namespace vibe_othello::board_core
