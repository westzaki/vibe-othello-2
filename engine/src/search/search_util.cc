#include "search_context_internal.h"
#include "search_limits_internal.h"
#include "search_util_internal.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstdlib>

namespace vibe_othello::search::internal {

namespace {

constexpr NodeCount kTimeCheckNodeInterval = 256;

bool external_stop_requested(const SearchLimitState& state) noexcept {
  return state.stop_requested != nullptr && state.stop_requested->load(std::memory_order_acquire);
}

bool time_limit_reached(const SearchLimitState& state) {
  return state.has_deadline && std::chrono::steady_clock::now() >= state.deadline;
}

} // namespace

Score terminal_score(board_core::Position position) noexcept {
  return static_cast<Score>(std::popcount(position.player)) -
         static_cast<Score>(std::popcount(position.opponent));
}

bool is_valid_evaluator_score(Score score) noexcept {
  return kScoreLoss < score && score < kScoreWin;
}

void require_invariant(bool condition) noexcept {
  assert(condition);
  if (!condition) {
    std::abort();
  }
}

void prepend_move(board_core::Move move, const Line& child, Line* line) noexcept {
  line->moves[0] = move;
  line->size = 1;

  const std::uint8_t copy_count =
      child.size < kMaxPly ? child.size : static_cast<std::uint8_t>(kMaxPly - 1);
  for (std::uint8_t index = 0; index < copy_count; ++index) {
    line->moves[index + 1] = child.moves[index];
  }
  line->size = static_cast<std::uint8_t>(line->size + copy_count);
}

void add_stats(SearchStats* total, const SearchStats& delta) {
  total->nodes += delta.nodes;
  total->leaf_nodes += delta.leaf_nodes;
  total->eval_calls += delta.eval_calls;
  total->incremental_eval_enabled |= delta.incremental_eval_enabled;
  total->incremental_state_initializations += delta.incremental_state_initializations;
  total->incremental_eval_calls += delta.incremental_eval_calls;
  total->stateless_eval_calls += delta.stateless_eval_calls;
  total->incremental_updates += delta.incremental_updates;
  total->incremental_touched_instances += delta.incremental_touched_instances;
  total->terminal_nodes += delta.terminal_nodes;
  total->pass_nodes += delta.pass_nodes;
  total->beta_cutoffs += delta.beta_cutoffs;
  total->alpha_updates += delta.alpha_updates;
  total->root_moves_searched += delta.root_moves_searched;
  total->tt_probes += delta.tt_probes;
  total->tt_hits += delta.tt_hits;
  total->tt_stores += delta.tt_stores;
  total->tt_cutoffs += delta.tt_cutoffs;
  total->tt_replacements += delta.tt_replacements;
  total->tt_bucket_conflicts += delta.tt_bucket_conflicts;
  total->tt_same_key_updates += delta.tt_same_key_updates;
  total->tt_probe_slots += delta.tt_probe_slots;
  total->tt_generation_age_hits += delta.tt_generation_age_hits;
  total->tt_rejected_stores += delta.tt_rejected_stores;
  total->tt_invalid_best_move_stores += delta.tt_invalid_best_move_stores;
  total->pvs_researches += delta.pvs_researches;
  total->aspiration_fail_lows += delta.aspiration_fail_lows;
  total->aspiration_fail_highs += delta.aspiration_fail_highs;
  total->iid_searches += delta.iid_searches;
  total->endgame_nodes += delta.endgame_nodes;
  total->endgame_last_flip_solved += delta.endgame_last_flip_solved;
  total->endgame_stability_probes += delta.endgame_stability_probes;
  total->endgame_stability_lower_candidates += delta.endgame_stability_lower_candidates;
  total->endgame_stability_upper_candidates += delta.endgame_stability_upper_candidates;
  total->endgame_stability_cutoffs += delta.endgame_stability_cutoffs;
  total->endgame_stability_shadow_verifications += delta.endgame_stability_shadow_verifications;
  total->endgame_stability_shadow_false_cutoffs += delta.endgame_stability_shadow_false_cutoffs;
  total->selective_cuts += delta.selective_cuts;
  total->probcut_attempts += delta.probcut_attempts;
  total->probcut_shallow_nodes += delta.probcut_shallow_nodes;
  total->probcut_successes += delta.probcut_successes;
  total->probcut_unsupported_profile += delta.probcut_unsupported_profile;
  total->probcut_rejected_by_phase += delta.probcut_rejected_by_phase;
  total->probcut_rejected_by_depth += delta.probcut_rejected_by_depth;
  total->probcut_rejected_near_exact += delta.probcut_rejected_near_exact;
  total->probcut_rejected_pass += delta.probcut_rejected_pass;
  total->probcut_rejected_pv += delta.probcut_rejected_pv;
  total->probcut_rejected_root += delta.probcut_rejected_root;
  total->probcut_rejected_overhead += delta.probcut_rejected_overhead;
  total->probcut_probe_limit_reached += delta.probcut_probe_limit_reached;
  total->probcut_rejected_confidence += delta.probcut_rejected_confidence;
  total->probcut_beta_cutoffs += delta.probcut_beta_cutoffs;
  total->probcut_cut_low_attempts += delta.probcut_cut_low_attempts;
  total->probcut_shadow_candidates += delta.probcut_shadow_candidates;
  total->probcut_shadow_verifications += delta.probcut_shadow_verifications;
  total->probcut_shadow_false_cuts += delta.probcut_shadow_false_cuts;
  total->probcut_estimated_saved_nodes += delta.probcut_estimated_saved_nodes;
  total->probcut_estimated_saved_nodes_available |= delta.probcut_estimated_saved_nodes_available;
  for (const ProbCutDepthPairStats& incoming : delta.probcut_by_phase_depth_pair) {
    auto existing = std::find_if(total->probcut_by_phase_depth_pair.begin(),
                                 total->probcut_by_phase_depth_pair.end(),
                                 [&incoming](const ProbCutDepthPairStats& value) {
                                   return value.phase == incoming.phase &&
                                          value.deep_depth == incoming.deep_depth &&
                                          value.shallow_depth == incoming.shallow_depth;
                                 });
    if (existing == total->probcut_by_phase_depth_pair.end()) {
      total->probcut_by_phase_depth_pair.push_back(incoming);
      continue;
    }
    existing->attempts += incoming.attempts;
    existing->shallow_nodes += incoming.shallow_nodes;
    existing->successes += incoming.successes;
    existing->confidence_rejections += incoming.confidence_rejections;
    existing->unsupported_profile += incoming.unsupported_profile;
    existing->near_exact_rejections += incoming.near_exact_rejections;
    existing->pass_rejections += incoming.pass_rejections;
    existing->pv_rejections += incoming.pv_rejections;
    existing->root_rejections += incoming.root_rejections;
    existing->beta_cuts += incoming.beta_cuts;
    existing->cut_low_attempts += incoming.cut_low_attempts;
    existing->shadow_candidates += incoming.shadow_candidates;
    existing->shadow_verifications += incoming.shadow_verifications;
    existing->shadow_false_cuts += incoming.shadow_false_cuts;
  }
}

SearchLimitState initialize_limit_state(SearchLimits limits) {
  const auto start = std::chrono::steady_clock::now();
  return SearchLimitState{
      .start = start,
      .deadline = start + limits.max_time,
      .stop_requested = limits.stop_requested,
      .max_nodes = limits.max_nodes,
      .nodes = 0,
      .nodes_until_next_time_check = 0,
      .has_deadline = limits.max_time.count() > 0,
      .stopped = false,
  };
}

bool should_stop(SearchLimitState* state) {
  if (state == nullptr) {
    return false;
  }

  if (state->stopped) {
    return true;
  }
  if (external_stop_requested(*state) || time_limit_reached(*state)) {
    state->stopped = true;
  }
  return state->stopped;
}

bool note_node_visited(SearchLimitState* state, SearchStats* stats,
                       SearchNodeAccounting accounting) {
  if (stats == nullptr) {
    return false;
  }

  if (state != nullptr) {
    if (state->stopped || external_stop_requested(*state)) {
      state->stopped = true;
      return true;
    }
    if (state->max_nodes != 0 && state->nodes >= state->max_nodes) {
      state->stopped = true;
      return true;
    }
  }

  ++stats->nodes;
  if (accounting == SearchNodeAccounting::endgame) {
    ++stats->endgame_nodes;
  }

  if (state == nullptr) {
    return false;
  }

  ++state->nodes;
  if (state->max_nodes != 0 && state->nodes >= state->max_nodes) {
    state->stopped = true;
    return false;
  }

  if (state->nodes_until_next_time_check == 0) {
    state->nodes_until_next_time_check = kTimeCheckNodeInterval;
    if (time_limit_reached(*state)) {
      state->stopped = true;
    }
  } else {
    --state->nodes_until_next_time_check;
  }

  return state->stopped;
}

bool should_stop_search(SearchContext* context) {
  return context != nullptr && should_stop(context->limit_state);
}

bool note_node_visited(SearchContext* context) {
  if (context == nullptr) {
    return false;
  }
  return note_node_visited(context->limit_state, &context->stats, SearchNodeAccounting::normal);
}

bool is_better_root_move(Score score, board_core::Move move, Score best_score,
                         std::optional<board_core::Move> best_move) noexcept {
  if (!best_move.has_value() || score > best_score) {
    return true;
  }
  if (score < best_score || move.kind != board_core::MoveKind::normal ||
      best_move->kind != board_core::MoveKind::normal) {
    return false;
  }
  return move.square.index < best_move->square.index;
}

} // namespace vibe_othello::search::internal
