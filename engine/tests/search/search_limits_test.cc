#include "vibe_othello/board_core/board.h"
#include "vibe_othello/search/search.h"

#include <array>
#include <atomic>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <thread>

namespace vibe_othello::search {
namespace {

class DiscDifferenceEvaluator final : public Evaluator {
public:
  Score evaluate(const board_core::Position& position) const noexcept override {
    ++calls;
    return static_cast<Score>(std::popcount(position.player)) -
           static_cast<Score>(std::popcount(position.opponent));
  }

  mutable int calls = 0;
};

Score terminal_score(board_core::Position position) noexcept {
  return static_cast<Score>(std::popcount(position.player)) -
         static_cast<Score>(std::popcount(position.opponent));
}

class StopAfterCallsEvaluator final : public Evaluator {
public:
  StopAfterCallsEvaluator(std::atomic_bool* stop_requested, int call_limit) noexcept
      : stop_requested_(stop_requested), call_limit_(call_limit) {}

  Score evaluate(const board_core::Position& position) const noexcept override {
    const int call = ++calls;
    if (call >= call_limit_) {
      stop_requested_->store(true, std::memory_order_release);
    }
    return static_cast<Score>(std::popcount(position.player)) -
           static_cast<Score>(std::popcount(position.opponent));
  }

  mutable int calls = 0;

private:
  std::atomic_bool* stop_requested_;
  int call_limit_;
};

class SlowEvaluator final : public Evaluator {
public:
  Score evaluate(const board_core::Position& position) const noexcept override {
    ++calls;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return static_cast<Score>(std::popcount(position.player)) -
           static_cast<Score>(std::popcount(position.opponent));
  }

  mutable int calls = 0;
};

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

void require_replayable_pv(board_core::Position position, Line pv) {
  for (std::uint8_t index = 0; index < pv.size; ++index) {
    board_core::MoveDelta delta{};
    REQUIRE(board_core::apply_move(&position, pv.moves[index], &delta));
  }
}

void require_legal_best_move_or_terminal(board_core::Position position,
                                         const SearchResult& result) {
  if (board_core::is_terminal(position)) {
    REQUIRE_FALSE(result.best_move.has_value());
    return;
  }

  REQUIRE(result.best_move.has_value());
  board_core::MoveDelta delta{};
  REQUIRE(board_core::make_move_delta(position, *result.best_move, &delta));
}

void require_basic_limit_invariants(board_core::Position position, const SearchResult& result) {
  REQUIRE(result.nodes == result.stats.nodes);
  require_legal_best_move_or_terminal(position, result);
  require_replayable_pv(position, result.pv);
  for (const RootMoveInfo& root_move : result.root_moves) {
    REQUIRE(root_move.pv.size > 0);
    REQUIRE(root_move.pv.moves[0] == root_move.move);
    require_replayable_pv(position, root_move.pv);
  }
}

NodeCount fixed_depth_node_sum(board_core::Position position, Depth max_depth) {
  NodeCount nodes = 0;
  for (Depth depth = 1; depth <= max_depth; ++depth) {
    DiscDifferenceEvaluator evaluator;
    nodes += search_fixed_depth(position, evaluator, depth).nodes;
  }
  return nodes;
}

TEST_CASE("iterative max depth behavior is unchanged with unlimited budgets", "[search][limits]") {
  const board_core::Position position = position_after_fixed_choices({0, 1, 2, 3, 1, 0, 2, 1});
  DiscDifferenceEvaluator actual_evaluator;
  DiscDifferenceEvaluator expected_evaluator;

  const SearchResult actual =
      search_iterative(position, actual_evaluator, SearchLimits{.max_depth = Depth{4}});
  const SearchResult expected =
      search_iterative(position, expected_evaluator, SearchLimits{.max_depth = Depth{4}});

  REQUIRE(actual.best_move == expected.best_move);
  REQUIRE(actual.score == expected.score);
  REQUIRE(actual.completed_depth == Depth{4});
  REQUIRE(actual.completed_depth == expected.completed_depth);
  REQUIRE_FALSE(actual.stopped);
  require_basic_limit_invariants(position, actual);
}

TEST_CASE("iterative max nodes stops deeper search and preserves completed depth",
          "[search][limits]") {
  const board_core::Position position = board_core::initial_position();
  const NodeCount depth_one_nodes = fixed_depth_node_sum(position, Depth{1});
  DiscDifferenceEvaluator evaluator;

  const SearchResult result =
      search_iterative(position, evaluator,
                       SearchLimits{.max_depth = Depth{5},
                                    .max_nodes = static_cast<NodeCount>(depth_one_nodes + 1)});

  REQUIRE(result.stopped);
  REQUIRE(result.completed_depth == Depth{1});
  REQUIRE(result.nodes <= depth_one_nodes + 1);
  for (const RootMoveInfo& root_move : result.root_moves) {
    REQUIRE(root_move.depth == Depth{1});
  }
  require_basic_limit_invariants(position, result);
}

TEST_CASE("stop requested before iterative search starts returns promptly", "[search][limits]") {
  std::atomic_bool stop_requested{true};
  DiscDifferenceEvaluator evaluator;

  const SearchResult result =
      search_iterative(board_core::initial_position(), evaluator,
                       SearchLimits{.max_depth = Depth{5}, .stop_requested = &stop_requested});

  REQUIRE(result.stopped);
  REQUIRE(result.completed_depth == Depth{0});
  REQUIRE(result.nodes == 0);
  REQUIRE(evaluator.calls == 0);
}

TEST_CASE("stop requested before terminal iterative search preserves exact score kind",
          "[search][limits]") {
  constexpr board_core::Bitboard player = (board_core::Bitboard{1} << 40) - 1;
  constexpr board_core::Position terminal{
      .player = player,
      .opponent = ~player,
      .side_to_move = board_core::Color::black,
  };
  std::atomic_bool stop_requested{true};
  DiscDifferenceEvaluator evaluator;

  const SearchResult result = search_iterative(
      terminal, evaluator, SearchLimits{.max_depth = Depth{5}, .stop_requested = &stop_requested});

  REQUIRE(result.stopped);
  REQUIRE(result.exact);
  REQUIRE_FALSE(result.best_move.has_value());
  REQUIRE(result.score == terminal_score(terminal));
  REQUIRE(result.score_kind == ScoreKind::exact_disc_diff);
  REQUIRE(result.nodes == 0);
  REQUIRE(evaluator.calls == 0);
}

TEST_CASE("stop requested during iterative search stops cooperatively", "[search][limits]") {
  std::atomic_bool stop_requested{false};
  StopAfterCallsEvaluator evaluator{&stop_requested, 6};

  const SearchResult result =
      search_iterative(board_core::initial_position(), evaluator,
                       SearchLimits{.max_depth = Depth{6}, .stop_requested = &stop_requested});

  REQUIRE(stop_requested.load(std::memory_order_acquire));
  REQUIRE(result.stopped);
  REQUIRE(result.nodes == result.stats.nodes);
  if (result.best_move.has_value()) {
    require_basic_limit_invariants(board_core::initial_position(), result);
  }
}

TEST_CASE("iterative max time stops with a slow evaluator", "[search][limits]") {
  SlowEvaluator evaluator;

  const SearchResult result = search_iterative(
      board_core::initial_position(), evaluator,
      SearchLimits{.max_depth = Depth{6}, .max_time = std::chrono::milliseconds{1}});

  REQUIRE(result.stopped);
  REQUIRE(result.nodes == result.stats.nodes);
}

TEST_CASE("interrupted iterative search does not publish incomplete depth", "[search][limits]") {
  const board_core::Position position = board_core::initial_position();
  const NodeCount depth_one_nodes = fixed_depth_node_sum(position, Depth{1});
  DiscDifferenceEvaluator limited_evaluator;
  DiscDifferenceEvaluator expected_evaluator;

  const SearchResult result =
      search_iterative(position, limited_evaluator,
                       SearchLimits{.max_depth = Depth{4},
                                    .max_nodes = static_cast<NodeCount>(depth_one_nodes + 1)});
  const SearchResult expected = search_fixed_depth(position, expected_evaluator, Depth{1});

  REQUIRE(result.stopped);
  REQUIRE(result.completed_depth == expected.completed_depth);
  REQUIRE(result.best_move == expected.best_move);
  REQUIRE(result.score == expected.score);
  REQUIRE(result.pv == expected.pv);
  for (const RootMoveInfo& root_move : result.root_moves) {
    REQUIRE(root_move.depth == expected.completed_depth);
  }
}

TEST_CASE("infinite iterative search ignores max depth until a budget stops it",
          "[search][limits]") {
  const board_core::Position position = board_core::initial_position();
  const NodeCount depth_two_nodes = fixed_depth_node_sum(position, Depth{2});
  DiscDifferenceEvaluator evaluator;

  const SearchResult result = search_iterative(position, evaluator,
                                               SearchLimits{
                                                   .max_depth = Depth{1},
                                                   .max_nodes = depth_two_nodes,
                                                   .infinite = true,
                                               });

  REQUIRE(result.stopped);
  REQUIRE(result.completed_depth == Depth{2});
  REQUIRE(result.completed_depth > Depth{1});
  require_basic_limit_invariants(position, result);
}

TEST_CASE("limited PVS and transposition table search keeps published PV replayable",
          "[search][limits][pvs]") {
  const board_core::Position position =
      position_after_fixed_choices({3, 2, 1, 0, 2, 3, 0, 1, 2, 0});
  const NodeCount depth_one_nodes = fixed_depth_node_sum(position, Depth{1});
  DiscDifferenceEvaluator evaluator;

  const SearchResult result =
      search_iterative(position, evaluator,
                       SearchLimits{
                           .max_depth = Depth{5},
                           .max_nodes = static_cast<NodeCount>(depth_one_nodes + 4),
                       },
                       SearchOptions{.use_pvs = true,
                                     .use_aspiration = true,
                                     .use_midgame_tt = true,
                                     .use_tt_best_move_ordering = true});

  REQUIRE(result.stopped);
  require_basic_limit_invariants(position, result);
}

} // namespace
} // namespace vibe_othello::search
