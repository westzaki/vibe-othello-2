#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/pattern_feature_set.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace vibe_othello::evaluation {
namespace {

using board_core::square_from_file_rank;

constexpr board_core::Square square(int file, int rank) noexcept {
  return square_from_file_rank(file, rank);
}

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

std::array<PatternCell, 9>
transform_square_d4(const std::array<PatternCell, 9>& cells,
                    const std::array<std::uint8_t, 9>& source_by_output) {
  std::array<PatternCell, 9> transformed{};
  for (std::size_t index = 0; index < transformed.size(); ++index) {
    transformed[index] = cells[source_by_output[index]];
  }
  return transformed;
}

std::array<PatternCell, 8> reverse_edge(const std::array<PatternCell, 8>& cells) {
  std::array<PatternCell, 8> reversed = cells;
  std::reverse(reversed.begin(), reversed.end());
  return reversed;
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
  REQUIRE(pattern_set.patterns[0].symmetry_policy == PatternSymmetryPolicy::none);
  REQUIRE(pattern_set.patterns[1].id == "corner-3x3");
  REQUIRE(pattern_set.patterns[1].length == 9);
  REQUIRE(pattern_set.patterns[1].squares.size() == 9);
  REQUIRE(pattern_set.patterns[1].symmetry_policy == PatternSymmetryPolicy::none);
  REQUIRE(validate_pattern_set(pattern_set).ok());
}

TEST_CASE("symmetry-aware fixed pattern-set fixture opts in edge and corner policies",
          "[evaluation][pattern_schema]") {
  const PatternSet& pattern_set = symmetry_aware_fixed_pattern_set_fixture();

  REQUIRE(pattern_set.id == "fixed-pattern-fixture-v1-symmetry-aware");
  REQUIRE(pattern_set.patterns.size() == 2);
  REQUIRE(pattern_set.patterns[0].id == "edge-8");
  REQUIRE(pattern_set.patterns[0].length == 8);
  REQUIRE(pattern_set.patterns[0].squares.size() == 8);
  REQUIRE(pattern_set.patterns[0].symmetry_policy == PatternSymmetryPolicy::reverse);
  REQUIRE(pattern_set.patterns[1].id == "corner-3x3");
  REQUIRE(pattern_set.patterns[1].length == 9);
  REQUIRE(pattern_set.patterns[1].squares.size() == 9);
  REQUIRE(pattern_set.patterns[1].symmetry_policy == PatternSymmetryPolicy::square_d4);
  REQUIRE(validate_pattern_set(pattern_set).ok());
}

TEST_CASE("buro-lite pattern set defines wider raw production-ish families",
          "[evaluation][pattern_schema]") {
  const PatternSet& pattern_set = buro_lite_pattern_set();
  const PatternFeatureSet feature_set = buro_lite_pattern_feature_set();

  REQUIRE(pattern_set.id == "pattern-v1-buro-lite");
  REQUIRE(feature_set.id == pattern_set.id);
  REQUIRE(pattern_set.patterns.size() == 6);
  REQUIRE(feature_set.tables.size() == pattern_set.patterns.size());

  const std::vector<std::string> expected_ids{"edge-8",     "near-edge-8", "diagonal-8",
                                              "diagonal-7", "corner-2x5",  "corner-3x3"};
  const std::vector<std::uint8_t> expected_lengths{8, 8, 8, 7, 10, 9};
  const std::vector<std::size_t> expected_instances{4, 4, 2, 4, 8, 4};

  std::size_t instance_count = 0;
  std::uint32_t phase_stride = 1;
  for (std::size_t index = 0; index < pattern_set.patterns.size(); ++index) {
    const PatternDefinition& pattern = pattern_set.patterns[index];
    const PatternFeatureTable& table = feature_set.tables[index];
    REQUIRE(pattern.id == expected_ids[index]);
    REQUIRE(pattern.length == expected_lengths[index]);
    REQUIRE(pattern.symmetry_policy == PatternSymmetryPolicy::none);
    REQUIRE(table.pattern_id == pattern.id);
    REQUIRE(table.pattern_length == pattern.length);
    REQUIRE(table.instances.size() == expected_instances[index]);
    instance_count += table.instances.size();
    phase_stride += pattern_size(pattern.length);
  }

  REQUIRE(instance_count == 26);
  REQUIRE(phase_stride == 100603);
  REQUIRE(validate_pattern_set(pattern_set).ok());
}

TEST_CASE("endgame-lite pattern set adds bounded late-phase families after buro-lite",
          "[evaluation][pattern_schema]") {
  const PatternSet& pattern_set = endgame_lite_pattern_set();
  const PatternFeatureSet feature_set = endgame_lite_pattern_feature_set();

  REQUIRE(pattern_set.id == "pattern-v2-endgame-lite");
  REQUIRE(feature_set.id == pattern_set.id);
  REQUIRE(pattern_set.patterns.size() == 11);
  REQUIRE(feature_set.tables.size() == pattern_set.patterns.size());

  const std::vector<std::string> expected_ids{
      "edge-8",        "near-edge-8",         "diagonal-8",       "diagonal-7",
      "corner-2x5",    "corner-3x3",          "corner-2x4-8",     "edge-plus-x-10",
      "corner-wing-8", "near-edge-segment-8", "diagonal-corner-8"};
  const std::vector<std::uint8_t> expected_lengths{8, 8, 8, 7, 10, 9, 8, 10, 8, 8, 8};
  const std::vector<std::size_t> expected_instances{4, 4, 2, 4, 8, 4, 8, 4, 8, 8, 4};

  std::size_t instance_count = 0;
  std::uint32_t phase_stride = 1;
  for (std::size_t index = 0; index < pattern_set.patterns.size(); ++index) {
    const PatternDefinition& pattern = pattern_set.patterns[index];
    const PatternFeatureTable& table = feature_set.tables[index];
    REQUIRE(pattern.id == expected_ids[index]);
    REQUIRE(pattern.length == expected_lengths[index]);
    REQUIRE(pattern.symmetry_policy == PatternSymmetryPolicy::none);
    REQUIRE(pattern_size(pattern.length) <= pattern_size(10));
    REQUIRE(table.pattern_id == pattern.id);
    REQUIRE(table.pattern_length == pattern.length);
    REQUIRE(table.instances.size() == expected_instances[index]);
    instance_count += table.instances.size();
    phase_stride += pattern_size(pattern.length);
  }

  REQUIRE(instance_count == 58);
  REQUIRE(phase_stride == 185896);
  REQUIRE(static_cast<std::uint64_t>(phase_stride) * 13U < 2500000U);
  REQUIRE(validate_pattern_set(pattern_set).ok());
}

TEST_CASE("pattern schema validation accepts valid definitions", "[evaluation][pattern_schema]") {
  const PatternDefinition pattern = valid_pattern();

  REQUIRE(validate_pattern_definition(pattern).ok());
  REQUIRE(pattern.symmetry_policy == PatternSymmetryPolicy::none);
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

TEST_CASE("pattern schema validation accepts supported symmetry policies",
          "[evaluation][pattern_schema]") {
  PatternDefinition edge = valid_pattern();
  edge.symmetry_policy = PatternSymmetryPolicy::reverse;

  PatternDefinition corner{
      .id = "corner-3x3",
      .length = 9,
      .squares =
          {
              square(0, 0),
              square(1, 0),
              square(2, 0),
              square(0, 1),
              square(1, 1),
              square(2, 1),
              square(0, 2),
              square(1, 2),
              square(2, 2),
          },
      .symmetry_policy = PatternSymmetryPolicy::square_d4,
  };

  REQUIRE(validate_pattern_definition(edge).ok());
  REQUIRE(validate_pattern_definition(corner).ok());
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

TEST_CASE("pattern schema validation rejects unsupported symmetry policy",
          "[evaluation][pattern_schema]") {
  PatternDefinition pattern = valid_pattern();
  pattern.symmetry_policy = static_cast<PatternSymmetryPolicy>(255);

  require_schema_error(pattern, PatternSchemaValidationError::unsupported_symmetry_policy);
}

TEST_CASE("pattern schema validation rejects square D4 for non-3x3 lengths",
          "[evaluation][pattern_schema]") {
  PatternDefinition pattern = valid_pattern();
  pattern.symmetry_policy = PatternSymmetryPolicy::square_d4;

  require_schema_error(pattern, PatternSchemaValidationError::unsupported_symmetry_policy);
}

TEST_CASE("canonical ternary pattern index keeps none policy unchanged",
          "[evaluation][pattern_schema]") {
  constexpr std::array<PatternCell, 3> cells{
      PatternCell::empty,
      PatternCell::player,
      PatternCell::opponent,
  };

  const std::optional<std::uint32_t> canonical =
      canonical_ternary_pattern_index(cells, PatternSymmetryPolicy::none);

  REQUIRE(canonical.has_value());
  REQUIRE(*canonical == ternary_pattern_index(cells));
  REQUIRE(canonical_ternary_pattern_index(cells, PatternSymmetryPolicy::none) == canonical);
}

TEST_CASE("canonical ternary pattern index handles reverse symmetry",
          "[evaluation][pattern_schema]") {
  constexpr std::array<PatternCell, 8> forward{
      PatternCell::opponent, PatternCell::empty,  PatternCell::player,   PatternCell::empty,
      PatternCell::empty,    PatternCell::player, PatternCell::opponent, PatternCell::empty,
  };
  const std::array<PatternCell, 8> reversed = reverse_edge(forward);

  const std::uint32_t forward_index = ternary_pattern_index(forward);
  const std::uint32_t reversed_index = ternary_pattern_index(reversed);
  const std::uint32_t expected = std::min(forward_index, reversed_index);

  const std::optional<std::uint32_t> canonical_forward =
      canonical_ternary_pattern_index(forward, PatternSymmetryPolicy::reverse);
  const std::optional<std::uint32_t> canonical_reversed =
      canonical_ternary_pattern_index(reversed, PatternSymmetryPolicy::reverse);
  const std::optional<std::uint32_t> canonical_from_index = canonical_ternary_pattern_index(
      forward_index, static_cast<std::uint8_t>(forward.size()), PatternSymmetryPolicy::reverse);

  REQUIRE(canonical_forward.has_value());
  REQUIRE(canonical_reversed.has_value());
  REQUIRE(canonical_from_index.has_value());
  REQUIRE(*canonical_forward == expected);
  REQUIRE(*canonical_reversed == expected);
  REQUIRE(*canonical_from_index == expected);
}

TEST_CASE("canonical ternary pattern index handles square D4 symmetry",
          "[evaluation][pattern_schema]") {
  constexpr std::array<PatternCell, 9> corner{
      PatternCell::empty,    PatternCell::player, PatternCell::opponent,
      PatternCell::opponent, PatternCell::empty,  PatternCell::player,
      PatternCell::player,   PatternCell::empty,  PatternCell::opponent,
  };

  std::uint32_t expected = std::numeric_limits<std::uint32_t>::max();
  for (const std::array<std::uint8_t, 9>& source_by_output : kSquareD4SourceByOutput) {
    const std::array<PatternCell, 9> transformed = transform_square_d4(corner, source_by_output);
    expected = std::min(expected, ternary_pattern_index(transformed));
  }

  const std::optional<std::uint32_t> canonical =
      canonical_ternary_pattern_index(corner, PatternSymmetryPolicy::square_d4);
  REQUIRE(canonical.has_value());
  REQUIRE(*canonical == expected);

  const std::array<PatternCell, 9> rotated =
      transform_square_d4(corner, kSquareD4SourceByOutput[1]);
  const std::array<PatternCell, 9> reflected =
      transform_square_d4(corner, kSquareD4SourceByOutput[4]);
  REQUIRE(canonical_ternary_pattern_index(rotated, PatternSymmetryPolicy::square_d4) == canonical);
  REQUIRE(canonical_ternary_pattern_index(reflected, PatternSymmetryPolicy::square_d4) ==
          canonical);

  for (const std::array<std::uint8_t, 9>& source_by_output : kSquareD4SourceByOutput) {
    const std::array<PatternCell, 9> transformed = transform_square_d4(corner, source_by_output);
    REQUIRE(canonical_ternary_pattern_index(transformed, PatternSymmetryPolicy::square_d4) ==
            canonical);
  }
}

TEST_CASE("symmetry-aware fixture policies canonicalize representative variants",
          "[evaluation][pattern_schema]") {
  const PatternSet& pattern_set = symmetry_aware_fixed_pattern_set_fixture();
  REQUIRE(pattern_set.patterns.size() == 2);

  constexpr std::array<PatternCell, 8> edge{
      PatternCell::opponent, PatternCell::empty,  PatternCell::player,   PatternCell::empty,
      PatternCell::empty,    PatternCell::player, PatternCell::opponent, PatternCell::empty,
  };
  const std::array<PatternCell, 8> reversed = reverse_edge(edge);
  const std::uint32_t edge_raw = ternary_pattern_index(edge);
  const std::uint32_t edge_reversed_raw = ternary_pattern_index(reversed);

  const std::optional<std::uint32_t> edge_canonical = canonical_ternary_pattern_index(
      edge_raw, static_cast<std::uint8_t>(edge.size()), pattern_set.patterns[0].symmetry_policy);
  const std::optional<std::uint32_t> reversed_edge_canonical =
      canonical_ternary_pattern_index(edge_reversed_raw, static_cast<std::uint8_t>(reversed.size()),
                                      pattern_set.patterns[0].symmetry_policy);

  REQUIRE(edge_canonical.has_value());
  REQUIRE(reversed_edge_canonical.has_value());
  REQUIRE(edge_canonical == reversed_edge_canonical);
  REQUIRE(*edge_canonical <= edge_raw);
  REQUIRE(*edge_canonical <= edge_reversed_raw);

  constexpr std::array<PatternCell, 9> corner{
      PatternCell::empty,    PatternCell::player, PatternCell::opponent,
      PatternCell::opponent, PatternCell::empty,  PatternCell::player,
      PatternCell::player,   PatternCell::empty,  PatternCell::opponent,
  };
  const std::array<PatternCell, 9> rotated =
      transform_square_d4(corner, kSquareD4SourceByOutput[1]);
  const std::array<PatternCell, 9> reflected =
      transform_square_d4(corner, kSquareD4SourceByOutput[4]);
  const std::uint32_t corner_raw = ternary_pattern_index(corner);

  const std::optional<std::uint32_t> corner_canonical =
      canonical_ternary_pattern_index(corner_raw, static_cast<std::uint8_t>(corner.size()),
                                      pattern_set.patterns[1].symmetry_policy);
  const std::optional<std::uint32_t> rotated_corner_canonical =
      canonical_ternary_pattern_index(rotated, pattern_set.patterns[1].symmetry_policy);
  const std::optional<std::uint32_t> reflected_corner_canonical =
      canonical_ternary_pattern_index(reflected, pattern_set.patterns[1].symmetry_policy);

  REQUIRE(corner_canonical.has_value());
  REQUIRE(rotated_corner_canonical.has_value());
  REQUIRE(reflected_corner_canonical.has_value());
  REQUIRE(corner_canonical == rotated_corner_canonical);
  REQUIRE(corner_canonical == reflected_corner_canonical);
  REQUIRE(*corner_canonical <= corner_raw);
}

TEST_CASE("canonical ternary pattern index preserves player and opponent digits",
          "[evaluation][pattern_schema]") {
  constexpr std::array<PatternCell, 9> player_corner{
      PatternCell::player, PatternCell::empty, PatternCell::empty,
      PatternCell::empty,  PatternCell::empty, PatternCell::empty,
      PatternCell::empty,  PatternCell::empty, PatternCell::empty,
  };
  constexpr std::array<PatternCell, 9> opponent_corner{
      PatternCell::opponent, PatternCell::empty, PatternCell::empty,
      PatternCell::empty,    PatternCell::empty, PatternCell::empty,
      PatternCell::empty,    PatternCell::empty, PatternCell::empty,
  };

  const std::optional<std::uint32_t> canonical_player =
      canonical_ternary_pattern_index(player_corner, PatternSymmetryPolicy::square_d4);
  const std::optional<std::uint32_t> canonical_opponent =
      canonical_ternary_pattern_index(opponent_corner, PatternSymmetryPolicy::square_d4);

  REQUIRE(canonical_player.has_value());
  REQUIRE(canonical_opponent.has_value());
  REQUIRE(*canonical_player == 1);
  REQUIRE(*canonical_opponent == 2);
}

TEST_CASE("canonical ternary pattern index rejects malformed inputs",
          "[evaluation][pattern_schema]") {
  constexpr std::array<PatternCell, kMaxPatternLengthForUint32Size + 1> too_long{};
  constexpr std::array<PatternCell, 8> edge{};

  REQUIRE_FALSE(
      canonical_ternary_pattern_index(std::span<const PatternCell>{}, PatternSymmetryPolicy::none)
          .has_value());
  REQUIRE_FALSE(canonical_ternary_pattern_index(too_long, PatternSymmetryPolicy::none).has_value());
  REQUIRE_FALSE(canonical_ternary_pattern_index(0, 0, PatternSymmetryPolicy::none).has_value());
  REQUIRE_FALSE(canonical_ternary_pattern_index(9, 2, PatternSymmetryPolicy::none).has_value());
  REQUIRE_FALSE(
      canonical_ternary_pattern_index(edge, PatternSymmetryPolicy::square_d4).has_value());
  REQUIRE_FALSE(
      canonical_ternary_pattern_index(edge, static_cast<PatternSymmetryPolicy>(255)).has_value());
}

} // namespace vibe_othello::evaluation
