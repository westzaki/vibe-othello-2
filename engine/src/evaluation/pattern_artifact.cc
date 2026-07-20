#include "vibe_othello/evaluation/pattern_artifact.h"

#include "vibe_othello/evaluation/pattern.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vibe_othello::evaluation {
namespace {

inline constexpr std::uint32_t kCrc32Initial = 0xFFFF'FFFFU;
inline constexpr std::uint32_t kCrc32Polynomial = 0xEDB8'8320U;
inline constexpr std::uint16_t kRuntimePhaseCount = 13;

struct ArtifactManifestFields {
  std::string artifact_id;
  std::string pattern_set_id;
  std::filesystem::path weights_file;
  std::string weights_checksum;
  std::uint16_t score_scale = 1;
  std::optional<std::vector<std::uint8_t>> trained_phases;
  std::optional<std::uint8_t> fallback_additive_through_phase;
};

struct LoadedPatternArtifactCore {
  std::string artifact_id;
  std::string pattern_set_id;
  std::string weights_checksum;
  std::optional<std::vector<std::uint8_t>> trained_phases;
  std::optional<std::uint8_t> fallback_additive_through_phase;
  PatternWeights weights;
  PatternFeatureSet feature_set;
};

struct PatternArtifactCoreLoadResult {
  std::optional<LoadedPatternArtifactCore> artifact;
  std::string error;

  [[nodiscard]] bool ok() const noexcept {
    return artifact.has_value();
  }
};

struct PatternRuntime {
  const PatternSet* pattern_set = nullptr;
  PatternFeatureSet feature_set;
};

[[nodiscard]] PatternArtifactLoadResult fail(std::string error) {
  return PatternArtifactLoadResult{.artifact = std::nullopt, .error = std::move(error)};
}

[[nodiscard]] PatternArtifactBytesLoadResult fail_bytes(std::string error) {
  return PatternArtifactBytesLoadResult{.artifact = std::nullopt, .error = std::move(error)};
}

[[nodiscard]] PatternArtifactCoreLoadResult fail_core(std::string error) {
  return PatternArtifactCoreLoadResult{.artifact = std::nullopt, .error = std::move(error)};
}

[[nodiscard]] std::optional<std::string> read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (!input.good() && !input.eof()) {
    return std::nullopt;
  }
  return buffer.str();
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>>
read_binary_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  input.seekg(0, std::ios::end);
  const std::streamoff size = input.tellg();
  if (size < 0) {
    return std::nullopt;
  }
  input.seekg(0, std::ios::beg);

  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  if (!input) {
    return std::nullopt;
  }
  return bytes;
}

[[nodiscard]] std::string quoted_field(std::string_view field) {
  return "\"" + std::string(field) + "\"";
}

[[nodiscard]] std::optional<std::size_t> field_value_offset(std::string_view json,
                                                            std::string_view field) {
  const std::string quoted = quoted_field(field);
  std::size_t pos = json.find(quoted);
  while (pos != std::string_view::npos) {
    pos += quoted.size();
    while (pos < json.size() &&
           (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) {
      ++pos;
    }
    if (pos < json.size() && json[pos] == ':') {
      ++pos;
      while (pos < json.size() &&
             (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) {
        ++pos;
      }
      return pos;
    }
    pos = json.find(quoted, pos);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> json_string_field(std::string_view json,
                                                           std::string_view field) {
  std::optional<std::size_t> pos = field_value_offset(json, field);
  if (!pos.has_value() || *pos >= json.size() || json[*pos] != '"') {
    return std::nullopt;
  }
  ++*pos;

  std::string value;
  while (*pos < json.size()) {
    const char ch = json[(*pos)++];
    if (ch == '"') {
      return value;
    }
    if (ch == '\\') {
      if (*pos >= json.size()) {
        return std::nullopt;
      }
      value.push_back(json[(*pos)++]);
    } else {
      value.push_back(ch);
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<int> json_int_field(std::string_view json, std::string_view field) {
  std::optional<std::size_t> pos = field_value_offset(json, field);
  if (!pos.has_value() || *pos >= json.size()) {
    return std::nullopt;
  }
  const char* begin = json.data() + *pos;
  const char* end = json.data() + json.size();
  int value = 0;
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc{} || ptr == begin) {
    return std::nullopt;
  }
  const char* cursor = ptr;
  while (cursor < end &&
         (*cursor == ' ' || *cursor == '\n' || *cursor == '\r' || *cursor == '\t')) {
    ++cursor;
  }
  if (cursor < end && *cursor != ',' && *cursor != '}' && *cursor != ']') {
    return std::nullopt;
  }
  return value;
}

void skip_json_whitespace(std::string_view json, std::size_t* pos) {
  while (*pos < json.size() &&
         (json[*pos] == ' ' || json[*pos] == '\n' || json[*pos] == '\r' || json[*pos] == '\t')) {
    ++*pos;
  }
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>>
parse_trained_phases(std::string_view json, std::string_view manifest_label, std::string* error) {
  std::optional<std::size_t> value_pos = field_value_offset(json, "trained_phases");
  if (!value_pos.has_value()) {
    return std::nullopt;
  }
  if (*value_pos >= json.size() || json[*value_pos] != '[') {
    *error = std::string(manifest_label) + ": trained_phases must be an array";
    return std::nullopt;
  }

  std::size_t pos = *value_pos + 1;
  skip_json_whitespace(json, &pos);
  if (pos < json.size() && json[pos] == ']') {
    *error = std::string(manifest_label) + ": trained_phases must not be empty";
    return std::nullopt;
  }

  std::vector<std::uint8_t> phases;
  while (true) {
    if (pos >= json.size()) {
      *error = std::string(manifest_label) + ": trained_phases must be a complete array";
      return std::nullopt;
    }
    const char* begin = json.data() + pos;
    const char* end = json.data() + json.size();
    int phase = 0;
    const auto [ptr, ec] = std::from_chars(begin, end, phase);
    if (ec != std::errc{} || ptr == begin) {
      *error = std::string(manifest_label) + ": trained_phases entries must be integers";
      return std::nullopt;
    }
    if (phase < 0 || phase >= kRuntimePhaseCount) {
      *error = std::string(manifest_label) + ": trained_phases entries must be in [0, 12]";
      return std::nullopt;
    }
    const std::uint8_t value = static_cast<std::uint8_t>(phase);
    if (std::find(phases.begin(), phases.end(), value) != phases.end()) {
      *error = std::string(manifest_label) + ": trained_phases entries must be unique";
      return std::nullopt;
    }
    phases.push_back(value);
    pos = static_cast<std::size_t>(ptr - json.data());
    skip_json_whitespace(json, &pos);
    if (pos >= json.size()) {
      *error = std::string(manifest_label) + ": trained_phases must be a complete array";
      return std::nullopt;
    }
    if (json[pos] == ']') {
      std::sort(phases.begin(), phases.end());
      return phases;
    }
    if (json[pos] != ',') {
      *error = std::string(manifest_label) + ": trained_phases entries must be comma-separated";
      return std::nullopt;
    }
    ++pos;
    skip_json_whitespace(json, &pos);
    if (pos >= json.size() || json[pos] == ']') {
      *error = std::string(manifest_label) + ": trained_phases entries must be integers";
      return std::nullopt;
    }
  }
}

[[nodiscard]] bool path_has_parent_reference(const std::filesystem::path& path) {
  return std::any_of(path.begin(), path.end(),
                     [](const std::filesystem::path& part) { return part == ".."; });
}

[[nodiscard]] std::optional<PatternRuntime> pattern_runtime_for_id(std::string_view id) {
  if (id == fixed_pattern_set_fixture().id) {
    return PatternRuntime{
        .pattern_set = &fixed_pattern_set_fixture(),
        .feature_set = tiny_pattern_feature_set_fixture(),
    };
  }
  if (id == buro_lite_pattern_set().id) {
    return PatternRuntime{
        .pattern_set = &buro_lite_pattern_set(),
        .feature_set = buro_lite_pattern_feature_set(),
    };
  }
  if (id == endgame_lite_pattern_set().id) {
    return PatternRuntime{
        .pattern_set = &endgame_lite_pattern_set(),
        .feature_set = endgame_lite_pattern_feature_set(),
    };
  }
  return std::nullopt;
}

[[nodiscard]] std::array<std::uint8_t, PatternWeights::kDiscCountEntries> phase_by_disc_count_13() {
  std::array<std::uint8_t, PatternWeights::kDiscCountEntries> phases{};
  for (std::uint8_t disc_count = 0; disc_count < phases.size(); ++disc_count) {
    const int normalized_count = disc_count < 4 ? 0 : static_cast<int>(disc_count) - 4;
    const int phase = std::min(12, (normalized_count * 13) / 60);
    phases[disc_count] = static_cast<std::uint8_t>(phase);
  }
  return phases;
}

[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> bytes) noexcept {
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

[[nodiscard]] std::string runtime_checksum_string(std::span<const std::uint8_t> artifact) {
  if (artifact.size() < sizeof(std::uint32_t)) {
    return {};
  }
  std::ostringstream output;
  output << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(8)
         << crc32(artifact.first(artifact.size() - sizeof(std::uint32_t)));
  return output.str();
}

[[nodiscard]] std::optional<std::string> validate_manifest_contract(std::string_view manifest_text,
                                                                    std::string_view path) {
  const std::optional<std::string> format = json_string_field(manifest_text, "format");
  if (format.has_value() && *format != "vibe-othello-pattern-eval") {
    return std::string(path) + ": format must be vibe-othello-pattern-eval";
  }
  const std::optional<int> format_version = json_int_field(manifest_text, "format_version");
  if (!format_version.has_value() || *format_version != kPatternWeightFormatVersion) {
    return std::string(path) + ": format_version must be 1";
  }
  const std::optional<std::string> bit_order = json_string_field(manifest_text, "bit_order");
  if (!bit_order.has_value() || *bit_order != "a1-lsb") {
    return std::string(path) + ": bit_order must be a1-lsb";
  }
  const std::optional<std::string> score_unit = json_string_field(manifest_text, "score_unit");
  if (!score_unit.has_value() || *score_unit != "disc-diff") {
    return std::string(path) + ": score_unit must be disc-diff";
  }
  const std::optional<int> score_scale = json_int_field(manifest_text, "score_scale");
  if (!score_scale.has_value() || *score_scale <= 0 ||
      *score_scale > std::numeric_limits<std::uint16_t>::max()) {
    return std::string(path) + ": score_scale must be in [1, 65535]";
  }
  const std::optional<int> phase_count = json_int_field(manifest_text, "phase_count");
  if (!phase_count.has_value() || *phase_count != kRuntimePhaseCount) {
    return std::string(path) + ": phase_count must be 13";
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<ArtifactManifestFields>
parse_manifest_fields(std::string_view manifest_text, std::string_view manifest_label,
                      std::string artifact_id_fallback, std::string* error) {
  const std::string manifest_label_string{manifest_label};
  error->clear();
  if (const std::optional<std::string> contract_error =
          validate_manifest_contract(manifest_text, manifest_label_string);
      contract_error.has_value()) {
    *error = *contract_error;
    return std::nullopt;
  }

  const std::optional<std::string> artifact_id = json_string_field(manifest_text, "artifact_id");
  const std::string resolved_artifact_id = artifact_id.has_value() && !artifact_id->empty()
                                               ? *artifact_id
                                               : std::move(artifact_id_fallback);
  const std::optional<std::string> pattern_set_id =
      json_string_field(manifest_text, "pattern_set_id");
  if (!pattern_set_id.has_value() || pattern_set_id->empty()) {
    *error = manifest_label_string + ": missing string field pattern_set_id";
    return std::nullopt;
  }
  const std::optional<std::string> weights_file = json_string_field(manifest_text, "weights_file");
  if (!weights_file.has_value() || weights_file->empty()) {
    *error = manifest_label_string + ": missing string field weights_file";
    return std::nullopt;
  }
  std::filesystem::path weights_path{*weights_file};
  if (weights_path.is_absolute() || path_has_parent_reference(weights_path)) {
    *error =
        manifest_label_string + ": weights_file must be relative and stay inside the artifact dir";
    return std::nullopt;
  }
  const std::optional<std::string> weights_checksum =
      json_string_field(manifest_text, "weights_checksum");
  if (!weights_checksum.has_value() || weights_checksum->empty()) {
    *error = manifest_label_string + ": missing string field weights_checksum";
    return std::nullopt;
  }
  const std::optional<int> score_scale = json_int_field(manifest_text, "score_scale");
  if (!score_scale.has_value() || *score_scale <= 0 ||
      *score_scale > std::numeric_limits<std::uint16_t>::max()) {
    *error = manifest_label_string + ": score_scale must be in [1, 65535]";
    return std::nullopt;
  }
  const std::optional<std::vector<std::uint8_t>> trained_phases =
      parse_trained_phases(manifest_text, manifest_label, error);
  if (!error->empty()) {
    return std::nullopt;
  }
  const bool has_fallback_additive_through_phase =
      field_value_offset(manifest_text, "fallback_additive_through_phase").has_value();
  const std::optional<int> fallback_additive_through_phase =
      json_int_field(manifest_text, "fallback_additive_through_phase");
  if (has_fallback_additive_through_phase && !fallback_additive_through_phase.has_value()) {
    *error = manifest_label_string + ": fallback_additive_through_phase must be an integer";
    return std::nullopt;
  }
  if (fallback_additive_through_phase.has_value() &&
      (*fallback_additive_through_phase < 0 || *fallback_additive_through_phase >= 13)) {
    *error = manifest_label_string + ": fallback_additive_through_phase must be in [0, 12]";
    return std::nullopt;
  }
  return ArtifactManifestFields{
      .artifact_id = resolved_artifact_id,
      .pattern_set_id = *pattern_set_id,
      .weights_file = std::move(weights_path),
      .weights_checksum = *weights_checksum,
      .score_scale = static_cast<std::uint16_t>(*score_scale),
      .trained_phases = trained_phases,
      .fallback_additive_through_phase =
          fallback_additive_through_phase.has_value()
              ? std::optional<std::uint8_t>{static_cast<std::uint8_t>(
                    *fallback_additive_through_phase)}
              : std::nullopt,
  };
}

[[nodiscard]] PatternArtifactCoreLoadResult
load_pattern_artifact_core(const ArtifactManifestFields& fields, std::string_view manifest_label,
                           std::string_view weights_label, std::span<const std::uint8_t> artifact) {
  std::optional<PatternRuntime> runtime = pattern_runtime_for_id(fields.pattern_set_id);
  if (!runtime.has_value()) {
    return fail_core("unsupported pattern_set_id in artifact manifest: " + fields.pattern_set_id);
  }
  if (artifact.size() < sizeof(std::uint32_t)) {
    return fail_core("artifact weights are too small: " + std::string(weights_label));
  }

  const std::string checksum = runtime_checksum_string(artifact);
  if (checksum != fields.weights_checksum) {
    return fail_core("artifact checksum mismatch for " + std::string(manifest_label) +
                     ": manifest " + fields.weights_checksum + ", actual " + checksum);
  }

  const PatternManifest manifest{
      .format_version = kPatternWeightFormatVersion,
      .bit_order = PatternBitOrder::a1_lsb,
      .score_unit = PatternScoreUnit::disc_diff,
      .score_scale = fields.score_scale,
      .phase_count = kRuntimePhaseCount,
      .pattern_set_id = runtime->pattern_set->id,
      .patterns = runtime->pattern_set->patterns,
  };
  const PatternWeightsLoadResult loaded = load_pattern_weights(manifest, artifact);
  if (!loaded.ok()) {
    return fail_core("runtime loader rejected artifact: " + std::string(manifest_label));
  }
  std::optional<PatternWeights> weights =
      make_pattern_weights(*loaded.weights, phase_by_disc_count_13());
  if (!weights.has_value()) {
    return fail_core("loaded artifact could not be converted to runtime weights: " +
                     std::string(manifest_label));
  }

  return PatternArtifactCoreLoadResult{
      .artifact =
          LoadedPatternArtifactCore{
              .artifact_id = fields.artifact_id,
              .pattern_set_id = fields.pattern_set_id,
              .weights_checksum = fields.weights_checksum,
              .trained_phases = fields.trained_phases,
              .fallback_additive_through_phase = fields.fallback_additive_through_phase,
              .weights = std::move(*weights),
              .feature_set = std::move(runtime->feature_set),
          },
      .error = {},
  };
}

[[nodiscard]] PatternArtifactLoadResult
load_pattern_artifact_impl(const std::filesystem::path& manifest_path,
                           const std::optional<std::filesystem::path>& weights_override_path) {
  const std::optional<std::string> manifest_text = read_text_file(manifest_path);
  if (!manifest_text.has_value()) {
    return fail("cannot read artifact manifest: " + manifest_path.generic_string());
  }

  std::string error;
  const std::string manifest_label = manifest_path.generic_string();
  std::optional<ArtifactManifestFields> fields = parse_manifest_fields(
      *manifest_text, manifest_label, manifest_path.stem().generic_string(), &error);
  if (!fields.has_value()) {
    return fail(std::move(error));
  }

  const std::filesystem::path weights_path =
      weights_override_path.has_value() ? *weights_override_path
                                        : manifest_path.parent_path() / fields->weights_file;
  const std::optional<std::vector<std::uint8_t>> artifact = read_binary_file(weights_path);
  if (!artifact.has_value()) {
    return fail("cannot read artifact weights: " + weights_path.generic_string());
  }

  PatternArtifactCoreLoadResult loaded =
      load_pattern_artifact_core(*fields, manifest_label, weights_path.generic_string(), *artifact);
  if (!loaded.ok()) {
    return fail(std::move(loaded.error));
  }

  LoadedPatternArtifactCore artifact_core = std::move(*loaded.artifact);
  return PatternArtifactLoadResult{
      .artifact =
          LoadedPatternArtifact{
              .artifact_id = std::move(artifact_core.artifact_id),
              .pattern_set_id = std::move(artifact_core.pattern_set_id),
              .weights_checksum = std::move(artifact_core.weights_checksum),
              .trained_phases = std::move(artifact_core.trained_phases),
              .fallback_additive_through_phase = artifact_core.fallback_additive_through_phase,
              .manifest_path = manifest_path,
              .weights_path = weights_path,
              .weights = std::move(artifact_core.weights),
              .feature_set = std::move(artifact_core.feature_set),
          },
      .error = {},
  };
}

[[nodiscard]] PatternArtifactLoadResult
resolve_default_manifest(const std::filesystem::path& eval_root) {
  const std::filesystem::path pointer_path = eval_root / "default-artifact.json";
  const std::optional<std::string> pointer_text = read_text_file(pointer_path);
  if (!pointer_text.has_value()) {
    return fail("cannot read default artifact pointer: " + pointer_path.generic_string());
  }
  const std::optional<std::string> manifest = json_string_field(*pointer_text, "artifact_manifest");
  if (!manifest.has_value() || manifest->empty()) {
    return fail(pointer_path.generic_string() + ": missing string field artifact_manifest");
  }
  std::filesystem::path manifest_path{*manifest};
  if (manifest_path.is_absolute() || path_has_parent_reference(manifest_path)) {
    return fail(pointer_path.generic_string() +
                ": artifact_manifest must be relative and stay inside data/eval");
  }
  return load_pattern_artifact(eval_root / manifest_path);
}

} // namespace

std::filesystem::path default_eval_root(const std::filesystem::path& source_root) {
  return source_root / "data" / "eval";
}

PatternArtifactLoadResult load_pattern_artifact(const std::filesystem::path& manifest_path) {
  return load_pattern_artifact_impl(manifest_path, std::nullopt);
}

PatternArtifactLoadResult
load_pattern_artifact(const std::filesystem::path& manifest_path,
                      const std::filesystem::path& weights_override_path) {
  return load_pattern_artifact_impl(manifest_path, weights_override_path);
}

PatternArtifactLoadResult load_default_pattern_artifact(const std::filesystem::path& eval_root) {
  return resolve_default_manifest(eval_root);
}

PatternArtifactBytesLoadResult
load_pattern_artifact_from_bytes(std::string_view manifest_text,
                                 std::span<const std::uint8_t> weights_bytes,
                                 std::string_view manifest_label) {
  const std::string manifest_label_string{manifest_label};
  std::string error;
  std::optional<ArtifactManifestFields> fields = parse_manifest_fields(
      manifest_text, manifest_label_string,
      std::filesystem::path{manifest_label_string}.stem().generic_string(), &error);
  if (!fields.has_value()) {
    return fail_bytes(std::move(error));
  }

  const std::string weights_label = manifest_label_string + " weights";
  PatternArtifactCoreLoadResult loaded =
      load_pattern_artifact_core(*fields, manifest_label_string, weights_label, weights_bytes);
  if (!loaded.ok()) {
    return fail_bytes(std::move(loaded.error));
  }

  LoadedPatternArtifactCore artifact_core = std::move(*loaded.artifact);
  return PatternArtifactBytesLoadResult{
      .artifact =
          LoadedPatternArtifactBytes{
              .artifact_id = std::move(artifact_core.artifact_id),
              .pattern_set_id = std::move(artifact_core.pattern_set_id),
              .weights_checksum = std::move(artifact_core.weights_checksum),
              .trained_phases = std::move(artifact_core.trained_phases),
              .fallback_additive_through_phase = artifact_core.fallback_additive_through_phase,
              .weights = std::move(artifact_core.weights),
              .feature_set = std::move(artifact_core.feature_set),
          },
      .error = {},
  };
}

} // namespace vibe_othello::evaluation
