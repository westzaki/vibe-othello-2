#include "search/reference_endgame.h"

#include "vibe_othello/board_core/board.h"

#include <bit>
#include <cassert>
#include <chrono>
#include <cstdlib>

namespace vibe_othello::search::test_support {
namespace {

struct ExactResult {
  Score score = 0;
  Line pv{};
};

Score terminal_score(board_core::Position position) noexcept {
  return static_cast<Score>(std::popcount(position.player)) -
         static_cast<Score>(std::popcount(position.opponent));
}

std::uint8_t empty_count(board_core::Position position) noexcept {
  return static_cast<std::uint8_t>(std::popcount(~board_core::occupied(position)));
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

bool improves_best(Score score, board_core::Move move, Score best_score, board_core::Move best_move,
                   bool has_best) noexcept {
  if (!has_best || score > best_score) {
    return true;
  }
  if (score < best_score || move.kind != board_core::MoveKind::normal ||
      best_move.kind != board_core::MoveKind::normal) {
    return false;
  }
  return move.square.index < best_move.square.index;
}

ExactResult exact_search(board_core::Position* position, SearchStats* stats) {
  ++stats->nodes;
  ++stats->endgame_nodes;

  if (board_core::is_terminal(*position)) {
    ++stats->terminal_nodes;
    return ExactResult{
        .score = terminal_score(*position),
        .pv = {},
    };
  }

  const board_core::Bitboard legal_moves = board_core::legal_moves(*position);
  if (legal_moves == 0) {
    ++stats->pass_nodes;
    board_core::MoveDelta delta{};
    const bool made_delta = board_core::make_move_delta(*position, board_core::make_pass(), &delta);
    require_invariant(made_delta);
    board_core::apply_move_delta(position, delta);

    const ExactResult child = exact_search(position, stats);
    board_core::undo_move(position, delta);

    ExactResult result{
        .score = static_cast<Score>(-child.score),
        .pv = {},
    };
    prepend_move(board_core::make_pass(), child.pv, &result.pv);
    return result;
  }

  ExactResult best{
      .score = kScoreLoss,
      .pv = {},
  };
  board_core::Move best_move{};
  bool has_best = false;

  for (int square_index = 0; square_index < board_core::kSquareCount; ++square_index) {
    const board_core::Square square = board_core::square_from_index(square_index);
    if ((legal_moves & board_core::bit(square)) == 0) {
      continue;
    }

    const board_core::Move move = board_core::make_move(square);
    board_core::MoveDelta delta{};
    const bool made_delta = board_core::make_move_delta(*position, move, &delta);
    require_invariant(made_delta);
    board_core::apply_move_delta(position, delta);

    const ExactResult child = exact_search(position, stats);
    board_core::undo_move(position, delta);

    const Score score = static_cast<Score>(-child.score);
    if (improves_best(score, move, best.score, best_move, has_best)) {
      best.score = score;
      best_move = move;
      has_best = true;
      prepend_move(move, child.pv, &best.pv);
    }
  }

  return best;
}

RootMoveInfo make_root_move(board_core::Move move, Score score, Depth depth, NodeCount nodes,
                            Line pv) noexcept {
  return RootMoveInfo{
      .move = move,
      .score = score,
      .bound = BoundType::exact,
      .depth = depth,
      .nodes = nodes,
      .pv = pv,
      .exact = true,
      .selective = false,
  };
}

} // namespace

SearchResult reference_exact_endgame(board_core::Position position) {
  const auto start = std::chrono::steady_clock::now();
  const Depth completed_depth = static_cast<Depth>(empty_count(position));

  SearchResult result{
      .bound = BoundType::exact,
      .completed_depth = completed_depth,
      .exact = true,
  };

  SearchStats stats{};
  ++stats.nodes;
  ++stats.endgame_nodes;

  if (board_core::is_terminal(position)) {
    ++stats.terminal_nodes;
    result.score = terminal_score(position);
    result.nodes = stats.nodes;
    result.stats = stats;
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
  }

  const board_core::Bitboard legal_moves = board_core::legal_moves(position);
  if (legal_moves == 0) {
    ++stats.pass_nodes;
    board_core::MoveDelta delta{};
    const bool made_delta = board_core::make_move_delta(position, board_core::make_pass(), &delta);
    require_invariant(made_delta);
    board_core::apply_move_delta(&position, delta);

    const NodeCount before_nodes = stats.nodes;
    const ExactResult child = exact_search(&position, &stats);
    board_core::undo_move(&position, delta);

    result.best_move = board_core::make_pass();
    result.score = static_cast<Score>(-child.score);
    prepend_move(board_core::make_pass(), child.pv, &result.pv);
    result.root_moves.push_back(make_root_move(board_core::make_pass(), result.score,
                                               completed_depth, stats.nodes - before_nodes,
                                               result.pv));
    stats.root_moves_searched = 1;
    result.nodes = stats.nodes;
    result.stats = stats;
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
  }

  Score best_score = kScoreLoss;
  Line best_line{};

  for (int square_index = 0; square_index < board_core::kSquareCount; ++square_index) {
    const board_core::Square square = board_core::square_from_index(square_index);
    if ((legal_moves & board_core::bit(square)) == 0) {
      continue;
    }

    const board_core::Move move = board_core::make_move(square);
    board_core::MoveDelta delta{};
    const bool made_delta = board_core::make_move_delta(position, move, &delta);
    require_invariant(made_delta);
    board_core::apply_move_delta(&position, delta);

    const NodeCount before_nodes = stats.nodes;
    const ExactResult child = exact_search(&position, &stats);
    board_core::undo_move(&position, delta);

    const Score score = static_cast<Score>(-child.score);
    Line line{};
    prepend_move(move, child.pv, &line);

    result.root_moves.push_back(
        make_root_move(move, score, completed_depth, stats.nodes - before_nodes, line));
    ++stats.root_moves_searched;

    if (improves_best(score, move, best_score, result.best_move.value_or(board_core::make_pass()),
                      result.best_move.has_value())) {
      result.best_move = move;
      best_score = score;
      best_line = line;
    }
  }

  result.score = best_score;
  result.nodes = stats.nodes;
  result.stats = stats;
  result.pv = best_line;
  result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  return result;
}

} // namespace vibe_othello::search::test_support
