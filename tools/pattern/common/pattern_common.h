#ifndef VIBE_OTHELLO_TOOLS_PATTERN_COMMON_PATTERN_COMMON_H_
#define VIBE_OTHELLO_TOOLS_PATTERN_COMMON_PATTERN_COMMON_H_

#include "vibe_othello/board_core/position.h"
#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/pattern_feature_set.h"
#include "vibe_othello/evaluation/pattern_weights.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace vibe_othello::tools::pattern {

enum class IndexMode {
  raw,
  canonical,
};

[[nodiscard]] std::optional<IndexMode> parse_index_mode(std::string_view text) noexcept;

[[nodiscard]] const evaluation::PatternSet& pattern_set_for_index_mode(IndexMode mode) noexcept;

[[nodiscard]] std::array<std::uint8_t, evaluation::PatternWeights::kDiscCountEntries>
tiny_fixture_phase_by_disc_count();

[[nodiscard]] std::uint8_t tiny_fixture_phase(board_core::Position position);

[[nodiscard]] bool validate_feature_set(const evaluation::PatternFeatureSet& feature_set,
                                        const evaluation::PatternSet& pattern_set);

[[nodiscard]] std::optional<std::uint32_t>
index_for_mode(board_core::Position position, std::span<const board_core::Square> squares,
               evaluation::PatternSymmetryPolicy symmetry_policy, IndexMode mode) noexcept;

} // namespace vibe_othello::tools::pattern

#endif // VIBE_OTHELLO_TOOLS_PATTERN_COMMON_PATTERN_COMMON_H_
