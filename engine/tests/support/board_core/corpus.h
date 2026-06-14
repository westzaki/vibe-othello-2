#ifndef VIBE_OTHELLO_TEST_SUPPORT_BOARD_CORE_CORPUS_H_
#define VIBE_OTHELLO_TEST_SUPPORT_BOARD_CORE_CORPUS_H_

#include "vibe_othello/board_core/board.h"

#include <array>
#include <bit>
#include <cstdint>
#include <initializer_list>

namespace vibe_othello::board_core::test_support {

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

inline int popcount(Bitboard bits) noexcept {
  return std::popcount(bits);
}

inline Square select_move(Bitboard moves, DeterministicRng* rng) noexcept {
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

inline constexpr std::array<Position, 8> kRepresentativePositions{{
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

inline constexpr std::array<std::uint64_t, 12> kRandomPlaySeeds{
    0x243F6A8885A308D3ULL, 0x13198A2E03707344ULL, 0xA4093822299F31D0ULL, 0x082EFA98EC4E6C89ULL,
    0x452821E638D01377ULL, 0xBE5466CF34E90C6CULL, 0xC0AC29B7C97C50DDULL, 0x3F84D5B5B5470917ULL,
    0x9216D5D98979FB1BULL, 0xD1310BA698DFB5ACULL, 0x2FFD72DBD01ADFB7ULL, 0xB8E1AFED6A267E96ULL,
};

} // namespace vibe_othello::board_core::test_support

#endif // VIBE_OTHELLO_TEST_SUPPORT_BOARD_CORE_CORPUS_H_
