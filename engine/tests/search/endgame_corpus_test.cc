#include "search/endgame_positions.h"
#include "search/reference_endgame.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/search/search.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#ifndef VIBE_OTHELLO_SOURCE_DIR
#error "VIBE_OTHELLO_SOURCE_DIR must be defined for endgame corpus tests"
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

std::vector<test_support::EndgamePositionCase> load_endgame_corpus() {
  const std::string path =
      std::string{VIBE_OTHELLO_SOURCE_DIR} + "/engine/testdata/endgame/positions.tsv";
  std::vector<test_support::EndgamePositionCase> cases =
      test_support::load_endgame_position_corpus(path);
  REQUIRE_FALSE(cases.empty());
  return cases;
}

SearchResult production_exact_endgame(board_core::Position position) {
  CountingEvaluator evaluator;
  const std::uint8_t empties = test_support::endgame_empty_count(position);
  const SearchResult result =
      search_iterative(position, evaluator, SearchLimits{.max_depth = Depth{0}},
                       SearchOptions{.exact_endgame = true, .endgame_exact_empties = empties});
  REQUIRE(evaluator.calls == 0);
  return result;
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

void require_matches_reference(const test_support::EndgamePositionCase& corpus_case) {
  INFO("position_id: " << corpus_case.id);
  const board_core::Position position = corpus_case.position;
  REQUIRE(test_support::endgame_empty_count(position) == corpus_case.expected_empties);

  const SearchResult reference = test_support::reference_exact_endgame(position);
  const SearchResult production = production_exact_endgame(position);

  REQUIRE(production.score == reference.score);
  REQUIRE(production.completed_depth == reference.completed_depth);
  REQUIRE(production.root_moves.size() == reference.root_moves.size());
  REQUIRE(production.best_move.has_value() == reference.best_move.has_value());
  if (production.best_move.has_value()) {
    REQUIRE(root_score_for_move(reference, *production.best_move) == reference.score);
  }
  for (const RootMoveInfo& root_move : production.root_moves) {
    REQUIRE(root_score_for_move(reference, root_move.move) == root_move.score);
  }
}

TEST_CASE("checked-in exact endgame corpus matches reference solver", "[search][endgame][corpus]") {
  for (const test_support::EndgamePositionCase& corpus_case : load_endgame_corpus()) {
    require_matches_reference(corpus_case);
  }
}

} // namespace
} // namespace vibe_othello::search
