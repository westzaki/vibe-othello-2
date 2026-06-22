#include "vibe_othello/board_core/serialization.h"
#include "vibe_othello/evaluation/pattern_artifact.h"
#include "vibe_othello/evaluation/pattern_evaluator.h"
#include "vibe_othello/search/search.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace vibe_othello::evaluation {
namespace {

#ifndef VIBE_OTHELLO_SOURCE_DIR
#define VIBE_OTHELLO_SOURCE_DIR "."
#endif

inline constexpr std::uint32_t kCrc32Initial = 0xFFFF'FFFFU;
inline constexpr std::uint32_t kCrc32Polynomial = 0xEDB8'8320U;

class TempDir {
public:
  TempDir() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("vibe-othello-pattern-artifact-test-" + std::to_string(unique));
    std::filesystem::create_directories(path_);
  }

  ~TempDir() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }

private:
  std::filesystem::path path_;
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

std::string hex_u32(std::uint32_t value) {
  std::ostringstream output;
  output << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(8) << value;
  return output.str();
}

std::vector<std::uint8_t> zero_tiny_artifact(std::uint32_t* checksum) {
  const std::string pattern_set_id = "fixed-pattern-fixture-v1";
  const std::uint16_t phase_count = 13;
  const std::uint16_t pattern_count = 2;
  const std::uint32_t phase_stride = 1 + 6561 + 19683;
  const std::uint32_t weight_count = phase_stride * phase_count;

  std::vector<std::uint8_t> bytes{'V', 'O', 'P', 'W', 'G', 'T', '\0', '\0'};
  append_u16(&bytes, 1);
  append_u16(&bytes, 1);
  append_u16(&bytes, 1);
  append_u16(&bytes, 1);
  append_u16(&bytes, phase_count);
  append_u16(&bytes, pattern_count);
  append_u16(&bytes, static_cast<std::uint16_t>(pattern_set_id.size()));
  append_u16(&bytes, 0);
  append_u32(&bytes, weight_count);
  bytes.insert(bytes.end(), pattern_set_id.begin(), pattern_set_id.end());
  bytes.resize(bytes.size() + static_cast<std::size_t>(weight_count) * sizeof(std::int32_t), 0);
  *checksum = crc32(bytes);
  append_u32(&bytes, *checksum);
  return bytes;
}

void write_bytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

void write_text(const std::filesystem::path& path, const std::string& text) {
  std::ofstream output(path);
  output << text;
}

void write_tiny_manifest(const std::filesystem::path& path, std::string checksum) {
  write_text(path, "{\n"
                   "  \"artifact_id\": \"tiny-test-artifact\",\n"
                   "  \"format\": \"vibe-othello-pattern-eval\",\n"
                   "  \"format_version\": 1,\n"
                   "  \"bit_order\": \"a1-lsb\",\n"
                   "  \"score_unit\": \"disc-diff\",\n"
                   "  \"score_scale\": 1,\n"
                   "  \"phase_count\": 13,\n"
                   "  \"pattern_set_id\": \"fixed-pattern-fixture-v1\",\n"
                   "  \"weights_file\": \"weights.bin\",\n"
                   "  \"weights_checksum\": \"" +
                       checksum +
                       "\"\n"
                       "}\n");
}

std::filesystem::path source_root() {
  return std::filesystem::path{VIBE_OTHELLO_SOURCE_DIR};
}

std::filesystem::path committed_manifest_path() {
  return source_root() / "data/eval/artifacts/pattern-v2-endgame-lite-100k-mt-v0/manifest.json";
}

board_core::Position late_midgame_position() {
  const std::optional<board_core::Position> position = board_core::parse_position(
      "WWWWWB../.WWWBW../BBWBBW../BWWBB.W./BWBBBWW./BWBBB.W./.WWWWB../BW.WWWWW b");
  REQUIRE(position.has_value());
  return *position;
}

} // namespace

TEST_CASE("default evaluation artifact pointer loads committed artifact",
          "[evaluation][artifact]") {
  const PatternArtifactLoadResult result =
      load_default_pattern_artifact(default_eval_root(source_root()));

  REQUIRE(result.ok());
  REQUIRE(result.artifact->artifact_id == "pattern-v2-endgame-lite-100k-mt-v0");
  REQUIRE(result.artifact->pattern_set_id == "pattern-v2-endgame-lite");
  REQUIRE(result.artifact->weights_checksum == "0x3d50ed72");
  REQUIRE(result.artifact->manifest_path == committed_manifest_path());
}

TEST_CASE("explicit evaluation artifact manifest loads the same committed artifact",
          "[evaluation][artifact]") {
  const PatternArtifactLoadResult default_result =
      load_default_pattern_artifact(default_eval_root(source_root()));
  const PatternArtifactLoadResult explicit_result =
      load_pattern_artifact(committed_manifest_path());

  REQUIRE(default_result.ok());
  REQUIRE(explicit_result.ok());
  REQUIRE(explicit_result.artifact->artifact_id == default_result.artifact->artifact_id);
  REQUIRE(explicit_result.artifact->weights_checksum == default_result.artifact->weights_checksum);
}

TEST_CASE("default evaluation artifact fails loudly when the pointer target is missing",
          "[evaluation][artifact]") {
  TempDir temp;
  write_text(temp.path() / "default-artifact.json",
             "{\n"
             "  \"artifact_manifest\": \"artifacts/missing/manifest.json\"\n"
             "}\n");

  const PatternArtifactLoadResult result = load_default_pattern_artifact(temp.path());

  REQUIRE_FALSE(result.ok());
  REQUIRE(result.error.find("cannot read artifact manifest") != std::string::npos);
}

TEST_CASE("evaluation artifact loader rejects manifest checksum mismatch",
          "[evaluation][artifact]") {
  TempDir temp;
  const std::filesystem::path artifact_dir = temp.path() / "artifacts/tiny";
  std::filesystem::create_directories(artifact_dir);
  std::uint32_t checksum = 0;
  const std::vector<std::uint8_t> artifact = zero_tiny_artifact(&checksum);
  write_bytes(artifact_dir / "weights.bin", artifact);
  write_tiny_manifest(artifact_dir / "manifest.json", "0x00000000");

  const PatternArtifactLoadResult result = load_pattern_artifact(artifact_dir / "manifest.json");

  REQUIRE_FALSE(result.ok());
  REQUIRE(result.error.find("artifact checksum mismatch") != std::string::npos);
  REQUIRE(hex_u32(checksum) != "0x00000000");
}

TEST_CASE("default evaluation artifact returns deterministic fixed-position score",
          "[evaluation][artifact]") {
  PatternArtifactLoadResult result =
      load_default_pattern_artifact(default_eval_root(source_root()));
  REQUIRE(result.ok());
  LoadedPatternArtifact artifact = std::move(*result.artifact);
  PatternEvaluator evaluator{std::move(artifact.weights), std::move(artifact.feature_set)};

  const search::Score score = evaluator.evaluate(late_midgame_position());

  REQUIRE(score == 0);
}

TEST_CASE("default evaluation artifact runs fixed-position search without illegal moves",
          "[evaluation][artifact][search]") {
  PatternArtifactLoadResult result =
      load_default_pattern_artifact(default_eval_root(source_root()));
  REQUIRE(result.ok());
  LoadedPatternArtifact artifact = std::move(*result.artifact);
  PatternEvaluator evaluator{std::move(artifact.weights), std::move(artifact.feature_set)};

  const search::SearchResult search_result =
      search::search_fixed_depth(late_midgame_position(), evaluator, 1);

  REQUIRE(search_result.completed_depth == 1);
  REQUIRE(search_result.best_move.has_value());
}

TEST_CASE("default and explicit committed artifacts produce the same fixed-position score",
          "[evaluation][artifact]") {
  PatternArtifactLoadResult default_result =
      load_default_pattern_artifact(default_eval_root(source_root()));
  PatternArtifactLoadResult explicit_result = load_pattern_artifact(committed_manifest_path());
  REQUIRE(default_result.ok());
  REQUIRE(explicit_result.ok());

  LoadedPatternArtifact default_artifact = std::move(*default_result.artifact);
  LoadedPatternArtifact explicit_artifact = std::move(*explicit_result.artifact);
  PatternEvaluator default_evaluator{std::move(default_artifact.weights),
                                     std::move(default_artifact.feature_set)};
  PatternEvaluator explicit_evaluator{std::move(explicit_artifact.weights),
                                      std::move(explicit_artifact.feature_set)};

  const board_core::Position position = late_midgame_position();
  REQUIRE(default_evaluator.evaluate(position) == explicit_evaluator.evaluate(position));
}

} // namespace vibe_othello::evaluation
