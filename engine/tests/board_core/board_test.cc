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

TEST_CASE("initial black move flips are generated", "[board_core][board]") {
  constexpr Position position = initial_position();

  REQUIRE(flips_for_move(position, square(2, 3)) == bit(square(3, 3)));
  REQUIRE(flips_for_move(position, square(3, 2)) == bit(square(3, 3)));
  REQUIRE(flips_for_move(position, square(4, 5)) == bit(square(4, 4)));
  REQUIRE(flips_for_move(position, square(5, 4)) == bit(square(4, 4)));
}

TEST_CASE("normal moves are applied from relative position", "[board_core][board]") {
  Position position = initial_position();
  MoveDelta delta{};

  REQUIRE(apply_move(&position, make_move(square(2, 3)), &delta));

  REQUIRE(delta.before == initial_position());
  REQUIRE(delta.move == make_move(square(2, 3)));
  REQUIRE(delta.flipped == bit(square(3, 3)));
  REQUIRE(position.side_to_move == Color::white);
  REQUIRE(black_discs(position) == squares({
                                       square(2, 3),
                                       square(3, 3),
                                       square(3, 4),
                                       square(4, 3),
                                   }));
  REQUIRE(white_discs(position) == bit(square(4, 4)));
  REQUIRE(position.player == white_discs(position));
  REQUIRE(position.opponent == black_discs(position));
  REQUIRE(is_valid(position));
}

TEST_CASE("white relative moves are applied from relative position", "[board_core][board]") {
  Position position{
      .player = initial_white_discs(),
      .opponent = initial_black_discs(),
      .side_to_move = Color::white,
  };
  const Position before = position;
  MoveDelta delta{};

  REQUIRE(apply_move(&position, make_move(square(2, 4)), &delta));

  REQUIRE(delta.before == before);
  REQUIRE(delta.move == make_move(square(2, 4)));
  REQUIRE(delta.flipped == bit(square(3, 4)));
  REQUIRE(position.side_to_move == Color::black);
  REQUIRE(black_discs(position) == bit(square(4, 3)));
  REQUIRE(white_discs(position) == squares({
                                       square(2, 4),
                                       square(3, 4),
                                       square(3, 3),
                                       square(4, 4),
                                   }));
  REQUIRE(position.player == black_discs(position));
  REQUIRE(position.opponent == white_discs(position));
  REQUIRE(is_valid(position));
}

TEST_CASE("apply move rejects invalid normal moves without changing position",
          "[board_core][board]") {
  const Position before = initial_position();
  Position position = before;
  MoveDelta delta{};

  REQUIRE_FALSE(apply_move(&position, make_move(square(0, 0)), &delta));
  REQUIRE(position == before);

  REQUIRE_FALSE(apply_move(&position, make_move(square(3, 3)), &delta));
  REQUIRE(position == before);

  REQUIRE_FALSE(apply_move(&position, make_move(square_from_index(-1)), &delta));
  REQUIRE(position == before);

  REQUIRE_FALSE(apply_move(nullptr, make_move(square(2, 3)), &delta));
  REQUIRE_FALSE(apply_move(&position, make_move(square(2, 3)), nullptr));
  REQUIRE(position == before);

  Position overlapping{
      .player = bit(square(0, 0)),
      .opponent = bit(square(0, 0)),
      .side_to_move = Color::black,
  };
  const Position invalid_before = overlapping;
  REQUIRE_FALSE(apply_move(&overlapping, make_move(square(0, 1)), &delta));
  REQUIRE(overlapping == invalid_before);
}

TEST_CASE("initial white move flips are generated from relative position", "[board_core][board]") {
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

TEST_CASE("apply then undo restores the exact position", "[board_core][board]") {
  Position position = initial_position();
  const Position before = position;
  MoveDelta delta{};

  REQUIRE(apply_move(&position, make_move(square(2, 3)), &delta));
  REQUIRE(position != before);

  undo_move(&position, delta);
  REQUIRE(position == before);
}

TEST_CASE("undo move ignores null position pointers", "[board_core][board]") {
  MoveDelta delta{
      .before = initial_position(),
      .move = make_move(square(2, 3)),
      .flipped = bit(square(3, 3)),
  };

  undo_move(nullptr, delta);
}

TEST_CASE("legal moves are empty squares", "[board_core][board]") {
  const Position position = initial_position();

  REQUIRE((legal_moves(position) & occupied(position)) == 0);
}

TEST_CASE("legal moves match squares that flip discs", "[board_core][board]") {
  constexpr Position position = initial_position();
  Bitboard moves_from_flips = 0;

  for (int index = 0; index < kSquareCount; ++index) {
    const Square move = square_from_index(index);
    if (flips_for_move(position, move) != 0) {
      moves_from_flips |= bit(move);
    }
  }

  REQUIRE(moves_from_flips == legal_moves(position));
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

TEST_CASE("illegal moves flip no discs", "[board_core][board]") {
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

TEST_CASE("invalid positions return zero legal moves", "[board_core][board]") {
  constexpr Position overlapping{
      .player = bit(square(0, 0)),
      .opponent = bit(square(0, 0)),
      .side_to_move = Color::black,
  };

  REQUIRE(legal_moves(overlapping) == 0);
  REQUIRE(flips_for_move(overlapping, square(0, 1)) == 0);
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

TEST_CASE("flip calculation does not wrap across files", "[board_core][board]") {
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
  REQUIRE(flips_for_move(west_edge, square(5, 0)) == bit(square(6, 0)));
  REQUIRE(flips_for_move(south_west_edge, square(5, 5)) == bit(square(6, 6)));
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
  REQUIRE(flips_for_move(position, square(3, 0)) == squares({
                                                        square(1, 0),
                                                        square(2, 0),
                                                    }));
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
  REQUIRE(flips_for_move(position, square(1, 1)) == bit(square(2, 2)));
  REQUIRE(flips_for_move(position, square(3, 1)) == bit(square(3, 2)));
  REQUIRE(flips_for_move(position, square(5, 1)) == bit(square(4, 2)));
}

TEST_CASE("flip calculation handles multi-direction moves", "[board_core][board]") {
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

TEST_CASE("multi-direction moves apply and undo", "[board_core][board]") {
  Position position{
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
  const Position before = position;
  constexpr Bitboard flipped = squares({
      square(1, 3),
      square(2, 3),
      square(3, 1),
      square(3, 2),
      square(4, 3),
      square(5, 3),
      square(3, 4),
      square(3, 5),
  });
  MoveDelta delta{};

  REQUIRE(apply_move(&position, make_move(square(3, 3)), &delta));

  REQUIRE(delta.before == before);
  REQUIRE(delta.move == make_move(square(3, 3)));
  REQUIRE(delta.flipped == flipped);
  REQUIRE(position.side_to_move == Color::white);
  REQUIRE(black_discs(position) == (before.player | bit(square(3, 3)) | flipped));
  REQUIRE(white_discs(position) == 0);

  undo_move(&position, delta);
  REQUIRE(position == before);
}

} // namespace
} // namespace vibe_othello::board_core
