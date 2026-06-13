#include "search_internal.h"

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

void add_stats(SearchStats* total, SearchStats delta) noexcept {
  total->nodes += delta.nodes;
  total->leaf_nodes += delta.leaf_nodes;
  total->eval_calls += delta.eval_calls;
  total->terminal_nodes += delta.terminal_nodes;
  total->pass_nodes += delta.pass_nodes;
  total->beta_cutoffs += delta.beta_cutoffs;
  total->alpha_updates += delta.alpha_updates;
  total->root_moves_searched += delta.root_moves_searched;
  total->tt_probes += delta.tt_probes;
  total->tt_hits += delta.tt_hits;
  total->tt_stores += delta.tt_stores;
  total->tt_cutoffs += delta.tt_cutoffs;
  total->pvs_researches += delta.pvs_researches;
  total->aspiration_fail_lows += delta.aspiration_fail_lows;
  total->aspiration_fail_highs += delta.aspiration_fail_highs;
  total->iid_searches += delta.iid_searches;
  total->endgame_nodes += delta.endgame_nodes;
  total->selective_cuts += delta.selective_cuts;
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

bool should_stop_search(SearchContext* context) {
  if (context == nullptr || context->limit_state == nullptr) {
    return false;
  }

  SearchLimitState* state = context->limit_state;
  if (state->stopped) {
    return true;
  }
  if (external_stop_requested(*state) || time_limit_reached(*state)) {
    state->stopped = true;
  }
  return state->stopped;
}

bool note_node_visited(SearchContext* context) {
  if (context == nullptr) {
    return false;
  }

  SearchLimitState* state = context->limit_state;
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

  ++context->stats.nodes;

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

} // namespace vibe_othello::search::internal
