#include "board_core/corpus.h"
#include "board_core/reference_board.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/serialization.h"

#include <catch2/catch_test_macros.hpp>
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

void require_position_matches_reference(Position position) {
  INFO("position: " << debug_position(position));

  REQUIRE(legal_moves(position) == reference_legal_moves(position));
  REQUIRE(has_legal_move(position) == reference_has_legal_move(position));
  REQUIRE(is_terminal(position) == reference_is_terminal(position));

  for (int index = 0; index < kSquareCount; ++index) {
    const Square move = square_from_index(index);
    REQUIRE(flips_for_move(position, move) == reference_flips_for_move(position, move));
  }
}

void require_move_applications_match_reference(Position position) {
  const Bitboard moves = legal_moves(position);

  if (moves == 0) {
    if (is_terminal(position)) {
      return;
    }

    Position production = position;
    Position reference = position;
    MoveDelta production_delta{};
    MoveDelta reference_delta{};
    REQUIRE(apply_move(&production, make_pass(), &production_delta) ==
            reference_apply_move(&reference, make_pass(), &reference_delta));
    REQUIRE(production == reference);
    REQUIRE(production_delta == reference_delta);
    return;
  }

  for (int index = 0; index < kSquareCount; ++index) {
    const Square move = square_from_index(index);
    if ((moves & bit(move)) == 0) {
      continue;
    }

    Position production = position;
    Position reference = position;
    MoveDelta production_delta{};
    MoveDelta reference_delta{};
    REQUIRE(apply_move(&production, make_move(move), &production_delta) ==
            reference_apply_move(&reference, make_move(move), &reference_delta));
    REQUIRE(production == reference);
    REQUIRE(production_delta == reference_delta);
  }
}

void require_corpus_position(Position position) {
  require_position_matches_reference(position);
  require_move_applications_match_reference(position);
}

void play_random_corpus_game(std::uint64_t seed) {
  test_support::DeterministicRng rng(seed);
  Position position = initial_position();

  for (int ply = 0; ply < 120; ++ply) {
    INFO("seed: " << seed);
    INFO("ply: " << ply);
    require_corpus_position(position);
    if (is_terminal(position)) {
      return;
    }

    const Bitboard moves = legal_moves(position);
    const Move move =
        moves == 0 ? make_pass() : make_move(test_support::select_move(moves, &rng));
    MoveDelta delta{};
    REQUIRE(apply_move(&position, move, &delta));
  }

  INFO("seed: " << seed);
  require_corpus_position(position);
  REQUIRE(is_terminal(position));
}

} // namespace

TEST_CASE("representative board core corpus matches reference", "[board_core][corpus]") {
  for (Position position : test_support::kRepresentativePositions) {
    require_corpus_position(position);
  }
}

TEST_CASE("randomized board core corpus matches reference", "[board_core][corpus][random]") {
  for (std::uint64_t seed : test_support::kRandomPlaySeeds) {
    play_random_corpus_game(seed);
  }
}

} // namespace vibe_othello::board_core
