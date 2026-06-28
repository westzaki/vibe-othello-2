#ifndef VIBE_OTHELLO_WASM_WASM_API_H_
#define VIBE_OTHELLO_WASM_WASM_API_H_

#include <stdint.h>

#define VIBE_OTHELLO_WASM_ABI_VERSION 1u

#define VIBE_OTHELLO_WASM_STATUS_OK 0u
#define VIBE_OTHELLO_WASM_STATUS_NULL_POINTER 1u
#define VIBE_OTHELLO_WASM_STATUS_INVALID_POSITION 2u
#define VIBE_OTHELLO_WASM_STATUS_INVALID_MOVE 3u
#define VIBE_OTHELLO_WASM_STATUS_ILLEGAL_MOVE 4u
#define VIBE_OTHELLO_WASM_STATUS_ILLEGAL_PASS 5u

#define VIBE_OTHELLO_WASM_SIDE_BLACK 0u
#define VIBE_OTHELLO_WASM_SIDE_WHITE 1u

#ifdef __cplusplus
#define VIBE_OTHELLO_WASM_NOEXCEPT noexcept
extern "C" {
#else
#define VIBE_OTHELLO_WASM_NOEXCEPT
#endif

typedef struct vibe_othello_wasm_position {
  uint64_t player;
  uint64_t opponent;
  uint8_t side_to_move;
} vibe_othello_wasm_position;

typedef struct vibe_othello_wasm_position_query {
  uint32_t status;
  uint64_t legal_moves;
  uint8_t has_legal_move;
  uint8_t is_terminal;
} vibe_othello_wasm_position_query;

typedef struct vibe_othello_wasm_apply_result {
  uint32_t status;
  vibe_othello_wasm_position position;
  uint64_t flipped;
  uint64_t legal_moves;
  uint8_t has_legal_move;
  uint8_t is_terminal;
} vibe_othello_wasm_apply_result;

uint32_t vibe_othello_wasm_abi_version(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_sizeof_position(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_position_player(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_position_opponent(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_position_side_to_move(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_sizeof_position_query(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_position_query_status(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_position_query_legal_moves(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_position_query_has_legal_move(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_position_query_is_terminal(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_sizeof_apply_result(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_apply_result_status(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_apply_result_position(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_apply_result_flipped(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_apply_result_legal_moves(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_apply_result_has_legal_move(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_apply_result_is_terminal(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_initial_position(vibe_othello_wasm_position* out_position)
    VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_query_position(const vibe_othello_wasm_position* position,
                                          vibe_othello_wasm_position_query* out_query)
    VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t
vibe_othello_wasm_apply_move(const vibe_othello_wasm_position* position, uint8_t square_index,
                             vibe_othello_wasm_apply_result* out_result) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t
vibe_othello_wasm_apply_pass(const vibe_othello_wasm_position* position,
                             vibe_othello_wasm_apply_result* out_result) VIBE_OTHELLO_WASM_NOEXCEPT;

#ifdef __cplusplus
} // extern "C"
#endif

#undef VIBE_OTHELLO_WASM_NOEXCEPT

#endif // VIBE_OTHELLO_WASM_WASM_API_H_
