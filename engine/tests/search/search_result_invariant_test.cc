#include "search/endgame_positions.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/serialization.h"
#include "vibe_othello/search/search.h"

#include <atomic>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string_view>

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

constexpr board_core::Square square(int file, int rank) noexcept {
  return board_core::square_from_file_rank(file, rank);
}

board_core::Position parse_position_or_fail(std::string_view text) {
  const std::optional<board_core::Position> position = board_core::parse_position(text);
  REQUIRE(position.has_value());
  return *position;
}

void require_replayable_pv(board_core::Position position, Line pv) {
  for (std::uint8_t index = 0; index < pv.size; ++index) {
    board_core::MoveDelta delta{};
    REQUIRE(board_core::apply_move(&position, pv.moves[index], &delta));
  }
}

void require_legal_root_move(board_core::Position position, board_core::Move move) {
  board_core::MoveDelta delta{};
  REQUIRE(board_core::make_move_delta(position, move, &delta));
}

void require_wld_score(Score score) {
  REQUIRE((score == static_cast<Score>(WldResult::loss) ||
           score == static_cast<Score>(WldResult::draw) ||
           score == static_cast<Score>(WldResult::win)));
}

void require_score_kind_matches_score(const SearchResult& result) {
  if (result.score_kind == ScoreKind::unavailable) {
    REQUIRE(result.stopped);
    REQUIRE_FALSE(result.best_move.has_value());
    REQUIRE(result.pv.size == 0);
    return;
  }

  if (result.score_kind == ScoreKind::win_loss_draw) {
    require_wld_score(result.score);
  }
}

void require_score_kind_matches_score(const RootMoveInfo& root_move) {
  if (root_move.score_kind == ScoreKind::unavailable) {
    FAIL("root move scores must be interpretable");
  }

  if (root_move.score_kind == ScoreKind::win_loss_draw) {
    require_wld_score(root_move.score);
  }
}

void require_search_result_invariants(board_core::Position position, const SearchResult& result) {
  REQUIRE(result.nodes == result.stats.nodes);
  require_score_kind_matches_score(result);

  if (result.best_move.has_value()) {
    require_legal_root_move(position, *result.best_move);
  } else {
    REQUIRE((board_core::is_terminal(position) || result.stopped));
  }

  require_replayable_pv(position, result.pv);
  for (const RootMoveInfo& root_move : result.root_moves) {
    require_legal_root_move(position, root_move.move);
    require_score_kind_matches_score(root_move);
    REQUIRE(root_move.pv.size > 0);
    REQUIRE(root_move.pv.moves[0] == root_move.move);
    require_replayable_pv(position, root_move.pv);
    if (root_move.exact) {
      REQUIRE_FALSE(root_move.selective);
      REQUIRE((root_move.score_kind == ScoreKind::exact_disc_diff ||
               root_move.score_kind == ScoreKind::win_loss_draw));
    }
  }
}

SearchOptions exact_root_options(std::uint8_t threshold) noexcept {
  return SearchOptions{
      .exact_endgame = true,
      .endgame_exact_empties = threshold,
      .endgame =
          EndgameSearchOptions{
              .exact_endgame = true,
              .endgame_exact_empties = threshold,
          },
  };
}

SearchOptions wld_options() noexcept {
  return SearchOptions{
      .endgame_wld_empties = 8,
      .endgame =
          EndgameSearchOptions{
              .endgame_wld_empties = 8,
          },
      .mode = SearchMode::win_loss_draw,
  };
}

TEST_CASE("midgame root publication satisfies SearchResult invariants",
          "[search][result][invariant]") {
  DiscDifferenceEvaluator fixed_evaluator;
  const SearchResult fixed =
      search_fixed_depth(board_core::initial_position(), fixed_evaluator, Depth{2});
  require_search_result_invariants(board_core::initial_position(), fixed);
  REQUIRE(fixed.score_kind == ScoreKind::heuristic);
  REQUIRE_FALSE(fixed.exact);
  REQUIRE_FALSE(fixed.stopped);

  DiscDifferenceEvaluator iterative_evaluator;
  SearchOptions options{
      .use_aspiration = true,
      .midgame = MidgameSearchOptions{.use_aspiration = true},
  };
  const SearchResult iterative =
      search_iterative(board_core::initial_position(), iterative_evaluator,
                       SearchLimits{.max_depth = Depth{3}}, options);
  require_search_result_invariants(board_core::initial_position(), iterative);
  REQUIRE(iterative.score_kind == ScoreKind::heuristic);
  REQUIRE_FALSE(iterative.exact);
  REQUIRE_FALSE(iterative.stopped);
}

TEST_CASE("endgame root publication satisfies SearchResult invariants",
          "[search][endgame][result][invariant]") {
  const board_core::Position position = test_support::generated_endgame_position(4);
  DiscDifferenceEvaluator evaluator;

  const SearchResult root_triggered_exact = search_iterative(
      position, evaluator, SearchLimits{.max_depth = Depth{0}}, exact_root_options(4));
  require_search_result_invariants(position, root_triggered_exact);
  REQUIRE(root_triggered_exact.score_kind == ScoreKind::exact_disc_diff);
  REQUIRE(root_triggered_exact.exact);

  const SearchResult direct_exact = solve_exact_endgame(position);
  require_search_result_invariants(position, direct_exact);
  REQUIRE(direct_exact.score_kind == ScoreKind::exact_disc_diff);
  REQUIRE(direct_exact.exact);

  const SearchResult direct_wld = solve_wld_endgame(position, SearchLimits{}, wld_options());
  require_search_result_invariants(position, direct_wld);
  REQUIRE(direct_wld.score_kind == ScoreKind::win_loss_draw);
  REQUIRE(direct_wld.exact);
}

TEST_CASE("terminal and forced-pass roots satisfy SearchResult invariants",
          "[search][endgame][result][invariant]") {
  const board_core::Position terminal = parse_position_or_fail(
      "BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/WWWWWWWW/WWWWWWWW/WWWWWWWW b");
  const board_core::Position forced_pass = parse_position_or_fail(
      "BBBBBWB./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b");

  const SearchResult terminal_result = solve_exact_endgame(terminal);
  require_search_result_invariants(terminal, terminal_result);
  REQUIRE_FALSE(terminal_result.best_move.has_value());
  REQUIRE(terminal_result.root_moves.empty());

  const SearchResult forced_pass_result = solve_exact_endgame(forced_pass);
  require_search_result_invariants(forced_pass, forced_pass_result);
  REQUIRE(forced_pass_result.best_move == board_core::make_pass());
  REQUIRE(forced_pass_result.root_moves.size() == 1);
  REQUIRE(forced_pass_result.root_moves[0].move == board_core::make_pass());
}

TEST_CASE("stopped root publication does not expose an exact root before completion",
          "[search][endgame][result][limits]") {
  const board_core::Position position = test_support::generated_endgame_position(4);
  const SearchResult max_nodes_result = solve_exact_endgame(position, SearchLimits{.max_nodes = 1});

  require_search_result_invariants(position, max_nodes_result);
  REQUIRE(max_nodes_result.stopped);
  REQUIRE_FALSE(max_nodes_result.exact);
  REQUIRE(max_nodes_result.score_kind == ScoreKind::unavailable);
  REQUIRE_FALSE(max_nodes_result.best_move.has_value());
  REQUIRE(max_nodes_result.root_moves.empty());

  std::atomic_bool stop_requested{true};
  const SearchResult stop_result =
      solve_wld_endgame(position, SearchLimits{.stop_requested = &stop_requested}, wld_options());

  require_search_result_invariants(position, stop_result);
  REQUIRE(stop_result.stopped);
  REQUIRE_FALSE(stop_result.exact);
  REQUIRE(stop_result.score_kind == ScoreKind::unavailable);
  REQUIRE_FALSE(stop_result.best_move.has_value());
  REQUIRE(stop_result.root_moves.empty());
}

TEST_CASE("midgame forced-pass root move remains legal and replayable",
          "[search][result][invariant]") {
  constexpr board_core::Position pass_position{
      .player = board_core::bit(square(1, 0)),
      .opponent = board_core::bit(square(0, 0)),
      .side_to_move = board_core::Color::black,
  };
  DiscDifferenceEvaluator evaluator;

  const SearchResult result = search_fixed_depth(pass_position, evaluator, Depth{3});

  require_search_result_invariants(pass_position, result);
  REQUIRE(result.best_move == board_core::make_pass());
  REQUIRE(result.root_moves.size() == 1);
  REQUIRE(result.root_moves[0].move == board_core::make_pass());
}

} // namespace
} // namespace vibe_othello::search
