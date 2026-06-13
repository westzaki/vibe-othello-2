#ifndef VIBE_OTHELLO_SEARCH_RESULT_H_
#define VIBE_OTHELLO_SEARCH_RESULT_H_

#include "vibe_othello/search/score.h"

#include <chrono>
#include <optional>
#include <vector>

namespace vibe_othello::search {

struct RootMoveInfo {
  board_core::Move move = board_core::make_pass();
  Score score = 0;
  BoundType bound = BoundType::exact;
  Depth depth = 0;
  NodeCount nodes = 0;
  Line pv{};
  bool exact = false;
  bool selective = false;

  friend bool operator==(const RootMoveInfo&, const RootMoveInfo&) = default;
};

struct SearchResult {
  std::optional<board_core::Move> best_move;
  Score score = 0;
  BoundType bound = BoundType::exact;
  Depth completed_depth = 0;
  NodeCount nodes = 0;
  std::chrono::milliseconds elapsed{0};
  Line pv{};
  std::vector<RootMoveInfo> root_moves;
  bool exact = false;
  bool stopped = false;
};

} // namespace vibe_othello::search

#endif // VIBE_OTHELLO_SEARCH_RESULT_H_
