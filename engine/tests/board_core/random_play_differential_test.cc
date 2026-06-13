#include "board_core/reference_board.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/serialization.h"

#include <array>
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

class DeterministicRng {
public:
  explicit constexpr DeterministicRng(std::uint64_t seed) noexcept : state_(seed) {}

  std::uint64_t next() noexcept {
    state_ = (state_ * 6364136223846793005ULL) + 1442695040888963407ULL;
    return state_;
  }

private:
  std::uint64_t state_;
};

int popcount(Bitboard bits) noexcept {
  int count = 0;
  while (bits != 0) {
    bits &= bits - 1;
    ++count;
  }
  return count;
}

Square select_move(Bitboard moves, DeterministicRng* rng) noexcept {
  int selected = static_cast<int>(rng->next() % static_cast<std::uint64_t>(popcount(moves)));
  for (int index = 0; index < kSquareCount; ++index) {
    const Square square = square_from_index(index);
    if ((moves & bit(square)) == 0) {
      continue;
    }
    if (selected == 0) {
      return square;
    }
    --selected;
  }

  return square_from_index(-1);
}

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
  DeterministicRng rng(seed);
  Position production = initial_position();
  Position reference = production;

  for (int ply = 0; ply < 120; ++ply) {
    require_position_matches_reference(production, reference, seed, ply);
    if (is_terminal(production)) {
      return;
    }

    const Bitboard moves = legal_moves(production);
    const Move move = moves == 0 ? make_pass() : make_move(select_move(moves, &rng));
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
  constexpr std::array<std::uint64_t, 5> kSeeds{
      0x243F6A8885A308D3ULL, 0x13198A2E03707344ULL, 0xA4093822299F31D0ULL,
      0x082EFA98EC4E6C89ULL, 0x452821E638D01377ULL,
  };

  for (const std::uint64_t seed : kSeeds) {
    play_random_game(seed);
  }
}

} // namespace vibe_othello::board_core
