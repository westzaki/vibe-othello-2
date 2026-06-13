#include "board_core/reference_board.h"
#include "vibe_othello/board_core/board.h"

#include <catch2/catch_test_macros.hpp>
#include <initializer_list>

namespace vibe_othello::board_core {
namespace {

using test_support::reference_apply_move;
using test_support::reference_flips_for_move;
using test_support::reference_has_legal_move;
using test_support::reference_is_terminal;
using test_support::reference_legal_moves;

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

void require_reference_matches(Position position) {
  REQUIRE(legal_moves(position) == reference_legal_moves(position));
  REQUIRE(has_legal_move(position) == reference_has_legal_move(position));
  REQUIRE(is_terminal(position) == reference_is_terminal(position));

  for (int index = 0; index < kSquareCount; ++index) {
    const Square move = square_from_index(index);
    REQUIRE(flips_for_move(position, move) == reference_flips_for_move(position, move));
  }
}

TEST_CASE("reference board matches initial positions", "[board_core][differential]") {
  require_reference_matches(initial_position());
  require_reference_matches(Position{
      .player = initial_white_discs(),
      .opponent = initial_black_discs(),
      .side_to_move = Color::white,
  });
}

TEST_CASE("reference board matches edge and wraparound positions", "[board_core][differential]") {
  require_reference_matches(Position{
      .player = bit(square(7, 0)),
      .opponent = bit(square(6, 0)),
      .side_to_move = Color::black,
  });
  require_reference_matches(Position{
      .player = bit(square(7, 0)),
      .opponent = bit(square(0, 1)),
      .side_to_move = Color::black,
  });
  require_reference_matches(Position{
      .player = bit(square(7, 0)),
      .opponent = bit(square(0, 2)),
      .side_to_move = Color::black,
  });
}

TEST_CASE("reference board matches multi-direction positions", "[board_core][differential]") {
  require_reference_matches(Position{
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
  });
}

TEST_CASE("reference board matches pass and terminal positions", "[board_core][differential]") {
  require_reference_matches(Position{
      .player = bit(square(1, 0)),
      .opponent = bit(square(0, 0)),
      .side_to_move = Color::black,
  });
  require_reference_matches(Position{
      .player = 0x5555555555555555ULL,
      .opponent = 0xAAAAAAAAAAAAAAAAULL,
      .side_to_move = Color::black,
  });
  require_reference_matches(Position{
      .player = 0,
      .opponent = 0,
      .side_to_move = Color::black,
  });
}

TEST_CASE("reference board matches invalid position guards", "[board_core][differential]") {
  require_reference_matches(Position{
      .player = bit(square(0, 0)),
      .opponent = bit(square(0, 0)),
      .side_to_move = Color::black,
  });
}

TEST_CASE("reference board matches move application", "[board_core][differential]") {
  Position production = initial_position();
  Position reference = production;
  MoveDelta production_delta{};
  MoveDelta reference_delta{};

  REQUIRE(apply_move(&production, make_move(square(2, 3)), &production_delta) ==
          reference_apply_move(&reference, make_move(square(2, 3)), &reference_delta));
  REQUIRE(production == reference);
  REQUIRE(production_delta == reference_delta);

  production = Position{
      .player = bit(square(1, 0)),
      .opponent = bit(square(0, 0)),
      .side_to_move = Color::black,
  };
  reference = production;

  REQUIRE(apply_move(&production, make_pass(), &production_delta) ==
          reference_apply_move(&reference, make_pass(), &reference_delta));
  REQUIRE(production == reference);
  REQUIRE(production_delta == reference_delta);
}

} // namespace
} // namespace vibe_othello::board_core
