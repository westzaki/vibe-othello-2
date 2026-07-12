#include "../../src/search/search_internal.h"
#include "vibe_othello/board_core/board.h"

#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <initializer_list>

namespace vibe_othello::search::internal {
namespace {

class ConstantEvaluator final : public Evaluator {
public:
  explicit constexpr ConstantEvaluator(Score score) noexcept : score_(score) {}

  Score evaluate(const board_core::Position&) const noexcept override {
    return score_;
  }

private:
  Score score_;
};

class DiscDifferenceEvaluator final : public Evaluator {
public:
  Score evaluate(const board_core::Position& position) const noexcept override {
    return static_cast<Score>(std::popcount(position.player)) -
           static_cast<Score>(std::popcount(position.opponent));
  }
};

constexpr board_core::Square square(int file, int rank) noexcept {
  return board_core::square_from_file_rank(file, rank);
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

SearchNodeResult run_alphabeta(board_core::Position position, const Evaluator& evaluator,
                               Depth depth) {
  SearchContext context{
      .position_state = make_search_position(position),
      .evaluator = evaluator,
      .limits = SearchLimits{.max_depth = depth},
  };
  return alphabeta(&context, kScoreLoss, kScoreWin, depth, Ply{0});
}

SearchNodeResult run_null_window(board_core::Position position, const Evaluator& evaluator,
                                 Score beta, Depth depth) {
  SearchContext context{
      .position_state = make_search_position(position),
      .evaluator = evaluator,
      .limits = SearchLimits{.max_depth = depth},
  };
  return null_window_search(&context, beta, depth, Ply{0});
}

void require_null_window_bounds(board_core::Position position, Depth depth) {
  DiscDifferenceEvaluator exact_evaluator;
  const SearchNodeResult exact_result = run_alphabeta(position, exact_evaluator, depth);
  REQUIRE(exact_result.is_complete());
  const SearchValue& exact = exact_result.value();
  REQUIRE(exact.score > kScoreLoss);
  REQUIRE(exact.score < kScoreWin);

  DiscDifferenceEvaluator fail_high_evaluator;
  const SearchNodeResult fail_high_result =
      run_null_window(position, fail_high_evaluator, exact.score, depth);
  REQUIRE(fail_high_result.is_complete());
  const SearchValue& fail_high = fail_high_result.value();
  REQUIRE(fail_high.score >= exact.score);

  DiscDifferenceEvaluator fail_low_evaluator;
  const SearchNodeResult fail_low_result =
      run_null_window(position, fail_low_evaluator, static_cast<Score>(exact.score + 1), depth);
  REQUIRE(fail_low_result.is_complete());
  const SearchValue& fail_low = fail_low_result.value();
  REQUIRE(fail_low.score < static_cast<Score>(exact.score + 1));
}

TEST_CASE("null-window search reports fail-high and fail-low bounds", "[search][null_window]") {
  constexpr board_core::Position pass_position{
      .player = board_core::bit(square(1, 0)),
      .opponent = board_core::bit(square(0, 0)),
      .side_to_move = board_core::Color::black,
  };
  constexpr board_core::Bitboard terminal_player = (board_core::Bitboard{1} << 40) - 1;
  constexpr board_core::Position terminal{
      .player = terminal_player,
      .opponent = ~terminal_player,
      .side_to_move = board_core::Color::black,
  };

  require_null_window_bounds(board_core::initial_position(), Depth{2});
  require_null_window_bounds(position_after_fixed_choices({0, 1, 2, 3, 1, 0, 2, 1}), Depth{3});
  require_null_window_bounds(pass_position, Depth{2});
  require_null_window_bounds(terminal, Depth{5});
}

TEST_CASE("null-window search handles depth zero", "[search][null_window]") {
  ConstantEvaluator exact_evaluator{13};
  const SearchNodeResult exact_result =
      run_alphabeta(board_core::initial_position(), exact_evaluator, Depth{0});
  REQUIRE(exact_result.is_complete());
  const SearchValue& exact = exact_result.value();
  REQUIRE(exact.score == 13);

  ConstantEvaluator fail_high_evaluator{13};
  const SearchNodeResult fail_high_result =
      run_null_window(board_core::initial_position(), fail_high_evaluator, Score{13}, Depth{0});
  REQUIRE(fail_high_result.is_complete());
  const SearchValue& fail_high = fail_high_result.value();
  REQUIRE(fail_high.score >= 13);

  ConstantEvaluator fail_low_evaluator{13};
  const SearchNodeResult fail_low_result =
      run_null_window(board_core::initial_position(), fail_low_evaluator, Score{14}, Depth{0});
  REQUIRE(fail_low_result.is_complete());
  const SearchValue& fail_low = fail_low_result.value();
  REQUIRE(fail_low.score < 14);
}

} // namespace
} // namespace vibe_othello::search::internal
