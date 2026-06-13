#include "vibe_othello/board_core/board.h"

#include <catch2/catch_test_macros.hpp>

namespace vibe_othello::board_core {
namespace {

constexpr Square square(int file, int rank) noexcept {
  return square_from_file_rank(file, rank);
}

TEST_CASE("pass moves swap perspective without changing discs", "[board_core][pass_terminal]") {
  Position position{
      .player = bit(square(1, 0)),
      .opponent = bit(square(0, 0)),
      .side_to_move = Color::black,
  };
  const Position before = position;
  MoveDelta delta{};

  REQUIRE_FALSE(has_legal_move(position));
  REQUIRE(has_legal_move(Position{
      .player = before.opponent,
      .opponent = before.player,
      .side_to_move = Color::white,
  }));
  REQUIRE(apply_pass(&position, &delta));

  REQUIRE(delta.move == make_pass());
  REQUIRE(delta.flipped == 0);
  REQUIRE(position.side_to_move == Color::white);
  REQUIRE(position.player == before.opponent);
  REQUIRE(position.opponent == before.player);
  REQUIRE(occupied(position) == occupied(before));
  REQUIRE(is_valid(position));

  undo_move(&position, delta);
  REQUIRE(position == before);
}

TEST_CASE("apply move accepts pass moves", "[board_core][pass_terminal]") {
  Position position{
      .player = bit(square(1, 0)),
      .opponent = bit(square(0, 0)),
      .side_to_move = Color::black,
  };
  const Position before = position;
  MoveDelta delta{};

  REQUIRE(apply_move(&position, make_pass(), &delta));

  REQUIRE(delta.move == make_pass());
  REQUIRE(delta.flipped == 0);
  REQUIRE(position.player == before.opponent);
  REQUIRE(position.opponent == before.player);
  REQUIRE(position.side_to_move == Color::white);
}

TEST_CASE("pass deltas can be prepared and applied separately", "[board_core][pass_terminal]") {
  Position position{
      .player = bit(square(1, 0)),
      .opponent = bit(square(0, 0)),
      .side_to_move = Color::black,
  };
  const Position before = position;
  MoveDelta delta{};

  REQUIRE(make_move_delta(position, make_pass(), &delta));
  REQUIRE(position == before);
  REQUIRE(delta.move == make_pass());
  REQUIRE(delta.flipped == 0);

  apply_move_delta(&position, delta);
  REQUIRE(position.player == before.opponent);
  REQUIRE(position.opponent == before.player);
  REQUIRE(position.side_to_move == Color::white);
}

TEST_CASE("pass moves are rejected unless exactly one side can move",
          "[board_core][pass_terminal]") {
  const Position before = initial_position();
  Position position = before;
  MoveDelta delta{};

  REQUIRE_FALSE(apply_pass(&position, &delta));
  REQUIRE(position == before);

  Position terminal{
      .player = 0x5555555555555555ULL,
      .opponent = 0xAAAAAAAAAAAAAAAAULL,
      .side_to_move = Color::black,
  };
  const Position terminal_before = terminal;
  REQUIRE_FALSE(apply_move(&terminal, make_pass(), &delta));
  REQUIRE(terminal == terminal_before);

  Position overlapping{
      .player = bit(square(0, 0)),
      .opponent = bit(square(0, 0)),
      .side_to_move = Color::black,
  };
  const Position invalid_before = overlapping;
  REQUIRE_FALSE(apply_pass(&overlapping, &delta));
  REQUIRE(overlapping == invalid_before);

  REQUIRE_FALSE(apply_pass(nullptr, &delta));
  REQUIRE_FALSE(apply_pass(&position, nullptr));
  REQUIRE_FALSE(make_move_delta(position, make_pass(), &delta));
  REQUIRE_FALSE(make_move_delta(position, make_pass(), nullptr));
  REQUIRE(position == before);
}

TEST_CASE("legal move availability is reported", "[board_core][pass_terminal]") {
  constexpr Position no_move{
      .player = bit(square(1, 0)),
      .opponent = bit(square(0, 0)),
      .side_to_move = Color::black,
  };
  constexpr Position overlapping{
      .player = bit(square(0, 0)),
      .opponent = bit(square(0, 0)),
      .side_to_move = Color::black,
  };

  REQUIRE(has_legal_move(initial_position()));
  REQUIRE_FALSE(has_legal_move(no_move));
  REQUIRE_FALSE(has_legal_move(overlapping));
}

TEST_CASE("terminal positions require neither side to have a legal move",
          "[board_core][pass_terminal]") {
  constexpr Position pass_available{
      .player = bit(square(1, 0)),
      .opponent = bit(square(0, 0)),
      .side_to_move = Color::black,
  };
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
  constexpr Position overlapping{
      .player = bit(square(0, 0)),
      .opponent = bit(square(0, 0)),
      .side_to_move = Color::black,
  };

  REQUIRE_FALSE(is_terminal(initial_position()));
  REQUIRE_FALSE(is_terminal(pass_available));
  REQUIRE(is_terminal(full));
  REQUIRE(is_terminal(empty));
  REQUIRE_FALSE(is_terminal(overlapping));
}

} // namespace
} // namespace vibe_othello::board_core
