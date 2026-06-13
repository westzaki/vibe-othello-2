#include "board_core/corpus.h"
#include "board_core/reference_board.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/serialization.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <string>

namespace vibe_othello::board_core {
namespace {

using test_support::reference_apply_move;
using test_support::reference_flips_for_move;
using test_support::reference_has_legal_move;
using test_support::reference_is_terminal;
using test_support::reference_legal_moves;

std::string debug_position(Position position) {
  if (!is_valid(position)) {
    return "<invalid position>";
  }
  return format_position(position);
}

void require_position_matches_reference(Position production, Position reference, std::uint64_t seed,
                                        int ply) {
  INFO("seed: " << seed);
  INFO("ply: " << ply);
  INFO("production: " << debug_position(production));
  INFO("reference: " << debug_position(reference));

  REQUIRE(production == reference);
  REQUIRE(legal_moves(production) == reference_legal_moves(reference));
  REQUIRE(has_legal_move(production) == reference_has_legal_move(reference));
  REQUIRE(is_terminal(production) == reference_is_terminal(reference));

  for (int index = 0; index < kSquareCount; ++index) {
    const Square move = square_from_index(index);
    REQUIRE(flips_for_move(production, move) == reference_flips_for_move(reference, move));
  }
}

void play_random_game(std::uint64_t seed) {
  test_support::DeterministicRng rng(seed);
  Position production = initial_position();
  Position reference = production;

  for (int ply = 0; ply < 120; ++ply) {
    require_position_matches_reference(production, reference, seed, ply);
    if (is_terminal(production)) {
      return;
    }

    const Bitboard moves = legal_moves(production);
    const Move move = moves == 0 ? make_pass() : make_move(test_support::select_move(moves, &rng));
    MoveDelta production_delta{};
    MoveDelta reference_delta{};

    INFO("seed: " << seed);
    INFO("ply: " << ply);
    INFO("before: " << debug_position(production));
    REQUIRE(apply_move(&production, move, &production_delta) ==
            reference_apply_move(&reference, move, &reference_delta));
    REQUIRE(production_delta == reference_delta);
  }

  require_position_matches_reference(production, reference, seed, 120);
  REQUIRE(is_terminal(production));
}

} // namespace

TEST_CASE("deterministic random play matches reference board",
          "[board_core][random_play][differential]") {
  for (std::size_t index = 0; index < 5; ++index) {
    play_random_game(test_support::kRandomPlaySeeds[index]);
  }
}

} // namespace vibe_othello::board_core
