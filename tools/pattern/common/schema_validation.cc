#include "schema_validation.h"

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace vibe_othello::tools::pattern {

namespace {

[[nodiscard]] FeatureSetValidationResult ok() {
  return {.valid = true, .error = ""};
}

[[nodiscard]] FeatureSetValidationResult error(std::string message) {
  return {.valid = false, .error = std::move(message)};
}

} // namespace

FeatureSetValidationResult validate_feature_set(const evaluation::PatternFeatureSet& feature_set,
                                                const evaluation::PatternSet& pattern_set) {
  namespace board = board_core;
  namespace eval = evaluation;

  if (feature_set.tables.size() != pattern_set.patterns.size()) {
    return error("pattern feature table count does not match runtime schema");
  }

  for (std::size_t table_index = 0; table_index < feature_set.tables.size(); ++table_index) {
    const eval::PatternFeatureTable& table = feature_set.tables[table_index];
    const eval::PatternDefinition& definition = pattern_set.patterns[table_index];
    if (table.pattern_id != definition.id || table.pattern_length != definition.length) {
      return error("pattern feature table does not match runtime schema: " + table.pattern_id);
    }

    if (table.instances.empty()) {
      return error("pattern feature table has no instances: " + table.pattern_id);
    }

    for (const std::vector<board::Square>& instance : table.instances) {
      if (instance.size() != table.pattern_length) {
        return error("pattern feature instance length does not match schema: " + table.pattern_id);
      }

      std::array<bool, board::kSquareCount> seen{};
      for (const board::Square square : instance) {
        if (!board::is_valid(square)) {
          return error("pattern feature instance uses an invalid square: " + table.pattern_id);
        }
        if (seen[square.index]) {
          return error("pattern feature instance uses a duplicate square: " + table.pattern_id);
        }
        seen[square.index] = true;
      }
    }
  }

  return ok();
}

} // namespace vibe_othello::tools::pattern
