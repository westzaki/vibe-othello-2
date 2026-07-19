#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/serialization.h"
#include "vibe_othello/search/search.h"

#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string_view>
#include <vector>

namespace vibe_othello::search {
namespace {

board_core::Position parse_position_or_fail(std::string_view text) {
  const std::optional<board_core::Position> position = board_core::parse_position(text);
  REQUIRE(position.has_value());
  return *position;
}

std::uint8_t empty_count(board_core::Position position) noexcept {
  return static_cast<std::uint8_t>(board_core::kSquareCount -
                                   std::popcount(position.player | position.opponent));
}

SearchOptions exact_all_root_move_options() noexcept {
  SearchOptions options;
  options.endgame.use_endgame_tt = true;
  options.reporting.multi_pv = 0;
  return options;
}

std::vector<board_core::Move> legal_root_moves(board_core::Position position) {
  std::vector<board_core::Move> moves;
  const board_core::Bitboard legal = board_core::legal_moves(position);
  for (int index = 0; index < board_core::kSquareCount; ++index) {
    const board_core::Square square = board_core::square_from_index(index);
    if ((legal & board_core::bit(square)) != 0) {
      moves.push_back(board_core::make_move(square));
    }
  }
  if (moves.empty() && !board_core::is_terminal(position)) {
    board_core::MoveDelta delta{};
    if (board_core::make_move_delta(position, board_core::make_pass(), &delta)) {
      moves.push_back(board_core::make_pass());
    }
  }
  return moves;
}

const RootMoveInfo& root_info_for_move(const SearchResult& result, board_core::Move move) {
  for (const RootMoveInfo& root_move : result.root_moves) {
    if (root_move.move == move) {
      return root_move;
    }
  }
  FAIL("root move was missing from exact result");
  return result.root_moves.front();
}

board_core::Position child_after_move(board_core::Position position, board_core::Move move) {
  board_core::MoveDelta delta{};
  const bool applied = move.kind == board_core::MoveKind::pass
                           ? board_core::apply_pass(&position, &delta)
                           : board_core::apply_move(&position, move, &delta);
  REQUIRE(applied);
  return position;
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

board_core::Position generated_multi_move_position(std::uint8_t target_empties,
                                                   std::size_t offset) {
  for (std::size_t seed = offset; seed < offset + 10000; ++seed) {
    const board_core::Position position = deterministic_playout_position(target_empties, seed);
    if (!board_core::is_terminal(position) &&
        std::popcount(board_core::legal_moves(position)) > 1) {
      return position;
    }
  }
  FAIL("could not generate multi-move small-empty position");
  return board_core::initial_position();
}

SearchResult require_root_moves_match_independent_child_solves(board_core::Position position) {
  const std::vector<board_core::Move> legal_moves = legal_root_moves(position);
  REQUIRE_FALSE(legal_moves.empty());

  const SearchResult root_result =
      solve_exact_endgame(position, SearchLimits{}, exact_all_root_move_options());
  REQUIRE_FALSE(root_result.stopped);
  REQUIRE(root_result.exact);
  REQUIRE(root_result.score_kind == ScoreKind::exact_disc_diff);
  REQUIRE(root_result.root_moves.size() == legal_moves.size());

  for (const board_core::Move move : legal_moves) {
    const RootMoveInfo& root_move = root_info_for_move(root_result, move);
    REQUIRE(root_move.exact);
    REQUIRE(root_move.score_kind == ScoreKind::exact_disc_diff);
    REQUIRE(root_move.bound == BoundType::exact);

    const board_core::Position child = child_after_move(position, move);
    const SearchResult child_result =
        solve_exact_endgame(child, SearchLimits{}, exact_all_root_move_options());
    REQUIRE_FALSE(child_result.stopped);
    REQUIRE(child_result.exact);
    REQUIRE(child_result.score_kind == ScoreKind::exact_disc_diff);
    REQUIRE(root_move.score == -child_result.score);
  }
  return root_result;
}

TEST_CASE("public exact root moves match independent child solves for low-empty roots",
          "[search][endgame][public][differential]") {
  const std::array<board_core::Position, 4> positions{
      generated_multi_move_position(2, 0),
      generated_multi_move_position(3, 100),
      generated_multi_move_position(4, 200),
      generated_multi_move_position(5, 300),
  };

  for (const board_core::Position position : positions) {
    const SearchResult result = require_root_moves_match_independent_child_solves(position);
    REQUIRE(result.root_moves.size() > 1);
  }
}

TEST_CASE("public exact root move differential handles forced pass root",
          "[search][endgame][public][differential]") {
  const board_core::Position forced_pass = parse_position_or_fail(
      "BBBBBWB./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b");

  const SearchResult result = require_root_moves_match_independent_child_solves(forced_pass);
  REQUIRE(result.root_moves.size() == 1);
  REQUIRE(result.root_moves[0].move == board_core::make_pass());
}

TEST_CASE("public exact root move differential covers best-score ties",
          "[search][endgame][public][differential]") {
  const board_core::Position tie_root = parse_position_or_fail(
      "WWWWWWW./.WWWBW../BBWBBB../BWWBB.B./BWBBBBBB/BWBBB.B./.WWWWB../BW.WWWWW b");

  const SearchResult result = require_root_moves_match_independent_child_solves(tie_root);
  const Score best_score = result.score;
  std::size_t best_tie_count = 0;
  for (const RootMoveInfo& root_move : result.root_moves) {
    if (root_move.score == best_score) {
      ++best_tie_count;
    }
  }
  REQUIRE(best_tie_count > 1);
}

} // namespace
} // namespace vibe_othello::search
