#include "vibe_othello_wasm/wasm_api.h"

#include "search_preset_internal.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/evaluation/pattern_artifact.h"
#include "vibe_othello/evaluation/phase_aware_evaluator.h"
#include "vibe_othello/search/search.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using vibe_othello::board_core::Bitboard;
using vibe_othello::board_core::Color;
using vibe_othello::board_core::Move;
using vibe_othello::board_core::MoveDelta;
using vibe_othello::board_core::MoveKind;
using vibe_othello::board_core::Position;
using vibe_othello::board_core::Square;

struct WasmEvaluationArtifact {
  explicit WasmEvaluationArtifact(vibe_othello::evaluation::LoadedPatternArtifactBytes artifact)
      : artifact_id(std::move(artifact.artifact_id)),
        pattern_set_id(std::move(artifact.pattern_set_id)),
        weights_checksum(std::move(artifact.weights_checksum)),
        trained_phases(std::move(artifact.trained_phases)),
        evaluator(std::move(artifact.weights), std::move(artifact.feature_set), trained_phases,
                  artifact.fallback_additive_through_phase) {}

  std::string artifact_id;
  std::string pattern_set_id;
  std::string weights_checksum;
  std::optional<std::vector<std::uint8_t>> trained_phases;
  vibe_othello::evaluation::PhaseAwareEvaluator evaluator;
};

using ArtifactRegistry = std::unordered_map<uintptr_t, std::unique_ptr<WasmEvaluationArtifact>>;

ArtifactRegistry& artifact_registry() {
  static ArtifactRegistry registry;
  return registry;
}

uintptr_t allocate_artifact_handle(std::unique_ptr<WasmEvaluationArtifact> artifact) {
  ArtifactRegistry& registry = artifact_registry();
  static uintptr_t next_handle = 1;

  const uintptr_t start = next_handle;
  do {
    const uintptr_t handle = next_handle;
    ++next_handle;
    if (next_handle == 0) {
      next_handle = 1;
    }

    if (!registry.contains(handle)) {
      registry.emplace(handle, std::move(artifact));
      return handle;
    }
  } while (next_handle != start);

  return 0;
}

WasmEvaluationArtifact* find_artifact(uintptr_t eval_handle) {
  if (eval_handle == 0) {
    return nullptr;
  }

  ArtifactRegistry& registry = artifact_registry();
  const auto it = registry.find(eval_handle);
  if (it == registry.end()) {
    return nullptr;
  }
  return it->second.get();
}

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

constexpr vibe_othello_wasm_search_result empty_search_result(uint32_t status) noexcept {
  return vibe_othello_wasm_search_result{
      .status = status,
      .has_best_move = 0,
      .best_move_square = 64,
      .is_pass = 0,
      .reserved0 = 0,
      .score = 0,
      .completed_depth = 0,
      .nodes = 0,
      .elapsed_ms = 0,
      .stopped = 0,
      .exact = 0,
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

uint32_t elapsed_ms_to_wasm(std::chrono::milliseconds elapsed) noexcept {
  const auto count = elapsed.count();
  if (count <= 0) {
    return 0;
  }
  const auto max = static_cast<decltype(count)>(std::numeric_limits<uint32_t>::max());
  return static_cast<uint32_t>(std::min(count, max));
}

uint32_t completed_depth_to_wasm(vibe_othello::search::Depth depth) noexcept {
  if (depth <= 0) {
    return 0;
  }
  return static_cast<uint32_t>(depth);
}

bool fill_best_move(Move move, vibe_othello_wasm_search_result* out_result) noexcept {
  out_result->has_best_move = 1;
  if (move.kind == MoveKind::pass) {
    out_result->best_move_square = 64;
    out_result->is_pass = 1;
    return true;
  }

  if (move.kind != MoveKind::normal ||
      !vibe_othello::board_core::is_valid_square_index(move.square.index)) {
    return false;
  }

  out_result->best_move_square = move.square.index;
  out_result->is_pass = 0;
  return true;
}

uint32_t fill_search_result(const vibe_othello::search::SearchResult& result,
                            vibe_othello_wasm_search_result* out_result) noexcept {
  *out_result = empty_search_result(VIBE_OTHELLO_WASM_STATUS_OK);
  out_result->score = result.score;
  out_result->completed_depth = completed_depth_to_wasm(result.completed_depth);
  out_result->nodes = result.nodes;
  out_result->elapsed_ms = elapsed_ms_to_wasm(result.elapsed);
  out_result->stopped = result.stopped ? uint8_t{1} : uint8_t{0};
  out_result->exact = result.exact ? uint8_t{1} : uint8_t{0};

  if (result.best_move.has_value() && !fill_best_move(*result.best_move, out_result)) {
    *out_result = empty_search_result(VIBE_OTHELLO_WASM_STATUS_SEARCH_FAILED);
    return VIBE_OTHELLO_WASM_STATUS_SEARCH_FAILED;
  }

  return VIBE_OTHELLO_WASM_STATUS_OK;
}

uint32_t search_best_move(uintptr_t eval_handle, const vibe_othello_wasm_position* position,
                          uint32_t max_depth, uint32_t max_nodes, uint32_t max_time_ms,
                          vibe_othello::search::SearchOptions options,
                          vibe_othello_wasm_search_result* out_result) noexcept {
  if (out_result == nullptr) {
    return VIBE_OTHELLO_WASM_STATUS_NULL_POINTER;
  }
  *out_result = empty_search_result(VIBE_OTHELLO_WASM_STATUS_OK);

  if (position == nullptr) {
    *out_result = empty_search_result(VIBE_OTHELLO_WASM_STATUS_NULL_POINTER);
    return VIBE_OTHELLO_WASM_STATUS_NULL_POINTER;
  }

  if (eval_handle == 0) {
    *out_result = empty_search_result(VIBE_OTHELLO_WASM_STATUS_EVALUATOR_NOT_LOADED);
    return VIBE_OTHELLO_WASM_STATUS_EVALUATOR_NOT_LOADED;
  }

  if (max_depth == 0 && max_nodes == 0 && max_time_ms == 0) {
    *out_result = empty_search_result(VIBE_OTHELLO_WASM_STATUS_INVALID_ARGUMENT);
    return VIBE_OTHELLO_WASM_STATUS_INVALID_ARGUMENT;
  }
  if (max_depth > static_cast<uint32_t>(std::numeric_limits<vibe_othello::search::Depth>::max())) {
    *out_result = empty_search_result(VIBE_OTHELLO_WASM_STATUS_INVALID_ARGUMENT);
    return VIBE_OTHELLO_WASM_STATUS_INVALID_ARGUMENT;
  }

  try {
    WasmEvaluationArtifact* artifact = find_artifact(eval_handle);
    if (artifact == nullptr) {
      *out_result = empty_search_result(VIBE_OTHELLO_WASM_STATUS_EVALUATOR_NOT_LOADED);
      return VIBE_OTHELLO_WASM_STATUS_EVALUATOR_NOT_LOADED;
    }

    Position engine_position{};
    const uint32_t status = to_engine_position(*position, &engine_position);
    if (status != VIBE_OTHELLO_WASM_STATUS_OK) {
      *out_result = empty_search_result(status);
      return status;
    }

    const vibe_othello::search::SearchLimits limits{
        .max_depth = static_cast<vibe_othello::search::Depth>(max_depth),
        .max_nodes = max_nodes,
        .max_time = std::chrono::milliseconds{max_time_ms},
        .infinite = false,
        .stop_requested = nullptr,
    };
    const vibe_othello::search::SearchResult result = vibe_othello::search::search_iterative(
        engine_position, artifact->evaluator, limits, options);
    return fill_search_result(result, out_result);
  } catch (...) {
    *out_result = empty_search_result(VIBE_OTHELLO_WASM_STATUS_SEARCH_FAILED);
    return VIBE_OTHELLO_WASM_STATUS_SEARCH_FAILED;
  }
}

} // namespace

extern "C" {

uint32_t vibe_othello_wasm_abi_version(void) noexcept {
  return VIBE_OTHELLO_WASM_ABI_VERSION;
}

uint32_t vibe_othello_wasm_sizeof_position(void) noexcept {
  return sizeof(vibe_othello_wasm_position);
}

uint32_t vibe_othello_wasm_offsetof_position_player(void) noexcept {
  return offsetof(vibe_othello_wasm_position, player);
}

uint32_t vibe_othello_wasm_offsetof_position_opponent(void) noexcept {
  return offsetof(vibe_othello_wasm_position, opponent);
}

uint32_t vibe_othello_wasm_offsetof_position_side_to_move(void) noexcept {
  return offsetof(vibe_othello_wasm_position, side_to_move);
}

uint32_t vibe_othello_wasm_sizeof_position_query(void) noexcept {
  return sizeof(vibe_othello_wasm_position_query);
}

uint32_t vibe_othello_wasm_offsetof_position_query_status(void) noexcept {
  return offsetof(vibe_othello_wasm_position_query, status);
}

uint32_t vibe_othello_wasm_offsetof_position_query_legal_moves(void) noexcept {
  return offsetof(vibe_othello_wasm_position_query, legal_moves);
}

uint32_t vibe_othello_wasm_offsetof_position_query_has_legal_move(void) noexcept {
  return offsetof(vibe_othello_wasm_position_query, has_legal_move);
}

uint32_t vibe_othello_wasm_offsetof_position_query_is_terminal(void) noexcept {
  return offsetof(vibe_othello_wasm_position_query, is_terminal);
}

uint32_t vibe_othello_wasm_sizeof_apply_result(void) noexcept {
  return sizeof(vibe_othello_wasm_apply_result);
}

uint32_t vibe_othello_wasm_offsetof_apply_result_status(void) noexcept {
  return offsetof(vibe_othello_wasm_apply_result, status);
}

uint32_t vibe_othello_wasm_offsetof_apply_result_position(void) noexcept {
  return offsetof(vibe_othello_wasm_apply_result, position);
}

uint32_t vibe_othello_wasm_offsetof_apply_result_flipped(void) noexcept {
  return offsetof(vibe_othello_wasm_apply_result, flipped);
}

uint32_t vibe_othello_wasm_offsetof_apply_result_legal_moves(void) noexcept {
  return offsetof(vibe_othello_wasm_apply_result, legal_moves);
}

uint32_t vibe_othello_wasm_offsetof_apply_result_has_legal_move(void) noexcept {
  return offsetof(vibe_othello_wasm_apply_result, has_legal_move);
}

uint32_t vibe_othello_wasm_offsetof_apply_result_is_terminal(void) noexcept {
  return offsetof(vibe_othello_wasm_apply_result, is_terminal);
}

uint32_t vibe_othello_wasm_sizeof_search_result(void) noexcept {
  return sizeof(vibe_othello_wasm_search_result);
}

uint32_t vibe_othello_wasm_offsetof_search_result_status(void) noexcept {
  return offsetof(vibe_othello_wasm_search_result, status);
}

uint32_t vibe_othello_wasm_offsetof_search_result_has_best_move(void) noexcept {
  return offsetof(vibe_othello_wasm_search_result, has_best_move);
}

uint32_t vibe_othello_wasm_offsetof_search_result_best_move_square(void) noexcept {
  return offsetof(vibe_othello_wasm_search_result, best_move_square);
}

uint32_t vibe_othello_wasm_offsetof_search_result_is_pass(void) noexcept {
  return offsetof(vibe_othello_wasm_search_result, is_pass);
}

uint32_t vibe_othello_wasm_offsetof_search_result_score(void) noexcept {
  return offsetof(vibe_othello_wasm_search_result, score);
}

uint32_t vibe_othello_wasm_offsetof_search_result_completed_depth(void) noexcept {
  return offsetof(vibe_othello_wasm_search_result, completed_depth);
}

uint32_t vibe_othello_wasm_offsetof_search_result_nodes(void) noexcept {
  return offsetof(vibe_othello_wasm_search_result, nodes);
}

uint32_t vibe_othello_wasm_offsetof_search_result_elapsed_ms(void) noexcept {
  return offsetof(vibe_othello_wasm_search_result, elapsed_ms);
}

uint32_t vibe_othello_wasm_offsetof_search_result_stopped(void) noexcept {
  return offsetof(vibe_othello_wasm_search_result, stopped);
}

uint32_t vibe_othello_wasm_offsetof_search_result_exact(void) noexcept {
  return offsetof(vibe_othello_wasm_search_result, exact);
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

uint32_t vibe_othello_wasm_load_eval_artifact(const uint8_t* manifest_text,
                                              uint32_t manifest_text_size,
                                              const uint8_t* weights_bytes,
                                              uint32_t weights_bytes_size,
                                              uintptr_t* out_eval_handle) noexcept {
  if (out_eval_handle == nullptr) {
    return VIBE_OTHELLO_WASM_STATUS_NULL_POINTER;
  }
  *out_eval_handle = 0;

  if (manifest_text_size == 0 || weights_bytes_size == 0) {
    return VIBE_OTHELLO_WASM_STATUS_INVALID_ARGUMENT;
  }
  if (manifest_text == nullptr || weights_bytes == nullptr) {
    return VIBE_OTHELLO_WASM_STATUS_NULL_POINTER;
  }

  try {
    const std::string_view manifest_view{reinterpret_cast<const char*>(manifest_text),
                                         manifest_text_size};
    const std::span<const uint8_t> weights_view{weights_bytes, weights_bytes_size};
    vibe_othello::evaluation::PatternArtifactBytesLoadResult load_result =
        vibe_othello::evaluation::load_pattern_artifact_from_bytes(manifest_view, weights_view,
                                                                   "<wasm-memory>");
    if (!load_result.ok()) {
      return VIBE_OTHELLO_WASM_STATUS_ARTIFACT_LOAD_FAILED;
    }

    auto artifact = std::make_unique<WasmEvaluationArtifact>(std::move(*load_result.artifact));
    const uintptr_t handle = allocate_artifact_handle(std::move(artifact));
    if (handle == 0) {
      return VIBE_OTHELLO_WASM_STATUS_ARTIFACT_LOAD_FAILED;
    }

    *out_eval_handle = handle;
    return VIBE_OTHELLO_WASM_STATUS_OK;
  } catch (...) {
    return VIBE_OTHELLO_WASM_STATUS_ARTIFACT_LOAD_FAILED;
  }
}

void vibe_othello_wasm_free_eval_artifact(uintptr_t eval_handle) noexcept {
  if (eval_handle == 0) {
    return;
  }

  try {
    artifact_registry().erase(eval_handle);
  } catch (...) {
  }
}

uint32_t vibe_othello_wasm_evaluate_position(uintptr_t eval_handle,
                                             const vibe_othello_wasm_position* position,
                                             int32_t* out_score) noexcept {
  if (position == nullptr || out_score == nullptr) {
    return VIBE_OTHELLO_WASM_STATUS_NULL_POINTER;
  }
  *out_score = 0;

  try {
    WasmEvaluationArtifact* artifact = find_artifact(eval_handle);
    if (artifact == nullptr) {
      return VIBE_OTHELLO_WASM_STATUS_EVALUATOR_NOT_LOADED;
    }

    Position engine_position{};
    const uint32_t status = to_engine_position(*position, &engine_position);
    if (status != VIBE_OTHELLO_WASM_STATUS_OK) {
      return status;
    }

    *out_score = artifact->evaluator.evaluate(engine_position);
    return VIBE_OTHELLO_WASM_STATUS_OK;
  } catch (...) {
    return VIBE_OTHELLO_WASM_STATUS_SEARCH_FAILED;
  }
}

uint32_t vibe_othello_wasm_search_best_move(uintptr_t eval_handle,
                                            const vibe_othello_wasm_position* position,
                                            uint32_t max_depth, uint32_t max_nodes,
                                            uint32_t max_time_ms,
                                            vibe_othello_wasm_search_result* out_result) noexcept {
  return search_best_move(eval_handle, position, max_depth, max_nodes, max_time_ms,
                          vibe_othello::search::SearchOptions{}, out_result);
}

uint32_t vibe_othello_wasm_search_best_move_v2(
    uintptr_t eval_handle, const vibe_othello_wasm_position* position, uint32_t max_depth,
    uint32_t max_nodes, uint32_t max_time_ms, uint32_t search_preset,
    uint32_t exact_endgame_empties, vibe_othello_wasm_search_result* out_result) noexcept {
  if (out_result == nullptr) {
    return VIBE_OTHELLO_WASM_STATUS_NULL_POINTER;
  }
  if (!vibe_othello::wasm_adapter::internal::is_valid_search_preset(search_preset) ||
      exact_endgame_empties > vibe_othello::board_core::kSquareCount) {
    *out_result = empty_search_result(VIBE_OTHELLO_WASM_STATUS_INVALID_ARGUMENT);
    return VIBE_OTHELLO_WASM_STATUS_INVALID_ARGUMENT;
  }
  if (exact_endgame_empties != 0 && max_nodes == 0 && max_time_ms == 0) {
    *out_result = empty_search_result(VIBE_OTHELLO_WASM_STATUS_INVALID_ARGUMENT);
    return VIBE_OTHELLO_WASM_STATUS_INVALID_ARGUMENT;
  }

  return search_best_move(eval_handle, position, max_depth, max_nodes, max_time_ms,
                          vibe_othello::wasm_adapter::internal::search_options_for_preset(
                              search_preset, static_cast<std::uint8_t>(exact_endgame_empties)),
                          out_result);
}

} // extern "C"
