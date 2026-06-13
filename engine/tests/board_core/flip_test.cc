#include "vibe_othello/board_core/board.h"

#include <catch2/catch_test_macros.hpp>
#include <initializer_list>

namespace vibe_othello::board_core {
namespace {

constexpr Bitboard squares(std::initializer_list<Square> squares) noexcept {
  Bitboard result = 0;
  for (Square square : squares) {
    result |= bit(square);
  }
  return result;
}

constexpr Square square(int file, int rank) noexcept {
  return square_from_file_rank(file, rank);
}

TEST_CASE("initial black move flips are generated", "[board_core][flip]") {
  constexpr Position position = initial_position();

  REQUIRE(flips_for_move(position, square(2, 3)) == bit(square(3, 3)));
  REQUIRE(flips_for_move(position, square(3, 2)) == bit(square(3, 3)));
  REQUIRE(flips_for_move(position, square(4, 5)) == bit(square(4, 4)));
  REQUIRE(flips_for_move(position, square(5, 4)) == bit(square(4, 4)));
}

TEST_CASE("initial white move flips are generated from relative position", "[board_core][flip]") {
  constexpr Position position{
      .player = initial_white_discs(),
      .opponent = initial_black_discs(),
      .side_to_move = Color::white,
  };

  REQUIRE(flips_for_move(position, square(2, 4)) == bit(square(3, 4)));
  REQUIRE(flips_for_move(position, square(3, 5)) == bit(square(3, 4)));
  REQUIRE(flips_for_move(position, square(4, 2)) == bit(square(4, 3)));
  REQUIRE(flips_for_move(position, square(5, 3)) == bit(square(4, 3)));
}

TEST_CASE("illegal moves flip no discs", "[board_core][flip]") {
  constexpr Position position = initial_position();
  constexpr Position open_run{
      .player = bit(square(3, 0)),
      .opponent = bit(square(1, 0)),
      .side_to_move = Color::black,
  };

  REQUIRE(flips_for_move(position, square(0, 0)) == 0);
  REQUIRE(flips_for_move(position, square(3, 3)) == 0);
  REQUIRE(flips_for_move(position, square_from_index(-1)) == 0);
  REQUIRE(flips_for_move(position, square_from_index(64)) == 0);
  REQUIRE(flips_for_move(open_run, square(0, 0)) == 0);
}

TEST_CASE("flip calculation does not wrap across files", "[board_core][flip]") {
  constexpr Position east_wrap{
      .player = bit(square(1, 1)),
      .opponent = bit(square(0, 1)),
      .side_to_move = Color::black,
  };
  constexpr Position north_east_wrap{
      .player = bit(square(1, 3)),
      .opponent = bit(square(0, 2)),
      .side_to_move = Color::black,
  };

  REQUIRE(flips_for_move(east_wrap, square(7, 0)) == 0);
  REQUIRE(flips_for_move(north_east_wrap, square(7, 0)) == 0);
}

TEST_CASE("flip calculation handles multi-direction moves", "[board_core][flip]") {
  constexpr Position position{
      .player = squares({
          square(0, 3),
          square(3, 0),
          square(6, 3),
          square(3, 6),
      }),
      .opponent = squares({
          square(1, 3),
          square(2, 3),
          square(3, 1),
          square(3, 2),
          square(4, 3),
          square(5, 3),
          square(3, 4),
          square(3, 5),
      }),
      .side_to_move = Color::black,
  };
  constexpr Bitboard expected = squares({
      square(1, 3),
      square(2, 3),
      square(3, 1),
      square(3, 2),
      square(4, 3),
      square(5, 3),
      square(3, 4),
      square(3, 5),
  });

  REQUIRE(legal_moves(position) == bit(square(3, 3)));
  REQUIRE(flips_for_move(position, square(3, 3)) == expected);
}

} // namespace
} // namespace vibe_othello::board_core
