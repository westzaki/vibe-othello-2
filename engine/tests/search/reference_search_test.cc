#include "search/reference_search.h"
#include "vibe_othello/board_core/board.h"

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

bool is_legal_root_move(board_core::Position position, board_core::Move move) {
  if (move.kind == board_core::MoveKind::pass) {
    board_core::MoveDelta delta{};
    return board_core::make_move_delta(position, move, &delta);
  }

  return (board_core::legal_moves(position) & board_core::bit(move.square)) != 0;
}

void require_replayable_pv(board_core::Position position, Line pv) {
  for (std::uint8_t index = 0; index < pv.size; ++index) {
    board_core::MoveDelta delta{};
    REQUIRE(board_core::apply_move(&position, pv.moves[index], &delta));
  }
}

TEST_CASE("reference depth zero evaluates the root without choosing a move",
          "[search][reference]") {
  ConstantEvaluator evaluator{17};

  const SearchResult result = test_support::reference_negamax_fixed_depth(
      board_core::initial_position(), evaluator, Depth{0});

  REQUIRE_FALSE(result.best_move.has_value());
  REQUIRE(result.score == 17);
  REQUIRE(result.completed_depth == 0);
  REQUIRE(result.nodes == 1);
  REQUIRE(result.pv.size == 0);
  REQUIRE(result.root_moves.empty());
  REQUIRE(evaluator.calls == 1);
  REQUIRE_FALSE(result.exact);
  REQUIRE_FALSE(result.stopped);
}

TEST_CASE("reference initial position returns a legal deterministic best move",
          "[search][reference]") {
  ConstantEvaluator evaluator{0};
  constexpr board_core::Move expected = board_core::make_move(square(3, 2));

  const SearchResult result = test_support::reference_negamax_fixed_depth(
      board_core::initial_position(), evaluator, Depth{1});

  REQUIRE(result.best_move.has_value());
  REQUIRE(*result.best_move == expected);
  REQUIRE(is_legal_root_move(board_core::initial_position(), *result.best_move));
  REQUIRE(result.score == 0);
  REQUIRE(result.completed_depth == 1);
  REQUIRE(result.root_moves.size() == 4);
  REQUIRE(result.pv.size == 1);
  REQUIRE(result.pv.moves[0] == expected);
  REQUIRE_FALSE(result.exact);
  REQUIRE_FALSE(result.stopped);

  for (const RootMoveInfo& root_move : result.root_moves) {
    REQUIRE(is_legal_root_move(board_core::initial_position(), root_move.move));
    REQUIRE(root_move.bound == BoundType::exact);
    REQUIRE_FALSE(root_move.exact);
    REQUIRE_FALSE(root_move.selective);
  }
}

TEST_CASE("reference search result is deterministic for repeated calls", "[search][reference]") {
  ConstantEvaluator evaluator{3};

  const SearchResult first = test_support::reference_negamax_fixed_depth(
      board_core::initial_position(), evaluator, Depth{2});
  const SearchResult second = test_support::reference_negamax_fixed_depth(
      board_core::initial_position(), evaluator, Depth{2});

  REQUIRE(first.best_move == second.best_move);
  REQUIRE(first.score == second.score);
  REQUIRE(first.completed_depth == second.completed_depth);
  REQUIRE(first.pv == second.pv);
  REQUIRE(first.root_moves.size() == second.root_moves.size());
  for (std::size_t index = 0; index < first.root_moves.size(); ++index) {
    REQUIRE(first.root_moves[index].move == second.root_moves[index].move);
    REQUIRE(first.root_moves[index].score == second.root_moves[index].score);
    REQUIRE(first.root_moves[index].pv == second.root_moves[index].pv);
  }
}

TEST_CASE("reference principal variation can be replayed from the root", "[search][reference]") {
  DiscDifferenceEvaluator evaluator;

  const SearchResult result = test_support::reference_negamax_fixed_depth(
      board_core::initial_position(), evaluator, Depth{2});

  REQUIRE(result.best_move.has_value());
  REQUIRE(result.pv.size > 0);
  require_replayable_pv(board_core::initial_position(), result.pv);
}

TEST_CASE("reference terminal root returns exact disc difference and no move",
          "[search][reference]") {
  constexpr board_core::Bitboard player = (board_core::Bitboard{1} << 40) - 1;
  constexpr board_core::Position terminal{
      .player = player,
      .opponent = ~player,
      .side_to_move = board_core::Color::black,
  };
  ConstantEvaluator evaluator{99};

  const SearchResult result =
      test_support::reference_negamax_fixed_depth(terminal, evaluator, Depth{5});

  REQUIRE_FALSE(result.best_move.has_value());
  REQUIRE(result.score == 16);
  REQUIRE(result.completed_depth == 5);
  REQUIRE(result.nodes == 1);
  REQUIRE(result.pv.size == 0);
  REQUIRE(result.root_moves.empty());
  REQUIRE(result.exact);
  REQUIRE(evaluator.calls == 0);
}

TEST_CASE("reference root pass is searched as the only child", "[search][reference]") {
  constexpr board_core::Position pass_position{
      .player = board_core::bit(square(1, 0)),
      .opponent = board_core::bit(square(0, 0)),
      .side_to_move = board_core::Color::black,
  };
  ConstantEvaluator evaluator{7};

  REQUIRE_FALSE(board_core::has_legal_move(pass_position));
  REQUIRE_FALSE(board_core::is_terminal(pass_position));

  const SearchResult result =
      test_support::reference_negamax_fixed_depth(pass_position, evaluator, Depth{1});

  REQUIRE(result.best_move.has_value());
  REQUIRE(*result.best_move == board_core::make_pass());
  REQUIRE(result.score == -7);
  REQUIRE(result.completed_depth == 1);
  REQUIRE(result.root_moves.size() == 1);
  REQUIRE(result.root_moves[0].move == board_core::make_pass());
  REQUIRE(result.root_moves[0].score == -7);
  REQUIRE(result.pv.size == 1);
  REQUIRE(result.pv.moves[0] == board_core::make_pass());
  REQUIRE(evaluator.calls == 1);
}

} // namespace
} // namespace vibe_othello::search
