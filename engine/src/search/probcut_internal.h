#pragma once

#include "search_node_internal.h"

#include <optional>

namespace vibe_othello::search::internal {

struct SearchContext;

struct ProbCutShadowCandidate {
  Score beta = 0;
};

// A result is present only for an actual beta cutoff or a stopped shallow
// search. Shadow candidates are returned separately and continue normally.
std::optional<SearchNodeResult>
maybe_probcut(SearchContext* context, Score alpha, Score beta, Depth depth, Ply ply, bool cut_node,
              std::optional<ProbCutShadowCandidate>* shadow_candidate);

void complete_probcut_shadow(SearchContext* context, const ProbCutShadowCandidate& candidate,
                             const SearchNodeResult& deep_result) noexcept;

} // namespace vibe_othello::search::internal
