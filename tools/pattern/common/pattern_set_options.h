#ifndef VIBE_OTHELLO_TOOLS_PATTERN_COMMON_PATTERN_SET_OPTIONS_H_
#define VIBE_OTHELLO_TOOLS_PATTERN_COMMON_PATTERN_SET_OPTIONS_H_

#include "index_mode.h"
#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/pattern_feature_set.h"

#include <optional>
#include <string_view>

namespace vibe_othello::tools::pattern {

struct PatternSetOption {
  const evaluation::PatternSet* pattern_set = nullptr;
  evaluation::PatternFeatureSet feature_set;
};

[[nodiscard]] std::optional<PatternSetOption> select_pattern_set(std::string_view name,
                                                                 IndexMode index_mode);

[[nodiscard]] std::string_view pattern_set_option_names() noexcept;

} // namespace vibe_othello::tools::pattern

#endif // VIBE_OTHELLO_TOOLS_PATTERN_COMMON_PATTERN_SET_OPTIONS_H_
