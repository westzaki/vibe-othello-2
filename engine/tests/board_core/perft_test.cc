#include "board_core/perft.h"
#include "vibe_othello/board_core/board.h"

#include <catch2/catch_test_macros.hpp>
#include <initializer_list>

namespace vibe_othello::board_core {
namespace {

using test_support::perft;
using test_support::reference_perft;

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

void require_perft_matches_reference(Position position, int max_depth) {
  for (int depth = 0; depth <= max_depth; ++depth) {
    REQUIRE(perft(position, depth) == reference_perft(position, depth));
  }
}

TEST_CASE("perft counts initial position shallow depths", "[board_core][perft]") {
  REQUIRE(perft(initial_position(), 0) == 1);
  REQUIRE(perft(initial_position(), 1) == 4);
  REQUIRE(perft(initial_position(), 2) == 12);
  REQUIRE(perft(initial_position(), 3) == 56);
  REQUIRE(perft(initial_position(), 4) == 244);
  REQUIRE(perft(initial_position(), 5) == 1396);
  REQUIRE(reference_perft(initial_position(), 3) == 56);
}

TEST_CASE("perft matches reference from representative positions", "[board_core][perft]") {
  require_perft_matches_reference(initial_position(), 3);
  require_perft_matches_reference(
      Position{
          .player = initial_white_discs(),
          .opponent = initial_black_discs(),
          .side_to_move = Color::white,
      },
      3);
  require_perft_matches_reference(
      Position{
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
      },
      2);
}

TEST_CASE("perft counts pass as one ply", "[board_core][perft]") {
  constexpr Position pass_available{
      .player = bit(square(1, 0)),
      .opponent = bit(square(0, 0)),
      .side_to_move = Color::black,
  };

  REQUIRE(perft(pass_available, 0) == 1);
  REQUIRE(perft(pass_available, 1) == 1);
  REQUIRE(perft(pass_available, 2) == 1);
  REQUIRE(perft(pass_available, 3) == 0);
  require_perft_matches_reference(pass_available, 3);
}

TEST_CASE("perft returns zero for terminal positions above depth zero", "[board_core][perft]") {
  constexpr Position full{
      .player = 0x5555555555555555ULL,
      .opponent = 0xAAAAAAAAAAAAAAAAULL,
      .side_to_move = Color::black,
  };
  constexpr Position empty{
      .player = 0,
      .opponent = 0,
      .side_to_move = Color::black,
  };

  REQUIRE(perft(full, 0) == 1);
  REQUIRE(perft(full, 1) == 0);
  REQUIRE(perft(empty, 0) == 1);
  REQUIRE(perft(empty, 1) == 0);
  require_perft_matches_reference(full, 2);
  require_perft_matches_reference(empty, 2);
}

TEST_CASE("perft rejects invalid positions as terminal-like dead ends", "[board_core][perft]") {
  constexpr Position overlapping{
      .player = bit(square(0, 0)),
      .opponent = bit(square(0, 0)),
      .side_to_move = Color::black,
  };

  REQUIRE(perft(overlapping, 0) == 1);
  REQUIRE(perft(overlapping, 1) == 0);
  require_perft_matches_reference(overlapping, 2);
}

} // namespace
} // namespace vibe_othello::board_core
