#ifndef VIBE_OTHELLO_EVALUATION_PATTERN_H_
#define VIBE_OTHELLO_EVALUATION_PATTERN_H_

#include "vibe_othello/board_core/coordinates.h"
#include "vibe_othello/board_core/position.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vibe_othello::evaluation {

enum class PatternCell : std::uint8_t {
  empty = 0,
  player = 1,
  opponent = 2,
};

enum class PatternSymmetryPolicy : std::uint8_t {
  none = 0,
  reverse = 1,
  square_d4 = 2,
};

struct PatternSchema {
  std::string_view name;
  std::uint8_t length;
};

struct PatternInstance {
  PatternSchema schema;
  std::span<const board_core::Square> squares;
};

struct PatternDefinition {
  std::string id;
  std::uint8_t length = 0;
  std::vector<board_core::Square> squares;
  bool allow_duplicate_squares = false;
  PatternSymmetryPolicy symmetry_policy = PatternSymmetryPolicy::none;
};

struct PatternSet {
  std::string id;
  std::vector<PatternDefinition> patterns;
};

enum class PatternSchemaValidationError : std::uint8_t {
  none,
  empty_pattern_id,
  invalid_pattern_length,
  pattern_length_mismatch,
  pattern_size_overflow,
  invalid_pattern_square,
  duplicate_pattern_square,
  unsupported_symmetry_policy,
};

struct PatternSchemaValidationResult {
  PatternSchemaValidationError error = PatternSchemaValidationError::none;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == PatternSchemaValidationError::none;
  }
};

inline constexpr std::uint8_t kMaxPatternLengthForUint32Size = 20;

constexpr std::optional<std::uint32_t> checked_pattern_size(std::uint8_t length) noexcept {
  if (length > kMaxPatternLengthForUint32Size) {
    return std::nullopt;
  }

  std::uint32_t size = 1;
  for (std::uint8_t index = 0; index < length; ++index) {
    size *= 3;
  }
  return size;
}

constexpr std::uint32_t pattern_size(std::uint8_t length) noexcept {
  return checked_pattern_size(length).value_or(0);
}

[[nodiscard]] PatternSchemaValidationResult
validate_pattern_definition(const PatternDefinition& pattern) noexcept;

[[nodiscard]] PatternSchemaValidationResult
validate_pattern_set(const PatternSet& pattern_set) noexcept;

[[nodiscard]] const PatternSet& fixed_pattern_set_fixture() noexcept;

[[nodiscard]] const PatternSet& symmetry_aware_fixed_pattern_set_fixture() noexcept;

[[nodiscard]] const PatternSet& buro_lite_pattern_set() noexcept;

[[nodiscard]] const PatternSet& endgame_lite_pattern_set() noexcept;

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

[[nodiscard]] std::optional<std::uint32_t>
canonical_ternary_pattern_index(std::span<const PatternCell> cells,
                                PatternSymmetryPolicy symmetry_policy) noexcept;

template <std::size_t Length>
[[nodiscard]] std::optional<std::uint32_t>
canonical_ternary_pattern_index(const std::array<PatternCell, Length>& cells,
                                PatternSymmetryPolicy symmetry_policy) noexcept {
  return canonical_ternary_pattern_index(std::span<const PatternCell>{cells}, symmetry_policy);
}

[[nodiscard]] std::optional<std::uint32_t>
canonical_ternary_pattern_index(std::uint32_t index, std::uint8_t pattern_length,
                                PatternSymmetryPolicy symmetry_policy) noexcept;

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
