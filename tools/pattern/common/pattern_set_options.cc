#include "pattern_set_options.h"

namespace vibe_othello::tools::pattern {

std::optional<PatternSetOption> select_pattern_set(std::string_view name, IndexMode index_mode) {
  namespace eval = evaluation;

  if (name == "tiny" || name == "fixed-pattern-fixture-v1") {
    const eval::PatternSet& pattern_set = index_mode == IndexMode::canonical
                                              ? eval::symmetry_aware_fixed_pattern_set_fixture()
                                              : eval::fixed_pattern_set_fixture();
    return PatternSetOption{
        .pattern_set = &pattern_set,
        .feature_set = eval::tiny_pattern_feature_set_fixture(),
    };
  }
  if (name == "buro-lite" || name == "pattern-v1-buro-lite") {
    return PatternSetOption{
        .pattern_set = &eval::buro_lite_pattern_set(),
        .feature_set = eval::buro_lite_pattern_feature_set(),
    };
  }
  return std::nullopt;
}

std::string_view pattern_set_option_names() noexcept {
  return "tiny, fixed-pattern-fixture-v1, buro-lite, or pattern-v1-buro-lite";
}

} // namespace vibe_othello::tools::pattern
