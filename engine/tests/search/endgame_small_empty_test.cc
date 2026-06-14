#include "../../src/search/search_internal.h"
#include "search/endgame_positions.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/serialization.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string_view>

namespace vibe_othello::search::internal {
namespace {

board_core::Position parse_position_or_fail(std::string_view text) {
  const std::optional<board_core::Position> position = board_core::parse_position(text);
  REQUIRE(position.has_value());
  return *position;
}

SearchResult solve_with_policy(board_core::Position position, SmallEndgamePolicy policy) {
  const std::uint8_t empties = empty_count(position);
  return solve_exact_endgame_with_small_endgame_policy(
      position, SearchLimits{.max_depth = Depth{0}},
      SearchOptions{.exact_endgame = true, .endgame_exact_empties = empties}, nullptr, policy);
}

void require_replayable_pv(board_core::Position position, Line pv) {
  for (std::uint8_t index = 0; index < pv.size; ++index) {
    board_core::MoveDelta delta{};
    REQUIRE(board_core::apply_move(&position, pv.moves[index], &delta));
  }
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

void require_exact_result(board_core::Position position, const SearchResult& result) {
  REQUIRE_FALSE(result.stopped);
  REQUIRE(result.exact);
  REQUIRE(result.bound == BoundType::exact);
  REQUIRE(result.nodes == result.stats.nodes);
  REQUIRE(result.stats.endgame_nodes == result.stats.nodes);
  REQUIRE(result.stats.eval_calls == 0);
  REQUIRE(result.stats.leaf_nodes == 0);
  require_replayable_pv(position, result.pv);

  for (const RootMoveInfo& root_move : result.root_moves) {
    REQUIRE(root_move.bound == BoundType::exact);
    REQUIRE(root_move.exact);
    REQUIRE_FALSE(root_move.selective);
    REQUIRE(root_move.pv.size > 0);
    REQUIRE(root_move.pv.moves[0] == root_move.move);
    require_replayable_pv(position, root_move.pv);
  }
}

void require_specialized_matches_generic(board_core::Position position) {
  const SearchResult specialized = solve_with_policy(position, SmallEndgamePolicy::enabled);
  const SearchResult generic = solve_with_policy(position, SmallEndgamePolicy::generic_only);

  require_exact_result(position, specialized);
  require_exact_result(position, generic);

  REQUIRE(specialized.score == generic.score);
  REQUIRE(specialized.completed_depth == generic.completed_depth);
  REQUIRE(specialized.best_move.has_value() == generic.best_move.has_value());
  if (specialized.best_move.has_value()) {
    REQUIRE(root_score_for_move(generic, *specialized.best_move) == generic.score);
  }
  REQUIRE(specialized.root_moves.size() == generic.root_moves.size());
  for (const RootMoveInfo& specialized_root_move : specialized.root_moves) {
    REQUIRE(root_score_for_move(generic, specialized_root_move.move) ==
            specialized_root_move.score);
  }
}

TEST_CASE("small-empty exact routines match generic terminal positions",
          "[search][endgame][small_empty]") {
  require_specialized_matches_generic(parse_position_or_fail(
      "BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/WWWWWWWW/WWWWWWWW/WWWWWWWW b"));
  require_specialized_matches_generic(parse_position_or_fail(
      "BBBBBBB./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b"));
}

TEST_CASE("small-empty exact routines match generic one-empty forced move and pass",
          "[search][endgame][small_empty]") {
  require_specialized_matches_generic(parse_position_or_fail(
      "BBBBBBW./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b"));
  require_specialized_matches_generic(parse_position_or_fail(
      "BBBBBWB./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b"));
}

TEST_CASE("small-empty exact routines match generic generated positions",
          "[search][endgame][small_empty]") {
  require_specialized_matches_generic(test_support::generated_endgame_position(2));
  require_specialized_matches_generic(test_support::generated_endgame_position(3));
}

} // namespace
} // namespace vibe_othello::search::internal
