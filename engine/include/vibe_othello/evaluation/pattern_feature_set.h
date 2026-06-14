#ifndef VIBE_OTHELLO_EVALUATION_PATTERN_FEATURE_SET_H_
#define VIBE_OTHELLO_EVALUATION_PATTERN_FEATURE_SET_H_

#include "vibe_othello/board_core/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vibe_othello::evaluation {

struct PatternFeatureTable {
  std::string pattern_id;
  std::uint8_t pattern_length = 0;
  std::vector<std::vector<board_core::Square>> instances;
};

struct PatternFeatureSet {
  std::string id;
  std::vector<PatternFeatureTable> tables;
};

[[nodiscard]] PatternFeatureSet tiny_pattern_feature_set_fixture();

} // namespace vibe_othello::evaluation

#endif // VIBE_OTHELLO_EVALUATION_PATTERN_FEATURE_SET_H_
