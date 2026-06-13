#include "vibe_othello/board_core/serialization.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string_view>

namespace vibe_othello::board_core {
namespace {

constexpr Square square(int file, int rank) noexcept {
  return square_from_file_rank(file, rank);
}

void require_parse_rejects(std::string_view text) {
  REQUIRE_FALSE(parse_position(text).has_value());
}

TEST_CASE("initial position is formatted canonically", "[board_core][serialization]") {
  REQUIRE(format_position(initial_position()) ==
          "......../......../......../...BW.../...WB.../......../......../........ b");
}

TEST_CASE("white-to-move positions are formatted from absolute colors",
          "[board_core][serialization]") {
  constexpr Position position{
      .player = initial_white_discs(),
      .opponent = initial_black_discs(),
      .side_to_move = Color::white,
  };

  REQUIRE(format_position(position) ==
          "......../......../......../...BW.../...WB.../......../......../........ w");
}

TEST_CASE("positions with discs on board edges are formatted by rank then file",
          "[board_core][serialization]") {
  constexpr Position position{
      .player = bit(square(0, 0)) | bit(square(7, 7)),
      .opponent = bit(square(7, 0)) | bit(square(0, 7)),
      .side_to_move = Color::black,
  };

  REQUIRE(format_position(position) ==
          "W......B/......../......../......../......../......../......../B......W b");
}

TEST_CASE("initial position is parsed from canonical text", "[board_core][serialization]") {
  const std::optional<Position> position =
      parse_position("......../......../......../...BW.../...WB.../......../......../........ b");

  REQUIRE(position.has_value());
  REQUIRE(*position == initial_position());
}

TEST_CASE("white-to-move text parses into relative white player view",
          "[board_core][serialization]") {
  const std::optional<Position> position =
      parse_position("......../......../......../...BW.../...WB.../......../......../........ w");

  REQUIRE(position.has_value());
  REQUIRE(position->player == initial_white_discs());
  REQUIRE(position->opponent == initial_black_discs());
  REQUIRE(position->side_to_move == Color::white);
}

TEST_CASE("format and parse round-trip positions", "[board_core][serialization]") {
  constexpr Position black_to_move = initial_position();
  constexpr Position white_to_move{
      .player = initial_white_discs(),
      .opponent = initial_black_discs(),
      .side_to_move = Color::white,
  };
  constexpr Position edge_position{
      .player = bit(square(0, 0)) | bit(square(7, 7)),
      .opponent = bit(square(7, 0)) | bit(square(0, 7)),
      .side_to_move = Color::black,
  };

  REQUIRE(parse_position(format_position(black_to_move)) == black_to_move);
  REQUIRE(parse_position(format_position(white_to_move)) == white_to_move);
  REQUIRE(parse_position(format_position(edge_position)) == edge_position);
}

TEST_CASE("parse then format emits canonical text", "[board_core][serialization]") {
  constexpr std::string_view text =
      "B......./......../......../......../......../......../......../.......W b";
  const std::optional<Position> position = parse_position(text);

  REQUIRE(position.has_value());
  REQUIRE(format_position(*position) == text);
}

TEST_CASE("parser rejects invalid board shapes", "[board_core][serialization]") {
  require_parse_rejects("");
  require_parse_rejects("......../......../......../...BW.../...WB.../......../........ b");
  require_parse_rejects(
      "......../......../......../...BW.../...WB.../......../......../......... b");
  require_parse_rejects("......../......../......../...BW.../...WB.../......../......../........");
  require_parse_rejects(
      "......../......../......../...BW.../...WB.../......../......../........  b");
  require_parse_rejects(
      "......../......../......../...BW.../...WB.../......../......../........ b\n");
}

TEST_CASE("parser rejects invalid separators and characters", "[board_core][serialization]") {
  require_parse_rejects(
      "........-......../......../...BW.../...WB.../......../......../........ b");
  require_parse_rejects(
      "......../......../......../...BX.../...WB.../......../......../........ b");
  require_parse_rejects(
      "......../......../......../...BW.../...WB.../......../......../........ x");
  require_parse_rejects(
      "......../......../......../...BW.../...WB.../......../......../........ B");
}

} // namespace
} // namespace vibe_othello::board_core
