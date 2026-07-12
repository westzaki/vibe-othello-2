#include "../../src/search/search_internal.h"
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

  const SearchResult actual = search_fixed_depth_with_options(position, pvs_evaluator, depth,
                                                              SearchOptions{.use_pvs = true});
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
      board_core::initial_position(), pvs_evaluator, Depth{0}, SearchOptions{.use_pvs = true});
  const SearchResult expected = search_fixed_depth_with_options(
      board_core::initial_position(), alphabeta_evaluator, Depth{0}, SearchOptions{});

  require_same_decision(actual, expected, board_core::initial_position());
  REQUIRE(actual.nodes == expected.nodes);
  REQUIRE(actual.stats == expected.stats);
}

TEST_CASE("PVS iterative search matches alpha-beta decisions", "[search][pvs]") {
  require_iterative_pvs_matches_alphabeta(board_core::initial_position(),
                                          SearchOptions{.use_pvs = true}, SearchOptions{});
  require_iterative_pvs_matches_alphabeta(
      position_after_fixed_choices({3, 2, 1, 0, 2, 3, 0, 1, 2, 0}), SearchOptions{.use_pvs = true},
      SearchOptions{});
}

TEST_CASE("legacy root kernel rollback switch preserves score and legal PV", "[search][pvs]") {
  DiscDifferenceEvaluator new_evaluator;
  DiscDifferenceEvaluator legacy_evaluator;
  SearchOptions new_options{};
  new_options.midgame.use_pvs = true;
  new_options.reporting.multi_pv = 1;
  SearchOptions legacy_options = new_options;
  legacy_options.experimental.use_legacy_search_kernel = true;

  const SearchResult current = search_iterative(board_core::initial_position(), new_evaluator,
                                                SearchLimits{.max_depth = Depth{5}}, new_options);
  const SearchResult legacy = search_iterative(board_core::initial_position(), legacy_evaluator,
                                               SearchLimits{.max_depth = Depth{5}}, legacy_options);

  REQUIRE(current.score == legacy.score);
  REQUIRE(current.best_move == legacy.best_move);
  require_replayable_pv(board_core::initial_position(), current.pv);
  require_replayable_pv(board_core::initial_position(), legacy.pv);
}

TEST_CASE("PVS preserves iterative TT option decisions", "[search][pvs]") {
  const board_core::Position position = position_after_fixed_choices({0, 1, 2, 3, 1, 0, 2, 1});

  require_iterative_pvs_matches_alphabeta(position,
                                          SearchOptions{.use_pvs = true, .use_midgame_tt = true},
                                          SearchOptions{.use_midgame_tt = true});
  require_iterative_pvs_matches_alphabeta(
      position, SearchOptions{.use_pvs = true, .use_tt_best_move_ordering = true},
      SearchOptions{.use_tt_best_move_ordering = true});
  require_iterative_pvs_matches_alphabeta(position,
                                          SearchOptions{
                                              .use_pvs = true,
                                              .use_midgame_tt = true,
                                              .use_tt_best_move_ordering = true,
                                          },
                                          SearchOptions{
                                              .use_midgame_tt = true,
                                              .use_tt_best_move_ordering = true,
                                          });
}

} // namespace
} // namespace vibe_othello::search
