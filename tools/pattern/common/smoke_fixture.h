#ifndef VIBE_OTHELLO_TOOLS_PATTERN_COMMON_SMOKE_FIXTURE_H_
#define VIBE_OTHELLO_TOOLS_PATTERN_COMMON_SMOKE_FIXTURE_H_

#include "index_mode.h"
#include "vibe_othello/board_core/position.h"
#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/pattern_weights.h"

#include <array>
#include <cstdint>

namespace vibe_othello::tools::pattern::smoke {

[[nodiscard]] const evaluation::PatternSet& pattern_set_for_index_mode(IndexMode mode) noexcept;

[[nodiscard]] std::array<std::uint8_t, evaluation::PatternWeights::kDiscCountEntries>
tiny_fixture_phase_by_disc_count();

[[nodiscard]] std::uint8_t tiny_fixture_phase(board_core::Position position);

} // namespace vibe_othello::tools::pattern::smoke

#endif // VIBE_OTHELLO_TOOLS_PATTERN_COMMON_SMOKE_FIXTURE_H_
