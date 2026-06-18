#pragma once

#include "vibe_othello/search/result.h"

#include <chrono>
#include <optional>

namespace vibe_othello::search::internal {

struct RootResultMetadata {
  SearchStats stats{};
  std::chrono::steady_clock::time_point start{};
};

struct CompletedRootResult {
  std::optional<board_core::Move> best_move;
  Score score = 0;
  ScoreKind score_kind = ScoreKind::heuristic;
  BoundType bound = BoundType::exact;
  Depth completed_depth = 0;
  Line pv{};
  bool exact = false;
};

struct StoppedRootResult {
  std::optional<board_core::Move> best_move;
  Score score = kScoreLoss;
  ScoreKind score_kind = ScoreKind::unavailable;
  BoundType bound = BoundType::lower;
  Depth completed_depth = 0;
  Line pv{};
  bool has_completed_score = false;
};

std::chrono::milliseconds elapsed_since(std::chrono::steady_clock::time_point start);
void publish_result_metadata(SearchResult* result, RootResultMetadata metadata);
void publish_completed_root_result(SearchResult* result, CompletedRootResult completed,
                                   RootResultMetadata metadata);
void publish_stopped_result(SearchResult* result, StoppedRootResult stopped,
                            RootResultMetadata metadata);
void publish_terminal_result(SearchResult* result, ScoreKind score_kind, Score score,
                             Depth completed_depth, RootResultMetadata metadata);
RootMoveInfo make_root_move_info(board_core::Move move, Score score, ScoreKind score_kind,
                                 BoundType bound, Depth depth, NodeCount nodes, Line pv, bool exact,
                                 bool selective) noexcept;

} // namespace vibe_othello::search::internal
