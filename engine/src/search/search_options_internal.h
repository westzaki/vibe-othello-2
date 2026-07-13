#pragma once

#include "vibe_othello/search/search.h"

#include <cstdint>

namespace vibe_othello::search::internal {

struct ResolvedSearchOptions {
  SearchMode mode = SearchMode::move;
  MidgameSearchOptions midgame{};
  MoveOrderingOptions ordering{};
  EndgameSearchOptions endgame{};
  SearchReportingOptions reporting{};
  ExperimentalSearchOptions experimental{};
  SelectiveSearchOptionsV1 selective{};
  ProbCutOptionsV1 probcut{};
  std::uint64_t probcut_profile_semantic_fingerprint = 0;

  friend constexpr bool operator==(const ResolvedSearchOptions&,
                                   const ResolvedSearchOptions&) = default;
};

ResolvedSearchOptions normalize_search_options(SearchOptions options) noexcept;

} // namespace vibe_othello::search::internal
