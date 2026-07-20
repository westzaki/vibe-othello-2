#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/serialization.h"
#include "vibe_othello/evaluation/pattern_artifact.h"
#include "vibe_othello/evaluation/pattern_evaluator.h"
#include "vibe_othello/search/search.h"

#include <array>
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
#include <string_view>
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

std::vector<std::uint8_t> zero_tiny_artifact(std::uint32_t* checksum,
                                             std::uint16_t score_scale = 1) {
  const std::string pattern_set_id = "fixed-pattern-fixture-v1";
  const std::uint16_t phase_count = 13;
  const std::uint16_t pattern_count = 2;
  const std::uint32_t phase_stride = 1 + 6561 + 19683;
  const std::uint32_t weight_count = phase_stride * phase_count;

  std::vector<std::uint8_t> bytes{'V', 'O', 'P', 'W', 'G', 'T', '\0', '\0'};
  append_u16(&bytes, 1);
  append_u16(&bytes, 1);
  append_u16(&bytes, 1);
  append_u16(&bytes, score_scale);
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
  return source_root() /
         "data/eval/artifacts/pattern-v2-egaroucid-lv17-full-value-v1/manifest.json";
}

std::filesystem::path committed_weights_path() {
  return source_root() / "data/eval/artifacts/pattern-v2-egaroucid-lv17-full-value-v1/weights.bin";
}

std::string read_text_or_fail(const std::filesystem::path& path) {
  std::ifstream input(path);
  REQUIRE(input);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  const bool read_ok = input.good() || input.eof();
  REQUIRE(read_ok);
  return buffer.str();
}

std::vector<std::uint8_t> read_bytes_or_fail(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  REQUIRE(input);
  input.seekg(0, std::ios::end);
  const std::streamoff size = input.tellg();
  REQUIRE(size >= 0);
  input.seekg(0, std::ios::beg);

  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  REQUIRE(input);
  return bytes;
}

std::string replace_once(std::string text, std::string_view needle, std::string_view replacement) {
  const std::size_t pos = text.find(needle);
  REQUIRE(pos != std::string::npos);
  text.replace(pos, needle.size(), std::string(replacement));
  return text;
}

std::string with_trained_phases(std::string manifest, std::string_view phases) {
  return replace_once(std::move(manifest), "  \"phase_count\": 13,\n",
                      "  \"phase_count\": 13,\n  \"trained_phases\": " + std::string(phases) +
                          ",\n");
}

std::string with_fallback_additive_phase(std::string manifest, std::string_view phase) {
  return replace_once(std::move(manifest), "  \"phase_count\": 13,\n",
                      "  \"phase_count\": 13,\n  \"fallback_additive_through_phase\": " +
                          std::string(phase) + ",\n");
}

std::string with_weights_checksum(std::string manifest, std::string_view checksum) {
  constexpr std::string_view prefix{"\"weights_checksum\": \""};
  const std::size_t value_start = manifest.find(prefix);
  REQUIRE(value_start != std::string::npos);
  const std::size_t checksum_start = value_start + prefix.size();
  const std::size_t checksum_end = manifest.find('"', checksum_start);
  REQUIRE(checksum_end != std::string::npos);
  manifest.replace(checksum_start, checksum_end - checksum_start, checksum);
  return manifest;
}

board_core::Position opening_move_position() {
  board_core::Position position = board_core::initial_position();
  board_core::MoveDelta delta{};
  REQUIRE(board_core::apply_move(
      &position, board_core::make_move(board_core::square_from_file_rank(2, 3)), &delta));
  return position;
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
  REQUIRE(result.artifact->artifact_id == "pattern-v2-egaroucid-lv17-full-value-v1");
  REQUIRE(result.artifact->pattern_set_id == "pattern-v2-endgame-lite");
  REQUIRE(result.artifact->weights_checksum == "0xfe3d38f9");
  REQUIRE(result.artifact->trained_phases ==
          std::vector<std::uint8_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
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

TEST_CASE("in-memory evaluation artifact loader matches filesystem loading",
          "[evaluation][artifact]") {
  const PatternArtifactLoadResult filesystem_result =
      load_pattern_artifact(committed_manifest_path());
  const std::string manifest_text = read_text_or_fail(committed_manifest_path());
  const std::vector<std::uint8_t> weights_bytes = read_bytes_or_fail(committed_weights_path());
  const PatternArtifactBytesLoadResult memory_result = load_pattern_artifact_from_bytes(
      manifest_text, weights_bytes, committed_manifest_path().generic_string());

  REQUIRE(filesystem_result.ok());
  REQUIRE(memory_result.ok());
  REQUIRE(memory_result.artifact->artifact_id == filesystem_result.artifact->artifact_id);
  REQUIRE(memory_result.artifact->pattern_set_id == filesystem_result.artifact->pattern_set_id);
  REQUIRE(memory_result.artifact->weights_checksum == filesystem_result.artifact->weights_checksum);
  REQUIRE(memory_result.artifact->trained_phases == filesystem_result.artifact->trained_phases);
  REQUIRE(memory_result.artifact->fallback_additive_through_phase ==
          filesystem_result.artifact->fallback_additive_through_phase);

  LoadedPatternArtifact filesystem_artifact = std::move(*filesystem_result.artifact);
  LoadedPatternArtifactBytes memory_artifact = std::move(*memory_result.artifact);
  PatternEvaluator filesystem_evaluator{std::move(filesystem_artifact.weights),
                                        std::move(filesystem_artifact.feature_set)};
  PatternEvaluator memory_evaluator{std::move(memory_artifact.weights),
                                    std::move(memory_artifact.feature_set)};

  const std::array<board_core::Position, 3> positions{
      board_core::initial_position(),
      opening_move_position(),
      late_midgame_position(),
  };
  for (const board_core::Position position : positions) {
    REQUIRE(memory_evaluator.evaluate(position) == filesystem_evaluator.evaluate(position));
  }
}

TEST_CASE("evaluation artifact loader accepts fixed point residual artifacts",
          "[evaluation][artifact]") {
  TempDir temp;
  std::uint32_t checksum = 0;
  const std::vector<std::uint8_t> artifact = zero_tiny_artifact(&checksum, 100);
  const std::filesystem::path manifest_path = temp.path() / "manifest.json";
  write_tiny_manifest(manifest_path, hex_u32(checksum));
  std::string manifest = read_text_or_fail(manifest_path);
  manifest = replace_once(std::move(manifest), "\"score_scale\": 1", "\"score_scale\": 100");
  manifest = with_trained_phases(std::move(manifest), "[0, 1, 10, 11, 12]");
  manifest = with_fallback_additive_phase(std::move(manifest), "9");

  const PatternArtifactBytesLoadResult result =
      load_pattern_artifact_from_bytes(manifest, artifact, "fixed-point-residual.json");

  INFO(result.error);
  REQUIRE(result.ok());
  REQUIRE(result.artifact->weights.score_scale() == 100);
  REQUIRE(result.artifact->fallback_additive_through_phase == 9);
}

TEST_CASE("evaluation artifact loader validates trained phase coverage", "[evaluation][artifact]") {
  TempDir temp;
  std::uint32_t checksum = 0;
  const std::vector<std::uint8_t> artifact = zero_tiny_artifact(&checksum);
  const std::filesystem::path manifest_path = temp.path() / "manifest.json";
  write_tiny_manifest(manifest_path, hex_u32(checksum));
  const std::string legacy_manifest = read_text_or_fail(manifest_path);

  SECTION("valid coverage is normalized and exposed") {
    const PatternArtifactBytesLoadResult result = load_pattern_artifact_from_bytes(
        with_trained_phases(legacy_manifest, "[12, 10, 11]"), artifact, "valid-coverage.json");

    REQUIRE(result.ok());
    REQUIRE(result.artifact->trained_phases == std::vector<std::uint8_t>{10, 11, 12});
  }

  SECTION("duplicate coverage is rejected") {
    const PatternArtifactBytesLoadResult result = load_pattern_artifact_from_bytes(
        with_trained_phases(legacy_manifest, "[10, 10]"), artifact, "duplicate-coverage.json");

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error.find("must be unique") != std::string::npos);
  }

  SECTION("out-of-range coverage is rejected") {
    const PatternArtifactBytesLoadResult result = load_pattern_artifact_from_bytes(
        with_trained_phases(legacy_manifest, "[13]"), artifact, "out-of-range-coverage.json");

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error.find("must be in [0, 12]") != std::string::npos);
  }

  SECTION("empty coverage is rejected") {
    const PatternArtifactBytesLoadResult result = load_pattern_artifact_from_bytes(
        with_trained_phases(legacy_manifest, "[]"), artifact, "empty-coverage.json");

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error.find("must not be empty") != std::string::npos);
  }

  SECTION("legacy artifacts report unknown coverage") {
    write_bytes(temp.path() / "weights.bin", artifact);
    const PatternArtifactLoadResult result = load_pattern_artifact(manifest_path);

    REQUIRE(result.ok());
    REQUIRE_FALSE(result.artifact->trained_phases.has_value());
  }

  SECTION("out-of-range fallback residual phase is rejected") {
    const PatternArtifactBytesLoadResult result =
        load_pattern_artifact_from_bytes(with_fallback_additive_phase(legacy_manifest, "13"),
                                         artifact, "out-of-range-fallback-additive.json");

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error.find("fallback_additive_through_phase must be in [0, 12]") !=
            std::string::npos);
  }

  SECTION("non-integer fallback residual phase is rejected") {
    const PatternArtifactBytesLoadResult result =
        load_pattern_artifact_from_bytes(with_fallback_additive_phase(legacy_manifest, "\"9\""),
                                         artifact, "non-integer-fallback-additive.json");

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error.find("fallback_additive_through_phase must be an integer") !=
            std::string::npos);
  }
}

TEST_CASE("in-memory evaluation artifact loader rejects invalid inputs", "[evaluation][artifact]") {
  const std::string manifest_text = read_text_or_fail(committed_manifest_path());
  const std::vector<std::uint8_t> weights_bytes = read_bytes_or_fail(committed_weights_path());

  SECTION("bad format_version") {
    const std::string manifest =
        replace_once(manifest_text, "\"format_version\": 1", "\"format_version\": 2");

    const PatternArtifactBytesLoadResult result =
        load_pattern_artifact_from_bytes(manifest, weights_bytes, "bad-format-version.json");

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error.find("format_version must be 1") != std::string::npos);
  }

  SECTION("unknown pattern_set_id") {
    const std::string manifest =
        replace_once(manifest_text, "\"pattern_set_id\": \"pattern-v2-endgame-lite\"",
                     "\"pattern_set_id\": \"unknown-pattern-set\"");

    const PatternArtifactBytesLoadResult result =
        load_pattern_artifact_from_bytes(manifest, weights_bytes, "unknown-pattern-set.json");

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error.find("unsupported pattern_set_id") != std::string::npos);
  }

  SECTION("checksum mismatch") {
    std::vector<std::uint8_t> corrupt_weights = weights_bytes;
    REQUIRE_FALSE(corrupt_weights.empty());
    corrupt_weights.front() ^= 0xFFU;

    const PatternArtifactBytesLoadResult result =
        load_pattern_artifact_from_bytes(manifest_text, corrupt_weights, "checksum-mismatch.json");

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error.find("artifact checksum mismatch") != std::string::npos);
  }

  SECTION("truncated weights") {
    std::vector<std::uint8_t> truncated_weights = weights_bytes;
    truncated_weights.resize(24);
    const std::string truncated_checksum = hex_u32(crc32(std::span<const std::uint8_t>{
        truncated_weights.data(), truncated_weights.size() - sizeof(std::uint32_t)}));
    const std::string manifest = with_weights_checksum(manifest_text, truncated_checksum);

    const PatternArtifactBytesLoadResult result =
        load_pattern_artifact_from_bytes(manifest, truncated_weights, "truncated-weights.json");

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error.find("runtime loader rejected artifact") != std::string::npos);
  }

  SECTION("missing weights_file") {
    const std::string manifest =
        replace_once(manifest_text, "  \"weights_file\": \"weights.bin\",\n", "");

    const PatternArtifactBytesLoadResult result =
        load_pattern_artifact_from_bytes(manifest, weights_bytes, "missing-weights-file.json");

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error.find("missing string field weights_file") != std::string::npos);
  }

  SECTION("parent traversal weights_file") {
    const std::string manifest = replace_once(manifest_text, "\"weights_file\": \"weights.bin\"",
                                              "\"weights_file\": \"../weights.bin\"");

    const PatternArtifactBytesLoadResult result =
        load_pattern_artifact_from_bytes(manifest, weights_bytes, "parent-weights-file.json");

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error.find("weights_file must be relative") != std::string::npos);
  }

#ifndef _WIN32
  SECTION("absolute weights_file") {
    const std::string manifest = replace_once(manifest_text, "\"weights_file\": \"weights.bin\"",
                                              "\"weights_file\": \"/tmp/weights.bin\"");

    const PatternArtifactBytesLoadResult result =
        load_pattern_artifact_from_bytes(manifest, weights_bytes, "absolute-weights-file.json");

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error.find("weights_file must be relative") != std::string::npos);
  }
#endif
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

  REQUIRE(score == -3);
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
