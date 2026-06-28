#include "vibe_othello/board_core/board.h"
#include "vibe_othello/evaluation/pattern_artifact.h"
#include "vibe_othello/evaluation/pattern_evaluator.h"
#include "vibe_othello/search/search.h"
#include "vibe_othello_wasm/wasm_api.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

#ifndef VIBE_OTHELLO_SOURCE_DIR
#define VIBE_OTHELLO_SOURCE_DIR "."
#endif

using vibe_othello::board_core::Bitboard;
using vibe_othello::board_core::Color;
using vibe_othello::board_core::Move;
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

std::filesystem::path source_root() {
  return std::filesystem::path{VIBE_OTHELLO_SOURCE_DIR};
}

std::filesystem::path committed_manifest_path() {
  return source_root() / "data/eval/artifacts/pattern-v2-endgame-lite-100k-mt-v0/manifest.json";
}

std::filesystem::path committed_weights_path() {
  return source_root() / "data/eval/artifacts/pattern-v2-endgame-lite-100k-mt-v0/weights.bin";
}

std::string read_text_or_fail(const std::filesystem::path& path) {
  std::ifstream input(path);
  REQUIRE(input);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  const bool read_ok = input.good() || input.eof();
  REQUIRE(read_ok);
  return buffer.str();
}

std::vector<std::uint8_t> read_bytes_or_fail(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  REQUIRE(input);
  input.seekg(0, std::ios::end);
  const std::streamoff size = input.tellg();
  REQUIRE(size >= 0);
  input.seekg(0, std::ios::beg);

  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  REQUIRE(input);
  return bytes;
}

class WasmEvalHandle {
public:
  WasmEvalHandle() = default;

  explicit WasmEvalHandle(uintptr_t handle) : handle_(handle) {}

  WasmEvalHandle(const WasmEvalHandle&) = delete;
  WasmEvalHandle& operator=(const WasmEvalHandle&) = delete;

  WasmEvalHandle(WasmEvalHandle&& other) noexcept : handle_(other.handle_) {
    other.handle_ = 0;
  }

  WasmEvalHandle& operator=(WasmEvalHandle&& other) noexcept {
    if (this != &other) {
      reset();
      handle_ = other.handle_;
      other.handle_ = 0;
    }
    return *this;
  }

  ~WasmEvalHandle() {
    reset();
  }

  [[nodiscard]] uintptr_t get() const noexcept {
    return handle_;
  }

  void reset() noexcept {
    vibe_othello_wasm_free_eval_artifact(handle_);
    handle_ = 0;
  }

private:
  uintptr_t handle_ = 0;
};

WasmEvalHandle load_wasm_eval_artifact(const std::string& manifest_text,
                                       const std::vector<std::uint8_t>& weights_bytes) {
  uintptr_t handle = 0;
  REQUIRE(vibe_othello_wasm_load_eval_artifact(
              reinterpret_cast<const std::uint8_t*>(manifest_text.data()),
              static_cast<std::uint32_t>(manifest_text.size()), weights_bytes.data(),
              static_cast<std::uint32_t>(weights_bytes.size()),
              &handle) == VIBE_OTHELLO_WASM_STATUS_OK);
  REQUIRE(handle != 0);
  return WasmEvalHandle{handle};
}

vibe_othello::evaluation::PatternEvaluator
direct_pattern_evaluator(const std::string& manifest_text,
                         const std::vector<std::uint8_t>& weights_bytes) {
  vibe_othello::evaluation::PatternArtifactBytesLoadResult result =
      vibe_othello::evaluation::load_pattern_artifact_from_bytes(
          manifest_text, weights_bytes, committed_manifest_path().generic_string());
  REQUIRE(result.ok());

  vibe_othello::evaluation::LoadedPatternArtifactBytes artifact = std::move(*result.artifact);
  return vibe_othello::evaluation::PatternEvaluator{std::move(artifact.weights),
                                                    std::move(artifact.feature_set)};
}

bool is_legal_root_move(Position position, Move move) {
  vibe_othello::board_core::MoveDelta delta{};
  return vibe_othello::board_core::make_move_delta(position, move, &delta);
}

TEST_CASE("WASM adapter exposes ABI version", "[wasm]") {
  REQUIRE(vibe_othello_wasm_abi_version() == VIBE_OTHELLO_WASM_ABI_VERSION);
  REQUIRE(vibe_othello_wasm_abi_version() == 1u);
}

TEST_CASE("WASM adapter exposes robust struct layout introspection", "[wasm]") {
  const uint32_t position_size = vibe_othello_wasm_sizeof_position();
  const uint32_t position_player = vibe_othello_wasm_offsetof_position_player();
  const uint32_t position_opponent = vibe_othello_wasm_offsetof_position_opponent();
  const uint32_t position_side_to_move = vibe_othello_wasm_offsetof_position_side_to_move();

  REQUIRE(position_size >= 17u);
  REQUIRE(position_player < position_size);
  REQUIRE(position_opponent < position_size);
  REQUIRE(position_side_to_move < position_size);
  REQUIRE(position_player != position_opponent);
  REQUIRE(position_player < position_opponent);
  REQUIRE(position_opponent < position_side_to_move);

  const uint32_t query_size = vibe_othello_wasm_sizeof_position_query();
  const uint32_t query_status = vibe_othello_wasm_offsetof_position_query_status();
  const uint32_t query_legal_moves = vibe_othello_wasm_offsetof_position_query_legal_moves();
  const uint32_t query_has_legal_move = vibe_othello_wasm_offsetof_position_query_has_legal_move();
  const uint32_t query_is_terminal = vibe_othello_wasm_offsetof_position_query_is_terminal();

  REQUIRE(query_size >= 14u);
  REQUIRE(query_status < query_legal_moves);
  REQUIRE(query_legal_moves < query_has_legal_move);
  REQUIRE(query_has_legal_move < query_is_terminal);
  REQUIRE(query_is_terminal < query_size);

  const uint32_t apply_size = vibe_othello_wasm_sizeof_apply_result();
  const uint32_t apply_status = vibe_othello_wasm_offsetof_apply_result_status();
  const uint32_t apply_position = vibe_othello_wasm_offsetof_apply_result_position();
  const uint32_t apply_flipped = vibe_othello_wasm_offsetof_apply_result_flipped();
  const uint32_t apply_legal_moves = vibe_othello_wasm_offsetof_apply_result_legal_moves();
  const uint32_t apply_has_legal_move = vibe_othello_wasm_offsetof_apply_result_has_legal_move();
  const uint32_t apply_is_terminal = vibe_othello_wasm_offsetof_apply_result_is_terminal();

  REQUIRE(apply_size >= 39u);
  REQUIRE(apply_status < apply_position);
  REQUIRE(apply_position < apply_flipped);
  REQUIRE(apply_flipped < apply_legal_moves);
  REQUIRE(apply_legal_moves < apply_has_legal_move);
  REQUIRE(apply_has_legal_move < apply_is_terminal);
  REQUIRE(apply_is_terminal < apply_size);
  REQUIRE(apply_position + position_size <= apply_size);

  const uint32_t search_size = vibe_othello_wasm_sizeof_search_result();
  const uint32_t search_status = vibe_othello_wasm_offsetof_search_result_status();
  const uint32_t search_has_best_move = vibe_othello_wasm_offsetof_search_result_has_best_move();
  const uint32_t search_best_move_square =
      vibe_othello_wasm_offsetof_search_result_best_move_square();
  const uint32_t search_is_pass = vibe_othello_wasm_offsetof_search_result_is_pass();
  const uint32_t search_score = vibe_othello_wasm_offsetof_search_result_score();
  const uint32_t search_completed_depth =
      vibe_othello_wasm_offsetof_search_result_completed_depth();
  const uint32_t search_nodes = vibe_othello_wasm_offsetof_search_result_nodes();
  const uint32_t search_elapsed_ms = vibe_othello_wasm_offsetof_search_result_elapsed_ms();
  const uint32_t search_stopped = vibe_othello_wasm_offsetof_search_result_stopped();
  const uint32_t search_exact = vibe_othello_wasm_offsetof_search_result_exact();

  REQUIRE(search_size >= 32u);
  REQUIRE(search_status < search_has_best_move);
  REQUIRE(search_has_best_move < search_best_move_square);
  REQUIRE(search_best_move_square < search_is_pass);
  REQUIRE(search_is_pass < search_score);
  REQUIRE(search_score < search_completed_depth);
  REQUIRE(search_completed_depth < search_nodes);
  REQUIRE(search_nodes < search_elapsed_ms);
  REQUIRE(search_elapsed_ms < search_stopped);
  REQUIRE(search_stopped < search_exact);
  REQUIRE(search_exact < search_size);
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

TEST_CASE("WASM adapter loads evaluation artifact and evaluates initial position", "[wasm]") {
  const std::string manifest_text = read_text_or_fail(committed_manifest_path());
  const std::vector<std::uint8_t> weights_bytes = read_bytes_or_fail(committed_weights_path());
  WasmEvalHandle artifact = load_wasm_eval_artifact(manifest_text, weights_bytes);
  vibe_othello::evaluation::PatternEvaluator direct_evaluator =
      direct_pattern_evaluator(manifest_text, weights_bytes);

  const Position engine_position = vibe_othello::board_core::initial_position();
  const vibe_othello_wasm_position position = to_wasm_position(engine_position);
  std::int32_t wasm_score = 0;

  REQUIRE(vibe_othello_wasm_evaluate_position(artifact.get(), &position, &wasm_score) ==
          VIBE_OTHELLO_WASM_STATUS_OK);
  REQUIRE(wasm_score == direct_evaluator.evaluate(engine_position));
}

TEST_CASE("WASM adapter runs bounded artifact-backed best-move search", "[wasm]") {
  const std::string manifest_text = read_text_or_fail(committed_manifest_path());
  const std::vector<std::uint8_t> weights_bytes = read_bytes_or_fail(committed_weights_path());
  WasmEvalHandle artifact = load_wasm_eval_artifact(manifest_text, weights_bytes);

  const Position engine_position = vibe_othello::board_core::initial_position();
  const vibe_othello_wasm_position position = to_wasm_position(engine_position);
  vibe_othello_wasm_search_result result{};

  REQUIRE(vibe_othello_wasm_search_best_move(artifact.get(), &position, 1, 0, 0, &result) ==
          VIBE_OTHELLO_WASM_STATUS_OK);

  REQUIRE(result.status == VIBE_OTHELLO_WASM_STATUS_OK);
  REQUIRE(result.completed_depth == 1);
  REQUIRE(result.nodes > 0);
  REQUIRE(result.has_best_move == 1);
  REQUIRE(result.is_pass == 0);
  REQUIRE(result.best_move_square < 64);

  const Move best_move = vibe_othello::board_core::make_move(
      vibe_othello::board_core::square_from_index(static_cast<int>(result.best_move_square)));
  REQUIRE(is_legal_root_move(engine_position, best_move));
}

TEST_CASE("WASM adapter rejects invalid evaluation artifact ABI inputs", "[wasm]") {
  const std::string manifest_text = read_text_or_fail(committed_manifest_path());
  const std::vector<std::uint8_t> weights_bytes = read_bytes_or_fail(committed_weights_path());
  uintptr_t handle = 0;

  REQUIRE(vibe_othello_wasm_load_eval_artifact(
              reinterpret_cast<const std::uint8_t*>(manifest_text.data()),
              static_cast<std::uint32_t>(manifest_text.size()), weights_bytes.data(),
              static_cast<std::uint32_t>(weights_bytes.size()),
              nullptr) == VIBE_OTHELLO_WASM_STATUS_NULL_POINTER);
  REQUIRE(vibe_othello_wasm_load_eval_artifact(
              reinterpret_cast<const std::uint8_t*>(manifest_text.data()), 0, weights_bytes.data(),
              static_cast<std::uint32_t>(weights_bytes.size()),
              &handle) == VIBE_OTHELLO_WASM_STATUS_INVALID_ARGUMENT);
  REQUIRE(vibe_othello_wasm_load_eval_artifact(
              reinterpret_cast<const std::uint8_t*>(manifest_text.data()),
              static_cast<std::uint32_t>(manifest_text.size()), weights_bytes.data(), 0,
              &handle) == VIBE_OTHELLO_WASM_STATUS_INVALID_ARGUMENT);
  REQUIRE(vibe_othello_wasm_load_eval_artifact(
              nullptr, static_cast<std::uint32_t>(manifest_text.size()), weights_bytes.data(),
              static_cast<std::uint32_t>(weights_bytes.size()),
              &handle) == VIBE_OTHELLO_WASM_STATUS_NULL_POINTER);
  REQUIRE(vibe_othello_wasm_load_eval_artifact(
              reinterpret_cast<const std::uint8_t*>(manifest_text.data()),
              static_cast<std::uint32_t>(manifest_text.size()), nullptr,
              static_cast<std::uint32_t>(weights_bytes.size()),
              &handle) == VIBE_OTHELLO_WASM_STATUS_NULL_POINTER);
  REQUIRE(handle == 0);
}

TEST_CASE("WASM adapter rejects missing evaluator and null eval/search pointers", "[wasm]") {
  const vibe_othello_wasm_position position =
      to_wasm_position(vibe_othello::board_core::initial_position());
  std::int32_t score = 0;
  vibe_othello_wasm_search_result search_result{};

  REQUIRE(vibe_othello_wasm_evaluate_position(0, &position, &score) ==
          VIBE_OTHELLO_WASM_STATUS_EVALUATOR_NOT_LOADED);
  REQUIRE(vibe_othello_wasm_search_best_move(0, &position, 1, 0, 0, &search_result) ==
          VIBE_OTHELLO_WASM_STATUS_EVALUATOR_NOT_LOADED);
  REQUIRE(search_result.status == VIBE_OTHELLO_WASM_STATUS_EVALUATOR_NOT_LOADED);

  const uintptr_t invalid_handle = std::numeric_limits<uintptr_t>::max();
  REQUIRE(vibe_othello_wasm_evaluate_position(invalid_handle, &position, &score) ==
          VIBE_OTHELLO_WASM_STATUS_EVALUATOR_NOT_LOADED);
  REQUIRE(vibe_othello_wasm_search_best_move(invalid_handle, &position, 1, 0, 0, &search_result) ==
          VIBE_OTHELLO_WASM_STATUS_EVALUATOR_NOT_LOADED);
  REQUIRE(search_result.status == VIBE_OTHELLO_WASM_STATUS_EVALUATOR_NOT_LOADED);

  const std::string manifest_text = read_text_or_fail(committed_manifest_path());
  const std::vector<std::uint8_t> weights_bytes = read_bytes_or_fail(committed_weights_path());
  WasmEvalHandle artifact = load_wasm_eval_artifact(manifest_text, weights_bytes);
  const uintptr_t stale_handle = artifact.get();
  artifact.reset();
  REQUIRE(vibe_othello_wasm_evaluate_position(stale_handle, &position, &score) ==
          VIBE_OTHELLO_WASM_STATUS_EVALUATOR_NOT_LOADED);
  REQUIRE(vibe_othello_wasm_search_best_move(stale_handle, &position, 1, 0, 0, &search_result) ==
          VIBE_OTHELLO_WASM_STATUS_EVALUATOR_NOT_LOADED);
  REQUIRE(search_result.status == VIBE_OTHELLO_WASM_STATUS_EVALUATOR_NOT_LOADED);

  artifact = load_wasm_eval_artifact(manifest_text, weights_bytes);

  REQUIRE(vibe_othello_wasm_evaluate_position(artifact.get(), nullptr, &score) ==
          VIBE_OTHELLO_WASM_STATUS_NULL_POINTER);
  REQUIRE(vibe_othello_wasm_evaluate_position(artifact.get(), &position, nullptr) ==
          VIBE_OTHELLO_WASM_STATUS_NULL_POINTER);
  REQUIRE(vibe_othello_wasm_search_best_move(artifact.get(), nullptr, 1, 0, 0, &search_result) ==
          VIBE_OTHELLO_WASM_STATUS_NULL_POINTER);
  REQUIRE(search_result.status == VIBE_OTHELLO_WASM_STATUS_NULL_POINTER);
  REQUIRE(vibe_othello_wasm_search_best_move(artifact.get(), &position, 1, 0, 0, nullptr) ==
          VIBE_OTHELLO_WASM_STATUS_NULL_POINTER);
  REQUIRE(vibe_othello_wasm_search_best_move(artifact.get(), &position, 0, 0, 0, &search_result) ==
          VIBE_OTHELLO_WASM_STATUS_INVALID_ARGUMENT);
  REQUIRE(search_result.status == VIBE_OTHELLO_WASM_STATUS_INVALID_ARGUMENT);
}

TEST_CASE("WASM adapter rejects invalid positions for eval/search", "[wasm]") {
  const std::string manifest_text = read_text_or_fail(committed_manifest_path());
  const std::vector<std::uint8_t> weights_bytes = read_bytes_or_fail(committed_weights_path());
  WasmEvalHandle artifact = load_wasm_eval_artifact(manifest_text, weights_bytes);

  vibe_othello_wasm_position position =
      to_wasm_position(vibe_othello::board_core::initial_position());
  position.opponent |= position.player;
  std::int32_t score = 0;
  vibe_othello_wasm_search_result search_result{};

  REQUIRE(vibe_othello_wasm_evaluate_position(artifact.get(), &position, &score) ==
          VIBE_OTHELLO_WASM_STATUS_INVALID_POSITION);
  REQUIRE(vibe_othello_wasm_search_best_move(artifact.get(), &position, 1, 0, 0, &search_result) ==
          VIBE_OTHELLO_WASM_STATUS_INVALID_POSITION);
  REQUIRE(search_result.status == VIBE_OTHELLO_WASM_STATUS_INVALID_POSITION);
}

TEST_CASE("WASM adapter reports artifact load failure for malformed artifact bytes", "[wasm]") {
  const std::string manifest_text = read_text_or_fail(committed_manifest_path());
  const std::vector<std::uint8_t> weights_bytes = read_bytes_or_fail(committed_weights_path());
  uintptr_t handle = 0;

  const std::string malformed_manifest = "{not json";
  REQUIRE(vibe_othello_wasm_load_eval_artifact(
              reinterpret_cast<const std::uint8_t*>(malformed_manifest.data()),
              static_cast<std::uint32_t>(malformed_manifest.size()), weights_bytes.data(),
              static_cast<std::uint32_t>(weights_bytes.size()),
              &handle) == VIBE_OTHELLO_WASM_STATUS_ARTIFACT_LOAD_FAILED);
  REQUIRE(handle == 0);

  std::vector<std::uint8_t> corrupt_weights = weights_bytes;
  REQUIRE_FALSE(corrupt_weights.empty());
  corrupt_weights.front() ^= 0xFFU;
  REQUIRE(vibe_othello_wasm_load_eval_artifact(
              reinterpret_cast<const std::uint8_t*>(manifest_text.data()),
              static_cast<std::uint32_t>(manifest_text.size()), corrupt_weights.data(),
              static_cast<std::uint32_t>(corrupt_weights.size()),
              &handle) == VIBE_OTHELLO_WASM_STATUS_ARTIFACT_LOAD_FAILED);
  REQUIRE(handle == 0);
}

TEST_CASE("WASM adapter free_eval_artifact accepts zero", "[wasm]") {
  vibe_othello_wasm_free_eval_artifact(0);
}

} // namespace
