#ifndef VIBE_OTHELLO_EVALUATION_PATTERN_H_
#define VIBE_OTHELLO_EVALUATION_PATTERN_H_

#include "vibe_othello/board_core/coordinates.h"
#include "vibe_othello/board_core/position.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace vibe_othello::evaluation {

enum class PatternCell : std::uint8_t {
  empty = 0,
  player = 1,
  opponent = 2,
};

struct PatternSchema {
  std::string_view name;
  std::uint8_t length;
};

struct PatternInstance {
  PatternSchema schema;
  std::span<const board_core::Square> squares;
};

constexpr std::uint32_t pattern_size(std::uint8_t length) noexcept {
  std::uint32_t size = 1;
  for (std::uint8_t index = 0; index < length; ++index) {
    size *= 3;
  }
  return size;
}

constexpr std::uint32_t ternary_pattern_index(std::span<const PatternCell> cells) noexcept {
  std::uint32_t index = 0;
  std::uint32_t place = 1;
  for (PatternCell cell : cells) {
    index += static_cast<std::uint32_t>(cell) * place;
    place *= 3;
  }
  return index;
}

template <std::size_t Length>
constexpr std::uint32_t
ternary_pattern_index(const std::array<PatternCell, Length>& cells) noexcept {
  return ternary_pattern_index(std::span<const PatternCell>{cells});
}

constexpr PatternCell cell_at(board_core::Position position, board_core::Square square) noexcept {
  const board_core::Bitboard mask = board_core::bit(square);
  if ((position.player & mask) != 0) {
    return PatternCell::player;
  }
  if ((position.opponent & mask) != 0) {
    return PatternCell::opponent;
  }
  return PatternCell::empty;
}

std::uint32_t ternary_pattern_index(board_core::Position position,
                                    std::span<const board_core::Square> squares) noexcept;

} // namespace vibe_othello::evaluation

#endif // VIBE_OTHELLO_EVALUATION_PATTERN_H_
