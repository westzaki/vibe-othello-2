#include "vibe_othello/evaluation/pattern.h"

#include <algorithm>
#include <array>
#include <limits>

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

constexpr std::array<std::array<std::uint8_t, 9>, 8> kSquareD4SourceByOutput{{
    {0, 1, 2, 3, 4, 5, 6, 7, 8},
    {6, 3, 0, 7, 4, 1, 8, 5, 2},
    {8, 7, 6, 5, 4, 3, 2, 1, 0},
    {2, 5, 8, 1, 4, 7, 0, 3, 6},
    {2, 1, 0, 5, 4, 3, 8, 7, 6},
    {6, 7, 8, 3, 4, 5, 0, 1, 2},
    {0, 3, 6, 1, 4, 7, 2, 5, 8},
    {8, 5, 2, 7, 4, 1, 6, 3, 0},
}};

[[nodiscard]] std::vector<board_core::Square>
to_vector(std::span<const board_core::Square> squares) {
  return std::vector<board_core::Square>{squares.begin(), squares.end()};
}

[[nodiscard]] std::vector<board_core::Square> line(int file, int rank, int file_delta,
                                                   int rank_delta, std::uint8_t length) {
  std::vector<board_core::Square> result;
  result.reserve(length);
  for (std::uint8_t index = 0; index < length; ++index) {
    result.push_back(square_from_file_rank(file + file_delta * index, rank + rank_delta * index));
  }
  return result;
}

[[nodiscard]] std::vector<board_core::Square>
band(int file, int rank, int primary_file_delta, int primary_rank_delta, int secondary_file_delta,
     int secondary_rank_delta, std::uint8_t primary_length, std::uint8_t secondary_length) {
  std::vector<board_core::Square> result;
  result.reserve(static_cast<std::size_t>(primary_length) * secondary_length);
  for (std::uint8_t secondary = 0; secondary < secondary_length; ++secondary) {
    for (std::uint8_t primary = 0; primary < primary_length; ++primary) {
      result.push_back(square_from_file_rank(
          file + primary_file_delta * primary + secondary_file_delta * secondary,
          rank + primary_rank_delta * primary + secondary_rank_delta * secondary));
    }
  }
  return result;
}

[[nodiscard]] std::vector<board_core::Square> edge_plus_x(int file, int rank, int file_delta,
                                                          int rank_delta, int first_x_file,
                                                          int first_x_rank, int second_x_file,
                                                          int second_x_rank) {
  std::vector<board_core::Square> result = line(file, rank, file_delta, rank_delta, 8);
  result.push_back(square_from_file_rank(first_x_file, first_x_rank));
  result.push_back(square_from_file_rank(second_x_file, second_x_rank));
  return result;
}

[[nodiscard]] std::vector<board_core::Square> corner_wing(int file, int rank, int edge_file_delta,
                                                          int edge_rank_delta,
                                                          int inside_file_delta,
                                                          int inside_rank_delta) {
  return {
      square_from_file_rank(file, rank),
      square_from_file_rank(file + edge_file_delta, rank + edge_rank_delta),
      square_from_file_rank(file + edge_file_delta * 2, rank + edge_rank_delta * 2),
      square_from_file_rank(file + edge_file_delta * 3, rank + edge_rank_delta * 3),
      square_from_file_rank(file + inside_file_delta, rank + inside_rank_delta),
      square_from_file_rank(file + edge_file_delta + inside_file_delta,
                            rank + edge_rank_delta + inside_rank_delta),
      square_from_file_rank(file + edge_file_delta * 2 + inside_file_delta,
                            rank + edge_rank_delta * 2 + inside_rank_delta),
      square_from_file_rank(file + edge_file_delta + inside_file_delta * 2,
                            rank + edge_rank_delta + inside_rank_delta * 2),
  };
}

[[nodiscard]] std::vector<board_core::Square>
diagonal_corner(int file, int rank, int diagonal_file_delta, int diagonal_rank_delta,
                int edge_file_delta, int edge_rank_delta, int side_file_delta,
                int side_rank_delta) {
  return {
      square_from_file_rank(file, rank),
      square_from_file_rank(file + diagonal_file_delta, rank + diagonal_rank_delta),
      square_from_file_rank(file + diagonal_file_delta * 2, rank + diagonal_rank_delta * 2),
      square_from_file_rank(file + diagonal_file_delta * 3, rank + diagonal_rank_delta * 3),
      square_from_file_rank(file + diagonal_file_delta * 4, rank + diagonal_rank_delta * 4),
      square_from_file_rank(file + diagonal_file_delta * 5, rank + diagonal_rank_delta * 5),
      square_from_file_rank(file + edge_file_delta, rank + edge_rank_delta),
      square_from_file_rank(file + side_file_delta, rank + side_rank_delta),
  };
}

[[nodiscard]] std::vector<PatternDefinition>
make_fixed_pattern_definitions(PatternSymmetryPolicy edge_symmetry,
                               PatternSymmetryPolicy corner_symmetry) {
  return {
      PatternDefinition{
          .id = "edge-8",
          .length = 8,
          .squares = to_vector(kEdge8Squares),
          .symmetry_policy = edge_symmetry,
      },
      PatternDefinition{
          .id = "corner-3x3",
          .length = 9,
          .squares = to_vector(kCorner3x3Squares),
          .symmetry_policy = corner_symmetry,
      },
  };
}

[[nodiscard]] std::vector<PatternDefinition> make_buro_lite_pattern_definitions() {
  return {
      PatternDefinition{
          .id = "edge-8",
          .length = 8,
          .squares = line(0, 0, 1, 0, 8),
      },
      PatternDefinition{
          .id = "near-edge-8",
          .length = 8,
          .squares = line(0, 1, 1, 0, 8),
      },
      PatternDefinition{
          .id = "diagonal-8",
          .length = 8,
          .squares = line(0, 0, 1, 1, 8),
      },
      PatternDefinition{
          .id = "diagonal-7",
          .length = 7,
          .squares = line(1, 0, 1, 1, 7),
      },
      PatternDefinition{
          .id = "corner-2x5",
          .length = 10,
          .squares = band(0, 0, 1, 0, 0, 1, 5, 2),
      },
      PatternDefinition{
          .id = "corner-3x3",
          .length = 9,
          .squares = band(0, 0, 1, 0, 0, 1, 3, 3),
      },
  };
}

[[nodiscard]] std::vector<PatternDefinition> make_endgame_lite_pattern_definitions() {
  std::vector<PatternDefinition> definitions = make_buro_lite_pattern_definitions();
  definitions.push_back(PatternDefinition{
      .id = "corner-2x4-8",
      .length = 8,
      .squares = band(0, 0, 1, 0, 0, 1, 4, 2),
  });
  definitions.push_back(PatternDefinition{
      .id = "edge-plus-x-10",
      .length = 10,
      .squares = edge_plus_x(0, 0, 1, 0, 1, 1, 6, 1),
  });
  definitions.push_back(PatternDefinition{
      .id = "corner-wing-8",
      .length = 8,
      .squares = corner_wing(0, 0, 1, 0, 0, 1),
  });
  definitions.push_back(PatternDefinition{
      .id = "near-edge-segment-8",
      .length = 8,
      .squares = band(1, 1, 1, 0, 0, 1, 4, 2),
  });
  definitions.push_back(PatternDefinition{
      .id = "diagonal-corner-8",
      .length = 8,
      .squares = diagonal_corner(0, 0, 1, 1, 1, 0, 0, 1),
  });
  return definitions;
}

[[nodiscard]] std::optional<std::uint8_t> checked_canonical_length(std::size_t length) noexcept {
  if (length == 0 || length > kMaxPatternLengthForUint32Size) {
    return std::nullopt;
  }
  return static_cast<std::uint8_t>(length);
}

[[nodiscard]] std::optional<std::uint8_t> pattern_cell_digit(PatternCell cell) noexcept {
  switch (cell) {
  case PatternCell::empty:
    return 0;
  case PatternCell::player:
    return 1;
  case PatternCell::opponent:
    return 2;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t>
checked_index_for_cells(std::span<const PatternCell> cells) noexcept {
  std::uint64_t index = 0;
  std::uint64_t place = 1;
  for (PatternCell cell : cells) {
    const std::optional<std::uint8_t> digit = pattern_cell_digit(cell);
    if (!digit.has_value()) {
      return std::nullopt;
    }
    index += static_cast<std::uint64_t>(*digit) * place;
    place *= 3;
  }
  if (index > std::numeric_limits<std::uint32_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(index);
}

[[nodiscard]] std::optional<std::uint32_t>
reversed_index_for_cells(std::span<const PatternCell> cells) noexcept {
  std::uint64_t index = 0;
  std::uint64_t place = 1;
  for (std::size_t offset = 0; offset < cells.size(); ++offset) {
    const std::optional<std::uint8_t> digit = pattern_cell_digit(cells[cells.size() - 1 - offset]);
    if (!digit.has_value()) {
      return std::nullopt;
    }
    index += static_cast<std::uint64_t>(*digit) * place;
    place *= 3;
  }
  return static_cast<std::uint32_t>(index);
}

[[nodiscard]] std::optional<std::uint32_t>
square_d4_index_for_cells(std::span<const PatternCell> cells,
                          const std::array<std::uint8_t, 9>& source_by_output) noexcept {
  std::uint64_t index = 0;
  std::uint64_t place = 1;
  for (std::uint8_t source : source_by_output) {
    const std::optional<std::uint8_t> digit = pattern_cell_digit(cells[source]);
    if (!digit.has_value()) {
      return std::nullopt;
    }
    index += static_cast<std::uint64_t>(*digit) * place;
    place *= 3;
  }
  return static_cast<std::uint32_t>(index);
}

[[nodiscard]] bool is_supported_symmetry_policy(PatternSymmetryPolicy symmetry_policy) noexcept {
  switch (symmetry_policy) {
  case PatternSymmetryPolicy::none:
  case PatternSymmetryPolicy::reverse:
  case PatternSymmetryPolicy::square_d4:
    return true;
  }
  return false;
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
  if (!is_supported_symmetry_policy(pattern.symmetry_policy) ||
      (pattern.symmetry_policy == PatternSymmetryPolicy::square_d4 && pattern.length != 9)) {
    return PatternSchemaValidationResult{
        .error = PatternSchemaValidationError::unsupported_symmetry_policy};
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
          make_fixed_pattern_definitions(PatternSymmetryPolicy::none, PatternSymmetryPolicy::none),
  };
  return pattern_set;
}

const PatternSet& symmetry_aware_fixed_pattern_set_fixture() noexcept {
  static const PatternSet pattern_set{
      .id = "fixed-pattern-fixture-v1-symmetry-aware",
      .patterns = make_fixed_pattern_definitions(PatternSymmetryPolicy::reverse,
                                                 PatternSymmetryPolicy::square_d4),
  };
  return pattern_set;
}

const PatternSet& buro_lite_pattern_set() noexcept {
  static const PatternSet pattern_set{
      .id = "pattern-v1-buro-lite",
      .patterns = make_buro_lite_pattern_definitions(),
  };
  return pattern_set;
}

const PatternSet& endgame_lite_pattern_set() noexcept {
  static const PatternSet pattern_set{
      .id = "pattern-v2-endgame-lite",
      .patterns = make_endgame_lite_pattern_definitions(),
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

std::optional<std::uint32_t>
canonical_ternary_pattern_index(std::span<const PatternCell> cells,
                                PatternSymmetryPolicy symmetry_policy) noexcept {
  const std::optional<std::uint8_t> length = checked_canonical_length(cells.size());
  if (!length.has_value()) {
    return std::nullopt;
  }

  switch (symmetry_policy) {
  case PatternSymmetryPolicy::none:
    return checked_index_for_cells(cells);
  case PatternSymmetryPolicy::reverse: {
    const std::optional<std::uint32_t> forward = checked_index_for_cells(cells);
    const std::optional<std::uint32_t> reversed = reversed_index_for_cells(cells);
    if (!forward.has_value() || !reversed.has_value()) {
      return std::nullopt;
    }
    return std::min(*forward, *reversed);
  }
  case PatternSymmetryPolicy::square_d4: {
    if (*length != 9) {
      return std::nullopt;
    }

    std::uint32_t canonical = std::numeric_limits<std::uint32_t>::max();
    for (const std::array<std::uint8_t, 9>& source_by_output : kSquareD4SourceByOutput) {
      const std::optional<std::uint32_t> transformed =
          square_d4_index_for_cells(cells, source_by_output);
      if (!transformed.has_value()) {
        return std::nullopt;
      }
      canonical = std::min(canonical, *transformed);
    }
    return canonical;
  }
  }
  return std::nullopt;
}

std::optional<std::uint32_t>
canonical_ternary_pattern_index(std::uint32_t index, std::uint8_t pattern_length,
                                PatternSymmetryPolicy symmetry_policy) noexcept {
  const std::optional<std::uint8_t> length = checked_canonical_length(pattern_length);
  if (!length.has_value()) {
    return std::nullopt;
  }
  const std::optional<std::uint32_t> size = checked_pattern_size(*length);
  if (!size.has_value() || index >= *size) {
    return std::nullopt;
  }

  std::array<PatternCell, kMaxPatternLengthForUint32Size> cells{};
  std::uint32_t remaining = index;
  for (std::uint8_t digit_index = 0; digit_index < *length; ++digit_index) {
    cells[digit_index] = static_cast<PatternCell>(remaining % 3);
    remaining /= 3;
  }

  return canonical_ternary_pattern_index(
      std::span<const PatternCell>{cells}.first(static_cast<std::size_t>(*length)),
      symmetry_policy);
}

} // namespace vibe_othello::evaluation
