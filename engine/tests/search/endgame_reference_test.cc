#include "search/endgame_positions.h"
#include "search/reference_endgame.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/serialization.h"
#include "vibe_othello/search/search.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifndef VIBE_OTHELLO_SOURCE_DIR
#error "VIBE_OTHELLO_SOURCE_DIR must be defined for endgame reference tests"
#endif

namespace vibe_othello::search {
namespace {

class CountingEvaluator final : public Evaluator {
public:
  Score evaluate(const board_core::Position&) const noexcept override {
    ++calls;
    return 0;
  }

  mutable int calls = 0;
};

board_core::Position parse_position_or_fail(std::string_view text) {
  const std::optional<board_core::Position> position = board_core::parse_position(text);
  REQUIRE(position.has_value());
  return *position;
}

std::string endgame_corpus_path() {
  return std::string{VIBE_OTHELLO_SOURCE_DIR} + "/engine/fixtures/endgame/positions.tsv";
}

board_core::Position corpus_position(const std::vector<test_support::EndgamePositionCase>& cases,
                                     std::string_view id) {
  const std::optional<test_support::EndgamePositionCase> position_case =
      test_support::find_endgame_position_case(cases, id);
  REQUIRE(position_case.has_value());
  REQUIRE(test_support::endgame_empty_count(position_case->position) ==
          position_case->expected_empties);
  return position_case->position;
}

SearchResult production_exact_endgame(board_core::Position position) {
  CountingEvaluator evaluator;
  const std::uint8_t empties = test_support::endgame_empty_count(position);
  const SearchResult result =
      search_iterative(position, evaluator, SearchLimits{.max_depth = Depth{0}},
                       SearchOptions{.endgame = EndgameSearchOptions{
                                         .exact_endgame = true,
                                         .endgame_exact_empties = empties,
                                     }});
  REQUIRE(evaluator.calls == 0);
  return result;
}

void require_replayable_pv(board_core::Position position, Line pv) {
  for (std::uint8_t index = 0; index < pv.size; ++index) {
    board_core::MoveDelta delta{};
    REQUIRE(board_core::apply_move(&position, pv.moves[index], &delta));
  }
}

void require_exact_invariants(board_core::Position position, const SearchResult& result) {
  REQUIRE_FALSE(result.stopped);
  REQUIRE(result.exact);
  REQUIRE(result.bound == BoundType::exact);
  REQUIRE(result.nodes == result.stats.nodes);
  REQUIRE(result.stats.eval_calls == 0);
  REQUIRE(result.stats.leaf_nodes == 0);
  REQUIRE(result.stats.endgame_nodes == result.stats.nodes);
  require_replayable_pv(position, result.pv);

  if (result.best_move.has_value()) {
    board_core::MoveDelta delta{};
    REQUIRE(board_core::make_move_delta(position, *result.best_move, &delta));
  }

  for (const RootMoveInfo& root_move : result.root_moves) {
    REQUIRE(root_move.bound == BoundType::exact);
    REQUIRE(root_move.exact);
    REQUIRE_FALSE(root_move.selective);
    REQUIRE(root_move.pv.size > 0);
    REQUIRE(root_move.pv.moves[0] == root_move.move);
    require_replayable_pv(position, root_move.pv);
  }
}

Score reference_score_for_move(const SearchResult& reference, board_core::Move move) {
  for (const RootMoveInfo& root_move : reference.root_moves) {
    if (root_move.move == move) {
      return root_move.score;
    }
  }
  FAIL("production root move was missing from reference root moves");
  return 0;
}

void require_matches_reference(board_core::Position position) {
  const SearchResult reference = test_support::reference_exact_endgame(position);
  const SearchResult production = production_exact_endgame(position);

  require_exact_invariants(position, reference);
  require_exact_invariants(position, production);

  REQUIRE(production.score == reference.score);
  REQUIRE(production.completed_depth == reference.completed_depth);
  REQUIRE(production.root_moves.size() == reference.root_moves.size());

  if (production.best_move.has_value()) {
    REQUIRE(reference_score_for_move(reference, *production.best_move) == reference.score);
  } else {
    REQUIRE_FALSE(reference.best_move.has_value());
  }

  for (const RootMoveInfo& production_root_move : production.root_moves) {
    REQUIRE(reference_score_for_move(reference, production_root_move.move) ==
            production_root_move.score);
  }
}

TEST_CASE("reference exact endgame matches terminal and one-empty positions",
          "[search][endgame][reference]") {
  require_matches_reference(parse_position_or_fail(
      "BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/WWWWWWWW/WWWWWWWW/WWWWWWWW b"));
  require_matches_reference(parse_position_or_fail(
      "BBBBBBW./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b"));
  require_matches_reference(parse_position_or_fail(
      "BBBBBWB./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b"));
}

TEST_CASE("reference exact endgame matches deterministic small-empty positions",
          "[search][endgame][reference]") {
  require_matches_reference(test_support::generated_endgame_position(2));
  require_matches_reference(test_support::generated_endgame_position(3));

  const std::vector<test_support::EndgamePositionCase> cases =
      test_support::load_endgame_position_corpus(endgame_corpus_path());
  require_matches_reference(corpus_position(cases, "four_empty_simple"));
  require_matches_reference(corpus_position(cases, "six_empty_simple"));
}

} // namespace
} // namespace vibe_othello::search
