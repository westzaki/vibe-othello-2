#include "arena_core.h"
#include "nboard_protocol.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

namespace vibe_othello::tools::arena {
namespace {

TEST_CASE("NBoard move parser accepts Edax and NTest response forms", "[arena][nboard]") {
  const auto edax = parse_nboard_move_response("=== D3 1.25 10.0");
  const auto ntest = parse_nboard_move_response("=== F5/-0.25/0.01");
  const auto pass = parse_nboard_move_response("=== PA");

  REQUIRE(edax == board_core::make_move(*parse_square("d3")));
  REQUIRE(ntest == board_core::make_move(*parse_square("f5")));
  REQUIRE(pass == board_core::make_pass());
  REQUIRE_FALSE(parse_nboard_move_response("status waiting").has_value());
  REQUIRE_FALSE(parse_nboard_move_response("=== Z9").has_value());
}

TEST_CASE("NBoard GGF formatter emits legal color-tagged moves", "[arena][nboard]") {
  const std::vector<board_core::Move> moves{
      board_core::make_move(*parse_square("f5")),
      board_core::make_move(*parse_square("f6")),
  };
  std::string error;

  const auto ggf = format_nboard_ggf(moves, &error);

  REQUIRE(ggf.has_value());
  REQUIRE(ggf->find("BO[8 ") != std::string::npos);
  REQUIRE(ggf->find("B[F5]W[F6]") != std::string::npos);
  REQUIRE(ggf->ends_with(";)"));
}

TEST_CASE("NBoard GGF formatter rejects illegal move sequences", "[arena][nboard]") {
  const std::vector<board_core::Move> moves{
      board_core::make_move(*parse_square("a1")),
  };
  std::string error;

  REQUIRE_FALSE(format_nboard_ggf(moves, &error).has_value());
  REQUIRE_FALSE(error.empty());
}

} // namespace
} // namespace vibe_othello::tools::arena
