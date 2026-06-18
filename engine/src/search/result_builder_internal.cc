#include "result_builder_internal.h"

namespace vibe_othello::search::internal {

std::chrono::milliseconds elapsed_since(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                               start);
}

void publish_result_metadata(SearchResult* result, RootResultMetadata metadata) {
  result->nodes = metadata.stats.nodes;
  result->stats = metadata.stats;
  result->elapsed = elapsed_since(metadata.start);
}

void publish_completed_root_result(SearchResult* result, CompletedRootResult completed,
                                   RootResultMetadata metadata) {
  result->best_move = completed.best_move;
  result->score = completed.score;
  result->score_kind = completed.score_kind;
  result->bound = completed.bound;
  result->completed_depth = completed.completed_depth;
  result->pv = completed.pv;
  result->exact = completed.exact;
  result->stopped = false;
  publish_result_metadata(result, metadata);
}

void publish_stopped_result(SearchResult* result, StoppedRootResult stopped,
                            RootResultMetadata metadata) {
  result->stopped = true;
  result->exact = false;
  result->bound = stopped.bound;
  result->completed_depth = stopped.completed_depth;
  if (stopped.has_completed_score) {
    result->best_move = stopped.best_move;
    result->score = stopped.score;
    result->score_kind = stopped.score_kind;
    result->pv = stopped.pv;
  } else {
    result->best_move = std::nullopt;
    result->score = kScoreLoss;
    result->score_kind = ScoreKind::unavailable;
    result->pv = {};
  }
  publish_result_metadata(result, metadata);
}

void publish_terminal_result(SearchResult* result, ScoreKind score_kind, Score score,
                             Depth completed_depth, RootResultMetadata metadata) {
  result->best_move = std::nullopt;
  result->score = score;
  result->score_kind = score_kind;
  result->bound = BoundType::exact;
  result->completed_depth = completed_depth;
  result->pv = {};
  result->exact = true;
  result->stopped = false;
  publish_result_metadata(result, metadata);
}

RootMoveInfo make_root_move_info(board_core::Move move, Score score, ScoreKind score_kind,
                                 BoundType bound, Depth depth, NodeCount nodes, Line pv, bool exact,
                                 bool selective) noexcept {
  return RootMoveInfo{
      .move = move,
      .score = score,
      .score_kind = score_kind,
      .bound = bound,
      .depth = depth,
      .nodes = nodes,
      .pv = pv,
      .exact = exact,
      .selective = selective,
  };
}

} // namespace vibe_othello::search::internal
