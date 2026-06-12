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

TEST_CASE("initial black legal moves are generated", "[board_core][board]") {
  constexpr Bitboard expected = squares({
      square(2, 3),
      square(3, 2),
      square(4, 5),
      square(5, 4),
  });

  REQUIRE(legal_moves(initial_position()) == expected);
}

TEST_CASE("initial white legal moves are generated from relative position", "[board_core][board]") {
  constexpr Position position{
      .player = initial_white_discs(),
      .opponent = initial_black_discs(),
      .side_to_move = Color::white,
  };
  constexpr Bitboard expected = squares({
      square(2, 4),
      square(3, 5),
      square(4, 2),
      square(5, 3),
  });

  REQUIRE(legal_moves(position) == expected);
}

TEST_CASE("legal moves are empty squares", "[board_core][board]") {
  const Position position = initial_position();

  REQUIRE((legal_moves(position) & occupied(position)) == 0);
}

TEST_CASE("positions without legal moves return zero", "[board_core][board]") {
  constexpr Position empty{
      .player = 0,
      .opponent = 0,
      .side_to_move = Color::black,
  };
  constexpr Position full{
      .player = 0x5555555555555555ULL,
      .opponent = 0xAAAAAAAAAAAAAAAAULL,
      .side_to_move = Color::black,
  };
  constexpr Position no_flip{
      .player = bit(square(3, 3)),
      .opponent = bit(square(5, 3)),
      .side_to_move = Color::black,
  };

  REQUIRE(legal_moves(empty) == 0);
  REQUIRE(legal_moves(full) == 0);
  REQUIRE(legal_moves(no_flip) == 0);
}

TEST_CASE("invalid positions return zero legal moves", "[board_core][board]") {
  constexpr Position overlapping{
      .player = bit(square(0, 0)),
      .opponent = bit(square(0, 0)),
      .side_to_move = Color::black,
  };

  REQUIRE(legal_moves(overlapping) == 0);
}

TEST_CASE("legal move generation does not wrap across files", "[board_core][board]") {
  constexpr Position horizontal_wrap{
      .player = bit(square(7, 0)),
      .opponent = bit(square(0, 1)),
      .side_to_move = Color::black,
  };
  constexpr Position diagonal_wrap{
      .player = bit(square(7, 0)),
      .opponent = bit(square(0, 2)),
      .side_to_move = Color::black,
  };

  REQUIRE(legal_moves(horizontal_wrap) == 0);
  REQUIRE(legal_moves(diagonal_wrap) == 0);
}

TEST_CASE("legal move generation works along board edges", "[board_core][board]") {
  constexpr Position west_edge{
      .player = bit(square(7, 0)),
      .opponent = bit(square(6, 0)),
      .side_to_move = Color::black,
  };
  constexpr Position south_west_edge{
      .player = bit(square(7, 7)),
      .opponent = bit(square(6, 6)),
      .side_to_move = Color::black,
  };

  REQUIRE(legal_moves(west_edge) == bit(square(5, 0)));
  REQUIRE(legal_moves(south_west_edge) == bit(square(5, 5)));
}

TEST_CASE("legal move generation handles multi-disc runs", "[board_core][board]") {
  constexpr Position position{
      .player = bit(square(0, 0)),
      .opponent = squares({
          square(1, 0),
          square(2, 0),
      }),
      .side_to_move = Color::black,
  };

  REQUIRE(legal_moves(position) == bit(square(3, 0)));
}

TEST_CASE("legal move generation handles all eight directions", "[board_core][board]") {
  constexpr Square center = square(3, 3);
  constexpr Position position{
      .player = bit(center),
      .opponent = squares({
          square(2, 2),
          square(3, 2),
          square(4, 2),
          square(2, 3),
          square(4, 3),
          square(2, 4),
          square(3, 4),
          square(4, 4),
      }),
      .side_to_move = Color::black,
  };
  constexpr Bitboard expected = squares({
      square(1, 1),
      square(3, 1),
      square(5, 1),
      square(1, 3),
      square(5, 3),
      square(1, 5),
      square(3, 5),
      square(5, 5),
  });

  REQUIRE(legal_moves(position) == expected);
}

} // namespace
} // namespace vibe_othello::board_core
