#ifndef VIBE_OTHELLO_SEARCH_PRODUCTION_PROBCUT_H_
#define VIBE_OTHELLO_SEARCH_PRODUCTION_PROBCUT_H_

#include "vibe_othello/search/probcut.h"

#include <cstdint>
#include <string_view>

namespace vibe_othello::search {

struct ProbCutRuntimeIdentityV1 {
  std::string_view evaluator_family;
  std::string_view artifact_family;
  std::string_view weights_checksum;
};

// Selects the built-in reviewed profile only for its exact evaluator,
// artifact bytes, search mode, and exact-handoff policy. Every mismatch
// resolves to a disabled configuration.
[[nodiscard]] ResolvedProbCutConfigurationV1
production_probcut_configuration_v1(ProbCutRuntimeIdentityV1 identity, SearchMode search_mode,
                                    std::uint8_t exact_handoff_threshold) noexcept;

} // namespace vibe_othello::search

#endif // VIBE_OTHELLO_SEARCH_PRODUCTION_PROBCUT_H_
