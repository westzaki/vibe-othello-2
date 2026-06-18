#pragma once

#include "vibe_othello/search/search.h"

#include <atomic>
#include <chrono>
#include <cstdint>

namespace vibe_othello::search::internal {

struct SearchLimitState {
  std::chrono::steady_clock::time_point start{};
  std::chrono::steady_clock::time_point deadline{};
  const std::atomic_bool* stop_requested = nullptr;
  NodeCount max_nodes = 0;
  NodeCount nodes = 0;
  NodeCount nodes_until_next_time_check = 0;
  bool has_deadline = false;
  bool stopped = false;
};

enum class SearchNodeAccounting : std::uint8_t {
  normal,
  endgame,
};

SearchLimitState initialize_limit_state(SearchLimits limits);
bool should_stop(SearchLimitState* state);
bool note_node_visited(SearchLimitState* state, SearchStats* stats,
                       SearchNodeAccounting accounting);

} // namespace vibe_othello::search::internal
