#include "vibe_othello/board_core/hash.h"

#include "bit_ops.h"

#include <array>

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

constexpr std::array<PositionHash, kSquareCount> make_piece_keys(Color color) noexcept {
  std::array<PositionHash, kSquareCount> keys{};
  for (int index = 0; index < kSquareCount; ++index) {
    keys[static_cast<std::size_t>(index)] = piece_key(square_from_index(index), color);
  }
  return keys;
}

constexpr std::array<PositionHash, kSquareCount> kBlackPieceKeys = make_piece_keys(Color::black);
constexpr std::array<PositionHash, kSquareCount> kWhitePieceKeys = make_piece_keys(Color::white);

PositionHash hash_pieces(Bitboard pieces,
                         const std::array<PositionHash, kSquareCount>& keys) noexcept {
  PositionHash hash = 0;
  while (pieces != 0) {
    hash ^= keys[static_cast<std::size_t>(detail::pop_lsb_index(pieces))];
  }
  return hash;
}

} // namespace

PositionHash hash_position(Position position) noexcept {
  PositionHash hash = side_to_move_key(position.side_to_move);
  hash ^= hash_pieces(black_discs(position), kBlackPieceKeys);
  hash ^= hash_pieces(white_discs(position), kWhitePieceKeys);
  return hash;
}

} // namespace vibe_othello::board_core
