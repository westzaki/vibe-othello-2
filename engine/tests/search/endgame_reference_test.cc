#include "search/reference_endgame.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/serialization.h"
#include "vibe_othello/search/search.h"

#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <string_view>

namespace vibe_othello::search {
namespace {

class CountingEvaluator final : public Evaluator {
public:
  Score evaluate(const board_core::Position&) const noexcept override {
    ++calls;
    return 0;
  }

  mutable int calls = 0;
};

constexpr board_core::Square square(int file, int rank) noexcept {
  return board_core::square_from_file_rank(file, rank);
}

std::uint8_t empty_count(board_core::Position position) noexcept {
  return static_cast<std::uint8_t>(std::popcount(~board_core::occupied(position)));
}

board_core::Position parse_position_or_fail(std::string_view text) {
  const std::optional<board_core::Position> position = board_core::parse_position(text);
  REQUIRE(position.has_value());
  return *position;
}

board_core::Move select_legal_move(board_core::Position position, std::size_t choice) {
  const board_core::Bitboard legal = board_core::legal_moves(position);
  if (legal == 0) {
    return board_core::make_pass();
  }

  std::array<board_core::Move, board_core::kSquareCount> moves{};
  std::size_t move_count = 0;
  for (int square_index = 0; square_index < board_core::kSquareCount; ++square_index) {
    const board_core::Square move_square = board_core::square_from_index(square_index);
    if ((legal & board_core::bit(move_square)) != 0) {
      moves[move_count] = board_core::make_move(move_square);
      ++move_count;
    }
  }

  REQUIRE(move_count > 0);
  return moves[choice % move_count];
}

board_core::Position generated_position(std::uint8_t target_empties) {
  static constexpr std::array<std::size_t, 64> kChoices{
      12, 11, 6, 6, 7,  11, 13, 13, 0, 15, 9, 12, 5,  7,  13, 15, 15, 13, 12, 8, 14, 5,
      3,  4,  2, 1, 12, 14, 5,  14, 4, 0,  9, 11, 13, 15, 12, 13, 2,  7,  5,  5, 7,  6,
      3,  8,  1, 9, 4,  2,  10, 6,  0, 11, 5, 13, 7,  3,  12, 1,  8,  4,  14, 2,
  };

  board_core::Position position = board_core::initial_position();
  std::size_t ply = 0;
  while (empty_count(position) > target_empties) {
    REQUIRE_FALSE(board_core::is_terminal(position));
    const board_core::Move move = select_legal_move(position, kChoices[ply % kChoices.size()]);
    board_core::MoveDelta delta{};
    REQUIRE(board_core::apply_move(&position, move, &delta));
    if (move.kind == board_core::MoveKind::normal) {
      ++ply;
    }
  }

  REQUIRE(empty_count(position) == target_empties);
  return position;
}

SearchResult production_exact_endgame(board_core::Position position) {
  CountingEvaluator evaluator;
  const std::uint8_t empties = empty_count(position);
  const SearchResult result =
      search_iterative(position, evaluator, SearchLimits{.max_depth = Depth{0}},
                       SearchOptions{.exact_endgame = true, .endgame_exact_empties = empties});
  REQUIRE(evaluator.calls == 0);
  return result;
}

void require_replayable_pv(board_core::Position position, Line pv) {
  for (std::uint8_t index = 0; index < pv.size; ++index) {
    board_core::MoveDelta delta{};
    REQUIRE(board_core::apply_move(&position, pv.moves[index], &delta));
  }
}

void require_exact_invariants(board_core::Position position, const SearchResult& result) {
  REQUIRE_FALSE(result.stopped);
  REQUIRE(result.exact);
  REQUIRE(result.bound == BoundType::exact);
  REQUIRE(result.nodes == result.stats.nodes);
  REQUIRE(result.stats.eval_calls == 0);
  REQUIRE(result.stats.leaf_nodes == 0);
  REQUIRE(result.stats.endgame_nodes == result.stats.nodes);
  require_replayable_pv(position, result.pv);

  if (result.best_move.has_value()) {
    board_core::MoveDelta delta{};
    REQUIRE(board_core::make_move_delta(position, *result.best_move, &delta));
  }

  for (const RootMoveInfo& root_move : result.root_moves) {
    REQUIRE(root_move.bound == BoundType::exact);
    REQUIRE(root_move.exact);
    REQUIRE_FALSE(root_move.selective);
    REQUIRE(root_move.pv.size > 0);
    REQUIRE(root_move.pv.moves[0] == root_move.move);
    require_replayable_pv(position, root_move.pv);
  }
}

Score reference_score_for_move(const SearchResult& reference, board_core::Move move) {
  for (const RootMoveInfo& root_move : reference.root_moves) {
    if (root_move.move == move) {
      return root_move.score;
    }
  }
  FAIL("production root move was missing from reference root moves");
  return 0;
}

void require_matches_reference(board_core::Position position) {
  const SearchResult reference = test_support::reference_exact_endgame(position);
  const SearchResult production = production_exact_endgame(position);

  require_exact_invariants(position, reference);
  require_exact_invariants(position, production);

  REQUIRE(production.score == reference.score);
  REQUIRE(production.completed_depth == reference.completed_depth);
  REQUIRE(production.root_moves.size() == reference.root_moves.size());

  if (production.best_move.has_value()) {
    REQUIRE(reference_score_for_move(reference, *production.best_move) == reference.score);
  } else {
    REQUIRE_FALSE(reference.best_move.has_value());
  }

  for (const RootMoveInfo& production_root_move : production.root_moves) {
    REQUIRE(reference_score_for_move(reference, production_root_move.move) ==
            production_root_move.score);
  }
}

TEST_CASE("reference exact endgame matches terminal and one-empty positions",
          "[search][endgame][reference]") {
  require_matches_reference(parse_position_or_fail(
      "BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/WWWWWWWW/WWWWWWWW/WWWWWWWW b"));
  require_matches_reference(parse_position_or_fail(
      "BBBBBBW./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b"));
  require_matches_reference(parse_position_or_fail(
      "BBBBBWB./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b"));
}

TEST_CASE("reference exact endgame matches deterministic small-empty positions",
          "[search][endgame][reference]") {
  for (const std::uint8_t empties :
       {std::uint8_t{2}, std::uint8_t{3}, std::uint8_t{4}, std::uint8_t{6}}) {
    require_matches_reference(generated_position(empties));
  }
}

} // namespace
} // namespace vibe_othello::search
