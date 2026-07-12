#include "../../src/search/search_session_internal.h"
#include "vibe_othello/search/search.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>

namespace vibe_othello::search {
namespace {

class DiscEvaluator final : public Evaluator {
public:
  Score evaluate(const board_core::Position& position) const noexcept override {
    return static_cast<Score>(std::popcount(position.player)) -
           static_cast<Score>(std::popcount(position.opponent));
  }
};

SearchOptions tt_options() {
  SearchOptions options{};
  options.midgame.use_pvs = true;
  options.midgame.use_midgame_tt = true;
  options.ordering.use_tt_best_move_ordering = true;
  options.reporting.multi_pv = 1;
  return options;
}

TEST_CASE("legacy and session search APIs have score and legal PV parity", "[search][session]") {
  DiscEvaluator evaluator;
  const SearchLimits limits{.max_depth = Depth{5}};
  const SearchOptions options = tt_options();
  const SearchResult legacy =
      search_iterative(board_core::initial_position(), evaluator, limits, options);
  SearchSession session;
  const SearchResult retained =
      search_iterative(session, board_core::initial_position(), evaluator, limits, options);

  REQUIRE(retained.score == legacy.score);
  REQUIRE(retained.best_move == legacy.best_move);
  REQUIRE(retained.pv == legacy.pv);
}

TEST_CASE("search session retains across roots and clears deterministically", "[search][session]") {
  DiscEvaluator evaluator;
  SearchSession session{SearchSessionConfig{
      .profile = SearchPlatformProfile::native,
      .transposition_table = TranspositionTableConfig{
          .capacity = 2 * 1024 * 1024,
          .unit = TranspositionTableCapacityUnit::bytes,
      },
  }};
  const SearchLimits limits{.max_depth = Depth{5}};
  const SearchOptions options = tt_options();
  const SearchResult first =
      search_iterative(session, board_core::initial_position(), evaluator, limits, options);
  const SearchResult retained =
      search_iterative(session, board_core::initial_position(), evaluator, limits, options);
  REQUIRE(retained.score == first.score);
  REQUIRE(retained.stats.tt_generation_age_hits > 0);

  session.start_new_game();
  const SearchResult reset =
      search_iterative(session, board_core::initial_position(), evaluator, limits, options);
  REQUIRE(reset.score == first.score);
  REQUIRE(reset.stats.tt_generation_age_hits == 0);
}

TEST_CASE("disabled session TT is reported and produces no TT telemetry", "[search][session]") {
  DiscEvaluator evaluator;
  SearchSession session{SearchSessionConfig{
      .profile = SearchPlatformProfile::wasm,
      .transposition_table = TranspositionTableConfig{.capacity = 0},
  }};
  REQUIRE_FALSE(session.transposition_table_allocation().enabled);
  const SearchResult result = search_iterative(
      session, board_core::initial_position(), evaluator, SearchLimits{.max_depth = Depth{3}},
      tt_options());
  REQUIRE(result.stats.tt_probes == 0);
  REQUIRE(result.stats.tt_stores == 0);
}

} // namespace
} // namespace vibe_othello::search
