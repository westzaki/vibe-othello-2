#include "search/reference_search.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/search/search.h"

#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <initializer_list>

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

void require_replayable_root_pvs(board_core::Position position, const SearchResult& result) {
  for (const RootMoveInfo& root_move : result.root_moves) {
    REQUIRE(root_move.pv.size > 0);
    REQUIRE(root_move.pv.moves[0] == root_move.move);
    require_replayable_pv(position, root_move.pv);
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

void require_basic_stats_invariants(const SearchResult& result) {
  REQUIRE(result.nodes == result.stats.nodes);
  REQUIRE(result.stats.root_moves_searched == result.root_moves.size());
  REQUIRE(result.stats.leaf_nodes <= result.stats.nodes);
  REQUIRE(result.stats.eval_calls <= result.stats.leaf_nodes);
  REQUIRE(result.stats.terminal_nodes <= result.stats.nodes);
  REQUIRE(result.stats.pass_nodes <= result.stats.nodes);
  REQUIRE(result.stats.beta_cutoffs <= result.stats.nodes);
  REQUIRE(result.stats.alpha_updates <= result.stats.nodes);
}

board_core::Move select_legal_move(board_core::Position position, std::size_t choice) {
  const board_core::Bitboard legal_moves = board_core::legal_moves(position);
  REQUIRE(legal_moves != 0);

  std::array<board_core::Move, board_core::kSquareCount> moves{};
  std::size_t move_count = 0;
  for (int square_index = 0; square_index < board_core::kSquareCount; ++square_index) {
    const board_core::Square move_square = board_core::square_from_index(square_index);
    if ((legal_moves & board_core::bit(move_square)) != 0) {
      moves[move_count] = board_core::make_move(move_square);
      ++move_count;
    }
  }

  REQUIRE(move_count > 0);
  return moves[choice % move_count];
}

board_core::Position position_after_fixed_choices(std::initializer_list<std::size_t> choices) {
  board_core::Position position = board_core::initial_position();

  for (const std::size_t choice : choices) {
    const board_core::Move move = select_legal_move(position, choice);
    board_core::MoveDelta delta{};
    REQUIRE(board_core::apply_move(&position, move, &delta));
  }

  return position;
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
  require_basic_stats_invariants(actual);
  REQUIRE(actual.stats.leaf_nodes == 1);
  REQUIRE(actual.stats.eval_calls == 1);
  REQUIRE(actual.stats.terminal_nodes == 0);
  REQUIRE(actual.stats.pass_nodes == 0);
  REQUIRE(actual.stats.beta_cutoffs == 0);
  REQUIRE(actual.stats.root_moves_searched == 0);
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
    require_basic_stats_invariants(actual);
    require_replayable_pv(board_core::initial_position(), actual.pv);
    require_replayable_root_pvs(board_core::initial_position(), actual);
    require_root_moves_match(actual, expected);
    REQUIRE(actual.nodes <= expected.nodes);
  }
}

TEST_CASE("alpha-beta matches reference negamax on fixed midgame positions",
          "[search][alphabeta]") {
  const std::array<board_core::Position, 3> positions{
      position_after_fixed_choices({0, 1, 2, 3, 1, 0, 2, 1}),
      position_after_fixed_choices({3, 2, 1, 0, 2, 3, 0, 1, 2, 0}),
      position_after_fixed_choices({1, 1, 0, 2, 2, 0, 3, 1, 0, 2, 1, 3}),
  };

  for (const board_core::Position position : positions) {
    REQUIRE(position != board_core::initial_position());

    for (Depth depth = 1; depth <= 4; ++depth) {
      DiscDifferenceEvaluator actual_evaluator;
      DiscDifferenceEvaluator expected_evaluator;

      const SearchResult actual = search_fixed_depth(position, actual_evaluator, depth);
      const SearchResult expected =
          test_support::reference_negamax_fixed_depth(position, expected_evaluator, depth);

      REQUIRE(actual.best_move == expected.best_move);
      REQUIRE(actual.score == expected.score);
      REQUIRE(actual.completed_depth == expected.completed_depth);
      REQUIRE(actual.pv == expected.pv);
      require_basic_stats_invariants(actual);
      require_replayable_pv(position, actual.pv);
      require_replayable_root_pvs(position, actual);
      require_root_moves_match(actual, expected);
      REQUIRE(actual.nodes <= expected.nodes);
    }
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
  require_basic_stats_invariants(actual);
  REQUIRE(actual.stats.pass_nodes >= 1);
  REQUIRE(actual.stats.root_moves_searched == 1);
  require_replayable_root_pvs(pass_position, actual);
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
  require_basic_stats_invariants(actual);
  REQUIRE(actual.stats.terminal_nodes == 1);
  REQUIRE(actual.stats.leaf_nodes == 0);
  REQUIRE(actual.stats.eval_calls == 0);
  REQUIRE(actual.stats.root_moves_searched == 0);
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
