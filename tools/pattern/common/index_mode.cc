#include "index_mode.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace vibe_othello::tools::pattern {

std::optional<IndexMode> parse_index_mode(std::string_view text) noexcept {
  if (text == "raw") {
    return IndexMode::raw;
  }
  if (text == "canonical") {
    return IndexMode::canonical;
  }
  return std::nullopt;
}

std::optional<std::uint32_t> index_for_mode(board_core::Position position,
                                            std::span<const board_core::Square> squares,
                                            evaluation::PatternSymmetryPolicy symmetry_policy,
                                            IndexMode mode) noexcept {
  const std::uint32_t raw_index = evaluation::ternary_pattern_index(position, squares);
  switch (mode) {
  case IndexMode::raw:
    return raw_index;
  case IndexMode::canonical:
    return evaluation::canonical_ternary_pattern_index(
        raw_index, static_cast<std::uint8_t>(squares.size()), symmetry_policy);
  }
  return std::nullopt;
}

} // namespace vibe_othello::tools::pattern
