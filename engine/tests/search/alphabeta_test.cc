#include "search/reference_search.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/search/search.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>

namespace vibe_othello::search {
namespace {

class ConstantEvaluator final : public Evaluator {
public:
  explicit constexpr ConstantEvaluator(Score score) noexcept : score_(score) {}

  Score evaluate(const board_core::Position&) const noexcept override {
    ++calls;
    return score_;
  }

  mutable int calls = 0;

private:
  Score score_;
};

class DiscDifferenceEvaluator final : public Evaluator {
public:
  Score evaluate(const board_core::Position& position) const noexcept override {
    ++calls;
    return static_cast<Score>(std::popcount(position.player)) -
           static_cast<Score>(std::popcount(position.opponent));
  }

  mutable int calls = 0;
};

constexpr board_core::Square square(int file, int rank) noexcept {
  return board_core::square_from_file_rank(file, rank);
}

void require_replayable_pv(board_core::Position position, Line pv) {
  for (std::uint8_t index = 0; index < pv.size; ++index) {
    board_core::MoveDelta delta{};
    REQUIRE(board_core::apply_move(&position, pv.moves[index], &delta));
  }
}

void require_root_moves_match(const SearchResult& actual, const SearchResult& expected) {
  REQUIRE(actual.root_moves.size() == expected.root_moves.size());
  for (std::size_t index = 0; index < actual.root_moves.size(); ++index) {
    REQUIRE(actual.root_moves[index].move == expected.root_moves[index].move);
    REQUIRE(actual.root_moves[index].score == expected.root_moves[index].score);
    REQUIRE(actual.root_moves[index].bound == BoundType::exact);
    REQUIRE_FALSE(actual.root_moves[index].exact);
    REQUIRE_FALSE(actual.root_moves[index].selective);
  }
}

TEST_CASE("alpha-beta depth zero matches reference negamax", "[search][alphabeta]") {
  ConstantEvaluator actual_evaluator{11};
  ConstantEvaluator expected_evaluator{11};

  const SearchResult actual =
      search_fixed_depth(board_core::initial_position(), actual_evaluator, Depth{0});
  const SearchResult expected = test_support::reference_negamax_fixed_depth(
      board_core::initial_position(), expected_evaluator, Depth{0});

  REQUIRE(actual.best_move == expected.best_move);
  REQUIRE(actual.score == expected.score);
  REQUIRE(actual.completed_depth == expected.completed_depth);
  REQUIRE(actual.nodes == expected.nodes);
  REQUIRE(actual.pv == expected.pv);
  require_root_moves_match(actual, expected);
}

TEST_CASE("alpha-beta fixed-depth results match reference negamax", "[search][alphabeta]") {
  DiscDifferenceEvaluator actual_evaluator;
  DiscDifferenceEvaluator expected_evaluator;

  for (Depth depth = 1; depth <= 4; ++depth) {
    const SearchResult actual =
        search_fixed_depth(board_core::initial_position(), actual_evaluator, depth);
    const SearchResult expected = test_support::reference_negamax_fixed_depth(
        board_core::initial_position(), expected_evaluator, depth);

    REQUIRE(actual.best_move == expected.best_move);
    REQUIRE(actual.score == expected.score);
    REQUIRE(actual.completed_depth == expected.completed_depth);
    REQUIRE(actual.pv.size > 0);
    require_replayable_pv(board_core::initial_position(), actual.pv);
    require_root_moves_match(actual, expected);
    REQUIRE(actual.nodes <= expected.nodes);
  }
}

TEST_CASE("alpha-beta handles root pass like reference negamax", "[search][alphabeta]") {
  constexpr board_core::Position pass_position{
      .player = board_core::bit(square(1, 0)),
      .opponent = board_core::bit(square(0, 0)),
      .side_to_move = board_core::Color::black,
  };
  ConstantEvaluator actual_evaluator{7};
  ConstantEvaluator expected_evaluator{7};

  const SearchResult actual = search_fixed_depth(pass_position, actual_evaluator, Depth{3});
  const SearchResult expected =
      test_support::reference_negamax_fixed_depth(pass_position, expected_evaluator, Depth{3});

  REQUIRE(actual.best_move == expected.best_move);
  REQUIRE(actual.score == expected.score);
  REQUIRE(actual.completed_depth == expected.completed_depth);
  REQUIRE(actual.pv == expected.pv);
  require_root_moves_match(actual, expected);
  REQUIRE(actual.nodes <= expected.nodes);
}

TEST_CASE("alpha-beta terminal root matches reference negamax", "[search][alphabeta]") {
  constexpr board_core::Bitboard player = (board_core::Bitboard{1} << 40) - 1;
  constexpr board_core::Position terminal{
      .player = player,
      .opponent = ~player,
      .side_to_move = board_core::Color::black,
  };
  ConstantEvaluator actual_evaluator{99};
  ConstantEvaluator expected_evaluator{99};

  const SearchResult actual = search_fixed_depth(terminal, actual_evaluator, Depth{5});
  const SearchResult expected =
      test_support::reference_negamax_fixed_depth(terminal, expected_evaluator, Depth{5});

  REQUIRE(actual.best_move == expected.best_move);
  REQUIRE(actual.score == expected.score);
  REQUIRE(actual.completed_depth == expected.completed_depth);
  REQUIRE(actual.nodes == expected.nodes);
  REQUIRE(actual.pv == expected.pv);
  REQUIRE(actual.exact);
}

TEST_CASE("alpha-beta search result is deterministic for repeated calls", "[search][alphabeta]") {
  ConstantEvaluator evaluator{3};

  const SearchResult first =
      search_fixed_depth(board_core::initial_position(), evaluator, Depth{3});
  const SearchResult second =
      search_fixed_depth(board_core::initial_position(), evaluator, Depth{3});

  REQUIRE(first.best_move == second.best_move);
  REQUIRE(first.score == second.score);
  REQUIRE(first.completed_depth == second.completed_depth);
  REQUIRE(first.pv == second.pv);
  require_root_moves_match(first, second);
}

} // namespace
} // namespace vibe_othello::search
