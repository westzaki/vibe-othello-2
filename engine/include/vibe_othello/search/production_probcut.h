#ifndef VIBE_OTHELLO_SEARCH_PRODUCTION_PROBCUT_H_
#define VIBE_OTHELLO_SEARCH_PRODUCTION_PROBCUT_H_

#include "vibe_othello/search/probcut.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace vibe_othello::search {

struct ProbCutRuntimeIdentityV1 {
  std::string_view evaluator_family;
  std::string_view artifact_family;
  std::string_view weights_checksum;
  std::uint16_t score_scale = 0;
  std::uint16_t trained_phase_mask = 0;
  std::optional<std::uint8_t> fallback_additive_through_phase;
};

// Selects the built-in reviewed profile only for its exact evaluator,
// artifact bytes, phase-routing semantics, search mode, and exact-handoff
// policy. Every mismatch resolves to a disabled configuration.
[[nodiscard]] ResolvedProbCutConfigurationV1
production_probcut_configuration_v1(ProbCutRuntimeIdentityV1 identity, SearchMode search_mode,
                                    std::uint8_t exact_handoff_threshold) noexcept;

} // namespace vibe_othello::search

#endif // VIBE_OTHELLO_SEARCH_PRODUCTION_PROBCUT_H_
