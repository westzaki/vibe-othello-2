#ifndef VIBE_OTHELLO_EVALUATION_PATTERN_ARTIFACT_H_
#define VIBE_OTHELLO_EVALUATION_PATTERN_ARTIFACT_H_

#include "vibe_othello/evaluation/pattern_feature_set.h"
#include "vibe_othello/evaluation/pattern_weights.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vibe_othello::evaluation {

struct LoadedPatternArtifact {
  std::string artifact_id;
  std::string pattern_set_id;
  std::string weights_checksum;
  // Missing metadata means the artifact predates coverage reporting; it does
  // not imply that every runtime phase was trained.
  std::optional<std::vector<std::uint8_t>> trained_phases;
  std::optional<std::uint8_t> fallback_additive_through_phase;
  std::filesystem::path manifest_path;
  std::filesystem::path weights_path;
  PatternWeights weights;
  PatternFeatureSet feature_set;
};

struct PatternArtifactLoadResult {
  std::optional<LoadedPatternArtifact> artifact;
  std::string error;

  [[nodiscard]] bool ok() const noexcept {
    return artifact.has_value();
  }
};

struct LoadedPatternArtifactBytes {
  std::string artifact_id;
  std::string pattern_set_id;
  std::string weights_checksum;
  // Matches LoadedPatternArtifact::trained_phases for in-memory loading.
  std::optional<std::vector<std::uint8_t>> trained_phases;
  std::optional<std::uint8_t> fallback_additive_through_phase;
  PatternWeights weights;
  PatternFeatureSet feature_set;
};

struct PatternArtifactBytesLoadResult {
  std::optional<LoadedPatternArtifactBytes> artifact;
  std::string error;

  [[nodiscard]] bool ok() const noexcept {
    return artifact.has_value();
  }
};

[[nodiscard]] std::filesystem::path default_eval_root(const std::filesystem::path& source_root);

[[nodiscard]] PatternArtifactLoadResult
load_pattern_artifact(const std::filesystem::path& manifest_path);

[[nodiscard]] PatternArtifactLoadResult
load_pattern_artifact(const std::filesystem::path& manifest_path,
                      const std::filesystem::path& weights_override_path);

[[nodiscard]] PatternArtifactLoadResult
load_default_pattern_artifact(const std::filesystem::path& eval_root);

[[nodiscard]] PatternArtifactBytesLoadResult
load_pattern_artifact_from_bytes(std::string_view manifest_text,
                                 std::span<const std::uint8_t> weights_bytes,
                                 std::string_view manifest_label = "<memory>");

} // namespace vibe_othello::evaluation

#endif // VIBE_OTHELLO_EVALUATION_PATTERN_ARTIFACT_H_
