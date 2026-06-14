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
                            bool use_endgame_parity_ordering = true) noexcept {
  return SearchOptions{
      .use_endgame_tt = use_endgame_tt,
      .exact_endgame = true,
      .use_endgame_parity_ordering = use_endgame_parity_ordering,
      .endgame_exact_empties = threshold,
  };
}

SearchResult search_exact(board_core::Position position, std::uint8_t threshold,
                          bool use_endgame_tt = false, bool use_endgame_parity_ordering = true) {
  CountingEvaluator evaluator{123};
  SearchResult result =
      search_iterative(position, evaluator, SearchLimits{.max_depth = Depth{0}},
                       exact_options(threshold, use_endgame_tt, use_endgame_parity_ordering));
  REQUIRE(evaluator.calls == 0);
  return result;
}

SearchResult search_exact_with_limits(board_core::Position position, std::uint8_t threshold,
                                      SearchLimits limits) {
  CountingEvaluator evaluator{123};
  SearchResult result = search_iterative(position, evaluator, limits, exact_options(threshold));
  REQUIRE(evaluator.calls == 0);
  return result;
}

std::string endgame_corpus_path() {
  return std::string{VIBE_OTHELLO_SOURCE_DIR} + "/engine/testdata/endgame/positions.tsv";
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

void require_same_exact_scores(const SearchResult& with_tt, const SearchResult& without_tt) {
  REQUIRE(with_tt.score == without_tt.score);
  REQUIRE(with_tt.completed_depth == without_tt.completed_depth);
  REQUIRE(with_tt.best_move == without_tt.best_move);
  REQUIRE(with_tt.best_move.has_value() == without_tt.best_move.has_value());
  REQUIRE(with_tt.root_moves.size() == without_tt.root_moves.size());
  if (with_tt.best_move.has_value()) {
    REQUIRE(root_score_for_move(without_tt, *with_tt.best_move) == without_tt.score);
  }
  for (const RootMoveInfo& root_move : with_tt.root_moves) {
    REQUIRE(root_score_for_move(without_tt, root_move.move) == root_move.score);
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
    require_same_exact_scores(with_tt, without_tt);

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
    const SearchResult without_parity = search_exact(position_case.position, empties, false, false);
    const SearchResult with_parity = search_exact(position_case.position, empties, false, true);

    require_exact_result_invariants(position_case.position, without_parity);
    require_exact_result_invariants(position_case.position, with_parity);
    require_same_exact_scores(with_parity, without_parity);
    saw_generic_parity_position = saw_generic_parity_position || empties > 3;
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

} // namespace
} // namespace vibe_othello::search
