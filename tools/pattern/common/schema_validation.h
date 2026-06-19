#ifndef VIBE_OTHELLO_TOOLS_PATTERN_COMMON_SCHEMA_VALIDATION_H_
#define VIBE_OTHELLO_TOOLS_PATTERN_COMMON_SCHEMA_VALIDATION_H_

#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/pattern_feature_set.h"

#include <string>

namespace vibe_othello::tools::pattern {

struct FeatureSetValidationResult {
  bool valid = false;
  std::string error;
};

[[nodiscard]] FeatureSetValidationResult
validate_feature_set(const evaluation::PatternFeatureSet& feature_set,
                     const evaluation::PatternSet& pattern_set);

} // namespace vibe_othello::tools::pattern

#endif // VIBE_OTHELLO_TOOLS_PATTERN_COMMON_SCHEMA_VALIDATION_H_
