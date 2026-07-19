#include "probcut_internal.h"
#include "search_context_internal.h"
#include "search_internal.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

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

const ProbCutCalibrationEntryV1*
entry_for(const ProbCutCalibrationProfileV1& profile, std::uint8_t phase, std::uint8_t empties,
          Depth deep_depth, Depth shallow_depth, SearchMode search_mode, bool exact_handoff_enabled,
          std::uint8_t exact_handoff_threshold, std::uint8_t exact_handoff_distance) noexcept {
  for (const ProbCutCalibrationEntryV1& entry : profile.entries) {
    if (entry.phase == phase && entry.search_mode == search_mode &&
        entry.minimum_empties <= empties && empties <= entry.maximum_empties &&
        entry.deep_depth == deep_depth && entry.shallow_depth == shallow_depth &&
        entry.exact_handoff_enabled == exact_handoff_enabled &&
        entry.exact_handoff_threshold == exact_handoff_threshold &&
        entry.minimum_exact_handoff_distance <= exact_handoff_distance &&
        exact_handoff_distance <= entry.maximum_exact_handoff_distance) {
      return &entry;
    }
  }
  return nullptr;
}

ProbCutDepthPairStats& pair_stats(SearchStats* stats, std::uint8_t phase, Depth deep_depth,
                                  Depth shallow_depth) {
  for (ProbCutDepthPairStats& entry : stats->probcut_by_phase_depth_pair) {
    if (entry.phase == phase && entry.deep_depth == deep_depth &&
        entry.shallow_depth == shallow_depth) {
      return entry;
    }
  }
  stats->probcut_by_phase_depth_pair.push_back(ProbCutDepthPairStats{
      .phase = phase,
      .deep_depth = deep_depth,
      .shallow_depth = shallow_depth,
  });
  return stats->probcut_by_phase_depth_pair.back();
}

enum class PairRejection : std::uint8_t {
  unsupported_profile,
  near_exact,
  pass,
  pv,
  root,
};

void record_pair_rejection(SearchStats* stats, const ProbCutOptionsV1& options, std::uint8_t phase,
                           Depth depth, PairRejection rejection) {
  if (options.calibration_profile == nullptr) {
    return;
  }
  const std::span<const ProbCutDepthPairV1> preference =
      options.ordered_depth_pairs.empty() ? options.calibration_profile->validated_pair_order
                                          : options.ordered_depth_pairs;
  for (const ProbCutDepthPairV1 pair : preference) {
    if (pair.deep_depth != depth || pair.shallow_depth <= 0 || pair.shallow_depth >= depth) {
      continue;
    }
    ProbCutDepthPairStats& telemetry =
        pair_stats(stats, phase, pair.deep_depth, pair.shallow_depth);
    switch (rejection) {
    case PairRejection::unsupported_profile:
      ++telemetry.unsupported_profile;
      break;
    case PairRejection::near_exact:
      ++telemetry.near_exact_rejections;
      break;
    case PairRejection::pass:
      ++telemetry.pass_rejections;
      break;
    case PairRejection::pv:
      ++telemetry.pv_rejections;
      break;
    case PairRejection::root:
      ++telemetry.root_rejections;
      break;
    }
  }
}

struct ScopedProbCutShallow {
  explicit ScopedProbCutShallow(SearchContext* search_context) noexcept
      : context(search_context), previous_in_probcut(search_context->in_probcut_shallow),
        previous_exact_endgame(search_context->options.endgame.exact_endgame),
        previous_exact_empties(search_context->options.endgame.endgame_exact_empties),
        previous_shadow_calibration(search_context->shadow_calibration),
        previous_transposition_table(search_context->transposition_table),
        previous_ordering_state(search_context->ordering_state) {
    context->in_probcut_shallow = true;
    context->options.endgame.exact_endgame = false;
    context->options.endgame.endgame_exact_empties = 0;
    context->shadow_calibration = nullptr;
    // Shallow probes are evidence for a bound, not official subtrees. Keep
    // their exact-looking values and ordering side effects out of reusable
    // state.
    context->transposition_table = nullptr;
    context->ordering_state = nullptr;
  }

  ~ScopedProbCutShallow() noexcept {
    context->in_probcut_shallow = previous_in_probcut;
    context->options.endgame.exact_endgame = previous_exact_endgame;
    context->options.endgame.endgame_exact_empties = previous_exact_empties;
    context->shadow_calibration = previous_shadow_calibration;
    context->transposition_table = previous_transposition_table;
    context->ordering_state = previous_ordering_state;
  }

  ScopedProbCutShallow(const ScopedProbCutShallow&) = delete;
  ScopedProbCutShallow& operator=(const ScopedProbCutShallow&) = delete;
  ScopedProbCutShallow(ScopedProbCutShallow&&) = delete;
  ScopedProbCutShallow& operator=(ScopedProbCutShallow&&) = delete;

  SearchContext* context;
  bool previous_in_probcut;
  bool previous_exact_endgame;
  std::uint8_t previous_exact_empties;
  ShadowCalibrationRun* previous_shadow_calibration;
  TranspositionTable* previous_transposition_table;
  MidgameOrderingState* previous_ordering_state;
};

bool confidence_accepts(const ProbCutOptionsV1& options, const ProbCutCalibrationEntryV1& entry,
                        Score shallow_score, Score beta, Score* lower_bound) noexcept {
  if (!score_is_safely_heuristic(shallow_score) || shallow_score < entry.minimum_shallow_score ||
      shallow_score > entry.maximum_shallow_score || beta < entry.minimum_beta ||
      beta > entry.maximum_beta) {
    return false;
  }

  const long double confidence = std::max({static_cast<long double>(entry.confidence_multiplier),
                                           static_cast<long double>(options.confidence_multiplier),
                                           static_cast<long double>(options.minimum_confidence)});
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

bool shallow_overhead_limit_reached(const SearchStats& stats, double maximum_ratio) noexcept {
  if (maximum_ratio <= 0.0 || stats.probcut_attempts == 0) {
    return false;
  }
  const NodeCount official_nodes =
      stats.nodes > stats.probcut_shallow_nodes ? stats.nodes - stats.probcut_shallow_nodes : 1;
  const long double ratio = static_cast<long double>(stats.probcut_shallow_nodes) /
                            static_cast<long double>(official_nodes);
  return ratio >= static_cast<long double>(maximum_ratio);
}

} // namespace

std::optional<SearchNodeResult>
maybe_probcut(SearchContext* context, Score alpha, Score beta, Depth depth, Ply ply, bool cut_node,
              std::optional<ProbCutShadowCandidate>* shadow_candidate) {
  *shadow_candidate = std::nullopt;
  if (context == nullptr || !context->options.probcut.use_probcut || context->in_probcut_shallow ||
      context->in_iid) {
    return std::nullopt;
  }

  const ProbCutOptionsV1& options = context->options.probcut;
  const std::uint8_t phase = phase_for(context->position_state.position);
  if (ply == 0) {
    ++context->stats.probcut_rejected_root;
    record_pair_rejection(&context->stats, options, phase, depth, PairRejection::root);
    return std::nullopt;
  }
  if (!cut_node) {
    ++context->stats.probcut_rejected_pv;
    record_pair_rejection(&context->stats, options, phase, depth, PairRejection::pv);
    return std::nullopt;
  }
  if (static_cast<std::int64_t>(beta) - alpha != 1 || !options.non_pv_only || !options.beta_only ||
      options.calibration_profile == nullptr ||
      options.calibration_profile->node_class != ProbCutNodeClassV1::non_pv_scout_beta_only) {
    return std::nullopt;
  }
  if (depth < options.minimum_depth) {
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
    record_pair_rejection(&context->stats, options, phase, depth, PairRejection::pass);
    return std::nullopt;
  }

  const auto empties = static_cast<std::uint8_t>(
      board_core::kSquareCount -
      std::popcount(board_core::occupied(context->position_state.position)));
  const bool exact_handoff_enabled =
      context->options.endgame.exact_endgame && context->options.endgame.endgame_exact_empties != 0;
  const std::uint8_t exact_threshold =
      exact_handoff_enabled ? context->options.endgame.endgame_exact_empties : std::uint8_t{0};
  const std::uint8_t near_exact_threshold =
      std::max(options.near_exact_disable_empties, exact_threshold);
  if (options.disable_near_exact &&
      ((near_exact_threshold != 0 && empties <= near_exact_threshold) ||
       context->options.mode == SearchMode::exact_score ||
       context->options.mode == SearchMode::win_loss_draw)) {
    ++context->stats.probcut_rejected_near_exact;
    record_pair_rejection(&context->stats, options, phase, depth, PairRejection::near_exact);
    return std::nullopt;
  }

  const ProbCutCalibrationProfileV1& profile = *options.calibration_profile;
  if ((options.enabled_phase_mask & (std::uint16_t{1} << phase)) == 0) {
    ++context->stats.probcut_rejected_by_phase;
    ++context->stats.probcut_unsupported_profile;
    record_pair_rejection(&context->stats, options, phase, depth,
                          PairRejection::unsupported_profile);
    return std::nullopt;
  }
  const std::uint8_t handoff_distance = exact_handoff_enabled && empties > exact_threshold
                                            ? static_cast<std::uint8_t>(empties - exact_threshold)
                                            : std::uint8_t{0};

  const std::span<const ProbCutDepthPairV1> preference = options.ordered_depth_pairs.empty()
                                                             ? profile.validated_pair_order
                                                             : options.ordered_depth_pairs;

  std::uint8_t probes = 0;
  bool matched_profile = false;
  for (const ProbCutDepthPairV1 pair : preference) {
    if (pair.deep_depth != depth || pair.shallow_depth <= 0 || pair.shallow_depth >= depth) {
      continue;
    }
    const ProbCutCalibrationEntryV1* entry =
        entry_for(profile, phase, empties, pair.deep_depth, pair.shallow_depth,
                  context->options.mode, exact_handoff_enabled, exact_threshold, handoff_distance);
    if (entry == nullptr) {
      ++pair_stats(&context->stats, phase, pair.deep_depth, pair.shallow_depth).unsupported_profile;
      continue;
    }
    matched_profile = true;
    if (probes >= options.maximum_probes_per_node) {
      ++context->stats.probcut_probe_limit_reached;
      break;
    }
    if (shallow_overhead_limit_reached(context->stats, options.maximum_shallow_overhead_ratio)) {
      ++context->stats.probcut_rejected_overhead;
      break;
    }
    ++probes;

    ProbCutDepthPairStats& telemetry =
        pair_stats(&context->stats, phase, pair.deep_depth, pair.shallow_depth);
    ++context->stats.probcut_attempts;
    ++telemetry.attempts;
    const NodeCount before_nodes = context->stats.nodes;
    const board_core::Bitboard legal_moves = frame.legal_moves;
    SearchNodeResult shallow;
    {
      const ScopedProbCutShallow guard{context};
      shallow = full_window_search(context, kScoreLoss, kScoreWin, pair.shallow_depth, ply);
    }
    const NodeCount shallow_nodes = context->stats.nodes - before_nodes;
    context->stats.probcut_shallow_nodes += shallow_nodes;
    telemetry.shallow_nodes += shallow_nodes;
    context->stack[ply] = StackFrame{};
    context->stack[ply].legal_moves = legal_moves;

    if (shallow.is_stopped() || should_stop_search(context)) {
      return SearchNodeResult::stopped();
    }

    Score conservative_lower = 0;
    if (!confidence_accepts(options, *entry, shallow.value().score, beta, &conservative_lower)) {
      ++context->stats.probcut_rejected_confidence;
      ++telemetry.confidence_rejections;
      continue;
    }

    ++context->stats.probcut_successes;
    ++telemetry.successes;
    if (options.shadow_verify) {
      ++context->stats.probcut_shadow_candidates;
      ++telemetry.shadow_candidates;
      *shadow_candidate = ProbCutShadowCandidate{
          .beta = beta,
          .phase = phase,
          .deep_depth = pair.deep_depth,
          .shallow_depth = pair.shallow_depth,
      };
      return std::nullopt;
    }

    ++context->stats.probcut_beta_cutoffs;
    ++telemetry.beta_cuts;
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

  if (!matched_profile) {
    ++context->stats.probcut_unsupported_profile;
    const bool phase_supported = std::any_of(
        profile.entries.begin(), profile.entries.end(),
        [phase](const ProbCutCalibrationEntryV1& entry) { return entry.phase == phase; });
    if (phase_supported) {
      ++context->stats.probcut_rejected_by_depth;
    } else {
      ++context->stats.probcut_rejected_by_phase;
    }
  }
  return std::nullopt;
}

void complete_probcut_shadow(SearchContext* context, const ProbCutShadowCandidate& candidate,
                             const SearchNodeResult& deep_result) noexcept {
  if (context == nullptr || !deep_result.is_complete()) {
    return;
  }
  ++context->stats.probcut_shadow_verifications;
  ProbCutDepthPairStats& telemetry =
      pair_stats(&context->stats, candidate.phase, candidate.deep_depth, candidate.shallow_depth);
  ++telemetry.shadow_verifications;
  if (deep_result.value().score < candidate.beta) {
    ++context->stats.probcut_shadow_false_cuts;
    ++telemetry.shadow_false_cuts;
  }
}

} // namespace vibe_othello::search::internal
