#include "../../src/search/search_session_internal.h"
#include "vibe_othello/search/search.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace vibe_othello::search {
namespace {

class DiscEvaluator final : public Evaluator {
public:
  Score evaluate(const board_core::Position& position) const noexcept override {
    return static_cast<Score>(std::popcount(position.player)) -
           static_cast<Score>(std::popcount(position.opponent));
  }
};

class MutableConstantEvaluator final : public Evaluator {
public:
  explicit MutableConstantEvaluator(Score score) noexcept : score_(score) {}

  Score evaluate(const board_core::Position&) const noexcept override {
    return score_;
  }

  std::uint64_t transposition_table_revision() const noexcept override {
    return revision_;
  }

  void set_score(Score score) noexcept {
    score_ = score;
    ++revision_;
  }

private:
  Score score_ = 0;
  std::uint64_t revision_ = 0;
};

SearchOptions tt_options() {
  SearchOptions options{};
  options.midgame.use_pvs = true;
  options.midgame.use_midgame_tt = true;
  options.ordering.use_tt_best_move_ordering = true;
  options.reporting.multi_pv = 1;
  return options;
}

TEST_CASE("non-session and session search APIs have score and legal PV parity",
          "[search][session]") {
  DiscEvaluator evaluator;
  const SearchLimits limits{.max_depth = Depth{5}};
  const SearchOptions options = tt_options();
  const SearchResult temporary_session =
      search_iterative(board_core::initial_position(), evaluator, limits, options);
  SearchSession session;
  const SearchResult retained =
      search_iterative(session, board_core::initial_position(), evaluator, limits, options);

  REQUIRE(retained.score == temporary_session.score);
  REQUIRE(retained.best_move == temporary_session.best_move);
  REQUIRE(retained.pv == temporary_session.pv);
}

TEST_CASE("search session retains across roots and clears deterministically", "[search][session]") {
  DiscEvaluator evaluator;
  SearchSession session{SearchSessionConfig{
      .profile = SearchPlatformProfile::native,
      .transposition_table =
          TranspositionTableConfig{
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
  const SearchResult result = search_iterative(session, board_core::initial_position(), evaluator,
                                               SearchLimits{.max_depth = Depth{3}}, tt_options());
  REQUIRE(result.stats.tt_probes == 0);
  REQUIRE(result.stats.tt_stores == 0);
}

TEST_CASE("session fingerprint tracks TT-relevant typed search semantics",
          "[search][session][tt]") {
  DiscEvaluator evaluator_a;
  DiscEvaluator evaluator_b;
  const internal::ResolvedSearchOptions resolved = internal::normalize_search_options(tt_options());
  const internal::SearchSemanticFingerprint baseline = internal::make_search_semantic_fingerprint(
      &evaluator_a, resolved, internal::SearchSemanticDomain::midgame);

  REQUIRE(internal::make_search_semantic_fingerprint(
              &evaluator_b, resolved, internal::SearchSemanticDomain::midgame) != baseline);
  REQUIRE(internal::make_search_semantic_fingerprint(
              &evaluator_a, resolved, internal::SearchSemanticDomain::exact_endgame) != baseline);

  internal::ResolvedSearchOptions changed = resolved;
  changed.midgame.pass_consumes_depth = !changed.midgame.pass_consumes_depth;
  REQUIRE(internal::make_search_semantic_fingerprint(
              &evaluator_a, changed, internal::SearchSemanticDomain::midgame) != baseline);

  changed = resolved;
  changed.ordering.use_history = !changed.ordering.use_history;
  REQUIRE(internal::make_search_semantic_fingerprint(
              &evaluator_a, changed, internal::SearchSemanticDomain::midgame) != baseline);

  changed = resolved;
  changed.endgame.endgame_exact_empties = 1;
  REQUIRE(internal::make_search_semantic_fingerprint(
              &evaluator_a, changed, internal::SearchSemanticDomain::midgame) != baseline);

  changed = resolved;
  changed.reporting.multi_pv = 2;
  REQUIRE(internal::make_search_semantic_fingerprint(
              &evaluator_a, changed, internal::SearchSemanticDomain::midgame) != baseline);

  changed = resolved;
  changed.mode = SearchMode::win_loss_draw;
  REQUIRE(internal::make_search_semantic_fingerprint(
              &evaluator_a, changed, internal::SearchSemanticDomain::midgame) != baseline);

  changed = resolved;
  changed.probcut.maximum_probes_per_node = 2;
  REQUIRE(internal::make_search_semantic_fingerprint(
              &evaluator_a, changed, internal::SearchSemanticDomain::midgame) != baseline);

  changed = resolved;
  changed.probcut_profile_semantic_fingerprint = 1;
  REQUIRE(internal::make_search_semantic_fingerprint(
              &evaluator_a, changed, internal::SearchSemanticDomain::midgame) != baseline);

  changed = resolved;
  changed.selective.enable_shadow_calibration = true;
  REQUIRE(internal::make_search_semantic_fingerprint(
              &evaluator_a, changed, internal::SearchSemanticDomain::midgame) == baseline);
}

TEST_CASE("session clears TT when evaluator identity or revision changes",
          "[search][session][tt]") {
  MutableConstantEvaluator evaluator_a{7};
  MutableConstantEvaluator evaluator_b{19};
  SearchSession session;
  const SearchLimits limits{.max_depth = Depth{4}};
  const SearchOptions options = tt_options();

  (void)search_iterative(session, board_core::initial_position(), evaluator_a, limits, options);
  const SearchResult reused_b =
      search_iterative(session, board_core::initial_position(), evaluator_b, limits, options);
  SearchSession fresh_b_session;
  const SearchResult fresh_b = search_iterative(fresh_b_session, board_core::initial_position(),
                                                evaluator_b, limits, options);
  REQUIRE(reused_b.score == fresh_b.score);
  REQUIRE(reused_b.best_move == fresh_b.best_move);
  REQUIRE(reused_b.pv == fresh_b.pv);
  REQUIRE(reused_b.stats.tt_generation_age_hits == 0);

  evaluator_b.set_score(-23);
  const SearchResult revised_b =
      search_iterative(session, board_core::initial_position(), evaluator_b, limits, options);
  SearchSession fresh_revised_session;
  const SearchResult fresh_revised = search_iterative(
      fresh_revised_session, board_core::initial_position(), evaluator_b, limits, options);
  REQUIRE(revised_b.score == fresh_revised.score);
  REQUIRE(revised_b.best_move == fresh_revised.best_move);
  REQUIRE(revised_b.pv == fresh_revised.pv);
  REQUIRE(revised_b.stats.tt_generation_age_hits == 0);
}

TEST_CASE("session clears TT when pass policy or exact threshold changes",
          "[search][session][tt]") {
  constexpr board_core::Position pass_position{
      .player = board_core::bit(board_core::square_from_file_rank(1, 0)),
      .opponent = board_core::bit(board_core::square_from_file_rank(0, 0)),
      .side_to_move = board_core::Color::black,
  };
  DiscEvaluator evaluator;
  const SearchLimits limits{.max_depth = Depth{4}};
  SearchOptions consuming = tt_options();
  SearchOptions retaining_depth = consuming;
  retaining_depth.midgame.pass_consumes_depth = false;
  SearchSession session;

  (void)search_iterative(session, pass_position, evaluator, limits, consuming);
  const SearchResult changed_pass =
      search_iterative(session, pass_position, evaluator, limits, retaining_depth);
  SearchSession fresh_pass_session;
  const SearchResult fresh_pass =
      search_iterative(fresh_pass_session, pass_position, evaluator, limits, retaining_depth);
  REQUIRE(changed_pass.score == fresh_pass.score);
  REQUIRE(changed_pass.best_move == fresh_pass.best_move);
  REQUIRE(changed_pass.pv == fresh_pass.pv);
  REQUIRE(changed_pass.stats.tt_generation_age_hits == 0);

  SearchOptions threshold_one = tt_options();
  threshold_one.endgame.exact_endgame = true;
  threshold_one.endgame.use_endgame_tt = true;
  threshold_one.endgame.endgame_exact_empties = 1;
  SearchOptions threshold_two = threshold_one;
  threshold_two.endgame.endgame_exact_empties = 2;
  (void)search_iterative(session, board_core::initial_position(), evaluator, limits, threshold_one);
  const SearchResult changed_threshold =
      search_iterative(session, board_core::initial_position(), evaluator, limits, threshold_two);
  SearchSession fresh_threshold_session;
  const SearchResult fresh_threshold = search_iterative(
      fresh_threshold_session, board_core::initial_position(), evaluator, limits, threshold_two);
  REQUIRE(changed_threshold.score == fresh_threshold.score);
  REQUIRE(changed_threshold.best_move == fresh_threshold.best_move);
  REQUIRE(changed_threshold.pv == fresh_threshold.pv);
  REQUIRE(changed_threshold.stats.tt_generation_age_hits == 0);
}

} // namespace
} // namespace vibe_othello::search
