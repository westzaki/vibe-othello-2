#include "../../src/search/endgame_policy_internal.h"
#include "search/endgame_positions.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/serialization.h"

#include <array>
#include <bit>
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

SearchResult solve_with_policy(board_core::Position position, SmallEndgamePolicy policy,
                               bool use_endgame_tt, bool use_endgame_parity_ordering,
                               SearchLimits limits = SearchLimits{.max_depth = Depth{0}}) {
  const std::uint8_t empties = empty_count(position);
  TranspositionTable tt;
  return solve_exact_endgame_with_small_endgame_policy(
      position, limits,
      SearchOptions{.use_endgame_tt = use_endgame_tt,
                    .exact_endgame = true,
                    .use_endgame_parity_ordering = use_endgame_parity_ordering,
                    .endgame_exact_empties = empties},
      use_endgame_tt ? &tt : nullptr, policy);
}

board_core::Move select_legal_move(board_core::Position position, std::size_t choice) {
  const board_core::Bitboard legal_moves = board_core::legal_moves(position);
  if (legal_moves == 0) {
    return board_core::make_pass();
  }

  std::array<board_core::Move, board_core::kSquareCount> moves{};
  std::size_t move_count = 0;
  for (int square_index = 0; square_index < board_core::kSquareCount; ++square_index) {
    const board_core::Square square = board_core::square_from_index(square_index);
    if ((legal_moves & board_core::bit(square)) != 0) {
      moves[move_count] = board_core::make_move(square);
      ++move_count;
    }
  }

  REQUIRE(move_count > 0);
  return moves[choice % move_count];
}

board_core::Position deterministic_playout_position(std::uint8_t target_empties, std::size_t seed) {
  board_core::Position position = board_core::initial_position();
  std::size_t ply = 0;
  while (empty_count(position) > target_empties) {
    REQUIRE_FALSE(board_core::is_terminal(position));
    const board_core::Move move = select_legal_move(position, seed + ply * 17 + (ply % 5) * 7);
    board_core::MoveDelta delta{};
    REQUIRE(board_core::apply_move(&position, move, &delta));
    if (move.kind == board_core::MoveKind::normal) {
      ++ply;
    }
  }
  REQUIRE(empty_count(position) == target_empties);
  return position;
}

board_core::Position generated_forced_pass_position(std::uint8_t target_empties) {
  for (std::size_t seed = 0; seed < 10000; ++seed) {
    const board_core::Position position = deterministic_playout_position(target_empties, seed);
    if (!board_core::is_terminal(position) && !board_core::has_legal_move(position)) {
      return position;
    }
  }
  FAIL("could not generate forced-pass small-empty position");
  return board_core::initial_position();
}

board_core::Position generated_multi_move_position(std::uint8_t target_empties) {
  for (std::size_t seed = 0; seed < 10000; ++seed) {
    const board_core::Position position = deterministic_playout_position(target_empties, seed);
    if (std::popcount(board_core::legal_moves(position)) > 1) {
      return position;
    }
  }
  FAIL("could not generate multi-move small-empty position");
  return board_core::initial_position();
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

const RootMoveInfo& root_info_for_move(const SearchResult& result, board_core::Move move) {
  for (const RootMoveInfo& root_move : result.root_moves) {
    if (root_move.move == move) {
      return root_move;
    }
  }
  FAIL("root move was missing from comparison result");
  return result.root_moves.front();
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

void require_small_empty_path_matches_generic(board_core::Position position) {
  const SearchResult small_empty = solve_with_policy(position, SmallEndgamePolicy::enabled);
  const SearchResult generic = solve_with_policy(position, SmallEndgamePolicy::generic_only);

  require_exact_result(position, small_empty);
  require_exact_result(position, generic);

  REQUIRE(small_empty.score == generic.score);
  REQUIRE(small_empty.completed_depth == generic.completed_depth);
  REQUIRE(small_empty.best_move == generic.best_move);
  REQUIRE(small_empty.best_move.has_value() == generic.best_move.has_value());
  REQUIRE(small_empty.exact == generic.exact);
  REQUIRE(small_empty.stopped == generic.stopped);
  REQUIRE(small_empty.pv == generic.pv);
  if (small_empty.best_move.has_value()) {
    REQUIRE(root_score_for_move(generic, *small_empty.best_move) == generic.score);
  }
  REQUIRE(small_empty.root_moves.size() == generic.root_moves.size());
  for (const RootMoveInfo& small_empty_root_move : small_empty.root_moves) {
    const RootMoveInfo& generic_root_move = root_info_for_move(generic, small_empty_root_move.move);
    REQUIRE(small_empty_root_move.score == generic_root_move.score);
    REQUIRE(small_empty_root_move.bound == generic_root_move.bound);
    REQUIRE(small_empty_root_move.exact == generic_root_move.exact);
    REQUIRE(small_empty_root_move.selective == generic_root_move.selective);
    REQUIRE(small_empty_root_move.pv == generic_root_move.pv);
  }
}

void require_best_move_uses_low_square_tie_break(const SearchResult& result) {
  if (!result.best_move.has_value()) {
    return;
  }

  for (const RootMoveInfo& root_move : result.root_moves) {
    if (root_move.score != result.score || root_move.move.kind != board_core::MoveKind::normal ||
        result.best_move->kind != board_core::MoveKind::normal) {
      continue;
    }
    REQUIRE(result.best_move->square.index <= root_move.move.square.index);
  }
}

TEST_CASE("small-empty exact path matches generic terminal positions",
          "[search][endgame][small_empty]") {
  require_small_empty_path_matches_generic(parse_position_or_fail(
      "BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/WWWWWWWW/WWWWWWWW/WWWWWWWW b"));
  require_small_empty_path_matches_generic(parse_position_or_fail(
      "BBBBBBB./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b"));
}

TEST_CASE("small-empty exact path matches generic one-empty forced move and pass",
          "[search][endgame][small_empty]") {
  require_small_empty_path_matches_generic(parse_position_or_fail(
      "BBBBBBW./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b"));
  require_small_empty_path_matches_generic(parse_position_or_fail(
      "BBBBBWB./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b"));
}

TEST_CASE("small-empty exact path matches generic generated positions",
          "[search][endgame][small_empty]") {
  require_small_empty_path_matches_generic(test_support::generated_endgame_position(2));
  require_small_empty_path_matches_generic(test_support::generated_endgame_position(3));
  require_small_empty_path_matches_generic(test_support::generated_endgame_position(4));
  require_small_empty_path_matches_generic(parse_position_or_fail(
      "WWWWWWW./BBBBBW../BBWBWWWW/BBBBBWWB/BWBBWWWB/BBBWB.WW/BBBBBBWW/BBBWWWWW b"));
}

TEST_CASE("small-empty exact path matches generic forced-pass two, three, and four-empty positions",
          "[search][endgame][small_empty]") {
  require_small_empty_path_matches_generic(generated_forced_pass_position(2));
  require_small_empty_path_matches_generic(generated_forced_pass_position(3));
  require_small_empty_path_matches_generic(generated_forced_pass_position(4));
}

TEST_CASE("small-empty exact path matches generic terminal-with-empty position",
          "[search][endgame][small_empty]") {
  require_small_empty_path_matches_generic(parse_position_or_fail(
      "BBBBBBB./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b"));
}

TEST_CASE("small-empty exact path preserves TT and parity independent scores",
          "[search][endgame][small_empty][tt][parity]") {
  const std::array<board_core::Position, 6> positions{
      parse_position_or_fail(
          "BBBBBBW./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b"),
      parse_position_or_fail(
          "BBBBBWB./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b"),
      test_support::generated_endgame_position(2),
      test_support::generated_endgame_position(3),
      test_support::generated_endgame_position(4),
      generated_forced_pass_position(3),
  };

  for (const board_core::Position position : positions) {
    const SearchResult baseline =
        solve_with_policy(position, SmallEndgamePolicy::enabled, false, false);
    const SearchResult with_tt =
        solve_with_policy(position, SmallEndgamePolicy::enabled, true, false);
    const SearchResult with_parity =
        solve_with_policy(position, SmallEndgamePolicy::enabled, false, true);

    require_exact_result(position, baseline);
    require_exact_result(position, with_tt);
    require_exact_result(position, with_parity);
    REQUIRE(with_tt.score == baseline.score);
    REQUIRE(with_parity.score == baseline.score);
    REQUIRE(with_tt.best_move == baseline.best_move);
    REQUIRE(with_parity.best_move == baseline.best_move);
  }
}

TEST_CASE("small-empty exact path keeps root tie-break deterministic",
          "[search][endgame][small_empty]") {
  const board_core::Position position = generated_multi_move_position(3);
  const SearchResult result = solve_with_policy(position, SmallEndgamePolicy::enabled);

  require_exact_result(position, result);
  REQUIRE(result.root_moves.size() > 1);
  require_best_move_uses_low_square_tie_break(result);
}

TEST_CASE("small-empty exact path marks final-node max-node interruption non-exact",
          "[search][endgame][small_empty][limits]") {
  const board_core::Position position = parse_position_or_fail(
      "BBBBBBW./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b");
  const SearchResult result =
      solve_with_policy(position, SmallEndgamePolicy::enabled, false, true,
                        SearchLimits{.max_depth = Depth{0}, .max_nodes = 2});

  REQUIRE(result.stopped);
  REQUIRE_FALSE(result.exact);
  REQUIRE(result.bound == BoundType::lower);
  REQUIRE(result.best_move.has_value());
  REQUIRE(result.score == 64);
  REQUIRE(result.root_moves.size() == 1);
  REQUIRE(result.root_moves[0].exact);
  REQUIRE_FALSE(result.root_moves[0].selective);
}

} // namespace
} // namespace vibe_othello::search::internal
