#include "../../src/search/search_options_internal.h"

#include <catch2/catch_test_macros.hpp>

namespace vibe_othello::search {
namespace {

void require_default_resolved_options(const internal::ResolvedSearchOptions& options) {
  REQUIRE(options.mode == SearchMode::move);

  REQUIRE_FALSE(options.midgame.use_pvs);
  REQUIRE_FALSE(options.midgame.use_aspiration);
  REQUIRE_FALSE(options.midgame.use_iid);
  REQUIRE_FALSE(options.midgame.use_midgame_tt);

  REQUIRE_FALSE(options.ordering.use_tt_best_move_ordering);
  REQUIRE_FALSE(options.ordering.use_history);
  REQUIRE_FALSE(options.ordering.use_killers);
  REQUIRE(options.ordering.use_endgame_parity_ordering);

  REQUIRE_FALSE(options.endgame.exact_endgame);
  REQUIRE_FALSE(options.endgame.use_endgame_tt);
  REQUIRE(options.endgame.endgame_exact_empties == 0);
  REQUIRE(options.endgame.endgame_wld_empties == 0);

  REQUIRE(options.reporting.multi_pv == 0);

  REQUIRE_FALSE(options.experimental.probcut);
  REQUIRE_FALSE(options.experimental.use_pv_table);
  REQUIRE_FALSE(options.experimental.use_parallel);
  REQUIRE(options.experimental.selectivity_level == 0);
}

} // namespace

TEST_CASE("default search options normalize to default resolved options", "[search][options]") {
  require_default_resolved_options(internal::normalize_search_options(SearchOptions{}));
}

TEST_CASE("legacy flat search options normalize into typed groups", "[search][options]") {
  const internal::ResolvedSearchOptions resolved = internal::normalize_search_options(SearchOptions{
      .use_pvs = true,
      .use_aspiration = true,
      .use_iid = true,
      .use_history = true,
      .use_killers = true,
      .use_midgame_tt = true,
      .use_endgame_tt = true,
      .exact_endgame = true,
      .probcut = true,
      .use_pv_table = true,
      .use_parallel = true,
      .use_tt_best_move_ordering = true,
      .use_endgame_parity_ordering = false,
      .multi_pv = 2,
      .endgame_exact_empties = 10,
      .endgame_wld_empties = 12,
      .selectivity_level = 3,
      .mode = SearchMode::win_loss_draw,
  });

  REQUIRE(resolved.mode == SearchMode::win_loss_draw);

  REQUIRE(resolved.midgame.use_pvs);
  REQUIRE(resolved.midgame.use_aspiration);
  REQUIRE(resolved.midgame.use_iid);
  REQUIRE(resolved.midgame.use_midgame_tt);

  REQUIRE(resolved.ordering.use_tt_best_move_ordering);
  REQUIRE(resolved.ordering.use_history);
  REQUIRE(resolved.ordering.use_killers);
  REQUIRE_FALSE(resolved.ordering.use_endgame_parity_ordering);

  REQUIRE(resolved.endgame.exact_endgame);
  REQUIRE(resolved.endgame.use_endgame_tt);
  REQUIRE(resolved.endgame.endgame_exact_empties == 10);
  REQUIRE(resolved.endgame.endgame_wld_empties == 12);

  REQUIRE(resolved.reporting.multi_pv == 2);

  REQUIRE(resolved.experimental.probcut);
  REQUIRE(resolved.experimental.use_pv_table);
  REQUIRE(resolved.experimental.use_parallel);
  REQUIRE(resolved.experimental.selectivity_level == 3);
}

TEST_CASE("typed search sub-configs normalize without legacy flat fields", "[search][options]") {
  const internal::ResolvedSearchOptions resolved = internal::normalize_search_options(SearchOptions{
      .midgame =
          MidgameSearchOptions{
              .use_pvs = true,
              .use_aspiration = true,
              .use_iid = true,
              .use_midgame_tt = true,
          },
      .ordering =
          MoveOrderingOptions{
              .use_tt_best_move_ordering = true,
              .use_history = true,
              .use_killers = true,
              .use_endgame_parity_ordering = false,
          },
      .endgame =
          EndgameSearchOptions{
              .exact_endgame = true,
              .use_endgame_tt = true,
              .endgame_exact_empties = 10,
              .endgame_wld_empties = 12,
          },
      .reporting = SearchReportingOptions{.multi_pv = 2},
      .experimental =
          ExperimentalSearchOptions{
              .probcut = true,
              .use_pv_table = true,
              .use_parallel = true,
              .selectivity_level = 3,
          },
      .mode = SearchMode::exact_score,
  });

  REQUIRE(resolved.mode == SearchMode::exact_score);

  REQUIRE(resolved.midgame.use_pvs);
  REQUIRE(resolved.midgame.use_aspiration);
  REQUIRE(resolved.midgame.use_iid);
  REQUIRE(resolved.midgame.use_midgame_tt);

  REQUIRE(resolved.ordering.use_tt_best_move_ordering);
  REQUIRE(resolved.ordering.use_history);
  REQUIRE(resolved.ordering.use_killers);
  REQUIRE_FALSE(resolved.ordering.use_endgame_parity_ordering);

  REQUIRE(resolved.endgame.exact_endgame);
  REQUIRE(resolved.endgame.use_endgame_tt);
  REQUIRE(resolved.endgame.endgame_exact_empties == 10);
  REQUIRE(resolved.endgame.endgame_wld_empties == 12);

  REQUIRE(resolved.reporting.multi_pv == 2);

  REQUIRE(resolved.experimental.probcut);
  REQUIRE(resolved.experimental.use_pv_table);
  REQUIRE(resolved.experimental.use_parallel);
  REQUIRE(resolved.experimental.selectivity_level == 3);
}

TEST_CASE("conflicting legacy and typed search options use compatibility rules",
          "[search][options]") {
  const internal::ResolvedSearchOptions resolved = internal::normalize_search_options(SearchOptions{
      .use_pvs = true,
      .use_aspiration = true,
      .use_iid = true,
      .use_history = true,
      .use_killers = true,
      .use_midgame_tt = true,
      .use_endgame_tt = true,
      .exact_endgame = true,
      .probcut = true,
      .use_pv_table = true,
      .use_parallel = true,
      .use_tt_best_move_ordering = true,
      .use_endgame_parity_ordering = true,
      .multi_pv = 1,
      .endgame_exact_empties = 8,
      .endgame_wld_empties = 9,
      .selectivity_level = 2,
      .midgame =
          MidgameSearchOptions{
              .use_pvs = false,
              .use_aspiration = false,
              .use_iid = false,
              .use_midgame_tt = false,
          },
      .ordering =
          MoveOrderingOptions{
              .use_tt_best_move_ordering = false,
              .use_history = false,
              .use_killers = false,
              .use_endgame_parity_ordering = false,
          },
      .endgame =
          EndgameSearchOptions{
              .exact_endgame = false,
              .use_endgame_tt = false,
              .endgame_exact_empties = 10,
              .endgame_wld_empties = 12,
          },
      .reporting = SearchReportingOptions{.multi_pv = 3},
      .experimental =
          ExperimentalSearchOptions{
              .probcut = false,
              .use_pv_table = false,
              .use_parallel = false,
              .selectivity_level = 4,
          },
  });

  REQUIRE(resolved.midgame.use_pvs);
  REQUIRE(resolved.midgame.use_aspiration);
  REQUIRE(resolved.midgame.use_iid);
  REQUIRE(resolved.midgame.use_midgame_tt);

  REQUIRE(resolved.ordering.use_tt_best_move_ordering);
  REQUIRE(resolved.ordering.use_history);
  REQUIRE(resolved.ordering.use_killers);
  REQUIRE_FALSE(resolved.ordering.use_endgame_parity_ordering);

  REQUIRE(resolved.endgame.exact_endgame);
  REQUIRE(resolved.endgame.use_endgame_tt);
  REQUIRE(resolved.endgame.endgame_exact_empties == 8);
  REQUIRE(resolved.endgame.endgame_wld_empties == 9);

  REQUIRE(resolved.reporting.multi_pv == 1);

  REQUIRE(resolved.experimental.probcut);
  REQUIRE(resolved.experimental.use_pv_table);
  REQUIRE(resolved.experimental.use_parallel);
  REQUIRE(resolved.experimental.selectivity_level == 2);
}

TEST_CASE("zero-valued legacy numeric search options defer to typed values", "[search][options]") {
  const internal::ResolvedSearchOptions resolved = internal::normalize_search_options(SearchOptions{
      .multi_pv = 0,
      .endgame_exact_empties = 0,
      .endgame_wld_empties = 0,
      .selectivity_level = 0,
      .endgame =
          EndgameSearchOptions{
              .endgame_exact_empties = 10,
              .endgame_wld_empties = 12,
          },
      .reporting = SearchReportingOptions{.multi_pv = 3},
      .experimental = ExperimentalSearchOptions{.selectivity_level = 4},
  });

  REQUIRE(resolved.endgame.endgame_exact_empties == 10);
  REQUIRE(resolved.endgame.endgame_wld_empties == 12);
  REQUIRE(resolved.reporting.multi_pv == 3);
  REQUIRE(resolved.experimental.selectivity_level == 4);
}

TEST_CASE("typed true bool search options enable features when legacy fields are false",
          "[search][options]") {
  const internal::ResolvedSearchOptions resolved = internal::normalize_search_options(SearchOptions{
      .midgame =
          MidgameSearchOptions{
              .use_pvs = true,
              .use_aspiration = true,
              .use_iid = true,
              .use_midgame_tt = true,
          },
      .ordering =
          MoveOrderingOptions{
              .use_tt_best_move_ordering = true,
              .use_history = true,
              .use_killers = true,
              .use_endgame_parity_ordering = true,
          },
      .endgame =
          EndgameSearchOptions{
              .exact_endgame = true,
              .use_endgame_tt = true,
          },
      .experimental =
          ExperimentalSearchOptions{
              .probcut = true,
              .use_pv_table = true,
              .use_parallel = true,
          },
  });

  REQUIRE(resolved.midgame.use_pvs);
  REQUIRE(resolved.midgame.use_aspiration);
  REQUIRE(resolved.midgame.use_iid);
  REQUIRE(resolved.midgame.use_midgame_tt);

  REQUIRE(resolved.ordering.use_tt_best_move_ordering);
  REQUIRE(resolved.ordering.use_history);
  REQUIRE(resolved.ordering.use_killers);
  REQUIRE(resolved.ordering.use_endgame_parity_ordering);

  REQUIRE(resolved.endgame.exact_endgame);
  REQUIRE(resolved.endgame.use_endgame_tt);

  REQUIRE(resolved.experimental.probcut);
  REQUIRE(resolved.experimental.use_pv_table);
  REQUIRE(resolved.experimental.use_parallel);
}

} // namespace vibe_othello::search
