#include "probcut_internal.h"
#include "search_context_internal.h"
#include "search_internal.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace vibe_othello::search::internal {
namespace {

// Heuristic ProbCut is kept away from search sentinels. Exact terminal values
// never reach this gate, and this guard also prevents a fitted value from being
// confused with kScoreLoss/kScoreWin.
constexpr Score kProbCutSentinelGuard = 256;

bool score_is_safely_heuristic(Score score) noexcept {
  return score > kScoreLoss + kProbCutSentinelGuard && score < kScoreWin - kProbCutSentinelGuard;
}

std::uint8_t phase_for(board_core::Position position) noexcept {
  const int occupied = std::popcount(board_core::occupied(position));
  const int normalized_count = std::max(0, occupied - 4);
  return static_cast<std::uint8_t>(std::min(12, (normalized_count * 13) / 60));
}

const ProbCutCalibrationEntryV1* entry_for(const ProbCutCalibrationProfileV1& profile,
                                           std::uint8_t phase, Depth deep_depth,
                                           Depth shallow_depth, bool* phase_supported) noexcept {
  *phase_supported = false;
  for (const ProbCutCalibrationEntryV1& entry : profile.entries) {
    if (entry.phase != phase) {
      continue;
    }
    *phase_supported = true;
    if (entry.deep_depth == deep_depth && entry.shallow_depth == shallow_depth) {
      return &entry;
    }
  }
  return nullptr;
}

struct ScopedProbCutShallow {
  explicit ScopedProbCutShallow(SearchContext* search_context) noexcept
      : context(search_context), previous_in_probcut(search_context->in_probcut_shallow),
        previous_exact_endgame(search_context->options.endgame.exact_endgame),
        previous_exact_empties(search_context->options.endgame.endgame_exact_empties),
        previous_shadow_calibration(search_context->shadow_calibration) {
    context->in_probcut_shallow = true;
    context->options.endgame.exact_endgame = false;
    context->options.endgame.endgame_exact_empties = 0;
    context->shadow_calibration = nullptr;
  }

  ~ScopedProbCutShallow() noexcept {
    context->in_probcut_shallow = previous_in_probcut;
    context->options.endgame.exact_endgame = previous_exact_endgame;
    context->options.endgame.endgame_exact_empties = previous_exact_empties;
    context->shadow_calibration = previous_shadow_calibration;
  }

  SearchContext* context;
  bool previous_in_probcut;
  bool previous_exact_endgame;
  std::uint8_t previous_exact_empties;
  ShadowCalibrationRun* previous_shadow_calibration;
};

bool confidence_accepts(const ProbCutOptionsV1& options, const ProbCutCalibrationEntryV1& entry,
                        Score shallow_score, Score beta, Score* lower_bound) noexcept {
  if (!score_is_safely_heuristic(shallow_score) || shallow_score < entry.minimum_shallow_score ||
      shallow_score > entry.maximum_shallow_score || beta < entry.minimum_beta ||
      beta > entry.maximum_beta) {
    return false;
  }

  const long double confidence = std::max(static_cast<long double>(entry.confidence_multiplier),
                                          static_cast<long double>(options.confidence_multiplier));
  const long double raw_margin = confidence * static_cast<long double>(entry.residual_sigma);
  if (!std::isfinite(raw_margin) || raw_margin < 0.0L ||
      raw_margin > static_cast<long double>(std::numeric_limits<Score>::max())) {
    return false;
  }
  const auto calibrated_margin = static_cast<std::int64_t>(std::ceil(raw_margin));
  const std::int64_t effective_margin =
      std::max<std::int64_t>(calibrated_margin, options.minimum_margin);
  if (effective_margin > options.maximum_margin) {
    return false;
  }

  const long double predicted = static_cast<long double>(entry.regression_slope) * shallow_score +
                                static_cast<long double>(entry.intercept);
  if (!std::isfinite(predicted) ||
      predicted < static_cast<long double>(std::numeric_limits<Score>::min()) ||
      predicted > static_cast<long double>(std::numeric_limits<Score>::max())) {
    return false;
  }

  // Integer decision for the calibrated one-sided confidence condition:
  //   predicted_deep = a * shallow_score + b
  //   floor(predicted_deep) - max(ceil(k * sigma), minimum_margin) >= beta
  // maximum_margin is an eligibility ceiling, never an unsafe downward clamp.
  const auto predicted_floor = static_cast<std::int64_t>(std::floor(predicted));
  const std::int64_t conservative_lower = predicted_floor - effective_margin;
  if (conservative_lower <= kScoreLoss || conservative_lower >= kScoreWin ||
      !score_is_safely_heuristic(static_cast<Score>(conservative_lower))) {
    return false;
  }
  *lower_bound = static_cast<Score>(conservative_lower);
  return *lower_bound >= beta;
}

} // namespace

std::optional<SearchNodeResult>
maybe_probcut(SearchContext* context, Score alpha, Score beta, Depth depth, Ply ply, bool cut_node,
              std::optional<ProbCutShadowCandidate>* shadow_candidate) {
  *shadow_candidate = std::nullopt;
  if (context == nullptr || !context->options.probcut.use_probcut || context->in_probcut_shallow ||
      context->in_iid || !cut_node) {
    return std::nullopt;
  }

  const ProbCutOptionsV1& options = context->options.probcut;
  if (static_cast<std::int64_t>(beta) - alpha != 1 || !options.non_pv_only || !options.beta_only ||
      context->options.experimental.use_legacy_search_kernel ||
      options.calibration_profile == nullptr ||
      options.calibration_profile->node_class != ProbCutNodeClassV1::non_pv_scout_beta_only) {
    return std::nullopt;
  }
  if (depth < options.minimum_depth || depth <= options.shallow_depth_reduction) {
    ++context->stats.probcut_rejected_by_depth;
    return std::nullopt;
  }
  if (!score_is_safely_heuristic(alpha) || !score_is_safely_heuristic(beta)) {
    ++context->stats.probcut_rejected_confidence;
    return std::nullopt;
  }

  StackFrame& frame = context->stack.at(ply);
  if (frame.legal_moves == 0) {
    ++context->stats.probcut_rejected_pass;
    return std::nullopt;
  }

  const auto empties = static_cast<std::uint8_t>(
      board_core::kSquareCount -
      std::popcount(board_core::occupied(context->position_state.position)));
  if (options.disable_near_exact && ((context->options.endgame.endgame_exact_empties != 0 &&
                                      empties <= context->options.endgame.endgame_exact_empties) ||
                                     context->options.mode == SearchMode::exact_score ||
                                     context->options.mode == SearchMode::win_loss_draw)) {
    ++context->stats.probcut_rejected_near_exact;
    return std::nullopt;
  }

  const Depth shallow_depth = static_cast<Depth>(depth - options.shallow_depth_reduction);
  const ProbCutCalibrationProfileV1& profile = *options.calibration_profile;
  bool phase_supported = false;
  const ProbCutCalibrationEntryV1* entry = entry_for(
      profile, phase_for(context->position_state.position), depth, shallow_depth, &phase_supported);
  if (entry == nullptr) {
    if (phase_supported) {
      ++context->stats.probcut_rejected_by_depth;
    } else {
      ++context->stats.probcut_rejected_by_phase;
    }
    return std::nullopt;
  }

  ++context->stats.probcut_attempts;
  const NodeCount before_nodes = context->stats.nodes;
  const board_core::Bitboard legal_moves = frame.legal_moves;
  SearchNodeResult shallow;
  {
    const ScopedProbCutShallow guard{context};
    shallow = full_window_search(context, kScoreLoss, kScoreWin, shallow_depth, ply);
  }
  context->stats.probcut_shallow_nodes += context->stats.nodes - before_nodes;
  context->stack[ply] = StackFrame{};
  context->stack[ply].legal_moves = legal_moves;

  if (shallow.is_stopped() || should_stop_search(context)) {
    return SearchNodeResult::stopped();
  }

  Score conservative_lower = 0;
  if (!confidence_accepts(options, *entry, shallow.value().score, beta, &conservative_lower)) {
    ++context->stats.probcut_rejected_confidence;
    return std::nullopt;
  }

  ++context->stats.probcut_successes;
  if (options.shadow_verify) {
    ++context->stats.probcut_shadow_candidates;
    *shadow_candidate = ProbCutShadowCandidate{.beta = beta};
    return std::nullopt;
  }

  ++context->stats.probcut_beta_cutoffs;
  ++context->stats.beta_cutoffs;
  ++context->stats.selective_cuts;
  if (context->transposition_table != nullptr && context->options.midgame.use_midgame_tt) {
    context->transposition_table->store_value(context->position_state.key, depth, beta,
                                              BoundType::lower, TTEntryKind::midgame,
                                              &context->stats, true);
  }
  return SearchNodeResult::completed(
      SearchValue{
          .score = beta,
          .pv = {},
      },
      true);
}

void complete_probcut_shadow(SearchContext* context, const ProbCutShadowCandidate& candidate,
                             const SearchNodeResult& deep_result) noexcept {
  if (context == nullptr || !deep_result.is_complete()) {
    return;
  }
  ++context->stats.probcut_shadow_verifications;
  if (deep_result.value().score < candidate.beta) {
    ++context->stats.probcut_shadow_false_cuts;
  }
}

} // namespace vibe_othello::search::internal
