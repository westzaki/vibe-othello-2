#include "vibe_othello/board_core/board.h"
#include "vibe_othello_wasm/wasm_api.h"

#include <catch2/catch_test_macros.hpp>

namespace {

using vibe_othello::board_core::Bitboard;
using vibe_othello::board_core::Color;
using vibe_othello::board_core::Position;
using vibe_othello::board_core::Square;

constexpr Square square(int file, int rank) noexcept {
  return vibe_othello::board_core::square_from_file_rank(file, rank);
}

constexpr uint8_t to_wasm_side(Color color) noexcept {
  return color == Color::black ? VIBE_OTHELLO_WASM_SIDE_BLACK : VIBE_OTHELLO_WASM_SIDE_WHITE;
}

constexpr vibe_othello_wasm_position to_wasm_position(Position position) noexcept {
  return vibe_othello_wasm_position{
      .player = position.player,
      .opponent = position.opponent,
      .side_to_move = to_wasm_side(position.side_to_move),
  };
}

constexpr Position to_engine_position(vibe_othello_wasm_position position) noexcept {
  return Position{
      .player = position.player,
      .opponent = position.opponent,
      .side_to_move =
          position.side_to_move == VIBE_OTHELLO_WASM_SIDE_BLACK ? Color::black : Color::white,
  };
}

TEST_CASE("WASM adapter exposes ABI version", "[wasm]") {
  REQUIRE(vibe_othello_wasm_abi_version() == VIBE_OTHELLO_WASM_ABI_VERSION);
  REQUIRE(vibe_othello_wasm_abi_version() == 1u);
}

TEST_CASE("WASM adapter initial position matches board core", "[wasm]") {
  vibe_othello_wasm_position position{};

  REQUIRE(vibe_othello_wasm_initial_position(&position) == VIBE_OTHELLO_WASM_STATUS_OK);

  REQUIRE(to_engine_position(position) == vibe_othello::board_core::initial_position());
}

TEST_CASE("WASM adapter query matches board core legal moves", "[wasm]") {
  const Position engine_position = vibe_othello::board_core::initial_position();
  const vibe_othello_wasm_position position = to_wasm_position(engine_position);
  vibe_othello_wasm_position_query query{};

  REQUIRE(vibe_othello_wasm_query_position(&position, &query) == VIBE_OTHELLO_WASM_STATUS_OK);

  REQUIRE(query.status == VIBE_OTHELLO_WASM_STATUS_OK);
  REQUIRE(query.legal_moves == vibe_othello::board_core::legal_moves(engine_position));
  REQUIRE(query.has_legal_move == 1);
  REQUIRE(query.is_terminal == 0);
}

TEST_CASE("WASM adapter applies a legal initial move", "[wasm]") {
  vibe_othello_wasm_position position{};
  vibe_othello_wasm_apply_result result{};
  REQUIRE(vibe_othello_wasm_initial_position(&position) == VIBE_OTHELLO_WASM_STATUS_OK);

  REQUIRE(vibe_othello_wasm_apply_move(&position, square(2, 3).index, &result) ==
          VIBE_OTHELLO_WASM_STATUS_OK);

  REQUIRE(result.status == VIBE_OTHELLO_WASM_STATUS_OK);
  REQUIRE(result.flipped != Bitboard{0});
  REQUIRE(result.flipped == vibe_othello::board_core::bit(square(3, 3)));
}

TEST_CASE("WASM adapter reports resulting legal moves after apply move", "[wasm]") {
  Position expected_position = vibe_othello::board_core::initial_position();
  vibe_othello::board_core::MoveDelta expected_delta{};
  REQUIRE(vibe_othello::board_core::apply_move(
      &expected_position, vibe_othello::board_core::make_move(square(2, 3)), &expected_delta));

  const vibe_othello_wasm_position position =
      to_wasm_position(vibe_othello::board_core::initial_position());
  vibe_othello_wasm_apply_result result{};

  REQUIRE(vibe_othello_wasm_apply_move(&position, square(2, 3).index, &result) ==
          VIBE_OTHELLO_WASM_STATUS_OK);

  REQUIRE(to_engine_position(result.position) == expected_position);
  REQUIRE(result.legal_moves == vibe_othello::board_core::legal_moves(expected_position));
  REQUIRE(result.has_legal_move ==
          (vibe_othello::board_core::has_legal_move(expected_position) ? uint8_t{1} : uint8_t{0}));
  REQUIRE(result.is_terminal ==
          (vibe_othello::board_core::is_terminal(expected_position) ? uint8_t{1} : uint8_t{0}));
}

TEST_CASE("WASM adapter rejects invalid and illegal moves", "[wasm]") {
  const vibe_othello_wasm_position position =
      to_wasm_position(vibe_othello::board_core::initial_position());
  vibe_othello_wasm_apply_result result{};

  REQUIRE(vibe_othello_wasm_apply_move(&position, 64, &result) ==
          VIBE_OTHELLO_WASM_STATUS_INVALID_MOVE);
  REQUIRE(result.status == VIBE_OTHELLO_WASM_STATUS_INVALID_MOVE);

  REQUIRE(vibe_othello_wasm_apply_move(&position, square(3, 3).index, &result) ==
          VIBE_OTHELLO_WASM_STATUS_ILLEGAL_MOVE);
  REQUIRE(result.status == VIBE_OTHELLO_WASM_STATUS_ILLEGAL_MOVE);

  REQUIRE(vibe_othello_wasm_apply_move(&position, square(0, 0).index, &result) ==
          VIBE_OTHELLO_WASM_STATUS_ILLEGAL_MOVE);
  REQUIRE(result.status == VIBE_OTHELLO_WASM_STATUS_ILLEGAL_MOVE);
}

TEST_CASE("WASM adapter rejects pass from initial position", "[wasm]") {
  const vibe_othello_wasm_position position =
      to_wasm_position(vibe_othello::board_core::initial_position());
  vibe_othello_wasm_apply_result result{};

  REQUIRE(vibe_othello_wasm_apply_pass(&position, &result) ==
          VIBE_OTHELLO_WASM_STATUS_ILLEGAL_PASS);
  REQUIRE(result.status == VIBE_OTHELLO_WASM_STATUS_ILLEGAL_PASS);
}

TEST_CASE("WASM adapter rejects invalid side to move", "[wasm]") {
  vibe_othello_wasm_position position =
      to_wasm_position(vibe_othello::board_core::initial_position());
  position.side_to_move = 2;
  vibe_othello_wasm_position_query query{};

  REQUIRE(vibe_othello_wasm_query_position(&position, &query) ==
          VIBE_OTHELLO_WASM_STATUS_INVALID_POSITION);
  REQUIRE(query.status == VIBE_OTHELLO_WASM_STATUS_INVALID_POSITION);
}

TEST_CASE("WASM adapter rejects overlapping bitboards", "[wasm]") {
  vibe_othello_wasm_position position =
      to_wasm_position(vibe_othello::board_core::initial_position());
  position.opponent |= position.player;
  vibe_othello_wasm_position_query query{};

  REQUIRE(vibe_othello_wasm_query_position(&position, &query) ==
          VIBE_OTHELLO_WASM_STATUS_INVALID_POSITION);
  REQUIRE(query.status == VIBE_OTHELLO_WASM_STATUS_INVALID_POSITION);
}

TEST_CASE("WASM adapter rejects null output pointers", "[wasm]") {
  const vibe_othello_wasm_position position =
      to_wasm_position(vibe_othello::board_core::initial_position());

  REQUIRE(vibe_othello_wasm_initial_position(nullptr) == VIBE_OTHELLO_WASM_STATUS_NULL_POINTER);
  REQUIRE(vibe_othello_wasm_query_position(&position, nullptr) ==
          VIBE_OTHELLO_WASM_STATUS_NULL_POINTER);
  REQUIRE(vibe_othello_wasm_apply_move(&position, square(2, 3).index, nullptr) ==
          VIBE_OTHELLO_WASM_STATUS_NULL_POINTER);
  REQUIRE(vibe_othello_wasm_apply_pass(&position, nullptr) ==
          VIBE_OTHELLO_WASM_STATUS_NULL_POINTER);
}

} // namespace
