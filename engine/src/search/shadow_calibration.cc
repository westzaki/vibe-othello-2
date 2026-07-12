#include "search_context_internal.h"
#include "search_internal.h"
#include "shadow_calibration_internal.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

namespace vibe_othello::search::internal {
namespace {

constexpr std::uint64_t kFnvOffset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;

void mix_byte(std::uint8_t value, std::uint64_t* hash) noexcept {
  *hash ^= value;
  *hash *= kFnvPrime;
}

template <typename Integer> void mix_integer(Integer value, std::uint64_t* hash) noexcept {
  using Unsigned = std::make_unsigned_t<Integer>;
  auto bits = static_cast<std::uint64_t>(static_cast<Unsigned>(value));
  for (std::size_t index = 0; index < sizeof(Unsigned); ++index) {
    mix_byte(static_cast<std::uint8_t>(bits & 0xffU), hash);
    bits >>= 8U;
  }
}

void mix_string(std::string_view value, std::uint64_t* hash) noexcept {
  mix_integer(static_cast<std::uint64_t>(value.size()), hash);
  for (const char character : value) {
    mix_byte(static_cast<std::uint8_t>(character), hash);
  }
}

std::string hex_u64(std::uint64_t value) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result(16, '0');
  for (auto output = result.rbegin(); output != result.rend(); ++output) {
    *output = digits.at(static_cast<std::size_t>(value & 0xfU));
    value >>= 4U;
  }
  return result;
}

enum class BoardTransform : std::uint8_t {
  identity,
  rotate_90,
  rotate_180,
  rotate_270,
  reflect,
  reflect_rotate_90,
  reflect_rotate_180,
  reflect_rotate_270,
};

board_core::Bitboard transform_bitboard(board_core::Bitboard bits,
                                        BoardTransform transform) noexcept {
  board_core::Bitboard transformed = 0;
  const int transform_index = static_cast<int>(transform);
  while (bits != 0) {
    const int source = std::countr_zero(bits);
    bits &= bits - 1;
    int file = source % board_core::kBoardSize;
    int rank = source / board_core::kBoardSize;
    if (transform_index >= 4) {
      file = board_core::kBoardSize - 1 - file;
    }
    for (int rotation = 0; rotation < transform_index % 4; ++rotation) {
      const int previous_file = file;
      file = board_core::kBoardSize - 1 - rank;
      rank = previous_file;
    }
    transformed |= board_core::Bitboard{1}
                   << static_cast<unsigned>((rank * board_core::kBoardSize) + file);
  }
  return transformed;
}

std::uint64_t canonical_position_hash(board_core::Position position) noexcept {
  board_core::Bitboard canonical_player = std::numeric_limits<board_core::Bitboard>::max();
  board_core::Bitboard canonical_opponent = std::numeric_limits<board_core::Bitboard>::max();
  for (std::uint8_t transform_index = 0; transform_index < 8; ++transform_index) {
    const BoardTransform transform = static_cast<BoardTransform>(transform_index);
    const board_core::Bitboard player = transform_bitboard(position.player, transform);
    const board_core::Bitboard opponent = transform_bitboard(position.opponent, transform);
    if (player < canonical_player ||
        (player == canonical_player && opponent < canonical_opponent)) {
      canonical_player = player;
      canonical_opponent = opponent;
    }
  }
  std::uint64_t hash = kFnvOffset;
  mix_integer(canonical_player, &hash);
  mix_integer(canonical_opponent, &hash);
  return hash;
}

std::string make_search_identity(std::uint64_t root_hash, const SelectiveSearchOptionsV1& options) {
  std::uint64_t hash = kFnvOffset;
  mix_string("mpc-shadow-search-v1", &hash);
  mix_integer(root_hash, &hash);
  mix_string(options.repo_sha, &hash);
  mix_string(options.search_config_id, &hash);
  mix_string(options.evaluator_id, &hash);
  mix_string(options.artifact_id, &hash);
  mix_integer(options.sampling_seed, &hash);
  return hex_u64(hash);
}

std::uint64_t sample_hash(const ShadowCalibrationRun& run, const ShadowCandidate& candidate,
                          NodeCount candidate_ordinal) noexcept {
  std::uint64_t hash = kFnvOffset;
  mix_string("mpc-shadow-sample-select-v1", &hash);
  mix_string(run.search_identity, &hash);
  mix_integer(candidate.canonical_position_hash, &hash);
  mix_integer(candidate.deep_depth, &hash);
  mix_integer(candidate.ply, &hash);
  mix_integer(candidate.alpha, &hash);
  mix_integer(candidate.beta, &hash);
  mix_integer(candidate_ordinal, &hash);
  return hash;
}

std::optional<board_core::Move> best_move(const SearchNodeResult& result) noexcept {
  if (!result.is_complete() || result.value().pv.size == 0) {
    return std::nullopt;
  }
  return result.value().pv.moves.front();
}

ShadowDeepResult deep_result_for(Score score, const ShadowCandidate& candidate) noexcept {
  if (score <= candidate.alpha) {
    return ShadowDeepResult::fail_low;
  }
  if (score >= candidate.beta) {
    return ShadowDeepResult::fail_high;
  }
  return ShadowDeepResult::exact;
}

ShadowNodeType node_type_for(bool pv_node, ShadowDeepResult deep_result) noexcept {
  if (pv_node) {
    return ShadowNodeType::pv;
  }
  return deep_result == ShadowDeepResult::fail_high ? ShadowNodeType::cut : ShadowNodeType::all;
}

std::uint8_t calibration_phase(std::uint8_t occupied_count) noexcept {
  const int normalized_count = occupied_count < 4 ? 0 : static_cast<int>(occupied_count) - 4;
  return static_cast<std::uint8_t>(std::min(12, (normalized_count * 13) / 60));
}

struct ScopedDeadlinePause {
  explicit ScopedDeadlinePause(SearchLimitState* search_limit_state) noexcept
      : state(search_limit_state), start(std::chrono::steady_clock::now()) {}

  ~ScopedDeadlinePause() noexcept {
    if (state != nullptr && state->has_deadline) {
      state->deadline += std::chrono::steady_clock::now() - start;
    }
  }

  ScopedDeadlinePause(const ScopedDeadlinePause&) = delete;
  ScopedDeadlinePause& operator=(const ScopedDeadlinePause&) = delete;
  ScopedDeadlinePause(ScopedDeadlinePause&&) = delete;
  ScopedDeadlinePause& operator=(ScopedDeadlinePause&&) = delete;

  SearchLimitState* state = nullptr;
  std::chrono::steady_clock::time_point start;
};

} // namespace

std::optional<ShadowCalibrationRun> make_shadow_calibration_run(board_core::Position root,
                                                                SelectiveSearchOptionsV1 options) {
  if (!options.enable_shadow_calibration || options.sink == nullptr || options.sample_rate == 0 ||
      options.max_samples_per_search == 0 || options.minimum_deep_depth <= 0 ||
      options.shallow_depth_reduction <= 0 || options.repo_sha.empty() ||
      options.search_config_id.empty() || options.evaluator_id.empty() ||
      options.artifact_id.empty()) {
    return std::nullopt;
  }
  options.sample_rate = std::min(options.sample_rate, kShadowCalibrationSampleRateScale);
  const std::uint64_t root_hash = canonical_position_hash(root);
  return ShadowCalibrationRun{
      .options = options,
      .search_identity = make_search_identity(root_hash, options),
  };
}

std::optional<ShadowCandidate> begin_shadow_candidate(SearchContext* context, Score alpha,
                                                      Score beta, Depth depth, Ply ply) {
  if (context == nullptr || context->shadow_calibration == nullptr) {
    return std::nullopt;
  }
  ShadowCalibrationRun& run = *context->shadow_calibration;
  if (context->in_iid || depth < run.options.minimum_deep_depth ||
      depth <= run.options.shallow_depth_reduction) {
    return std::nullopt;
  }

  const bool pv_node = static_cast<int>(beta) - static_cast<int>(alpha) > 1;
  if (pv_node && !run.options.include_pv_nodes) {
    return std::nullopt;
  }

  const board_core::Bitboard legal = context->stack.at(ply).legal_moves;
  const bool pass_state = legal == 0;
  const bool terminal_state = pass_state && opponent_legal_moves(context->position_state) == 0;
  if (pass_state && !run.options.include_pass_nodes) {
    return std::nullopt;
  }

  const auto occupied_count = static_cast<std::uint8_t>(
      std::popcount(board_core::occupied(context->position_state.position)));
  const auto empties = static_cast<std::uint8_t>(board_core::kSquareCount - occupied_count);
  const bool exact_handoff_eligible = context->options.endgame.exact_endgame &&
                                      empties <= context->options.endgame.endgame_exact_empties;
  if (exact_handoff_eligible && !run.options.include_near_exact_nodes) {
    return std::nullopt;
  }

  ++run.stats.shadow_candidates;
  if (run.reserved_samples >= run.options.max_samples_per_search) {
    return std::nullopt;
  }

  ShadowCandidate candidate{
      .position = context->position_state.position,
      .canonical_position_hash = canonical_position_hash(context->position_state.position),
      .alpha = alpha,
      .beta = beta,
      .deep_depth = depth,
      .shallow_depth = static_cast<Depth>(depth - run.options.shallow_depth_reduction),
      .ply = ply,
      .occupied_count = occupied_count,
      .empties = empties,
      .pv_node = pv_node,
      .pass_state = pass_state,
      .terminal_state = terminal_state,
      .exact_handoff_eligible = exact_handoff_eligible,
  };

  if (sample_hash(run, candidate, run.stats.shadow_candidates) %
          kShadowCalibrationSampleRateScale >=
      run.options.sample_rate) {
    return std::nullopt;
  }
  ++run.reserved_samples;
  return candidate;
}

void complete_shadow_candidate(SearchContext* context, const ShadowCandidate& candidate,
                               const SearchNodeResult& deep_result) {
  if (context == nullptr || context->shadow_calibration == nullptr || !deep_result.is_complete()) {
    return;
  }
  ShadowCalibrationRun& run = *context->shadow_calibration;
  const ScopedDeadlinePause deadline_pause{context->limit_state};

  ResolvedSearchOptions shadow_options = context->options;
  shadow_options.selective = {};
  shadow_options.endgame.exact_endgame = false;
  shadow_options.endgame.endgame_exact_empties = 0;
  MidgameOrderingState ordering_state{};
  SearchContext shallow_context{
      .position_state =
          make_search_position(candidate.position, &context->evaluator, candidate.shallow_depth),
      .evaluator = context->evaluator,
      .limits = SearchLimits{.max_depth = candidate.shallow_depth},
      .options = shadow_options,
      .ordering_state = &ordering_state,
  };
  if (shallow_context.position_state.evaluation_state.has_value()) {
    shallow_context.stats.incremental_eval_enabled = true;
    ++shallow_context.stats.incremental_state_initializations;
  }
  const SearchNodeResult shallow_result = full_window_search(
      &shallow_context, candidate.alpha, candidate.beta, candidate.shallow_depth, Ply{0});
  run.stats.shadow_shallow_nodes += shallow_context.stats.nodes;
  if (!shallow_result.is_complete()) {
    return;
  }

  const Score deep_score = deep_result.value().score;
  const Score shallow_score = shallow_result.value().score;
  const ShadowDeepResult actual_deep_result = deep_result_for(deep_score, candidate);
  const ShadowNodeType node_type = node_type_for(candidate.pv_node, actual_deep_result);
  const std::optional<board_core::Move> shallow_best_move = best_move(shallow_result);
  const std::optional<board_core::Move> deep_best_move = best_move(deep_result);
  const bool best_move_agreement = shallow_best_move.has_value() && deep_best_move.has_value() &&
                                   shallow_best_move == deep_best_move;
  const bool hypothetical_cut_high = shallow_score >= candidate.beta;
  const bool hypothetical_cut_low = shallow_score <= candidate.alpha;
  const bool false_cut_high_candidate = hypothetical_cut_high && deep_score < candidate.beta;
  const bool false_cut_low_candidate = hypothetical_cut_low && deep_score > candidate.alpha;

  ShadowCalibrationSample sample{
      .repo_sha = std::string(run.options.repo_sha),
      .search_config_id = std::string(run.options.search_config_id),
      .evaluator_id = std::string(run.options.evaluator_id),
      .artifact_id = std::string(run.options.artifact_id),
      .canonical_position_hash = candidate.canonical_position_hash,
      .phase = calibration_phase(candidate.occupied_count),
      .occupied_count = candidate.occupied_count,
      .empties = candidate.empties,
      .ply = candidate.ply,
      .node_type = node_type,
      .pv_node = candidate.pv_node,
      .cut_node = node_type == ShadowNodeType::cut,
      .all_node = node_type == ShadowNodeType::all,
      .deep_depth = candidate.deep_depth,
      .shallow_depth = candidate.shallow_depth,
      .alpha = candidate.alpha,
      .beta = candidate.beta,
      .shallow_score = shallow_score,
      .deep_score = deep_score,
      .deep_bound = classify_bound(deep_score, candidate.alpha, candidate.beta),
      .shallow_best_move = shallow_best_move,
      .deep_best_move = deep_best_move,
      .best_move_agreement = best_move_agreement,
      .pass_state = candidate.pass_state,
      .terminal_state = candidate.terminal_state,
      .exact_handoff_eligible = candidate.exact_handoff_eligible,
      .actual_deep_result = actual_deep_result,
      .hypothetical_cut_high = hypothetical_cut_high,
      .hypothetical_cut_low = hypothetical_cut_low,
      .false_cut_high_candidate = false_cut_high_candidate,
      .false_cut_low_candidate = false_cut_low_candidate,
      .sampling_seed = run.options.sampling_seed,
      .search_identity = run.search_identity,
  };

  ++run.stats.shadow_samples;
  run.stats.shadow_best_move_agreements += best_move_agreement ? 1 : 0;
  run.stats.hypothetical_cut_highs += hypothetical_cut_high ? 1 : 0;
  run.stats.hypothetical_cut_lows += hypothetical_cut_low ? 1 : 0;
  run.stats.false_cut_high_candidates += false_cut_high_candidate ? 1 : 0;
  run.stats.false_cut_low_candidates += false_cut_low_candidate ? 1 : 0;
  run.options.sink->record(sample);
}

} // namespace vibe_othello::search::internal
