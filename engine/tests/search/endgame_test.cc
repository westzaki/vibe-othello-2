#include "search/endgame_positions.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/serialization.h"
#include "vibe_othello/search/search.h"

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifndef VIBE_OTHELLO_SOURCE_DIR
#error "VIBE_OTHELLO_SOURCE_DIR must be defined for endgame tests"
#endif

namespace vibe_othello::search {
namespace {

constexpr std::uint8_t kFastCorpusMaxEmpties = 12;

class CountingEvaluator final : public Evaluator {
public:
  explicit constexpr CountingEvaluator(Score score) noexcept : score_(score) {}

  Score evaluate(const board_core::Position&) const noexcept override {
    ++calls;
    return score_;
  }

  mutable int calls = 0;

private:
  Score score_;
};

constexpr board_core::Square square(int file, int rank) noexcept {
  return board_core::square_from_file_rank(file, rank);
}

board_core::Position parse_position_or_fail(std::string_view text) {
  const std::optional<board_core::Position> position = board_core::parse_position(text);
  REQUIRE(position.has_value());
  return *position;
}

SearchOptions exact_options(std::uint8_t threshold, bool use_endgame_tt = false,
                            bool use_endgame_parity_ordering = true,
                            std::uint8_t multi_pv = 0) noexcept {
  return SearchOptions{
      .use_endgame_tt = use_endgame_tt,
      .exact_endgame = true,
      .use_endgame_parity_ordering = use_endgame_parity_ordering,
      .multi_pv = multi_pv,
      .endgame_exact_empties = threshold,
  };
}

SearchOptions wld_options(std::uint8_t threshold, bool use_endgame_tt = false,
                          bool use_endgame_parity_ordering = true, std::uint8_t multi_pv = 0,
                          std::uint8_t exact_threshold = 0, bool exact_endgame = false) noexcept {
  return SearchOptions{
      .use_endgame_tt = use_endgame_tt,
      .exact_endgame = exact_endgame,
      .use_endgame_parity_ordering = use_endgame_parity_ordering,
      .multi_pv = multi_pv,
      .endgame_exact_empties = exact_threshold,
      .endgame_wld_empties = threshold,
      .mode = SearchMode::win_loss_draw,
  };
}

SearchResult search_exact(board_core::Position position, std::uint8_t threshold,
                          bool use_endgame_tt = false, bool use_endgame_parity_ordering = true,
                          std::uint8_t multi_pv = 0) {
  CountingEvaluator evaluator{123};
  SearchResult result = search_iterative(
      position, evaluator, SearchLimits{.max_depth = Depth{0}},
      exact_options(threshold, use_endgame_tt, use_endgame_parity_ordering, multi_pv));
  REQUIRE(evaluator.calls == 0);
  return result;
}

SearchResult search_wld(board_core::Position position, std::uint8_t threshold,
                        bool use_endgame_tt = false, bool use_endgame_parity_ordering = true,
                        std::uint8_t multi_pv = 0) {
  CountingEvaluator evaluator{123};
  SearchResult result = search_iterative(
      position, evaluator, SearchLimits{.max_depth = Depth{0}},
      wld_options(threshold, use_endgame_tt, use_endgame_parity_ordering, multi_pv));
  REQUIRE(evaluator.calls == 0);
  return result;
}

SearchResult search_exact_with_limits(board_core::Position position, std::uint8_t threshold,
                                      SearchLimits limits, std::uint8_t multi_pv = 0) {
  CountingEvaluator evaluator{123};
  SearchResult result = search_iterative(position, evaluator, limits,
                                         exact_options(threshold, false, true, multi_pv));
  REQUIRE(evaluator.calls == 0);
  return result;
}

SearchResult search_wld_with_limits(board_core::Position position, std::uint8_t threshold,
                                    SearchLimits limits, std::uint8_t multi_pv = 0) {
  CountingEvaluator evaluator{123};
  SearchResult result =
      search_iterative(position, evaluator, limits, wld_options(threshold, false, true, multi_pv));
  REQUIRE(evaluator.calls == 0);
  return result;
}

SearchResult solve_direct_exact(board_core::Position position, SearchLimits limits = {},
                                SearchOptions options = {}) {
  return solve_exact_endgame(position, limits, options);
}

SearchResult solve_direct_exact(board_core::Position position, bool use_endgame_tt,
                                bool use_endgame_parity_ordering, std::uint8_t multi_pv = 0) {
  return solve_exact_endgame(
      position, SearchLimits{},
      SearchOptions{.use_endgame_tt = use_endgame_tt,
                    .exact_endgame = false,
                    .use_endgame_parity_ordering = use_endgame_parity_ordering,
                    .multi_pv = multi_pv,
                    .endgame_exact_empties = 0});
}

SearchResult solve_direct_wld(board_core::Position position, SearchLimits limits = {},
                              SearchOptions options = {}) {
  return solve_wld_endgame(position, limits, options);
}

SearchResult solve_direct_wld(board_core::Position position, bool use_endgame_tt,
                              bool use_endgame_parity_ordering, std::uint8_t multi_pv = 0) {
  return solve_wld_endgame(position, SearchLimits{},
                           SearchOptions{.use_endgame_tt = use_endgame_tt,
                                         .exact_endgame = false,
                                         .use_endgame_parity_ordering = use_endgame_parity_ordering,
                                         .multi_pv = multi_pv,
                                         .endgame_exact_empties = 0});
}

std::string endgame_corpus_path() {
  return std::string{VIBE_OTHELLO_SOURCE_DIR} + "/engine/fixtures/endgame/positions.tsv";
}

board_core::Position corpus_position(std::string_view id) {
  const std::vector<test_support::EndgamePositionCase> cases =
      test_support::load_endgame_position_corpus(endgame_corpus_path());
  const std::optional<test_support::EndgamePositionCase> position_case =
      test_support::find_endgame_position_case(cases, id);
  REQUIRE(position_case.has_value());
  REQUIRE(test_support::endgame_empty_count(position_case->position) ==
          position_case->expected_empties);
  return position_case->position;
}

void require_replayable_pv(board_core::Position position, Line pv) {
  for (std::uint8_t index = 0; index < pv.size; ++index) {
    board_core::MoveDelta delta{};
    REQUIRE(board_core::apply_move(&position, pv.moves[index], &delta));
  }
}

void require_replayable_root_pvs(board_core::Position position, const SearchResult& result) {
  for (const RootMoveInfo& root_move : result.root_moves) {
    REQUIRE(root_move.pv.size > 0);
    REQUIRE(root_move.pv.moves[0] == root_move.move);
    require_replayable_pv(position, root_move.pv);
  }
}

void require_exact_result_invariants(board_core::Position position, const SearchResult& result) {
  REQUIRE_FALSE(result.stopped);
  REQUIRE(result.exact);
  REQUIRE(result.bound == BoundType::exact);
  REQUIRE(result.nodes == result.stats.nodes);
  REQUIRE(result.stats.endgame_nodes == result.stats.nodes);
  REQUIRE(result.stats.eval_calls == 0);
  REQUIRE(result.stats.leaf_nodes == 0);
  require_replayable_pv(position, result.pv);
  require_replayable_root_pvs(position, result);
  for (const RootMoveInfo& root_move : result.root_moves) {
    REQUIRE(root_move.bound == BoundType::exact);
    REQUIRE(root_move.exact);
    REQUIRE_FALSE(root_move.selective);
  }
}

Score wld_from_exact_score(Score score) noexcept {
  if (score > 0) {
    return static_cast<Score>(WldResult::win);
  }
  if (score < 0) {
    return static_cast<Score>(WldResult::loss);
  }
  return static_cast<Score>(WldResult::draw);
}

void require_wld_score(Score score) {
  REQUIRE((score == static_cast<Score>(WldResult::loss) ||
           score == static_cast<Score>(WldResult::draw) ||
           score == static_cast<Score>(WldResult::win)));
}

void require_wld_result_invariants(board_core::Position position, const SearchResult& result) {
  REQUIRE_FALSE(result.stopped);
  REQUIRE(result.exact);
  REQUIRE(result.bound == BoundType::exact);
  REQUIRE(result.nodes == result.stats.nodes);
  REQUIRE(result.stats.endgame_nodes == result.stats.nodes);
  REQUIRE(result.stats.eval_calls == 0);
  REQUIRE(result.stats.leaf_nodes == 0);
  require_wld_score(result.score);
  require_replayable_pv(position, result.pv);
  require_replayable_root_pvs(position, result);
  for (const RootMoveInfo& root_move : result.root_moves) {
    require_wld_score(root_move.score);
    REQUIRE(root_move.bound == BoundType::exact);
    REQUIRE(root_move.exact);
    REQUIRE_FALSE(root_move.selective);
  }
}

bool is_legal_root_move(board_core::Position position, board_core::Move move) {
  board_core::MoveDelta delta{};
  return board_core::make_move_delta(position, move, &delta);
}

board_core::Position child_after_move(board_core::Position position, board_core::Move move) {
  board_core::MoveDelta delta{};
  REQUIRE(board_core::apply_move(&position, move, &delta));
  return position;
}

Score root_score_for_move(const SearchResult& result, board_core::Move move) {
  for (const RootMoveInfo& root_move : result.root_moves) {
    if (root_move.move == move) {
      return root_move.score;
    }
  }
  FAIL("root move was missing from comparison result");
  return 0;
}

const RootMoveInfo& root_info_for_move(const SearchResult& result, board_core::Move move) {
  for (const RootMoveInfo& root_move : result.root_moves) {
    if (root_move.move == move) {
      return root_move;
    }
  }
  FAIL("root move was missing from comparison result");
  return result.root_moves.front();
}

void require_same_exact_scores(board_core::Position position, const SearchResult& with_tt,
                               const SearchResult& without_tt) {
  REQUIRE(with_tt.score == without_tt.score);
  REQUIRE(with_tt.completed_depth == without_tt.completed_depth);
  REQUIRE(with_tt.best_move == without_tt.best_move);
  REQUIRE(with_tt.best_move.has_value() == without_tt.best_move.has_value());
  REQUIRE(with_tt.root_moves.size() == without_tt.root_moves.size());
  if (with_tt.best_move.has_value()) {
    REQUIRE(is_legal_root_move(position, *with_tt.best_move));
    REQUIRE(root_score_for_move(without_tt, *with_tt.best_move) == without_tt.score);
  }
  for (const RootMoveInfo& root_move : with_tt.root_moves) {
    const RootMoveInfo& without_tt_root_move = root_info_for_move(without_tt, root_move.move);
    REQUIRE(without_tt_root_move.score == root_move.score);
    REQUIRE(without_tt_root_move.bound == root_move.bound);
    REQUIRE(without_tt_root_move.exact == root_move.exact);
    REQUIRE(without_tt_root_move.selective == root_move.selective);
  }
}

TEST_CASE("exact endgame terminal root returns disc difference without evaluator",
          "[search][endgame]") {
  const board_core::Position terminal = parse_position_or_fail(
      "BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/WWWWWWWW/WWWWWWWW/WWWWWWWW b");
  REQUIRE(board_core::is_terminal(terminal));

  const SearchResult result = search_exact(terminal, 0);

  REQUIRE_FALSE(result.best_move.has_value());
  REQUIRE(result.score == 16);
  REQUIRE(result.completed_depth == Depth{0});
  REQUIRE(result.root_moves.empty());
  REQUIRE(result.stats.terminal_nodes == 1);
  require_exact_result_invariants(terminal, result);
}

TEST_CASE("exact endgame solves one-empty root without evaluator", "[search][endgame]") {
  const board_core::Position position = parse_position_or_fail(
      "BBBBBBW./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b");
  REQUIRE_FALSE(board_core::is_terminal(position));

  const SearchResult result = search_exact(position, 1);

  REQUIRE(result.best_move.has_value());
  REQUIRE(*result.best_move == board_core::make_move(square(7, 7)));
  REQUIRE(result.score == 64);
  REQUIRE(result.completed_depth == Depth{1});
  REQUIRE(result.root_moves.size() == 1);
  REQUIRE(result.root_moves[0].score == 64);
  REQUIRE(result.pv.size == 1);
  REQUIRE(result.pv.moves[0] == *result.best_move);
  require_exact_result_invariants(position, result);
}

TEST_CASE("exact endgame represents forced pass in principal variation", "[search][endgame]") {
  const board_core::Position position = parse_position_or_fail(
      "BBBBBWB./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b");
  REQUIRE_FALSE(board_core::has_legal_move(position));
  REQUIRE_FALSE(board_core::is_terminal(position));

  const SearchResult result = search_exact(position, 1);

  REQUIRE(result.best_move.has_value());
  REQUIRE(*result.best_move == board_core::make_pass());
  REQUIRE(result.score == 58);
  REQUIRE(result.completed_depth == Depth{1});
  REQUIRE(result.root_moves.size() == 1);
  REQUIRE(result.root_moves[0].move == board_core::make_pass());
  REQUIRE(result.root_moves[0].score == 58);
  REQUIRE(result.pv.size == 2);
  REQUIRE(result.pv.moves[0] == board_core::make_pass());
  REQUIRE(result.pv.moves[1] == board_core::make_move(square(7, 7)));
  REQUIRE(result.stats.pass_nodes >= 1);
  require_exact_result_invariants(position, result);
}

TEST_CASE("exact endgame option triggers only at or below threshold", "[search][endgame]") {
  const board_core::Position position = parse_position_or_fail(
      "BBBBBBW./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b");
  CountingEvaluator below_threshold_evaluator{77};
  CountingEvaluator at_threshold_evaluator{77};

  const SearchResult below_threshold = search_iterative(
      position, below_threshold_evaluator, SearchLimits{.max_depth = Depth{0}}, exact_options(0));
  const SearchResult at_threshold = search_iterative(
      position, at_threshold_evaluator, SearchLimits{.max_depth = Depth{0}}, exact_options(1));

  REQUIRE_FALSE(below_threshold.exact);
  REQUIRE(below_threshold.score == 77);
  REQUIRE(below_threshold_evaluator.calls == 1);
  REQUIRE(at_threshold.exact);
  REQUIRE(at_threshold.score == 64);
  REQUIRE(at_threshold_evaluator.calls == 0);
}

TEST_CASE("public direct exact endgame matches root-triggered exact search",
          "[search][endgame][public]") {
  const board_core::Position position = corpus_position("four_empty_simple");
  const std::uint8_t empties = test_support::endgame_empty_count(position);

  const SearchResult through_iterative = search_exact(position, empties);
  const SearchResult direct = solve_direct_exact(position);

  require_exact_result_invariants(position, through_iterative);
  require_exact_result_invariants(position, direct);
  require_same_exact_scores(position, direct, through_iterative);
  REQUIRE(direct.completed_depth == through_iterative.completed_depth);
  REQUIRE(direct.root_moves == through_iterative.root_moves);
  REQUIRE(direct.pv == through_iterative.pv);
}

TEST_CASE("public direct exact endgame bypasses exact endgame threshold options",
          "[search][endgame][public]") {
  const board_core::Position position = corpus_position("four_empty_simple");
  const std::uint8_t empties = test_support::endgame_empty_count(position);
  CountingEvaluator evaluator{77};

  const SearchResult gated_off =
      search_iterative(position, evaluator, SearchLimits{.max_depth = Depth{0}},
                       SearchOptions{.exact_endgame = false, .endgame_exact_empties = 0});
  const SearchResult through_iterative = search_exact(position, empties);
  const SearchResult direct =
      solve_direct_exact(position, SearchLimits{.max_depth = Depth{0}},
                         SearchOptions{.exact_endgame = false, .endgame_exact_empties = 0});

  REQUIRE_FALSE(gated_off.exact);
  REQUIRE(gated_off.stats.eval_calls == 1);
  REQUIRE(evaluator.calls == 1);
  require_exact_result_invariants(position, direct);
  require_same_exact_scores(position, direct, through_iterative);
  REQUIRE(direct.completed_depth == Depth{empties});
}

TEST_CASE("public direct exact endgame solves terminal root without evaluator",
          "[search][endgame][public]") {
  const board_core::Position terminal = parse_position_or_fail(
      "BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/WWWWWWWW/WWWWWWWW/WWWWWWWW b");
  REQUIRE(board_core::is_terminal(terminal));

  const SearchResult result = solve_direct_exact(terminal);

  REQUIRE_FALSE(result.best_move.has_value());
  REQUIRE(result.score == 16);
  REQUIRE(result.completed_depth == Depth{0});
  REQUIRE(result.root_moves.empty());
  REQUIRE(result.stats.terminal_nodes == 1);
  require_exact_result_invariants(terminal, result);
}

TEST_CASE("public direct exact endgame reports forced pass replayably",
          "[search][endgame][public]") {
  const board_core::Position position = parse_position_or_fail(
      "BBBBBWB./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b");
  REQUIRE_FALSE(board_core::has_legal_move(position));
  REQUIRE_FALSE(board_core::is_terminal(position));

  const SearchResult result = solve_direct_exact(position);

  REQUIRE(result.best_move == board_core::make_pass());
  REQUIRE(result.score == 58);
  REQUIRE(result.completed_depth == Depth{1});
  REQUIRE(result.root_moves.size() == 1);
  REQUIRE(result.root_moves[0].move == board_core::make_pass());
  REQUIRE(result.pv.size == 2);
  REQUIRE(result.pv.moves[0] == board_core::make_pass());
  REQUIRE(result.pv.moves[1] == board_core::make_move(square(7, 7)));
  require_exact_result_invariants(position, result);
}

TEST_CASE("public direct WLD endgame solves terminal root by exact score sign",
          "[search][endgame][wld][public]") {
  const board_core::Position terminal = parse_position_or_fail(
      "BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/WWWWWWWW/WWWWWWWW/WWWWWWWW b");
  REQUIRE(board_core::is_terminal(terminal));

  const SearchResult exact = solve_direct_exact(terminal);
  const SearchResult wld = solve_direct_wld(terminal);

  REQUIRE_FALSE(wld.best_move.has_value());
  REQUIRE(wld.score == wld_from_exact_score(exact.score));
  REQUIRE(wld.completed_depth == exact.completed_depth);
  REQUIRE(wld.root_moves.empty());
  REQUIRE(wld.stats.terminal_nodes == 1);
  require_wld_result_invariants(terminal, wld);
}

TEST_CASE("public direct WLD endgame solves one-empty root without final margin",
          "[search][endgame][wld][public]") {
  const board_core::Position position = parse_position_or_fail(
      "BBBBBBW./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b");

  const SearchResult exact = solve_direct_exact(position);
  const SearchResult wld = solve_direct_wld(position);

  REQUIRE(wld.best_move == exact.best_move);
  REQUIRE(wld.score == wld_from_exact_score(exact.score));
  REQUIRE(wld.score == static_cast<Score>(WldResult::win));
  REQUIRE(wld.completed_depth == Depth{1});
  REQUIRE(wld.root_moves.size() == 1);
  REQUIRE(wld.root_moves[0].score == wld.score);
  require_wld_result_invariants(position, wld);
}

TEST_CASE("public direct WLD endgame represents forced pass replayably",
          "[search][endgame][wld][public]") {
  const board_core::Position position = parse_position_or_fail(
      "BBBBBWB./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b");
  REQUIRE_FALSE(board_core::has_legal_move(position));
  REQUIRE_FALSE(board_core::is_terminal(position));

  const SearchResult exact = solve_direct_exact(position);
  const SearchResult wld = solve_direct_wld(position);

  REQUIRE(wld.best_move == board_core::make_pass());
  REQUIRE(wld.score == wld_from_exact_score(exact.score));
  REQUIRE(wld.root_moves.size() == 1);
  REQUIRE(wld.root_moves[0].move == board_core::make_pass());
  REQUIRE(wld.pv.size == exact.pv.size);
  REQUIRE(wld.pv.moves[0] == board_core::make_pass());
  REQUIRE(wld.stats.pass_nodes >= 1);
  require_wld_result_invariants(position, wld);
}

TEST_CASE("public direct WLD endgame sign matches exact corpus results",
          "[search][endgame][wld][corpus]") {
  const std::vector<test_support::EndgamePositionCase> cases =
      test_support::load_endgame_position_corpus(endgame_corpus_path());

  bool saw_generic_wld_position = false;
  for (const test_support::EndgamePositionCase& position_case : cases) {
    INFO("position_id: " << position_case.id);
    if (position_case.expected_empties > kFastCorpusMaxEmpties) {
      continue;
    }

    const SearchResult exact = solve_direct_exact(position_case.position);
    const SearchResult wld = solve_direct_wld(position_case.position);

    require_exact_result_invariants(position_case.position, exact);
    require_wld_result_invariants(position_case.position, wld);
    REQUIRE(wld.score == wld_from_exact_score(exact.score));
    REQUIRE(wld.completed_depth == exact.completed_depth);
    REQUIRE(wld.best_move.has_value() == exact.best_move.has_value());
    if (wld.best_move.has_value()) {
      REQUIRE(is_legal_root_move(position_case.position, *wld.best_move));
    }
    for (const RootMoveInfo& wld_root_move : wld.root_moves) {
      REQUIRE(wld_root_move.score ==
              wld_from_exact_score(root_score_for_move(exact, wld_root_move.move)));
    }
    saw_generic_wld_position = saw_generic_wld_position || position_case.expected_empties > 4;
  }

  REQUIRE(saw_generic_wld_position);
}

TEST_CASE("root-triggered WLD search matches direct WLD result",
          "[search][endgame][wld][iterative]") {
  const board_core::Position position = corpus_position("four_empty_simple");
  const std::uint8_t empties = test_support::endgame_empty_count(position);

  const SearchResult through_iterative = search_wld(position, empties);
  const SearchResult direct = solve_direct_wld(position);

  require_wld_result_invariants(position, through_iterative);
  require_wld_result_invariants(position, direct);
  REQUIRE(through_iterative.score == direct.score);
  REQUIRE(through_iterative.completed_depth == direct.completed_depth);
  REQUIRE(through_iterative.best_move == direct.best_move);
  REQUIRE(through_iterative.pv == direct.pv);
  REQUIRE(through_iterative.root_moves == direct.root_moves);
}

TEST_CASE("root-triggered WLD search sign matches exact-score endgame",
          "[search][endgame][wld][iterative]") {
  for (const std::string_view id :
       {"four_empty_simple", "eight_empty_simple", "twelve_empty_simple"}) {
    INFO("position_id: " << id);
    const board_core::Position position = corpus_position(id);
    const std::uint8_t empties = test_support::endgame_empty_count(position);

    const SearchResult exact = solve_direct_exact(position);
    const SearchResult wld = search_wld(position, empties);

    require_exact_result_invariants(position, exact);
    require_wld_result_invariants(position, wld);
    REQUIRE(wld.score == wld_from_exact_score(exact.score));
    REQUIRE(wld.completed_depth == exact.completed_depth);
    REQUIRE(wld.best_move.has_value() == exact.best_move.has_value());
    if (wld.best_move.has_value()) {
      REQUIRE(is_legal_root_move(position, *wld.best_move));
    }
    for (const RootMoveInfo& wld_root_move : wld.root_moves) {
      REQUIRE(wld_root_move.score ==
              wld_from_exact_score(root_score_for_move(exact, wld_root_move.move)));
    }
  }
}

TEST_CASE("root-triggered WLD requires explicit WLD mode and threshold",
          "[search][endgame][wld][iterative]") {
  const board_core::Position position = corpus_position("four_empty_simple");
  const std::uint8_t empties = test_support::endgame_empty_count(position);

  CountingEvaluator no_mode_evaluator{77};
  const SearchResult no_mode =
      search_iterative(position, no_mode_evaluator, SearchLimits{.max_depth = Depth{0}},
                       SearchOptions{.endgame_wld_empties = empties});
  REQUIRE_FALSE(no_mode.exact);
  REQUIRE(no_mode.score == 77);
  REQUIRE(no_mode.stats.endgame_nodes == 0);
  REQUIRE(no_mode_evaluator.calls == 1);

  CountingEvaluator disabled_threshold_evaluator{78};
  const SearchResult disabled_threshold = search_iterative(
      position, disabled_threshold_evaluator, SearchLimits{.max_depth = Depth{0}}, wld_options(0));
  REQUIRE_FALSE(disabled_threshold.exact);
  REQUIRE(disabled_threshold.score == 78);
  REQUIRE(disabled_threshold.stats.endgame_nodes == 0);
  REQUIRE(disabled_threshold_evaluator.calls == 1);

  CountingEvaluator above_threshold_evaluator{79};
  const SearchResult above_threshold = search_iterative(
      position, above_threshold_evaluator, SearchLimits{.max_depth = Depth{0}},
      wld_options(static_cast<std::uint8_t>(empties - 1), false, true, 0, empties, true));
  REQUIRE_FALSE(above_threshold.exact);
  REQUIRE(above_threshold.score == 79);
  REQUIRE(above_threshold.stats.endgame_nodes == 0);
  REQUIRE(above_threshold_evaluator.calls == 1);
}

TEST_CASE("exact-score request is not changed by WLD threshold",
          "[search][endgame][wld][iterative]") {
  const board_core::Position position = corpus_position("four_empty_simple");
  const std::uint8_t empties = test_support::endgame_empty_count(position);
  CountingEvaluator evaluator{123};

  const SearchOptions options{
      .exact_endgame = true,
      .endgame_exact_empties = empties,
      .endgame_wld_empties = empties,
      .mode = SearchMode::exact_score,
  };
  const SearchResult through_iterative =
      search_iterative(position, evaluator, SearchLimits{.max_depth = Depth{0}}, options);
  const SearchResult direct = solve_direct_exact(position);

  REQUIRE(evaluator.calls == 0);
  require_exact_result_invariants(position, through_iterative);
  require_same_exact_scores(position, through_iterative, direct);
  REQUIRE(through_iterative.score == direct.score);
  REQUIRE(through_iterative.root_moves == direct.root_moves);
}

TEST_CASE("root-triggered WLD TT and parity options preserve outcome",
          "[search][endgame][wld][iterative][tt][parity]") {
  const board_core::Position position = test_support::generated_endgame_position(6);

  const SearchResult baseline = search_wld(position, 6, false, false);
  const SearchResult with_parity = search_wld(position, 6, false, true);
  const SearchResult with_tt = search_wld(position, 6, true, false);
  const SearchResult with_both = search_wld(position, 6, true, true);

  require_wld_result_invariants(position, baseline);
  require_wld_result_invariants(position, with_parity);
  require_wld_result_invariants(position, with_tt);
  require_wld_result_invariants(position, with_both);
  REQUIRE(with_parity.score == baseline.score);
  REQUIRE(with_tt.score == baseline.score);
  REQUIRE(with_both.score == baseline.score);
  REQUIRE(with_parity.best_move == baseline.best_move);
  REQUIRE(with_tt.best_move == baseline.best_move);
  REQUIRE(with_both.best_move == baseline.best_move);
  REQUIRE(with_tt.stats.tt_probes > 0);
  REQUIRE(with_both.stats.tt_probes > 0);
}

TEST_CASE("root-triggered WLD respects max node and stop limits",
          "[search][endgame][wld][iterative][limits]") {
  const board_core::Position position = corpus_position("four_empty_simple");

  const SearchResult max_nodes_result =
      search_wld_with_limits(position, 4, SearchLimits{.max_nodes = 1});

  REQUIRE(max_nodes_result.stopped);
  REQUIRE_FALSE(max_nodes_result.exact);
  REQUIRE(max_nodes_result.bound == BoundType::lower);
  REQUIRE(max_nodes_result.completed_depth == Depth{0});
  REQUIRE_FALSE(max_nodes_result.best_move.has_value());
  REQUIRE(max_nodes_result.nodes == 1);
  REQUIRE(max_nodes_result.stats.nodes == 1);
  REQUIRE(max_nodes_result.stats.endgame_nodes == 1);

  std::atomic_bool stop_requested{true};
  const SearchResult stop_result =
      search_wld_with_limits(position, 4, SearchLimits{.stop_requested = &stop_requested});

  REQUIRE(stop_result.stopped);
  REQUIRE_FALSE(stop_result.exact);
  REQUIRE(stop_result.bound == BoundType::lower);
  REQUIRE(stop_result.completed_depth == Depth{0});
  REQUIRE_FALSE(stop_result.best_move.has_value());
  REQUIRE(stop_result.nodes == 0);
  REQUIRE(stop_result.stats.endgame_nodes == 0);
}

TEST_CASE("public direct WLD endgame TT and parity options preserve outcome",
          "[search][endgame][wld][tt][parity]") {
  const board_core::Position position = test_support::generated_endgame_position(6);

  const SearchResult baseline = solve_direct_wld(position, false, false);
  const SearchResult with_parity = solve_direct_wld(position, false, true);
  const SearchResult with_tt = solve_direct_wld(position, true, false);
  const SearchResult with_both = solve_direct_wld(position, true, true);

  require_wld_result_invariants(position, baseline);
  require_wld_result_invariants(position, with_parity);
  require_wld_result_invariants(position, with_tt);
  require_wld_result_invariants(position, with_both);
  REQUIRE(with_parity.score == baseline.score);
  REQUIRE(with_tt.score == baseline.score);
  REQUIRE(with_both.score == baseline.score);
  REQUIRE(with_parity.best_move == baseline.best_move);
  REQUIRE(with_tt.best_move == baseline.best_move);
  REQUIRE(with_both.best_move == baseline.best_move);
  REQUIRE(with_tt.stats.tt_probes > 0);
  REQUIRE(with_both.stats.tt_probes > 0);
}

TEST_CASE("public direct WLD endgame best-only reports only the exact best root move",
          "[search][endgame][wld][public]") {
  const board_core::Position position = corpus_position("four_empty_simple");

  const SearchResult all_roots = solve_direct_wld(position, false, true, 0);
  const SearchResult best_only = solve_direct_wld(position, false, true, 1);

  require_wld_result_invariants(position, all_roots);
  require_wld_result_invariants(position, best_only);
  REQUIRE(all_roots.root_moves.size() > 1);
  REQUIRE(best_only.score == all_roots.score);
  REQUIRE(best_only.best_move == all_roots.best_move);
  REQUIRE(best_only.pv == all_roots.pv);
  REQUIRE(best_only.root_moves.size() == 1);
  REQUIRE(best_only.root_moves[0].move == *best_only.best_move);
  REQUIRE(best_only.root_moves[0].score == best_only.score);
}

TEST_CASE("public direct WLD endgame respects max node and stop limits",
          "[search][endgame][wld][public][limits]") {
  const board_core::Position position = corpus_position("four_empty_simple");

  const SearchResult max_nodes_result = solve_direct_wld(position, SearchLimits{.max_nodes = 1});

  REQUIRE(max_nodes_result.stopped);
  REQUIRE_FALSE(max_nodes_result.exact);
  REQUIRE(max_nodes_result.bound == BoundType::lower);
  REQUIRE(max_nodes_result.completed_depth == Depth{0});
  REQUIRE_FALSE(max_nodes_result.best_move.has_value());
  REQUIRE(max_nodes_result.nodes == 1);
  REQUIRE(max_nodes_result.stats.nodes == 1);
  REQUIRE(max_nodes_result.stats.endgame_nodes == 1);

  std::atomic_bool stop_requested{true};
  const SearchResult stop_result =
      solve_direct_wld(position, SearchLimits{.stop_requested = &stop_requested});

  REQUIRE(stop_result.stopped);
  REQUIRE_FALSE(stop_result.exact);
  REQUIRE(stop_result.bound == BoundType::lower);
  REQUIRE(stop_result.completed_depth == Depth{0});
  REQUIRE_FALSE(stop_result.best_move.has_value());
  REQUIRE(stop_result.nodes == 0);
  REQUIRE(stop_result.stats.endgame_nodes == 0);
}

TEST_CASE("public direct exact endgame respects max node and stop limits",
          "[search][endgame][public][limits]") {
  const board_core::Position position = corpus_position("four_empty_simple");

  const SearchResult max_nodes_result = solve_direct_exact(position, SearchLimits{.max_nodes = 1});

  REQUIRE(max_nodes_result.stopped);
  REQUIRE_FALSE(max_nodes_result.exact);
  REQUIRE(max_nodes_result.bound == BoundType::lower);
  REQUIRE(max_nodes_result.completed_depth == Depth{0});
  REQUIRE_FALSE(max_nodes_result.best_move.has_value());
  REQUIRE(max_nodes_result.nodes == 1);
  REQUIRE(max_nodes_result.stats.nodes == 1);
  REQUIRE(max_nodes_result.stats.endgame_nodes == 1);
  REQUIRE(max_nodes_result.nodes <= 1);

  std::atomic_bool stop_requested{true};
  const SearchResult stop_result =
      solve_direct_exact(position, SearchLimits{.stop_requested = &stop_requested});

  REQUIRE(stop_result.stopped);
  REQUIRE_FALSE(stop_result.exact);
  REQUIRE(stop_result.bound == BoundType::lower);
  REQUIRE(stop_result.completed_depth == Depth{0});
  REQUIRE_FALSE(stop_result.best_move.has_value());
  REQUIRE(stop_result.nodes == 0);
  REQUIRE(stop_result.stats.endgame_nodes == 0);
}

TEST_CASE("public direct exact endgame TT and parity options preserve exact result",
          "[search][endgame][public][tt][parity]") {
  const board_core::Position position = test_support::generated_endgame_position(6);

  const SearchResult baseline = solve_direct_exact(position, false, false);
  const SearchResult with_parity = solve_direct_exact(position, false, true);
  const SearchResult with_tt = solve_direct_exact(position, true, false);
  const SearchResult with_both = solve_direct_exact(position, true, true);

  require_exact_result_invariants(position, baseline);
  require_exact_result_invariants(position, with_parity);
  require_exact_result_invariants(position, with_tt);
  require_exact_result_invariants(position, with_both);
  require_same_exact_scores(position, with_parity, baseline);
  require_same_exact_scores(position, with_tt, baseline);
  require_same_exact_scores(position, with_both, baseline);
  REQUIRE(with_tt.stats.tt_probes > 0);
  REQUIRE(with_both.stats.tt_probes > 0);
}

TEST_CASE("midgame leaf cutover solves small exact endgames without evaluator",
          "[search][endgame][internal]") {
  const board_core::Position position = test_support::generated_endgame_position(5);
  REQUIRE(test_support::endgame_empty_count(position) == 5);
  CountingEvaluator evaluator{77};

  const SearchResult result =
      search_iterative(position, evaluator, SearchLimits{.max_depth = Depth{1}},
                       SearchOptions{.exact_endgame = true, .endgame_exact_empties = 4});

  REQUIRE_FALSE(result.stopped);
  REQUIRE_FALSE(result.exact);
  REQUIRE(result.completed_depth == Depth{1});
  REQUIRE(result.nodes == result.stats.nodes);
  REQUIRE(result.stats.eval_calls == 0);
  REQUIRE(result.stats.leaf_nodes == 0);
  REQUIRE(result.stats.endgame_nodes > 0);
  REQUIRE(evaluator.calls == 0);
  REQUIRE_FALSE(result.root_moves.empty());
  for (const RootMoveInfo& root_move : result.root_moves) {
    REQUIRE_FALSE(root_move.exact);
    REQUIRE(root_move.move.kind == board_core::MoveKind::normal);

    const board_core::Position child = child_after_move(position, root_move.move);
    REQUIRE(test_support::endgame_empty_count(child) == 4);
    const SearchResult direct = search_exact(child, 4);
    REQUIRE(root_move.score == static_cast<Score>(-direct.score));
  }
}

TEST_CASE("disabled internal leaf cutover preserves evaluator behavior",
          "[search][endgame][internal]") {
  const board_core::Position position = test_support::generated_endgame_position(5);
  CountingEvaluator evaluator{31};

  const SearchResult result =
      search_iterative(position, evaluator, SearchLimits{.max_depth = Depth{1}},
                       SearchOptions{.exact_endgame = false, .endgame_exact_empties = 4});

  REQUIRE_FALSE(result.stopped);
  REQUIRE_FALSE(result.exact);
  REQUIRE(result.completed_depth == Depth{1});
  REQUIRE(result.stats.eval_calls > 0);
  REQUIRE(result.stats.leaf_nodes == result.stats.eval_calls);
  REQUIRE(result.stats.endgame_nodes == 0);
  REQUIRE(evaluator.calls == result.stats.eval_calls);
}

TEST_CASE("internal leaf cutover respects stopped limits safely",
          "[search][endgame][internal][limits]") {
  const board_core::Position position = test_support::generated_endgame_position(5);
  CountingEvaluator max_nodes_evaluator{77};

  const SearchResult max_nodes_result = search_iterative(
      position, max_nodes_evaluator, SearchLimits{.max_depth = Depth{1}, .max_nodes = 2},
      SearchOptions{.exact_endgame = true, .endgame_exact_empties = 4});

  REQUIRE(max_nodes_result.stopped);
  REQUIRE_FALSE(max_nodes_result.exact);
  REQUIRE(max_nodes_result.nodes == max_nodes_result.stats.nodes);
  REQUIRE(max_nodes_result.stats.eval_calls == 0);
  REQUIRE(max_nodes_evaluator.calls == 0);

  std::atomic_bool stop_requested{true};
  CountingEvaluator stop_evaluator{77};
  const SearchResult stop_result =
      search_iterative(position, stop_evaluator,
                       SearchLimits{.max_depth = Depth{1}, .stop_requested = &stop_requested},
                       SearchOptions{.exact_endgame = true, .endgame_exact_empties = 4});

  REQUIRE(stop_result.stopped);
  REQUIRE_FALSE(stop_result.exact);
  REQUIRE(stop_result.nodes == 0);
  REQUIRE(stop_result.stats.endgame_nodes == 0);
  REQUIRE(stop_result.stats.eval_calls == 0);
  REQUIRE(stop_evaluator.calls == 0);
}

TEST_CASE("disabled exact endgame preserves existing depth-zero behavior", "[search][endgame]") {
  const board_core::Position position = parse_position_or_fail(
      "BBBBBBW./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b");
  CountingEvaluator evaluator{91};

  const SearchResult result =
      search_iterative(position, evaluator, SearchLimits{.max_depth = Depth{0}},
                       SearchOptions{.exact_endgame = false, .endgame_exact_empties = 1});

  REQUIRE_FALSE(result.exact);
  REQUIRE(result.score == 91);
  REQUIRE(result.completed_depth == Depth{0});
  REQUIRE(result.root_moves.empty());
  REQUIRE(evaluator.calls == 1);
}

TEST_CASE("exact endgame reports exact root flags and legal best move", "[search][endgame]") {
  const board_core::Position position = parse_position_or_fail(
      "BBBBBBW./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b");

  const SearchResult result = search_exact(position, 1);

  REQUIRE(result.best_move.has_value());
  REQUIRE(is_legal_root_move(position, *result.best_move));
  REQUIRE(result.exact);
  REQUIRE(result.root_moves.size() == 1);
  REQUIRE(result.root_moves[0].exact);
  REQUIRE_FALSE(result.root_moves[0].selective);
  require_exact_result_invariants(position, result);
}

TEST_CASE("exact endgame default and multi pv zero report all root moves", "[search][endgame]") {
  const board_core::Position position = corpus_position("four_empty_simple");

  const SearchResult default_result = search_exact(position, 4);
  const SearchResult multi_pv_zero = search_exact(position, 4, false, true, 0);

  require_exact_result_invariants(position, default_result);
  require_exact_result_invariants(position, multi_pv_zero);
  REQUIRE(default_result.root_moves.size() > 1);
  REQUIRE(multi_pv_zero.best_move == default_result.best_move);
  REQUIRE(multi_pv_zero.score == default_result.score);
  REQUIRE(multi_pv_zero.pv == default_result.pv);
  REQUIRE(multi_pv_zero.root_moves == default_result.root_moves);
}

TEST_CASE("exact endgame multi pv one reports only the exact best root move", "[search][endgame]") {
  const board_core::Position position = corpus_position("four_empty_simple");

  const SearchResult all_roots = search_exact(position, 4);
  const SearchResult best_only = search_exact(position, 4, false, true, 1);

  require_exact_result_invariants(position, all_roots);
  require_exact_result_invariants(position, best_only);
  REQUIRE(all_roots.root_moves.size() > 1);
  REQUIRE(best_only.score == all_roots.score);
  REQUIRE(best_only.best_move == all_roots.best_move);
  REQUIRE(best_only.pv == all_roots.pv);
  REQUIRE(best_only.completed_depth == all_roots.completed_depth);
  REQUIRE(best_only.root_moves.size() == 1);
  REQUIRE(best_only.root_moves[0].move == *best_only.best_move);
  REQUIRE(best_only.root_moves[0].score == best_only.score);
  REQUIRE(best_only.root_moves[0].pv == best_only.pv);
  REQUIRE(best_only.root_moves[0].exact);
  REQUIRE_FALSE(best_only.root_moves[0].selective);
  REQUIRE(best_only.stats.root_moves_searched == all_roots.root_moves.size());
}

TEST_CASE("exact endgame multi pv greater than one is a safe all-root no-op", "[search][endgame]") {
  const board_core::Position position = corpus_position("four_empty_simple");

  const SearchResult all_roots = search_exact(position, 4);
  const SearchResult top_n_unimplemented = search_exact(position, 4, false, true, 2);

  require_exact_result_invariants(position, all_roots);
  require_exact_result_invariants(position, top_n_unimplemented);
  REQUIRE(top_n_unimplemented.best_move == all_roots.best_move);
  REQUIRE(top_n_unimplemented.score == all_roots.score);
  REQUIRE(top_n_unimplemented.pv == all_roots.pv);
  REQUIRE(top_n_unimplemented.root_moves == all_roots.root_moves);
}

TEST_CASE("exact endgame best-only keeps terminal and forced pass root reports stable",
          "[search][endgame]") {
  const board_core::Position terminal = parse_position_or_fail(
      "BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/WWWWWWWW/WWWWWWWW/WWWWWWWW b");
  const board_core::Position forced_pass = parse_position_or_fail(
      "BBBBBWB./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b");

  const SearchResult terminal_result = search_exact(terminal, 0, false, true, 1);
  const SearchResult forced_pass_result = search_exact(forced_pass, 1, false, true, 1);

  require_exact_result_invariants(terminal, terminal_result);
  require_exact_result_invariants(forced_pass, forced_pass_result);
  REQUIRE_FALSE(terminal_result.best_move.has_value());
  REQUIRE(terminal_result.root_moves.empty());
  REQUIRE(forced_pass_result.best_move == board_core::make_pass());
  REQUIRE(forced_pass_result.root_moves.size() == 1);
  REQUIRE(forced_pass_result.root_moves[0].move == board_core::make_pass());
  REQUIRE(forced_pass_result.root_moves[0].score == forced_pass_result.score);
}

TEST_CASE("exact endgame TT preserves exact scores for representative positions",
          "[search][endgame][tt]") {
  const std::array<board_core::Position, 6> positions{
      parse_position_or_fail(
          "BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/WWWWWWWW/WWWWWWWW/WWWWWWWW b"),
      parse_position_or_fail(
          "BBBBBBW./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b"),
      parse_position_or_fail(
          "BBBBBWB./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b"),
      test_support::generated_endgame_position(2),
      test_support::generated_endgame_position(4),
      test_support::generated_endgame_position(6),
  };

  bool saw_tt_probe = false;
  bool saw_tt_store = false;
  bool saw_tt_cutoff = false;

  for (const board_core::Position position : positions) {
    const std::uint8_t empties = test_support::endgame_empty_count(position);
    const SearchResult without_tt = search_exact(position, empties, false);
    const SearchResult with_tt = search_exact(position, empties, true);

    require_exact_result_invariants(position, without_tt);
    require_exact_result_invariants(position, with_tt);
    require_same_exact_scores(position, with_tt, without_tt);

    saw_tt_probe = saw_tt_probe || with_tt.stats.tt_probes > 0;
    saw_tt_store = saw_tt_store || with_tt.stats.tt_stores > 0;
    saw_tt_cutoff = saw_tt_cutoff || with_tt.stats.tt_cutoffs > 0;
  }

  REQUIRE(saw_tt_probe);
  REQUIRE(saw_tt_store);
  REQUIRE(saw_tt_cutoff);
}

TEST_CASE("exact endgame parity ordering preserves corpus scores and best move tie behavior",
          "[search][endgame][parity]") {
  const std::vector<test_support::EndgamePositionCase> cases =
      test_support::load_endgame_position_corpus(endgame_corpus_path());

  bool saw_generic_parity_position = false;
  for (const test_support::EndgamePositionCase& position_case : cases) {
    INFO("position_id: " << position_case.id);
    const std::uint8_t empties = position_case.expected_empties;
    if (empties > kFastCorpusMaxEmpties) {
      continue;
    }
    const SearchResult without_parity = search_exact(position_case.position, empties, false, false);
    const SearchResult with_parity = search_exact(position_case.position, empties, false, true);

    require_exact_result_invariants(position_case.position, without_parity);
    require_exact_result_invariants(position_case.position, with_parity);
    require_same_exact_scores(position_case.position, with_parity, without_parity);
    saw_generic_parity_position = saw_generic_parity_position || empties > 4;
  }

  REQUIRE(saw_generic_parity_position);
}

TEST_CASE("exact endgame parity ordering leaves terminal and forced pass roots unchanged",
          "[search][endgame][parity]") {
  const std::array<board_core::Position, 2> positions{
      parse_position_or_fail(
          "BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/WWWWWWWW/WWWWWWWW/WWWWWWWW b"),
      parse_position_or_fail(
          "BBBBBWB./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b"),
  };

  for (const board_core::Position position : positions) {
    const std::uint8_t empties = test_support::endgame_empty_count(position);
    const SearchResult without_parity = search_exact(position, empties, false, false);
    const SearchResult with_parity = search_exact(position, empties, false, true);

    require_exact_result_invariants(position, without_parity);
    require_exact_result_invariants(position, with_parity);
    REQUIRE(with_parity.best_move == without_parity.best_move);
    REQUIRE(with_parity.score == without_parity.score);
    REQUIRE(with_parity.pv == without_parity.pv);
    REQUIRE(with_parity.root_moves == without_parity.root_moves);
  }
}

TEST_CASE("exact endgame stop requested before search publishes no exact result",
          "[search][endgame][limits]") {
  const board_core::Position position = parse_position_or_fail(
      "BBBBBBW./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b");
  std::atomic_bool stop_requested{true};

  const SearchResult result =
      search_exact_with_limits(position, 1, SearchLimits{.stop_requested = &stop_requested});

  REQUIRE(result.stopped);
  REQUIRE_FALSE(result.exact);
  REQUIRE(result.bound == BoundType::lower);
  REQUIRE(result.completed_depth == Depth{0});
  REQUIRE(result.score == kScoreLoss);
  REQUIRE_FALSE(result.best_move.has_value());
  REQUIRE(result.pv.size == 0);
  REQUIRE(result.root_moves.empty());
  REQUIRE(result.nodes == 0);
  REQUIRE(result.stats.nodes == 0);
  REQUIRE(result.stats.endgame_nodes == 0);
}

TEST_CASE("exact endgame max nodes can stop before any root move completes",
          "[search][endgame][limits]") {
  const board_core::Position position = corpus_position("four_empty_simple");

  const SearchResult result = search_exact_with_limits(position, 4, SearchLimits{.max_nodes = 1});

  REQUIRE(result.stopped);
  REQUIRE_FALSE(result.exact);
  REQUIRE(result.bound == BoundType::lower);
  REQUIRE(result.completed_depth == Depth{0});
  REQUIRE(result.score == kScoreLoss);
  REQUIRE_FALSE(result.best_move.has_value());
  REQUIRE(result.pv.size == 0);
  REQUIRE(result.root_moves.empty());
  REQUIRE(result.nodes == 1);
  REQUIRE(result.stats.nodes == 1);
  REQUIRE(result.stats.endgame_nodes == 1);
}

TEST_CASE("best-only exact endgame max nodes can stop before any root move completes",
          "[search][endgame][limits]") {
  const board_core::Position position = corpus_position("four_empty_simple");

  const SearchResult result =
      search_exact_with_limits(position, 4, SearchLimits{.max_nodes = 1}, 1);

  REQUIRE(result.stopped);
  REQUIRE_FALSE(result.exact);
  REQUIRE(result.bound == BoundType::lower);
  REQUIRE(result.completed_depth == Depth{0});
  REQUIRE(result.score == kScoreLoss);
  REQUIRE_FALSE(result.best_move.has_value());
  REQUIRE(result.pv.size == 0);
  REQUIRE(result.root_moves.empty());
  REQUIRE(result.nodes == 1);
  REQUIRE(result.stats.nodes == 1);
  REQUIRE(result.stats.endgame_nodes == 1);
}

TEST_CASE("interrupted exact endgame publishes only completed exact root moves",
          "[search][endgame][limits]") {
  const board_core::Position position = corpus_position("four_empty_simple");
  const SearchResult complete = search_exact(position, 4);
  REQUIRE(complete.root_moves.size() > 1);
  REQUIRE(complete.root_moves[0].nodes > 0);

  const NodeCount nodes_after_first_root = static_cast<NodeCount>(2 + complete.root_moves[0].nodes);
  const SearchResult limited =
      search_exact_with_limits(position, 4, SearchLimits{.max_nodes = nodes_after_first_root});

  REQUIRE(limited.stopped);
  REQUIRE_FALSE(limited.exact);
  REQUIRE(limited.bound == BoundType::lower);
  REQUIRE(limited.completed_depth == complete.completed_depth);
  REQUIRE(limited.best_move.has_value());
  REQUIRE(limited.score == complete.root_moves[0].score);
  REQUIRE(limited.pv == complete.root_moves[0].pv);
  REQUIRE(limited.root_moves.size() == 1);
  REQUIRE(limited.root_moves[0] == complete.root_moves[0]);
  REQUIRE(limited.root_moves[0].exact);
  REQUIRE_FALSE(limited.root_moves[0].selective);
  REQUIRE(limited.stats.root_moves_searched == 1);
  REQUIRE(limited.nodes == nodes_after_first_root);
  REQUIRE(limited.stats.endgame_nodes == limited.nodes);
  REQUIRE(is_legal_root_move(position, *limited.best_move));
  require_replayable_pv(position, limited.pv);
  require_replayable_root_pvs(position, limited);
}

TEST_CASE("interrupted best-only exact endgame publishes the completed best root move",
          "[search][endgame][limits]") {
  const board_core::Position position = corpus_position("four_empty_simple");
  const SearchResult complete = search_exact(position, 4);
  REQUIRE(complete.root_moves.size() > 1);
  REQUIRE(complete.root_moves[0].nodes > 0);

  const NodeCount nodes_after_first_root = static_cast<NodeCount>(2 + complete.root_moves[0].nodes);
  const SearchResult limited =
      search_exact_with_limits(position, 4, SearchLimits{.max_nodes = nodes_after_first_root}, 1);

  REQUIRE(limited.stopped);
  REQUIRE_FALSE(limited.exact);
  REQUIRE(limited.bound == BoundType::lower);
  REQUIRE(limited.completed_depth == complete.completed_depth);
  REQUIRE(limited.best_move.has_value());
  REQUIRE(limited.score == complete.root_moves[0].score);
  REQUIRE(limited.pv == complete.root_moves[0].pv);
  REQUIRE(limited.root_moves.size() == 1);
  REQUIRE(limited.root_moves[0] == complete.root_moves[0]);
  REQUIRE(limited.root_moves[0].move == *limited.best_move);
  REQUIRE(limited.root_moves[0].exact);
  REQUIRE_FALSE(limited.root_moves[0].selective);
  REQUIRE(limited.stats.root_moves_searched == 1);
  REQUIRE(limited.nodes == nodes_after_first_root);
  REQUIRE(limited.stats.endgame_nodes == limited.nodes);
  REQUIRE(is_legal_root_move(position, *limited.best_move));
  require_replayable_pv(position, limited.pv);
  require_replayable_root_pvs(position, limited);
}

} // namespace
} // namespace vibe_othello::search
