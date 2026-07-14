#include "../../src/search/search_internal.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/search/search.h"

#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>

namespace vibe_othello::search {
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

class RootMoveScoreEvaluator final : public Evaluator {
public:
  RootMoveScoreEvaluator(board_core::Square first, Score first_score, board_core::Square second,
                         Score second_score) noexcept
      : first_(first), first_score_(first_score), second_(second), second_score_(second_score) {}

  Score evaluate(const board_core::Position& position) const noexcept override {
    const board_core::Bitboard occupied = position.player | position.opponent;
    if ((occupied & board_core::bit(first_)) != 0) {
      return first_score_;
    }
    if ((occupied & board_core::bit(second_)) != 0) {
      return second_score_;
    }
    return 0;
  }

private:
  board_core::Square first_{};
  Score first_score_ = 0;
  board_core::Square second_{};
  Score second_score_ = 0;
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

std::pair<board_core::Move, board_core::Move>
smallest_and_largest_legal_moves(board_core::Position position) {
  const board_core::Bitboard legal = board_core::legal_moves(position);
  REQUIRE(std::popcount(legal) >= 2);
  const int smallest = static_cast<int>(std::countr_zero(legal));
  const int largest = 63 - static_cast<int>(std::countl_zero(legal));
  return {board_core::make_move(board_core::square_from_index(smallest)),
          board_core::make_move(board_core::square_from_index(largest))};
}

void require_replayable_pv(board_core::Position position, Line pv) {
  for (std::uint8_t index = 0; index < pv.size; ++index) {
    board_core::MoveDelta delta{};
    REQUIRE(board_core::apply_move(&position, pv.moves[index], &delta));
  }
}

void require_root_move_scores_match(const SearchResult& actual, const SearchResult& expected) {
  REQUIRE(actual.root_moves.size() == expected.root_moves.size());
  for (const RootMoveInfo& expected_root_move : expected.root_moves) {
    bool found = false;
    for (const RootMoveInfo& actual_root_move : actual.root_moves) {
      if (actual_root_move.move != expected_root_move.move) {
        continue;
      }
      REQUIRE(actual_root_move.score == expected_root_move.score);
      REQUIRE(actual_root_move.bound == expected_root_move.bound);
      REQUIRE(actual_root_move.depth == expected_root_move.depth);
      REQUIRE(actual_root_move.exact == expected_root_move.exact);
      REQUIRE(actual_root_move.selective == expected_root_move.selective);
      found = true;
      break;
    }
    REQUIRE(found);
  }
}

void require_same_decision(const SearchResult& actual, const SearchResult& expected,
                           board_core::Position position) {
  REQUIRE(actual.best_move == expected.best_move);
  REQUIRE(actual.score == expected.score);
  REQUIRE(actual.bound == expected.bound);
  REQUIRE(actual.completed_depth == expected.completed_depth);
  REQUIRE(actual.exact == expected.exact);
  REQUIRE(actual.stopped == expected.stopped);
  require_root_move_scores_match(actual, expected);
  require_replayable_pv(position, actual.pv);
  for (const RootMoveInfo& root_move : actual.root_moves) {
    require_replayable_pv(position, root_move.pv);
  }
}

SearchResult search_fixed_depth_with_options(board_core::Position position,
                                             const Evaluator& evaluator, Depth depth,
                                             SearchOptions options) {
  return internal::search_fixed_depth_with_hint(position, evaluator, depth,
                                                internal::MoveOrderingHints{}, options, nullptr);
}

void require_fixed_depth_pvs_matches_alphabeta(board_core::Position position, Depth depth) {
  DiscDifferenceEvaluator pvs_evaluator;
  DiscDifferenceEvaluator alphabeta_evaluator;

  const SearchResult actual = search_fixed_depth_with_options(
      position, pvs_evaluator, depth,
      SearchOptions{.midgame = MidgameSearchOptions{.use_pvs = true}});
  const SearchResult expected =
      search_fixed_depth_with_options(position, alphabeta_evaluator, depth, SearchOptions{});

  require_same_decision(actual, expected, position);
}

void require_iterative_pvs_matches_alphabeta(board_core::Position position,
                                             SearchOptions pvs_options,
                                             SearchOptions alphabeta_options) {
  DiscDifferenceEvaluator pvs_evaluator;
  DiscDifferenceEvaluator alphabeta_evaluator;

  const SearchResult actual =
      search_iterative(position, pvs_evaluator, SearchLimits{.max_depth = Depth{4}}, pvs_options);
  const SearchResult expected = search_iterative(
      position, alphabeta_evaluator, SearchLimits{.max_depth = Depth{4}}, alphabeta_options);

  require_same_decision(actual, expected, position);
}

TEST_CASE("PVS fixed-depth search matches alpha-beta decisions", "[search][pvs]") {
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

  require_fixed_depth_pvs_matches_alphabeta(board_core::initial_position(), Depth{4});
  require_fixed_depth_pvs_matches_alphabeta(position_after_fixed_choices({0, 1, 2, 3, 1, 0, 2, 1}),
                                            Depth{4});
  require_fixed_depth_pvs_matches_alphabeta(pass_position, Depth{3});
  require_fixed_depth_pvs_matches_alphabeta(terminal, Depth{5});
}

TEST_CASE("PVS fixed-depth search handles depth zero", "[search][pvs]") {
  ConstantEvaluator pvs_evaluator{19};
  ConstantEvaluator alphabeta_evaluator{19};

  const SearchResult actual = search_fixed_depth_with_options(
      board_core::initial_position(), pvs_evaluator, Depth{0},
      SearchOptions{.midgame = MidgameSearchOptions{.use_pvs = true}});
  const SearchResult expected = search_fixed_depth_with_options(
      board_core::initial_position(), alphabeta_evaluator, Depth{0}, SearchOptions{});

  require_same_decision(actual, expected, board_core::initial_position());
  REQUIRE(actual.nodes == expected.nodes);
  REQUIRE(actual.stats == expected.stats);
}

TEST_CASE("PVS iterative search matches alpha-beta decisions", "[search][pvs]") {
  require_iterative_pvs_matches_alphabeta(
      board_core::initial_position(),
      SearchOptions{.midgame = MidgameSearchOptions{.use_pvs = true}}, SearchOptions{});
  require_iterative_pvs_matches_alphabeta(
      position_after_fixed_choices({3, 2, 1, 0, 2, 3, 0, 1, 2, 0}),
      SearchOptions{.midgame = MidgameSearchOptions{.use_pvs = true}}, SearchOptions{});
}

TEST_CASE("root PVS does not replace an exact best move with an upper-bound tie",
          "[search][pvs][tt]") {
  const board_core::Position position = board_core::initial_position();
  const auto [smaller_move, larger_move] = smallest_and_largest_legal_moves(position);
  RootMoveScoreEvaluator evaluator{larger_move.square, 10, smaller_move.square, 5};
  internal::TranspositionTable tt{1024};
  board_core::Position smaller_child = position;
  board_core::MoveDelta delta{};
  REQUIRE(board_core::apply_move(&smaller_child, smaller_move, &delta));
  SearchStats store_stats{};
  tt.store_value(board_core::hash_position(smaller_child), Depth{1}, Score{-10}, BoundType::lower,
                 internal::TTEntryKind::midgame, &store_stats);

  SearchOptions options{};
  options.midgame.use_pvs = true;
  options.midgame.use_midgame_tt = true;
  options.reporting.multi_pv = 1;
  const SearchResult result = internal::search_fixed_depth_with_hint(
      position, evaluator, Depth{2}, internal::MoveOrderingHints{.root_best_move = larger_move},
      options, &tt);

  REQUIRE(result.best_move == larger_move);
  REQUIRE(result.score == 10);
  const auto smaller = std::find_if(
      result.root_moves.begin(), result.root_moves.end(),
      [smaller_move](const RootMoveInfo& root_move) { return root_move.move == smaller_move; });
  REQUIRE(smaller != result.root_moves.end());
  REQUIRE(smaller->score == 5);
  REQUIRE(smaller->bound == BoundType::upper);
}

TEST_CASE("root PVS move nodes include null-window and full re-search work",
          "[search][pvs][stats]") {
  const board_core::Position position = board_core::initial_position();
  const auto [smaller_move, larger_move] = smallest_and_largest_legal_moves(position);
  RootMoveScoreEvaluator evaluator{larger_move.square, 10, smaller_move.square, 11};
  SearchOptions options{};
  options.midgame.use_pvs = true;
  options.reporting.multi_pv = 1;
  const SearchResult result = internal::search_fixed_depth_with_hint(
      position, evaluator, Depth{2}, internal::MoveOrderingHints{.root_best_move = larger_move},
      options, nullptr);

  REQUIRE(result.stats.pvs_researches > 0);
  NodeCount root_move_nodes = 0;
  for (const RootMoveInfo& root_move : result.root_moves) {
    root_move_nodes += root_move.nodes;
  }
  REQUIRE(result.nodes == root_move_nodes + 1);
}

TEST_CASE("PVS preserves iterative TT option decisions", "[search][pvs]") {
  const board_core::Position position = position_after_fixed_choices({0, 1, 2, 3, 1, 0, 2, 1});

  require_iterative_pvs_matches_alphabeta(
      position,
      SearchOptions{.midgame = MidgameSearchOptions{.use_pvs = true, .use_midgame_tt = true}},
      SearchOptions{.midgame = MidgameSearchOptions{.use_midgame_tt = true}});
  require_iterative_pvs_matches_alphabeta(
      position,
      SearchOptions{.midgame = MidgameSearchOptions{.use_pvs = true},
                    .ordering = MoveOrderingOptions{.use_tt_best_move_ordering = true}},
      SearchOptions{.ordering = MoveOrderingOptions{.use_tt_best_move_ordering = true}});
  require_iterative_pvs_matches_alphabeta(position,
                                          SearchOptions{
                                              .midgame =
                                                  MidgameSearchOptions{
                                                      .use_pvs = true,
                                                      .use_midgame_tt = true,
                                                  },
                                              .ordering =
                                                  MoveOrderingOptions{
                                                      .use_tt_best_move_ordering = true,
                                                  },
                                          },
                                          SearchOptions{
                                              .midgame =
                                                  MidgameSearchOptions{
                                                      .use_midgame_tt = true,
                                                  },
                                              .ordering =
                                                  MoveOrderingOptions{
                                                      .use_tt_best_move_ordering = true,
                                                  },
                                          });
}

} // namespace
} // namespace vibe_othello::search
