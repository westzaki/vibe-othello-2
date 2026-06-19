#ifndef VIBE_OTHELLO_TOOLS_PATTERN_COMMON_INDEX_MODE_H_
#define VIBE_OTHELLO_TOOLS_PATTERN_COMMON_INDEX_MODE_H_

#include "vibe_othello/board_core/position.h"
#include "vibe_othello/evaluation/pattern.h"

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

[[nodiscard]] std::optional<std::uint32_t>
index_for_mode(board_core::Position position, std::span<const board_core::Square> squares,
               evaluation::PatternSymmetryPolicy symmetry_policy, IndexMode mode) noexcept;

} // namespace vibe_othello::tools::pattern

#endif // VIBE_OTHELLO_TOOLS_PATTERN_COMMON_INDEX_MODE_H_
