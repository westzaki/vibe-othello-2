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
#define VIBE_OTHELLO_WASM_STATUS_INVALID_ARGUMENT 6u
#define VIBE_OTHELLO_WASM_STATUS_ARTIFACT_LOAD_FAILED 7u
#define VIBE_OTHELLO_WASM_STATUS_EVALUATOR_NOT_LOADED 8u
#define VIBE_OTHELLO_WASM_STATUS_SEARCH_FAILED 9u

#define VIBE_OTHELLO_WASM_SIDE_BLACK 0u
#define VIBE_OTHELLO_WASM_SIDE_WHITE 1u

#define VIBE_OTHELLO_WASM_SEARCH_PRESET_EASY 0u
#define VIBE_OTHELLO_WASM_SEARCH_PRESET_NORMAL 1u
#define VIBE_OTHELLO_WASM_SEARCH_PRESET_HARD 2u

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

typedef struct vibe_othello_wasm_search_result {
  uint32_t status;
  uint8_t has_best_move;
  uint8_t best_move_square;
  uint8_t is_pass;
  uint8_t reserved0;
  int32_t score;
  uint32_t completed_depth;
  uint64_t nodes;
  uint32_t elapsed_ms;
  uint8_t stopped;
  uint8_t exact;
} vibe_othello_wasm_search_result;

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

uint32_t vibe_othello_wasm_sizeof_search_result(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_search_result_status(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_search_result_has_best_move(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_search_result_best_move_square(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_search_result_is_pass(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_search_result_score(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_search_result_completed_depth(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_search_result_nodes(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_search_result_elapsed_ms(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_search_result_stopped(void) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_offsetof_search_result_exact(void) VIBE_OTHELLO_WASM_NOEXCEPT;

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

uint32_t
vibe_othello_wasm_load_eval_artifact(const uint8_t* manifest_text, uint32_t manifest_text_size,
                                     const uint8_t* weights_bytes, uint32_t weights_bytes_size,
                                     uintptr_t* out_eval_handle) VIBE_OTHELLO_WASM_NOEXCEPT;

void vibe_othello_wasm_free_eval_artifact(uintptr_t eval_handle) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_evaluate_position(uintptr_t eval_handle,
                                             const vibe_othello_wasm_position* position,
                                             int32_t* out_score) VIBE_OTHELLO_WASM_NOEXCEPT;

uint32_t vibe_othello_wasm_search_best_move(
    uintptr_t eval_handle, const vibe_othello_wasm_position* position, uint32_t max_depth,
    uint32_t max_nodes, uint32_t max_time_ms,
    vibe_othello_wasm_search_result* out_result) VIBE_OTHELLO_WASM_NOEXCEPT;

// Run bounded best-move search using a stable algorithm preset.
//
// Search limits are explicit and independent from the preset. A nonzero
// exact_endgame_empties enables exact-score endgame search at or below that
// root empty-count threshold and requires max_nodes or max_time_ms because an
// exact root ignores max_depth. The legacy search_best_move function remains
// available for callers that require its empty-SearchOptions behavior.
uint32_t vibe_othello_wasm_search_best_move_v2(
    uintptr_t eval_handle, const vibe_othello_wasm_position* position, uint32_t max_depth,
    uint32_t max_nodes, uint32_t max_time_ms, uint32_t search_preset,
    uint32_t exact_endgame_empties,
    vibe_othello_wasm_search_result* out_result) VIBE_OTHELLO_WASM_NOEXCEPT;

#ifdef __cplusplus
} // extern "C"
#endif

#undef VIBE_OTHELLO_WASM_NOEXCEPT

#endif // VIBE_OTHELLO_WASM_WASM_API_H_
