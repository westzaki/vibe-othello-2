#include "board_core/corpus.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/serialization.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace vibe_othello::board_core {
namespace {

void require_board_core_properties(Position position) {
  INFO("position: " << format_position(position));

  REQUIRE(is_valid(position));
  REQUIRE((position.player & position.opponent) == 0);

  const Bitboard occupied_before = occupied(position);
  const Bitboard moves = legal_moves(position);
  REQUIRE((moves & occupied_before) == 0);
  REQUIRE(has_legal_move(position) == (moves != 0));

  const auto parsed = parse_position(format_position(position));
  REQUIRE(parsed.has_value());
  REQUIRE(*parsed == position);

  for (int index = 0; index < kSquareCount; ++index) {
    const Square square = square_from_index(index);
    const Bitboard move_bit = bit(square);
    const bool is_legal_move = (moves & move_bit) != 0;
    const Bitboard flipped = flips_for_move(position, square);

    REQUIRE((flipped & ~position.opponent) == 0);
    REQUIRE(is_legal_move == (flipped != 0));

    if (!is_legal_move) {
      continue;
    }

    Position applied = position;
    MoveDelta delta{};
    REQUIRE(apply_move(&applied, make_move(square), &delta));
    REQUIRE(delta.move == make_move(square));
    REQUIRE(delta.flipped == flipped);
    REQUIRE(is_valid(applied));
    REQUIRE(test_support::popcount(occupied(applied)) ==
            test_support::popcount(occupied_before) + 1);

    Position applied_from_delta = position;
    apply_move_delta(&applied_from_delta, delta);
    REQUIRE(applied_from_delta == applied);

    undo_move(&applied, delta);
    REQUIRE(applied == position);
  }
}

void play_random_property_game(std::uint64_t seed) {
  test_support::DeterministicRng rng(seed);
  Position position = initial_position();

  for (int ply = 0; ply < 120; ++ply) {
    INFO("seed: " << seed);
    INFO("ply: " << ply);
    require_board_core_properties(position);
    if (is_terminal(position)) {
      return;
    }

    const Bitboard moves = legal_moves(position);
    MoveDelta delta{};
    if (moves == 0) {
      REQUIRE(apply_move(&position, make_pass(), &delta));
    } else {
      REQUIRE(apply_move(&position, make_move(test_support::select_move(moves, &rng)), &delta));
    }
    REQUIRE(is_valid(position));
  }

  INFO("seed: " << seed);
  require_board_core_properties(position);
  REQUIRE(is_terminal(position));
}

} // namespace

TEST_CASE("representative positions satisfy board core properties",
          "[board_core][property]") {
  for (Position position : test_support::kRepresentativePositions) {
    require_board_core_properties(position);
  }
}

TEST_CASE("random reachable positions satisfy board core properties",
          "[board_core][property][random]") {
  for (std::uint64_t seed : test_support::kRandomPlaySeeds) {
    play_random_property_game(seed);
  }
}

} // namespace vibe_othello::board_core
