#include "normalized_tsv.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <string_view>
#include <vector>

namespace vibe_othello::tools::pattern {
namespace {

TEST_CASE("normalized TSV text helpers preserve empty fields", "[pattern][tsv]") {
  REQUIRE(trim_trailing_cr("alpha\r") == "alpha");
  REQUIRE(trim_trailing_cr("alpha") == "alpha");
  REQUIRE(split_tabs("alpha\t\tbeta\t") == std::vector<std::string_view>{"alpha", "", "beta", ""});
  REQUIRE(parse_int("-12") == -12);
  REQUIRE_FALSE(parse_int("12x").has_value());
  REQUIRE(parse_u64("42") == 42);
  REQUIRE_FALSE(parse_u64("-1").has_value());
}

TEST_CASE("normalized TSV board parser validates the shared relative-board format",
          "[pattern][tsv]") {
  constexpr std::string_view kInitialBoard =
      "---------------------------OX------XO---------------------------";

  const std::optional<board_core::Position> position = position_from_a1_to_h8_board(kInitialBoard);

  REQUIRE(position.has_value());
  REQUIRE(std::popcount(position->player) == 2);
  REQUIRE(std::popcount(position->opponent) == 2);
  REQUIRE(position->side_to_move == board_core::Color::black);
  REQUIRE_FALSE(position_from_a1_to_h8_board(kInitialBoard.substr(1)).has_value());
  REQUIRE_FALSE(position_from_a1_to_h8_board(
                    "---------------------------O?------XO---------------------------")
                    .has_value());
}

} // namespace
} // namespace vibe_othello::tools::pattern
