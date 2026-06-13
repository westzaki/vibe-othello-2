#include "../../src/search/search_internal.h"
#include "vibe_othello/board_core/board.h"

#include <array>
#include <catch2/catch_test_macros.hpp>

namespace vibe_othello::search::internal {
namespace {

constexpr board_core::Square square(int file, int rank) noexcept {
  return board_core::square_from_file_rank(file, rank);
}

constexpr board_core::Move move(int file, int rank) noexcept {
  return board_core::make_move(square(file, rank));
}

void require_ordered_moves_match_legal_set(board_core::Position position, MoveList moves) {
  const board_core::Bitboard legal_moves = board_core::legal_moves(position);
  std::array<bool, board_core::kSquareCount> seen{};
  std::uint8_t legal_count = 0;

  for (int square_index = 0; square_index < board_core::kSquareCount; ++square_index) {
    const board_core::Square candidate = board_core::square_from_index(square_index);
    if ((legal_moves & board_core::bit(candidate)) == 0) {
      continue;
    }
    ++legal_count;
  }

  REQUIRE(moves.size == legal_count);
  for (std::uint8_t index = 0; index < moves.size; ++index) {
    REQUIRE(moves.moves[index].kind == board_core::MoveKind::normal);
    REQUIRE((legal_moves & board_core::bit(moves.moves[index].square)) != 0);
    REQUIRE_FALSE(seen[moves.moves[index].square.index]);
    seen[moves.moves[index].square.index] = true;
  }
}

TEST_CASE("move ordering keeps the legal move set complete", "[search][move_ordering]") {
  const board_core::Position position{
      .player = board_core::bit(square(3, 0)) | board_core::bit(square(3, 3)),
      .opponent = board_core::bit(square(2, 0)) | board_core::bit(square(4, 0)) |
                  board_core::bit(square(2, 2)),
      .side_to_move = board_core::Color::black,
  };

  const MoveList moves = ordered_moves(position, MoveOrderingHints{});

  require_ordered_moves_match_legal_set(position, moves);
}

TEST_CASE("move ordering prefers legal corners", "[search][move_ordering]") {
  const board_core::Position position{
      .player = board_core::bit(square(2, 0)) | board_core::bit(square(3, 0)),
      .opponent = board_core::bit(square(1, 0)) | board_core::bit(square(4, 0)),
      .side_to_move = board_core::Color::black,
  };

  const MoveList moves = ordered_moves(position, MoveOrderingHints{});

  require_ordered_moves_match_legal_set(position, moves);
  REQUIRE(moves.size >= 2);
  REQUIRE(moves.moves[0] == move(0, 0));
}

TEST_CASE("move ordering delays X and C squares next to empty corners", "[search][move_ordering]") {
  const board_core::Position position{
      .player = board_core::bit(square(3, 0)) | board_core::bit(square(3, 3)),
      .opponent = board_core::bit(square(2, 0)) | board_core::bit(square(4, 0)) |
                  board_core::bit(square(2, 2)),
      .side_to_move = board_core::Color::black,
  };

  const MoveList moves = ordered_moves(position, MoveOrderingHints{});

  require_ordered_moves_match_legal_set(position, moves);
  REQUIRE(moves.size >= 3);
  REQUIRE(moves.moves[0] == move(5, 0));
  REQUIRE(moves.moves[moves.size - 2] == move(1, 0));
  REQUIRE(moves.moves[moves.size - 1] == move(1, 1));
}

TEST_CASE("move ordering applies root best before TT best", "[search][move_ordering]") {
  const board_core::Position position{
      .player = board_core::bit(square(3, 0)) | board_core::bit(square(3, 3)),
      .opponent = board_core::bit(square(1, 0)) | board_core::bit(square(2, 0)) |
                  board_core::bit(square(4, 0)) | board_core::bit(square(2, 2)),
      .side_to_move = board_core::Color::black,
  };

  const MoveList moves = ordered_moves(position, MoveOrderingHints{
                                                     .root_best_move = move(1, 1),
                                                     .tt_best_move = move(0, 0),
                                                 });

  require_ordered_moves_match_legal_set(position, moves);
  REQUIRE(moves.size >= 2);
  REQUIRE(moves.moves[0] == move(1, 1));
  REQUIRE(moves.moves[1] == move(0, 0));
}

TEST_CASE("move ordering breaks equal scores by square index", "[search][move_ordering]") {
  const board_core::Position position{
      .player = board_core::bit(square(3, 3)) | board_core::bit(square(5, 5)),
      .opponent = board_core::bit(square(3, 2)) | board_core::bit(square(5, 4)),
      .side_to_move = board_core::Color::black,
  };

  const MoveList moves = ordered_moves(position, MoveOrderingHints{});

  require_ordered_moves_match_legal_set(position, moves);
  REQUIRE(moves.size == 2);
  REQUIRE(moves.moves[0] == move(3, 1));
  REQUIRE(moves.moves[1] == move(5, 3));
}

} // namespace
} // namespace vibe_othello::search::internal
