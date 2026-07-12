#include "vibe_othello/board_core/coordinates.h"
#include "vibe_othello/evaluation/pattern_weights.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace vibe_othello::evaluation {
namespace {

using board_core::square_from_file_rank;

inline constexpr std::uint32_t kCrc32Initial = 0xFFFF'FFFFU;
inline constexpr std::uint32_t kCrc32Polynomial = 0xEDB8'8320U;

struct ArtifactOptions {
  std::uint16_t format_version = kPatternWeightFormatVersion;
  std::uint16_t bit_order = static_cast<std::uint16_t>(PatternBitOrder::a1_lsb);
  std::uint16_t score_unit = static_cast<std::uint16_t>(PatternScoreUnit::disc_diff);
  std::uint16_t score_scale = 1;
  std::uint16_t phase_count = 2;
  std::uint16_t pattern_count = 1;
  std::string pattern_set_id = "tiny-pattern-v1";
  std::uint32_t weight_count = 20;
};

void append_u16(std::vector<std::uint8_t>* bytes, std::uint16_t value) {
  bytes->push_back(static_cast<std::uint8_t>(value & 0xFFU));
  bytes->push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void append_u32(std::vector<std::uint8_t>* bytes, std::uint32_t value) {
  bytes->push_back(static_cast<std::uint8_t>(value & 0xFFU));
  bytes->push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  bytes->push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  bytes->push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

void append_i32(std::vector<std::uint8_t>* bytes, std::int32_t value) {
  append_u32(bytes, static_cast<std::uint32_t>(value));
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
  std::uint32_t crc = kCrc32Initial;
  for (const std::uint8_t byte : bytes) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      const std::uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (kCrc32Polynomial & mask);
    }
  }
  return ~crc;
}

PatternManifest tiny_manifest() {
  return PatternManifest{
      .format_version = kPatternWeightFormatVersion,
      .bit_order = PatternBitOrder::a1_lsb,
      .score_unit = PatternScoreUnit::disc_diff,
      .score_scale = 1,
      .phase_count = 2,
      .pattern_set_id = "tiny-pattern-v1",
      .patterns =
          {
              PatternDefinition{
                  .id = "tiny-corner-pair",
                  .length = 2,
                  .squares =
                      {
                          square_from_file_rank(0, 0),
                          square_from_file_rank(1, 0),
                      },
              },
          },
  };
}

std::vector<search::Score> tiny_weights() {
  return {
      10, -4, -3, -2, -1, 0, 1, 2, 3, 4, 20, 14, 13, 12, 11, 10, 9, 8, 7, 6,
  };
}

std::vector<std::uint8_t> tiny_artifact(ArtifactOptions options = {}) {
  std::vector<std::uint8_t> bytes{'V', 'O', 'P', 'W', 'G', 'T', '\0', '\0'};
  append_u16(&bytes, options.format_version);
  append_u16(&bytes, options.bit_order);
  append_u16(&bytes, options.score_unit);
  append_u16(&bytes, options.score_scale);
  append_u16(&bytes, options.phase_count);
  append_u16(&bytes, options.pattern_count);
  append_u16(&bytes, static_cast<std::uint16_t>(options.pattern_set_id.size()));
  append_u16(&bytes, 0);
  append_u32(&bytes, options.weight_count);

  bytes.insert(bytes.end(), options.pattern_set_id.begin(), options.pattern_set_id.end());

  const std::vector<search::Score> weights = tiny_weights();
  for (std::uint32_t index = 0; index < options.weight_count; ++index) {
    append_i32(&bytes, index < weights.size() ? weights[index] : search::Score{0});
  }

  append_u32(&bytes, crc32(bytes));
  return bytes;
}

void require_error(const PatternManifest& manifest, const std::vector<std::uint8_t>& artifact,
                   PatternWeightsLoadError error) {
  const PatternWeightsLoadResult result = load_pattern_weights(manifest, artifact);
  REQUIRE_FALSE(result.ok());
  REQUIRE_FALSE(result.weights.has_value());
  REQUIRE(result.error == error);
}

} // namespace

TEST_CASE("pattern weight artifact loader accepts tiny hand-authored artifact") {
  const PatternManifest manifest = tiny_manifest();
  const PatternWeightsLoadResult result = load_pattern_weights(manifest, tiny_artifact());

  REQUIRE(result.ok());
  REQUIRE(result.error == PatternWeightsLoadError::none);
  REQUIRE(result.weights->manifest.pattern_set_id == "tiny-pattern-v1");
  REQUIRE(result.weights->manifest.phase_count == 2);
  REQUIRE(result.weights->phase_stride == 10);
  REQUIRE(result.weights->pattern_table_offsets == std::vector<std::uint32_t>{1});
  REQUIRE(result.weights->weights == tiny_weights());
}

TEST_CASE("pattern weight artifact layout exposes bias slot and ordered pattern offsets") {
  const PatternManifest manifest{
      .format_version = kPatternWeightFormatVersion,
      .bit_order = PatternBitOrder::a1_lsb,
      .score_unit = PatternScoreUnit::disc_diff,
      .score_scale = 1,
      .phase_count = 1,
      .pattern_set_id = "tiny-pattern-v1",
      .patterns =
          {
              PatternDefinition{
                  .id = "first-single-square",
                  .length = 1,
                  .squares = {square_from_file_rank(0, 0)},
              },
              PatternDefinition{
                  .id = "second-corner-pair",
                  .length = 2,
                  .squares =
                      {
                          square_from_file_rank(0, 0),
                          square_from_file_rank(1, 0),
                      },
              },
          },
  };

  const PatternWeightsLoadResult result =
      load_pattern_weights(manifest, tiny_artifact(ArtifactOptions{
                                         .phase_count = 1,
                                         .pattern_count = 2,
                                         .weight_count = 13,
                                     }));

  REQUIRE(result.ok());
  REQUIRE(result.weights->phase_stride == 13);
  REQUIRE(result.weights->pattern_table_offsets == std::vector<std::uint32_t>{1, 4});
}

TEST_CASE("loaded pattern weights convert to evaluator runtime tables") {
  const PatternWeightsLoadResult result = load_pattern_weights(tiny_manifest(), tiny_artifact());
  REQUIRE(result.ok());

  std::array<std::uint8_t, PatternWeights::kDiscCountEntries> phases{};
  phases.fill(0);
  phases.back() = 1;

  const std::optional<PatternWeights> runtime_weights =
      make_pattern_weights(*result.weights, phases);

  REQUIRE(runtime_weights.has_value());
  REQUIRE(runtime_weights->phase_count() == 2);
  REQUIRE(runtime_weights->score_scale() == 1);
  REQUIRE(runtime_weights->phase_bias(0) == 10);
  REQUIRE(runtime_weights->phase_bias(1) == 20);
  REQUIRE(std::vector<search::Score>(runtime_weights->phase_biases().begin(),
                                     runtime_weights->phase_biases().end()) ==
          std::vector<search::Score>{10, 20});
  REQUIRE(runtime_weights->tables().size() == 1);
  REQUIRE(runtime_weights->tables()[0].pattern_id == "tiny-corner-pair");
  REQUIRE(runtime_weights->tables()[0].pattern_length == 2);
  REQUIRE(runtime_weights->weight(0, 0, 0) == -4);
  REQUIRE(runtime_weights->weight(0, 0, 8) == 4);
  REQUIRE(runtime_weights->weight(0, 1, 0) == 14);
  REQUIRE(runtime_weights->phase_for_disc_count(64) == 1);
}

TEST_CASE("pattern weight artifact loader validates format version") {
  require_error(tiny_manifest(), tiny_artifact(ArtifactOptions{.format_version = 2}),
                PatternWeightsLoadError::unsupported_format_version);
}

TEST_CASE("pattern weight artifact loader validates bit order") {
  require_error(tiny_manifest(), tiny_artifact(ArtifactOptions{.bit_order = 2}),
                PatternWeightsLoadError::unsupported_bit_order);
}

TEST_CASE("pattern weight artifact loader validates score unit") {
  require_error(tiny_manifest(), tiny_artifact(ArtifactOptions{.score_unit = 2}),
                PatternWeightsLoadError::unsupported_score_unit);
}

TEST_CASE("pattern weight artifact loader validates score scale") {
  require_error(tiny_manifest(), tiny_artifact(ArtifactOptions{.score_scale = 100}),
                PatternWeightsLoadError::unsupported_score_scale);
}

TEST_CASE("pattern weight artifact loader accepts matching fixed point score scale") {
  PatternManifest manifest = tiny_manifest();
  manifest.score_scale = 100;
  const PatternWeightsLoadResult result =
      load_pattern_weights(manifest, tiny_artifact(ArtifactOptions{.score_scale = 100}));

  REQUIRE(result.ok());
  REQUIRE(result.weights->manifest.score_scale == 100);
  const std::optional<PatternWeights> runtime_weights =
      make_pattern_weights(*result.weights, std::array<std::uint8_t, 65>{});
  REQUIRE(runtime_weights.has_value());
  REQUIRE(runtime_weights->score_scale() == 100);
}

TEST_CASE("pattern weight artifact loader validates phase count") {
  require_error(tiny_manifest(), tiny_artifact(ArtifactOptions{.phase_count = 3}),
                PatternWeightsLoadError::phase_count_mismatch);
}

TEST_CASE("pattern weight artifact loader validates pattern count") {
  require_error(tiny_manifest(), tiny_artifact(ArtifactOptions{.pattern_count = 2}),
                PatternWeightsLoadError::pattern_count_mismatch);
}

TEST_CASE("pattern weight artifact loader validates pattern set id") {
  require_error(tiny_manifest(), tiny_artifact(ArtifactOptions{.pattern_set_id = "other"}),
                PatternWeightsLoadError::pattern_set_id_mismatch);
}

TEST_CASE("pattern weight artifact loader validates pattern length against square count") {
  PatternManifest manifest = tiny_manifest();
  manifest.patterns[0].length = board_core::kSquareCount + 1;
  manifest.patterns[0].squares.assign(board_core::kSquareCount + 1, square_from_file_rank(0, 0));

  require_error(manifest, tiny_artifact(), PatternWeightsLoadError::invalid_pattern_length);
}

TEST_CASE("pattern weight artifact loader rejects invalid pattern schema") {
  PatternManifest empty_id = tiny_manifest();
  empty_id.patterns[0].id.clear();
  require_error(empty_id, tiny_artifact(), PatternWeightsLoadError::invalid_pattern_length);

  PatternManifest duplicate_square = tiny_manifest();
  duplicate_square.patterns[0].squares[1] = duplicate_square.patterns[0].squares[0];
  require_error(duplicate_square, tiny_artifact(), PatternWeightsLoadError::invalid_pattern_length);
}

TEST_CASE("pattern weight artifact loader validates pattern squares") {
  PatternManifest manifest = tiny_manifest();
  manifest.patterns[0].squares[1] = board_core::Square{board_core::kInvalidSquareIndex};

  require_error(manifest, tiny_artifact(), PatternWeightsLoadError::invalid_pattern_square);
}

TEST_CASE("pattern weight artifact loader validates weight count") {
  require_error(tiny_manifest(), tiny_artifact(ArtifactOptions{.weight_count = 19}),
                PatternWeightsLoadError::invalid_weight_count);
}

TEST_CASE("pattern weight artifact loader validates checksum") {
  std::vector<std::uint8_t> artifact = tiny_artifact();
  artifact[artifact.size() - 1] ^= 0x80U;

  require_error(tiny_manifest(), artifact, PatternWeightsLoadError::checksum_mismatch);
}

TEST_CASE("pattern weight artifact loader rejects bad magic and truncation") {
  std::vector<std::uint8_t> bad_magic = tiny_artifact();
  bad_magic[0] = 'X';
  require_error(tiny_manifest(), bad_magic, PatternWeightsLoadError::bad_magic);

  std::vector<std::uint8_t> truncated = tiny_artifact();
  truncated.pop_back();
  require_error(tiny_manifest(), truncated, PatternWeightsLoadError::truncated);
}

} // namespace vibe_othello::evaluation
