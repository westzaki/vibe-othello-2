#include "search_internal.h"

#include <bit>
#include <cassert>
#include <chrono>
#include <cstdlib>

namespace vibe_othello::search::internal {

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

bool limits_reached(const SearchLimits& limits, std::chrono::steady_clock::time_point start_time,
                    NodeCount nodes) noexcept {
  if (limits.stop_requested != nullptr && limits.stop_requested->load(std::memory_order_relaxed)) {
    return true;
  }
  if (limits.max_nodes != 0 && nodes >= limits.max_nodes) {
    return true;
  }
  if (limits.max_time.count() > 0 &&
      std::chrono::steady_clock::now() - start_time >= limits.max_time) {
    return true;
  }
  return false;
}

bool should_stop(SearchContext* context) noexcept {
  if (context->stopped) {
    return true;
  }
  context->stopped = limits_reached(context->limits, context->start_time, context->stats.nodes);
  return context->stopped;
}

} // namespace vibe_othello::search::internal
