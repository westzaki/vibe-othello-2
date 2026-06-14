#include "vibe_othello/evaluation/pattern.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

namespace vibe_othello::evaluation {
namespace {

using board_core::square_from_file_rank;

constexpr board_core::Square square(int file, int rank) noexcept {
  return square_from_file_rank(file, rank);
}

PatternDefinition valid_pattern() {
  return PatternDefinition{
      .id = "edge-8",
      .length = 8,
      .squares =
          {
              square(0, 0),
              square(1, 0),
              square(2, 0),
              square(3, 0),
              square(4, 0),
              square(5, 0),
              square(6, 0),
              square(7, 0),
          },
  };
}

void require_schema_error(const PatternDefinition& pattern, PatternSchemaValidationError error) {
  const PatternSchemaValidationResult result = validate_pattern_definition(pattern);
  REQUIRE_FALSE(result.ok());
  REQUIRE(result.error == error);
}

} // namespace

TEST_CASE("fixed pattern-set fixture validates edge and corner schemas",
          "[evaluation][pattern_schema]") {
  const PatternSet& pattern_set = fixed_pattern_set_fixture();

  REQUIRE(pattern_set.id == "fixed-pattern-fixture-v1");
  REQUIRE(pattern_set.patterns.size() == 2);
  REQUIRE(pattern_set.patterns[0].id == "edge-8");
  REQUIRE(pattern_set.patterns[0].length == 8);
  REQUIRE(pattern_set.patterns[0].squares.size() == 8);
  REQUIRE(pattern_set.patterns[1].id == "corner-3x3");
  REQUIRE(pattern_set.patterns[1].length == 9);
  REQUIRE(pattern_set.patterns[1].squares.size() == 9);
  REQUIRE(validate_pattern_set(pattern_set).ok());
}

TEST_CASE("pattern schema validation accepts valid definitions", "[evaluation][pattern_schema]") {
  const PatternDefinition pattern = valid_pattern();

  REQUIRE(validate_pattern_definition(pattern).ok());
  REQUIRE(pattern_size(pattern.length) == 6561);
}

TEST_CASE("pattern schema validation rejects empty pattern id", "[evaluation][pattern_schema]") {
  PatternDefinition pattern = valid_pattern();
  pattern.id.clear();

  require_schema_error(pattern, PatternSchemaValidationError::empty_pattern_id);
}

TEST_CASE("pattern schema validation rejects zero length", "[evaluation][pattern_schema]") {
  PatternDefinition pattern = valid_pattern();
  pattern.length = 0;
  pattern.squares.clear();

  require_schema_error(pattern, PatternSchemaValidationError::invalid_pattern_length);
}

TEST_CASE("pattern schema validation rejects length and square count mismatch",
          "[evaluation][pattern_schema]") {
  PatternDefinition pattern = valid_pattern();
  pattern.length = 7;

  require_schema_error(pattern, PatternSchemaValidationError::pattern_length_mismatch);
}

TEST_CASE("pattern schema validation rejects invalid squares", "[evaluation][pattern_schema]") {
  PatternDefinition pattern = valid_pattern();
  pattern.squares[3] = board_core::Square{board_core::kInvalidSquareIndex};

  require_schema_error(pattern, PatternSchemaValidationError::invalid_pattern_square);
}

TEST_CASE("pattern schema validation rejects duplicate squares by default",
          "[evaluation][pattern_schema]") {
  PatternDefinition pattern = valid_pattern();
  pattern.squares[7] = pattern.squares[0];

  require_schema_error(pattern, PatternSchemaValidationError::duplicate_pattern_square);
}

TEST_CASE("pattern schema validation accepts duplicate squares when explicitly allowed",
          "[evaluation][pattern_schema]") {
  PatternDefinition pattern = valid_pattern();
  pattern.squares[7] = pattern.squares[0];
  pattern.allow_duplicate_squares = true;

  REQUIRE(validate_pattern_definition(pattern).ok());
}

TEST_CASE("pattern schema validation rejects pattern size overflow",
          "[evaluation][pattern_schema]") {
  PatternDefinition pattern{
      .id = "too-large",
      .length = static_cast<std::uint8_t>(kMaxPatternLengthForUint32Size + 1),
      .squares = std::vector<board_core::Square>(
          static_cast<std::size_t>(kMaxPatternLengthForUint32Size + 1), square(0, 0)),
      .allow_duplicate_squares = true,
  };

  require_schema_error(pattern, PatternSchemaValidationError::pattern_size_overflow);
  REQUIRE_FALSE(checked_pattern_size(pattern.length).has_value());
  REQUIRE(pattern_size(pattern.length) == 0);
}

} // namespace vibe_othello::evaluation
