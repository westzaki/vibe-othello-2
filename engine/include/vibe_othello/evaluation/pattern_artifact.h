#ifndef VIBE_OTHELLO_EVALUATION_PATTERN_ARTIFACT_H_
#define VIBE_OTHELLO_EVALUATION_PATTERN_ARTIFACT_H_

#include "vibe_othello/evaluation/pattern_feature_set.h"
#include "vibe_othello/evaluation/pattern_weights.h"

#include <filesystem>
#include <optional>
#include <string>

namespace vibe_othello::evaluation {

struct LoadedPatternArtifact {
  std::string artifact_id;
  std::string pattern_set_id;
  std::string weights_checksum;
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

[[nodiscard]] std::filesystem::path default_eval_root(const std::filesystem::path& source_root);

[[nodiscard]] PatternArtifactLoadResult
load_pattern_artifact(const std::filesystem::path& manifest_path);

[[nodiscard]] PatternArtifactLoadResult
load_pattern_artifact(const std::filesystem::path& manifest_path,
                      const std::filesystem::path& weights_override_path);

[[nodiscard]] PatternArtifactLoadResult
load_default_pattern_artifact(const std::filesystem::path& eval_root);

} // namespace vibe_othello::evaluation

#endif // VIBE_OTHELLO_EVALUATION_PATTERN_ARTIFACT_H_
