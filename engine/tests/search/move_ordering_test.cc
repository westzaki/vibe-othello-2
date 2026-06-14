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

void require_same_move_order(MoveList left, MoveList right) {
  REQUIRE(left.size == right.size);
  for (std::uint8_t index = 0; index < left.size; ++index) {
    REQUIRE(left.moves[index] == right.moves[index]);
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

TEST_CASE("ordered_moves remains the midgame ordering compatibility entry",
          "[search][move_ordering]") {
  const board_core::Position position{
      .player = board_core::bit(square(3, 0)) | board_core::bit(square(3, 3)),
      .opponent = board_core::bit(square(1, 0)) | board_core::bit(square(2, 0)) |
                  board_core::bit(square(4, 0)) | board_core::bit(square(2, 2)),
      .side_to_move = board_core::Color::black,
  };
  const MoveOrderingHints hints{
      .root_best_move = move(1, 1),
      .tt_best_move = move(0, 0),
  };

  require_same_move_order(ordered_moves(position, hints), order_midgame_moves(position, hints));
}

TEST_CASE("endgame ordering without parity matches the existing empty-hint ordering",
          "[search][move_ordering][endgame]") {
  const board_core::Position position{
      .player = board_core::bit(square(3, 0)) | board_core::bit(square(3, 3)),
      .opponent = board_core::bit(square(2, 0)) | board_core::bit(square(4, 0)) |
                  board_core::bit(square(2, 2)),
      .side_to_move = board_core::Color::black,
  };

  require_same_move_order(order_endgame_moves(position,
                                              EndgameOrderingHints{
                                                  .use_parity_ordering = false,
                                              }),
                          ordered_moves(position, MoveOrderingHints{}));
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

TEST_CASE("midgame IID hint is weaker than TT and stronger than static ordering",
          "[search][move_ordering]") {
  const board_core::Position position{
      .player = board_core::bit(square(3, 0)) | board_core::bit(square(3, 3)),
      .opponent = board_core::bit(square(2, 0)) | board_core::bit(square(4, 0)) |
                  board_core::bit(square(2, 2)),
      .side_to_move = board_core::Color::black,
  };

  const MoveList moves = ordered_moves(position, MoveOrderingHints{
                                                     .tt_best_move = move(5, 0),
                                                     .iid_best_move = move(1, 1),
                                                 });

  require_ordered_moves_match_legal_set(position, moves);
  REQUIRE(moves.size >= 3);
  REQUIRE(moves.moves[0] == move(5, 0));
  REQUIRE(moves.moves[1] == move(1, 1));
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

TEST_CASE("midgame killer and history hints reorder only legal move membership",
          "[search][move_ordering]") {
  const board_core::Position position{
      .player = board_core::bit(square(3, 3)) | board_core::bit(square(5, 5)),
      .opponent = board_core::bit(square(3, 2)) | board_core::bit(square(5, 4)),
      .side_to_move = board_core::Color::black,
  };
  std::array<int, board_core::kSquareCount> history{};
  history[move(3, 1).square.index] = 10'000;

  const MoveList moves =
      ordered_moves(position, MoveOrderingHints{
                                  .killer_moves = {move(5, 3), board_core::make_pass()},
                                  .history = &history,
                              });

  require_ordered_moves_match_legal_set(position, moves);
  REQUIRE(moves.size == 2);
  REQUIRE(moves.moves[0] == move(5, 3));
  REQUIRE(moves.moves[1] == move(3, 1));
}

TEST_CASE("midgame Othello-specific ordering outranks killer and history hints",
          "[search][move_ordering]") {
  const board_core::Position position{
      .player = board_core::bit(square(3, 0)) | board_core::bit(square(3, 3)),
      .opponent = board_core::bit(square(2, 0)) | board_core::bit(square(4, 0)) |
                  board_core::bit(square(2, 2)),
      .side_to_move = board_core::Color::black,
  };
  std::array<int, board_core::kSquareCount> history{};
  history[move(1, 1).square.index] = 10'000;

  const MoveList moves =
      ordered_moves(position, MoveOrderingHints{
                                  .killer_moves = {move(1, 1), board_core::make_pass()},
                                  .history = &history,
                              });

  require_ordered_moves_match_legal_set(position, moves);
  REQUIRE(moves.size >= 3);
  REQUIRE(moves.moves[0] == move(5, 0));
  REQUIRE(moves.moves[moves.size - 2] == move(1, 0));
  REQUIRE(moves.moves[moves.size - 1] == move(1, 1));
}

TEST_CASE("endgame ordering combines opponent mobility and parity deterministically",
          "[search][move_ordering][endgame]") {
  const board_core::Position position{
      .player = ~board_core::Bitboard{0} &
                ~(board_core::bit(square(1, 0)) | board_core::bit(square(1, 1)) |
                  board_core::bit(square(5, 5)) | board_core::bit(square(1, 2)) |
                  board_core::bit(square(5, 4))),
      .opponent = board_core::bit(square(1, 2)) | board_core::bit(square(5, 4)),
      .side_to_move = board_core::Color::black,
  };

  const MoveList parity_moves = order_endgame_moves(position, EndgameOrderingHints{});
  const MoveList static_moves =
      order_endgame_moves(position, EndgameOrderingHints{.use_parity_ordering = false});

  require_ordered_moves_match_legal_set(position, parity_moves);
  require_ordered_moves_match_legal_set(position, static_moves);
  REQUIRE(parity_moves.size == 2);
  REQUIRE(static_moves.size == 2);
  REQUIRE(static_moves.moves[0] == move(5, 5));
  REQUIRE(static_moves.moves[1] == move(1, 1));
  REQUIRE(parity_moves.moves[0] == move(5, 5));
  REQUIRE(parity_moves.moves[1] == move(1, 1));
}

TEST_CASE("endgame ordering prefers legal TT best move before other endgame hints",
          "[search][move_ordering][endgame]") {
  const board_core::Position position{
      .player = ~board_core::Bitboard{0} &
                ~(board_core::bit(square(1, 0)) | board_core::bit(square(1, 1)) |
                  board_core::bit(square(5, 5)) | board_core::bit(square(1, 2)) |
                  board_core::bit(square(5, 4))),
      .opponent = board_core::bit(square(1, 2)) | board_core::bit(square(5, 4)),
      .side_to_move = board_core::Color::black,
  };

  const MoveList moves = order_endgame_moves(position, EndgameOrderingHints{
                                                           .tt_best_move = move(1, 1),
                                                           .root_best_move = move(5, 5),
                                                       });

  require_ordered_moves_match_legal_set(position, moves);
  REQUIRE(moves.size == 2);
  REQUIRE(moves.moves[0] == move(1, 1));
  REQUIRE(moves.moves[1] == move(5, 5));
}

} // namespace
} // namespace vibe_othello::search::internal
