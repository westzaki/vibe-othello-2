#include "../../src/search/endgame_stability_internal.h"
#include "search/endgame_positions.h"
#include "search/reference_endgame.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/serialization.h"
#include "vibe_othello/search/search.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifndef VIBE_OTHELLO_SOURCE_DIR
#error "VIBE_OTHELLO_SOURCE_DIR must be defined for endgame stability tests"
#endif

namespace vibe_othello::search {
namespace {

board_core::Position parse_position_or_fail(std::string_view text) {
  const std::optional<board_core::Position> position = board_core::parse_position(text);
  REQUIRE(position.has_value());
  return *position;
}

std::vector<test_support::EndgamePositionCase> load_endgame_corpus() {
  return test_support::load_endgame_position_corpus(std::string{VIBE_OTHELLO_SOURCE_DIR} +
                                                    "/engine/fixtures/endgame/positions.tsv");
}

SearchResult solve_with_stability(board_core::Position position, EndgameStabilityMode mode) {
  return solve_exact_endgame(
      position, SearchLimits{},
      SearchOptions{.endgame = EndgameSearchOptions{.stability_mode = mode}});
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

void require_same_exact_root_scores(const SearchResult& expected, const SearchResult& actual) {
  REQUIRE_FALSE(actual.stopped);
  REQUIRE(actual.exact);
  REQUIRE(actual.score_kind == ScoreKind::exact_disc_diff);
  REQUIRE(actual.bound == BoundType::exact);
  REQUIRE(actual.score == expected.score);
  REQUIRE(actual.completed_depth == expected.completed_depth);
  REQUIRE(actual.root_moves.size() == expected.root_moves.size());
  REQUIRE(actual.best_move.has_value() == expected.best_move.has_value());
  if (actual.best_move.has_value()) {
    REQUIRE(root_score_for_move(expected, *actual.best_move) == expected.score);
  }
  for (const RootMoveInfo& root_move : actual.root_moves) {
    REQUIRE(root_score_for_move(expected, root_move.move) == root_move.score);
  }
}

void require_stable_discs_survive_all_games(board_core::Position position,
                                            board_core::Bitboard required_black = 0,
                                            board_core::Bitboard required_white = 0) {
  const board_core::Bitboard occupied = board_core::occupied(position);
  const board_core::Bitboard stable_player = internal::stable_discs(position.player, occupied);
  const board_core::Bitboard stable_opponent = internal::stable_discs(position.opponent, occupied);
  if (position.side_to_move == board_core::Color::black) {
    required_black |= stable_player;
    required_white |= stable_opponent;
  } else {
    required_white |= stable_player;
    required_black |= stable_opponent;
  }

  board_core::Bitboard moves = board_core::legal_moves(position);
  if (moves == 0) {
    if (board_core::is_terminal(position)) {
      REQUIRE((board_core::black_discs(position) & required_black) == required_black);
      REQUIRE((board_core::white_discs(position) & required_white) == required_white);
      return;
    }
    board_core::MoveDelta delta{};
    REQUIRE(board_core::apply_move(&position, board_core::make_pass(), &delta));
    require_stable_discs_survive_all_games(position, required_black, required_white);
    return;
  }

  while (moves != 0) {
    const int square_index = std::countr_zero(moves);
    moves &= moves - 1;
    board_core::Position child = position;
    board_core::MoveDelta delta{};
    REQUIRE(board_core::apply_move(
        &child, board_core::make_move(board_core::square_from_index(square_index)), &delta));
    require_stable_discs_survive_all_games(child, required_black, required_white);
  }
}

TEST_CASE("stable disc estimator recognizes only anchored or full-line discs",
          "[search][endgame][stability]") {
  const board_core::Position initial = board_core::initial_position();
  REQUIRE(internal::stable_discs(initial.player, board_core::occupied(initial)) == 0);
  REQUIRE(internal::stable_discs(initial.opponent, board_core::occupied(initial)) == 0);

  const board_core::Position anchored = parse_position_or_fail(
      "......../......../......../...W..../......../B......./B......./BBB..... b");
  const board_core::Bitboard expected = board_core::bit(board_core::square_from_file_rank(0, 0)) |
                                        board_core::bit(board_core::square_from_file_rank(1, 0)) |
                                        board_core::bit(board_core::square_from_file_rank(2, 0)) |
                                        board_core::bit(board_core::square_from_file_rank(0, 1)) |
                                        board_core::bit(board_core::square_from_file_rank(0, 2));
  REQUIRE(internal::stable_discs(anchored.player, board_core::occupied(anchored)) == expected);

  const board_core::Position full = parse_position_or_fail(
      "BWBWBWBW/WBWBWBWB/BWBWBWBW/WBWBWBWB/BWBWBWBW/WBWBWBWB/BWBWBWBW/WBWBWBWB b");
  REQUIRE(internal::stable_discs(full.player, board_core::occupied(full)) == full.player);
  REQUIRE(internal::stable_discs(full.opponent, board_core::occupied(full)) == full.opponent);
}

TEST_CASE("reported stable discs survive every continuation from a small endgame",
          "[search][endgame][stability]") {
  require_stable_discs_survive_all_games(test_support::generated_endgame_position(6));
}

TEST_CASE("stability score bounds contain reference exact corpus scores",
          "[search][endgame][stability][reference]") {
  for (const test_support::EndgamePositionCase& corpus_case : load_endgame_corpus()) {
    if (corpus_case.expected_empties > 12) {
      continue;
    }
    INFO("position_id: " << corpus_case.id);
    const internal::EndgameStabilityBounds bounds =
        internal::endgame_stability_bounds(corpus_case.position);
    const SearchResult reference = test_support::reference_exact_endgame(corpus_case.position);
    REQUIRE(bounds.lower <= reference.score);
    REQUIRE(reference.score <= bounds.upper);
    REQUIRE((bounds.stable_player & ~corpus_case.position.player) == 0);
    REQUIRE((bounds.stable_opponent & ~corpus_case.position.opponent) == 0);
  }
}

TEST_CASE("stability shadow has zero false cuts before cutoff preserves exact corpus results",
          "[search][endgame][stability][shadow][cutoff]") {
  NodeCount shadow_candidates = 0;
  NodeCount shadow_verifications = 0;
  NodeCount shadow_false_cutoffs = 0;
  NodeCount real_cutoffs = 0;
  NodeCount off_nodes = 0;
  NodeCount cutoff_nodes = 0;

  for (const test_support::EndgamePositionCase& corpus_case : load_endgame_corpus()) {
    if (corpus_case.expected_empties > 12) {
      continue;
    }
    INFO("position_id: " << corpus_case.id);
    const SearchResult off = solve_with_stability(corpus_case.position, EndgameStabilityMode::off);
    const SearchResult shadow =
        solve_with_stability(corpus_case.position, EndgameStabilityMode::shadow);
    const SearchResult cutoff =
        solve_with_stability(corpus_case.position, EndgameStabilityMode::cutoff);

    require_same_exact_root_scores(off, shadow);
    require_same_exact_root_scores(off, cutoff);
    REQUIRE(shadow.nodes == off.nodes);
    REQUIRE(cutoff.nodes <= off.nodes);

    const NodeCount candidates = shadow.stats.endgame_stability_lower_candidates +
                                 shadow.stats.endgame_stability_upper_candidates;
    REQUIRE(shadow.stats.endgame_stability_shadow_verifications == candidates);
    REQUIRE(shadow.stats.endgame_stability_shadow_false_cutoffs == 0);
    REQUIRE(cutoff.stats.endgame_stability_cutoffs ==
            cutoff.stats.endgame_stability_lower_candidates +
                cutoff.stats.endgame_stability_upper_candidates);

    shadow_candidates += candidates;
    shadow_verifications += shadow.stats.endgame_stability_shadow_verifications;
    shadow_false_cutoffs += shadow.stats.endgame_stability_shadow_false_cutoffs;
    real_cutoffs += cutoff.stats.endgame_stability_cutoffs;
    off_nodes += off.nodes;
    cutoff_nodes += cutoff.nodes;
  }

  REQUIRE(shadow_candidates > 0);
  REQUIRE(shadow_verifications == shadow_candidates);
  REQUIRE(shadow_false_cutoffs == 0);
  REQUIRE(real_cutoffs > 0);
  REQUIRE(cutoff_nodes < off_nodes);
}

} // namespace
} // namespace vibe_othello::search
