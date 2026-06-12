#include "vibe_othello/board_core/coordinates.h"

#include <catch2/catch_test_macros.hpp>

namespace vibe_othello::board_core {
namespace {

TEST_CASE("squares use a1 as bit zero", "[board_core][coordinates]") {
  STATIC_REQUIRE(square_from_file_rank(0, 0).index == 0);
  STATIC_REQUIRE(square_from_file_rank(7, 0).index == 7);
  STATIC_REQUIRE(square_from_file_rank(0, 7).index == 56);
  STATIC_REQUIRE(square_from_file_rank(7, 7).index == 63);
}

TEST_CASE("file and rank are derived from square index", "[board_core][coordinates]") {
  constexpr Square d5 = square_from_file_rank(3, 4);

  STATIC_REQUIRE(file_of(d5) == 3);
  STATIC_REQUIRE(rank_of(d5) == 4);
}

TEST_CASE("coordinate validators reject invalid inputs", "[board_core][coordinates]") {
  STATIC_REQUIRE(is_valid_square_index(0));
  STATIC_REQUIRE(is_valid_square_index(63));
  STATIC_REQUIRE_FALSE(is_valid_square_index(-1));
  STATIC_REQUIRE_FALSE(is_valid_square_index(64));

  STATIC_REQUIRE(is_valid_file_rank(0, 0));
  STATIC_REQUIRE(is_valid_file_rank(7, 7));
  STATIC_REQUIRE_FALSE(is_valid_file_rank(-1, 0));
  STATIC_REQUIRE_FALSE(is_valid_file_rank(8, 0));
  STATIC_REQUIRE_FALSE(is_valid_file_rank(0, -1));
  STATIC_REQUIRE_FALSE(is_valid_file_rank(0, 8));

  STATIC_REQUIRE(is_valid(square_from_index(0)));
  STATIC_REQUIRE(is_valid(square_from_index(63)));
  STATIC_REQUIRE_FALSE(is_valid(square_from_index(-1)));
  STATIC_REQUIRE_FALSE(is_valid(square_from_index(64)));
  STATIC_REQUIRE_FALSE(is_valid(Square{kInvalidSquareIndex}));
  STATIC_REQUIRE_FALSE(is_valid(Square{255}));
}

TEST_CASE("invalid coordinates produce invalid squares", "[board_core][coordinates]") {
  STATIC_REQUIRE(square_from_index(-1).index == kInvalidSquareIndex);
  STATIC_REQUIRE(square_from_index(64).index == kInvalidSquareIndex);
  STATIC_REQUIRE(square_from_file_rank(-1, 0).index == kInvalidSquareIndex);
  STATIC_REQUIRE(square_from_file_rank(8, 0).index == kInvalidSquareIndex);
  STATIC_REQUIRE(square_from_file_rank(0, -1).index == kInvalidSquareIndex);
  STATIC_REQUIRE(square_from_file_rank(0, 8).index == kInvalidSquareIndex);
}

TEST_CASE("square bits follow documented bit order", "[board_core][coordinates]") {
  STATIC_REQUIRE(bit(square_from_file_rank(0, 0)) == 0x0000000000000001ULL);
  STATIC_REQUIRE(bit(square_from_file_rank(7, 0)) == 0x0000000000000080ULL);
  STATIC_REQUIRE(bit(square_from_file_rank(0, 7)) == 0x0100000000000000ULL);
  STATIC_REQUIRE(bit(square_from_file_rank(7, 7)) == 0x8000000000000000ULL);
}

TEST_CASE("invalid square helpers return safe sentinel values", "[board_core][coordinates]") {
  STATIC_REQUIRE(file_of(Square{kInvalidSquareIndex}) == -1);
  STATIC_REQUIRE(rank_of(Square{kInvalidSquareIndex}) == -1);
  STATIC_REQUIRE(bit(Square{kInvalidSquareIndex}) == 0);
  STATIC_REQUIRE(bit(Square{255}) == 0);
}

TEST_CASE("edge masks match the internal bit order", "[board_core][coordinates]") {
  STATIC_REQUIRE(kFileA == 0x0101010101010101ULL);
  STATIC_REQUIRE(kFileH == 0x8080808080808080ULL);
  STATIC_REQUIRE(kRank1 == 0x00000000000000FFULL);
  STATIC_REQUIRE(kRank8 == 0xFF00000000000000ULL);
}

TEST_CASE("directional shifts do not wrap around board edges", "[board_core][coordinates]") {
  STATIC_REQUIRE(shift_east(bit(square_from_file_rank(7, 0))) == 0);
  STATIC_REQUIRE(shift_west(bit(square_from_file_rank(0, 0))) == 0);
  STATIC_REQUIRE(shift_north_east(bit(square_from_file_rank(7, 7))) == 0);
  STATIC_REQUIRE(shift_south_west(bit(square_from_file_rank(0, 0))) == 0);

  STATIC_REQUIRE(shift_east(bit(square_from_file_rank(0, 0))) == bit(square_from_file_rank(1, 0)));
  STATIC_REQUIRE(shift_north(bit(square_from_file_rank(0, 0))) == bit(square_from_file_rank(0, 1)));
  STATIC_REQUIRE(shift_south_east(bit(square_from_file_rank(0, 1))) ==
                 bit(square_from_file_rank(1, 0)));
}

} // namespace
} // namespace vibe_othello::board_core
