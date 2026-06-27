#include "vibe_othello_wasm/wasm_api.h"

#include "vibe_othello/board_core/board.h"

namespace {

using vibe_othello::board_core::Bitboard;
using vibe_othello::board_core::Color;
using vibe_othello::board_core::MoveDelta;
using vibe_othello::board_core::Position;
using vibe_othello::board_core::Square;

constexpr uint8_t to_wasm_side(Color color) noexcept {
  return color == Color::black ? VIBE_OTHELLO_WASM_SIDE_BLACK : VIBE_OTHELLO_WASM_SIDE_WHITE;
}

constexpr bool is_valid_wasm_side(uint8_t side_to_move) noexcept {
  return side_to_move == VIBE_OTHELLO_WASM_SIDE_BLACK ||
         side_to_move == VIBE_OTHELLO_WASM_SIDE_WHITE;
}

constexpr Color to_engine_side(uint8_t side_to_move) noexcept {
  return side_to_move == VIBE_OTHELLO_WASM_SIDE_BLACK ? Color::black : Color::white;
}

constexpr vibe_othello_wasm_position to_wasm_position(Position position) noexcept {
  return vibe_othello_wasm_position{
      .player = position.player,
      .opponent = position.opponent,
      .side_to_move = to_wasm_side(position.side_to_move),
  };
}

uint32_t to_engine_position(const vibe_othello_wasm_position& input,
                            Position* out_position) noexcept {
  if (!is_valid_wasm_side(input.side_to_move)) {
    return VIBE_OTHELLO_WASM_STATUS_INVALID_POSITION;
  }

  const Position position{
      .player = input.player,
      .opponent = input.opponent,
      .side_to_move = to_engine_side(input.side_to_move),
  };
  if (!vibe_othello::board_core::is_valid(position)) {
    return VIBE_OTHELLO_WASM_STATUS_INVALID_POSITION;
  }

  *out_position = position;
  return VIBE_OTHELLO_WASM_STATUS_OK;
}

constexpr vibe_othello_wasm_position_query empty_query(uint32_t status) noexcept {
  return vibe_othello_wasm_position_query{
      .status = status,
      .legal_moves = 0,
      .has_legal_move = 0,
      .is_terminal = 0,
  };
}

constexpr vibe_othello_wasm_apply_result empty_apply_result(uint32_t status) noexcept {
  return vibe_othello_wasm_apply_result{
      .status = status,
      .position = {},
      .flipped = 0,
      .legal_moves = 0,
      .has_legal_move = 0,
      .is_terminal = 0,
  };
}

vibe_othello_wasm_position_query query_position(Position position) noexcept {
  return vibe_othello_wasm_position_query{
      .status = VIBE_OTHELLO_WASM_STATUS_OK,
      .legal_moves = vibe_othello::board_core::legal_moves(position),
      .has_legal_move =
          vibe_othello::board_core::has_legal_move(position) ? uint8_t{1} : uint8_t{0},
      .is_terminal = vibe_othello::board_core::is_terminal(position) ? uint8_t{1} : uint8_t{0},
  };
}

vibe_othello_wasm_apply_result apply_result(Position position, Bitboard flipped) noexcept {
  const vibe_othello_wasm_position_query query = query_position(position);
  return vibe_othello_wasm_apply_result{
      .status = VIBE_OTHELLO_WASM_STATUS_OK,
      .position = to_wasm_position(position),
      .flipped = flipped,
      .legal_moves = query.legal_moves,
      .has_legal_move = query.has_legal_move,
      .is_terminal = query.is_terminal,
  };
}

} // namespace

extern "C" {

uint32_t vibe_othello_wasm_abi_version(void) noexcept {
  return VIBE_OTHELLO_WASM_ABI_VERSION;
}

uint32_t vibe_othello_wasm_initial_position(vibe_othello_wasm_position* out_position) noexcept {
  if (out_position == nullptr) {
    return VIBE_OTHELLO_WASM_STATUS_NULL_POINTER;
  }

  *out_position = to_wasm_position(vibe_othello::board_core::initial_position());
  return VIBE_OTHELLO_WASM_STATUS_OK;
}

uint32_t vibe_othello_wasm_query_position(const vibe_othello_wasm_position* position,
                                          vibe_othello_wasm_position_query* out_query) noexcept {
  if (out_query == nullptr) {
    return VIBE_OTHELLO_WASM_STATUS_NULL_POINTER;
  }
  *out_query = empty_query(VIBE_OTHELLO_WASM_STATUS_OK);

  if (position == nullptr) {
    *out_query = empty_query(VIBE_OTHELLO_WASM_STATUS_NULL_POINTER);
    return VIBE_OTHELLO_WASM_STATUS_NULL_POINTER;
  }

  Position engine_position{};
  const uint32_t status = to_engine_position(*position, &engine_position);
  if (status != VIBE_OTHELLO_WASM_STATUS_OK) {
    *out_query = empty_query(status);
    return status;
  }

  *out_query = query_position(engine_position);
  return VIBE_OTHELLO_WASM_STATUS_OK;
}

uint32_t vibe_othello_wasm_apply_move(const vibe_othello_wasm_position* position,
                                      uint8_t square_index,
                                      vibe_othello_wasm_apply_result* out_result) noexcept {
  if (out_result == nullptr) {
    return VIBE_OTHELLO_WASM_STATUS_NULL_POINTER;
  }
  *out_result = empty_apply_result(VIBE_OTHELLO_WASM_STATUS_OK);

  if (position == nullptr) {
    *out_result = empty_apply_result(VIBE_OTHELLO_WASM_STATUS_NULL_POINTER);
    return VIBE_OTHELLO_WASM_STATUS_NULL_POINTER;
  }

  Position engine_position{};
  uint32_t status = to_engine_position(*position, &engine_position);
  if (status != VIBE_OTHELLO_WASM_STATUS_OK) {
    *out_result = empty_apply_result(status);
    return status;
  }

  if (!vibe_othello::board_core::is_valid_square_index(square_index)) {
    *out_result = empty_apply_result(VIBE_OTHELLO_WASM_STATUS_INVALID_MOVE);
    return VIBE_OTHELLO_WASM_STATUS_INVALID_MOVE;
  }

  MoveDelta delta{};
  const Square square = vibe_othello::board_core::square_from_index(square_index);
  if (!vibe_othello::board_core::apply_move(&engine_position,
                                            vibe_othello::board_core::make_move(square), &delta)) {
    *out_result = empty_apply_result(VIBE_OTHELLO_WASM_STATUS_ILLEGAL_MOVE);
    return VIBE_OTHELLO_WASM_STATUS_ILLEGAL_MOVE;
  }

  *out_result = apply_result(engine_position, delta.flipped);
  return VIBE_OTHELLO_WASM_STATUS_OK;
}

uint32_t vibe_othello_wasm_apply_pass(const vibe_othello_wasm_position* position,
                                      vibe_othello_wasm_apply_result* out_result) noexcept {
  if (out_result == nullptr) {
    return VIBE_OTHELLO_WASM_STATUS_NULL_POINTER;
  }
  *out_result = empty_apply_result(VIBE_OTHELLO_WASM_STATUS_OK);

  if (position == nullptr) {
    *out_result = empty_apply_result(VIBE_OTHELLO_WASM_STATUS_NULL_POINTER);
    return VIBE_OTHELLO_WASM_STATUS_NULL_POINTER;
  }

  Position engine_position{};
  const uint32_t status = to_engine_position(*position, &engine_position);
  if (status != VIBE_OTHELLO_WASM_STATUS_OK) {
    *out_result = empty_apply_result(status);
    return status;
  }

  MoveDelta delta{};
  if (!vibe_othello::board_core::apply_pass(&engine_position, &delta)) {
    *out_result = empty_apply_result(VIBE_OTHELLO_WASM_STATUS_ILLEGAL_PASS);
    return VIBE_OTHELLO_WASM_STATUS_ILLEGAL_PASS;
  }

  *out_result = apply_result(engine_position, delta.flipped);
  return VIBE_OTHELLO_WASM_STATUS_OK;
}

} // extern "C"
