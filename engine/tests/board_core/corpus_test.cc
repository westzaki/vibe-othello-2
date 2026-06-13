#include "board_core/reference_board.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/serialization.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <initializer_list>
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

constexpr Square square(int file, int rank) noexcept {
  return square_from_file_rank(file, rank);
}

constexpr Bitboard squares(std::initializer_list<Square> values) noexcept {
  Bitboard result = 0;
  for (Square value : values) {
    result |= bit(value);
  }
  return result;
}

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
    const Square move = square_from_index(index);
    if ((moves & bit(move)) == 0) {
      continue;
    }
    if (selected == 0) {
      return move;
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
  DeterministicRng rng(seed);
  Position position = initial_position();

  for (int ply = 0; ply < 120; ++ply) {
    INFO("seed: " << seed);
    INFO("ply: " << ply);
    require_corpus_position(position);
    if (is_terminal(position)) {
      return;
    }

    const Bitboard moves = legal_moves(position);
    const Move move = moves == 0 ? make_pass() : make_move(select_move(moves, &rng));
    MoveDelta delta{};
    REQUIRE(apply_move(&position, move, &delta));
  }

  INFO("seed: " << seed);
  require_corpus_position(position);
  REQUIRE(is_terminal(position));
}

} // namespace

TEST_CASE("representative board core corpus matches reference", "[board_core][corpus]") {
  constexpr std::array<Position, 8> kPositions{{
      initial_position(),
      Position{
          .player = initial_white_discs(),
          .opponent = initial_black_discs(),
          .side_to_move = Color::white,
      },
      Position{
          .player = squares({
              square(0, 3),
              square(3, 0),
              square(6, 3),
              square(3, 6),
          }),
          .opponent = squares({
              square(1, 3),
              square(2, 3),
              square(3, 1),
              square(3, 2),
              square(4, 3),
              square(5, 3),
              square(3, 4),
              square(3, 5),
          }),
          .side_to_move = Color::black,
      },
      Position{
          .player = bit(square(1, 0)),
          .opponent = bit(square(0, 0)),
          .side_to_move = Color::black,
      },
      Position{
          .player = bit(square(7, 0)),
          .opponent = bit(square(6, 0)),
          .side_to_move = Color::black,
      },
      Position{
          .player = 0x00F0F7F7F7F70F00ULL,
          .opponent = 0x7F0F08080808F07EULL,
          .side_to_move = Color::black,
      },
      Position{
          .player = 0x5555555555555555ULL,
          .opponent = 0xAAAAAAAAAAAAAAAAULL,
          .side_to_move = Color::black,
      },
      Position{
          .player = 0,
          .opponent = 0,
          .side_to_move = Color::black,
      },
  }};

  for (Position position : kPositions) {
    require_corpus_position(position);
  }
}

TEST_CASE("randomized board core corpus matches reference", "[board_core][corpus][random]") {
  constexpr std::array<std::uint64_t, 12> kSeeds{
      0x243F6A8885A308D3ULL, 0x13198A2E03707344ULL, 0xA4093822299F31D0ULL, 0x082EFA98EC4E6C89ULL,
      0x452821E638D01377ULL, 0xBE5466CF34E90C6CULL, 0xC0AC29B7C97C50DDULL, 0x3F84D5B5B5470917ULL,
      0x9216D5D98979FB1BULL, 0xD1310BA698DFB5ACULL, 0x2FFD72DBD01ADFB7ULL, 0xB8E1AFED6A267E96ULL,
  };

  for (std::uint64_t seed : kSeeds) {
    play_random_corpus_game(seed);
  }
}

} // namespace vibe_othello::board_core
