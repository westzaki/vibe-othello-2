#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/hash.h"
#include "vibe_othello/board_core/serialization.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>

namespace vibe_othello::board_core {
namespace {

constexpr Square square(int file, int rank) noexcept {
  return square_from_file_rank(file, rank);
}

TEST_CASE("position hash is stable for the same position", "[board_core][hash]") {
  const Position position = initial_position();

  REQUIRE(hash_position(position) == hash_position(position));
}

TEST_CASE("position hash includes side to move", "[board_core][hash]") {
  constexpr Position black_to_move = initial_position();
  constexpr Position white_to_move{
      .player = initial_white_discs(),
      .opponent = initial_black_discs(),
      .side_to_move = Color::white,
  };

  REQUIRE(hash_position(black_to_move) != hash_position(white_to_move));
}

TEST_CASE("position hash includes absolute disc colors", "[board_core][hash]") {
  constexpr Position normal_colors = initial_position();
  constexpr Position swapped_colors{
      .player = initial_white_discs(),
      .opponent = initial_black_discs(),
      .side_to_move = Color::black,
  };

  REQUIRE(hash_position(normal_colors) != hash_position(swapped_colors));
}

TEST_CASE("position hash includes occupied squares", "[board_core][hash]") {
  constexpr Position initial = initial_position();
  constexpr Position edge_position{
      .player = bit(square(0, 0)) | bit(square(7, 7)),
      .opponent = bit(square(7, 0)) | bit(square(0, 7)),
      .side_to_move = Color::black,
  };

  REQUIRE(hash_position(initial) != hash_position(edge_position));
}

TEST_CASE("position hash survives serialization round-trips", "[board_core][hash]") {
  constexpr Position position{
      .player = bit(square(0, 0)) | bit(square(7, 7)) | bit(square(3, 4)),
      .opponent = bit(square(7, 0)) | bit(square(0, 7)) | bit(square(4, 3)),
      .side_to_move = Color::black,
  };

  const std::optional<Position> round_tripped = parse_position(format_position(position));

  REQUIRE(round_tripped.has_value());
  REQUIRE(hash_position(*round_tripped) == hash_position(position));
}

TEST_CASE("position hash changes after apply and restores after undo", "[board_core][hash]") {
  Position position = initial_position();
  const PositionHash before_hash = hash_position(position);
  MoveDelta delta{};

  REQUIRE(apply_move(&position, make_move(square(3, 2)), &delta));
  REQUIRE(hash_position(position) != before_hash);

  undo_move(&position, delta);
  REQUIRE(hash_position(position) == before_hash);
  REQUIRE(position == initial_position());
}

} // namespace
} // namespace vibe_othello::board_core
