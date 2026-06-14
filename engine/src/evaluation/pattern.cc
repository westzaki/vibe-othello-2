#include "vibe_othello/evaluation/pattern.h"

#include <array>

namespace vibe_othello::evaluation {
namespace {

using board_core::square_from_file_rank;

constexpr std::array<board_core::Square, 8> kEdge8Squares{
    square_from_file_rank(0, 0), square_from_file_rank(1, 0), square_from_file_rank(2, 0),
    square_from_file_rank(3, 0), square_from_file_rank(4, 0), square_from_file_rank(5, 0),
    square_from_file_rank(6, 0), square_from_file_rank(7, 0),
};

constexpr std::array<board_core::Square, 9> kCorner3x3Squares{
    square_from_file_rank(0, 0), square_from_file_rank(1, 0), square_from_file_rank(2, 0),
    square_from_file_rank(0, 1), square_from_file_rank(1, 1), square_from_file_rank(2, 1),
    square_from_file_rank(0, 2), square_from_file_rank(1, 2), square_from_file_rank(2, 2),
};

[[nodiscard]] std::vector<board_core::Square>
to_vector(std::span<const board_core::Square> squares) {
  return std::vector<board_core::Square>{squares.begin(), squares.end()};
}

} // namespace

PatternSchemaValidationResult
validate_pattern_definition(const PatternDefinition& pattern) noexcept {
  if (pattern.id.empty()) {
    return PatternSchemaValidationResult{.error = PatternSchemaValidationError::empty_pattern_id};
  }
  if (pattern.length == 0) {
    return PatternSchemaValidationResult{.error =
                                             PatternSchemaValidationError::invalid_pattern_length};
  }
  if (pattern.length != pattern.squares.size()) {
    return PatternSchemaValidationResult{.error =
                                             PatternSchemaValidationError::pattern_length_mismatch};
  }
  if (!checked_pattern_size(pattern.length).has_value()) {
    return PatternSchemaValidationResult{.error =
                                             PatternSchemaValidationError::pattern_size_overflow};
  }

  std::array<bool, board_core::kSquareCount> seen{};
  for (const board_core::Square square : pattern.squares) {
    if (!board_core::is_valid(square)) {
      return PatternSchemaValidationResult{
          .error = PatternSchemaValidationError::invalid_pattern_square};
    }
    if (!pattern.allow_duplicate_squares && seen[square.index]) {
      return PatternSchemaValidationResult{
          .error = PatternSchemaValidationError::duplicate_pattern_square};
    }
    seen[square.index] = true;
  }

  return PatternSchemaValidationResult{};
}

PatternSchemaValidationResult validate_pattern_set(const PatternSet& pattern_set) noexcept {
  for (const PatternDefinition& pattern : pattern_set.patterns) {
    const PatternSchemaValidationResult result = validate_pattern_definition(pattern);
    if (!result.ok()) {
      return result;
    }
  }
  return PatternSchemaValidationResult{};
}

const PatternSet& fixed_pattern_set_fixture() noexcept {
  static const PatternSet pattern_set{
      .id = "fixed-pattern-fixture-v1",
      .patterns =
          {
              PatternDefinition{
                  .id = "edge-8",
                  .length = 8,
                  .squares = to_vector(kEdge8Squares),
              },
              PatternDefinition{
                  .id = "corner-3x3",
                  .length = 9,
                  .squares = to_vector(kCorner3x3Squares),
              },
          },
  };
  return pattern_set;
}

std::uint32_t ternary_pattern_index(board_core::Position position,
                                    std::span<const board_core::Square> squares) noexcept {
  std::uint32_t index = 0;
  std::uint32_t place = 1;
  for (board_core::Square square : squares) {
    index += static_cast<std::uint32_t>(cell_at(position, square)) * place;
    place *= 3;
  }
  return index;
}

} // namespace vibe_othello::evaluation
