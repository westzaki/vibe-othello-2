#include "vibe_othello/board_core/hash.h"

namespace vibe_othello::board_core {
namespace {

constexpr PositionHash splitmix64(std::uint64_t value) noexcept {
  value += 0x9E3779B97F4A7C15ULL;
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

constexpr PositionHash piece_key(Square square, Color color) noexcept {
  const std::uint64_t color_offset = color == Color::black ? 0ULL : 1ULL;
  const std::uint64_t key_index = (static_cast<std::uint64_t>(square.index) * 2ULL) + color_offset;
  return splitmix64(0x6A09E667F3BCC909ULL ^ key_index);
}

constexpr PositionHash side_to_move_key(Color color) noexcept {
  return color == Color::black ? 0ULL : splitmix64(0xBB67AE8584CAA73BULL);
}

} // namespace

PositionHash hash_position(Position position) noexcept {
  PositionHash hash = side_to_move_key(position.side_to_move);
  const Bitboard black = black_discs(position);
  const Bitboard white = white_discs(position);

  for (int index = 0; index < kSquareCount; ++index) {
    const Square square = square_from_index(index);
    const Bitboard square_bit = bit(square);
    if ((black & square_bit) != 0) {
      hash ^= piece_key(square, Color::black);
    } else if ((white & square_bit) != 0) {
      hash ^= piece_key(square, Color::white);
    }
  }

  return hash;
}

} // namespace vibe_othello::board_core
