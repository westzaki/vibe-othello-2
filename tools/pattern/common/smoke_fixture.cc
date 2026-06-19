#include "smoke_fixture.h"

#include <array>
#include <bit>
#include <cstdint>

namespace vibe_othello::tools::pattern::smoke {

const evaluation::PatternSet& pattern_set_for_index_mode(IndexMode mode) noexcept {
  switch (mode) {
  case IndexMode::raw:
    return evaluation::fixed_pattern_set_fixture();
  case IndexMode::canonical:
    return evaluation::symmetry_aware_fixed_pattern_set_fixture();
  }
  return evaluation::fixed_pattern_set_fixture();
}

std::array<std::uint8_t, evaluation::PatternWeights::kDiscCountEntries>
tiny_fixture_phase_by_disc_count() {
  std::array<std::uint8_t, evaluation::PatternWeights::kDiscCountEntries> phases{};
  for (std::uint8_t disc_count = 0; disc_count < phases.size(); ++disc_count) {
    phases[disc_count] = disc_count < 20 ? 0 : 1;
  }
  return phases;
}

std::uint8_t tiny_fixture_phase(board_core::Position position) {
  static const evaluation::PatternWeights phase_selector{
      2,
      tiny_fixture_phase_by_disc_count(),
      {0, 0},
      {},
  };
  const int discs = std::popcount(board_core::occupied(position));
  return phase_selector.phase_for_disc_count(discs);
}

} // namespace vibe_othello::tools::pattern::smoke
