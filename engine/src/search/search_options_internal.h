#pragma once

#include "vibe_othello/search/search.h"

namespace vibe_othello::search::internal {

struct ResolvedSearchOptions {
  SearchMode mode = SearchMode::move;
  MidgameSearchOptions midgame{};
  MoveOrderingOptions ordering{};
  EndgameSearchOptions endgame{};
  SearchReportingOptions reporting{};
  ExperimentalSearchOptions experimental{};
  SelectiveSearchOptionsV1 selective{};

  friend constexpr bool operator==(const ResolvedSearchOptions&,
                                   const ResolvedSearchOptions&) = default;
};

inline ResolvedSearchOptions normalize_search_options(SearchOptions options) noexcept {
  // Compatibility rule for the staged migration: legacy flat fields remain
  // sticky, while typed config can express the same behavior for new callers.
  ResolvedSearchOptions resolved{
      .mode = options.mode,
      .midgame =
          MidgameSearchOptions{
              .use_pvs = options.use_pvs || options.midgame.use_pvs,
              .use_aspiration = options.use_aspiration || options.midgame.use_aspiration,
              .use_iid = options.use_iid || options.midgame.use_iid,
              .use_midgame_tt = options.use_midgame_tt || options.midgame.use_midgame_tt,
              .pass_consumes_depth =
                  options.pass_consumes_depth && options.midgame.pass_consumes_depth,
          },
      .ordering =
          MoveOrderingOptions{
              .use_tt_best_move_ordering =
                  options.use_tt_best_move_ordering || options.ordering.use_tt_best_move_ordering,
              .use_history = options.use_history || options.ordering.use_history,
              .use_killers = options.use_killers || options.ordering.use_killers,
              .use_endgame_parity_ordering = options.use_endgame_parity_ordering &&
                                             options.ordering.use_endgame_parity_ordering,
          },
      .endgame =
          EndgameSearchOptions{
              .exact_endgame = options.exact_endgame || options.endgame.exact_endgame,
              .use_endgame_tt = options.use_endgame_tt || options.endgame.use_endgame_tt,
              .endgame_exact_empties = options.endgame_exact_empties != 0
                                           ? options.endgame_exact_empties
                                           : options.endgame.endgame_exact_empties,
              .endgame_wld_empties = options.endgame_wld_empties != 0
                                         ? options.endgame_wld_empties
                                         : options.endgame.endgame_wld_empties,
          },
      .reporting =
          SearchReportingOptions{
              .multi_pv = options.multi_pv != 0 ? options.multi_pv : options.reporting.multi_pv,
          },
      .experimental =
          ExperimentalSearchOptions{
              .probcut = options.probcut || options.experimental.probcut,
              .use_pv_table = options.use_pv_table || options.experimental.use_pv_table,
              .use_parallel = options.use_parallel || options.experimental.use_parallel,
              .selectivity_level = options.selectivity_level != 0
                                       ? options.selectivity_level
                                       : options.experimental.selectivity_level,
              .use_legacy_search_kernel =
                  options.use_legacy_search_kernel || options.experimental.use_legacy_search_kernel,
          },
      .selective = options.selective,
  };
  return resolved;
}

} // namespace vibe_othello::search::internal
