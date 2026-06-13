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

TEST_CASE("normal moves are applied from relative position", "[board_core][apply_undo]") {
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

TEST_CASE("white relative moves are applied from relative position", "[board_core][apply_undo]") {
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
          "[board_core][apply_undo]") {
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

TEST_CASE("apply then undo restores the exact position", "[board_core][apply_undo]") {
  Position position = initial_position();
  const Position before = position;
  MoveDelta delta{};

  REQUIRE(apply_move(&position, make_move(square(2, 3)), &delta));
  REQUIRE(position != before);

  undo_move(&position, delta);
  REQUIRE(position == before);
}

TEST_CASE("undo move ignores null position pointers", "[board_core][apply_undo]") {
  MoveDelta delta{
      .before = initial_position(),
      .move = make_move(square(2, 3)),
      .flipped = bit(square(3, 3)),
  };

  undo_move(nullptr, delta);
}

TEST_CASE("multi-direction moves apply and undo", "[board_core][apply_undo]") {
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
