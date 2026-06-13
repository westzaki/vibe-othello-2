#include "vibe_othello/board_core/board.h"
#include "vibe_othello/search/search.h"

#include <bit>
#include <cassert>
#include <chrono>
#include <cstdlib>

namespace vibe_othello::search {
namespace {

struct NegamaxResult {
  Score score = 0;
  Line pv{};
};

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

NegamaxResult negamax(board_core::Position* position, const Evaluator& evaluator, Depth depth,
                      NodeCount* nodes) {
  ++(*nodes);

  if (board_core::is_terminal(*position)) {
    return NegamaxResult{
        .score = terminal_score(*position),
        .pv = {},
    };
  }

  if (depth <= 0) {
    const Score score = evaluator.evaluate(*position);
    require_invariant(is_valid_evaluator_score(score));
    return NegamaxResult{
        .score = score,
        .pv = {},
    };
  }

  const board_core::Bitboard legal_moves = board_core::legal_moves(*position);
  if (legal_moves == 0) {
    board_core::MoveDelta delta{};
    const bool made_delta = board_core::make_move_delta(*position, board_core::make_pass(), &delta);
    require_invariant(made_delta);
    const bool applied_delta = made_delta && board_core::apply_move_delta(position, delta);
    require_invariant(applied_delta);

    const NegamaxResult child = negamax(position, evaluator, static_cast<Depth>(depth - 1), nodes);
    board_core::undo_move(position, delta);

    NegamaxResult result{
        .score = static_cast<Score>(-child.score),
        .pv = {},
    };
    prepend_move(board_core::make_pass(), child.pv, &result.pv);
    return result;
  }

  NegamaxResult best{
      .score = kScoreLoss,
      .pv = {},
  };

  for (int square_index = 0; square_index < board_core::kSquareCount; ++square_index) {
    const board_core::Square square = board_core::square_from_index(square_index);
    if ((legal_moves & board_core::bit(square)) == 0) {
      continue;
    }

    const board_core::Move move = board_core::make_move(square);
    board_core::MoveDelta delta{};
    const bool made_delta = board_core::make_move_delta(*position, move, &delta);
    require_invariant(made_delta);
    const bool applied_delta = made_delta && board_core::apply_move_delta(position, delta);
    require_invariant(applied_delta);

    const NegamaxResult child = negamax(position, evaluator, static_cast<Depth>(depth - 1), nodes);
    board_core::undo_move(position, delta);

    const Score score = static_cast<Score>(-child.score);
    if (score > best.score) {
      best.score = score;
      prepend_move(move, child.pv, &best.pv);
    }
  }

  return best;
}

} // namespace

SearchResult search_fixed_depth(board_core::Position position, const Evaluator& evaluator,
                                Depth depth) {
  const auto start = std::chrono::steady_clock::now();
  const Depth completed_depth = depth < 0 ? Depth{0} : depth;

  SearchResult result{
      .completed_depth = completed_depth,
  };

  NodeCount nodes = 0;
  if (board_core::is_terminal(position) || completed_depth == 0) {
    const NegamaxResult root = negamax(&position, evaluator, completed_depth, &nodes);
    result.score = root.score;
    result.nodes = nodes;
    result.pv = root.pv;
    result.exact = board_core::is_terminal(position);
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
  }

  nodes = 1;
  const board_core::Bitboard legal_moves = board_core::legal_moves(position);
  if (legal_moves == 0) {
    board_core::MoveDelta delta{};
    const bool made_delta = board_core::make_move_delta(position, board_core::make_pass(), &delta);
    require_invariant(made_delta);
    const bool applied_delta = made_delta && board_core::apply_move_delta(&position, delta);
    require_invariant(applied_delta);

    const NodeCount before_nodes = nodes;
    const NegamaxResult child =
        negamax(&position, evaluator, static_cast<Depth>(completed_depth - 1), &nodes);
    board_core::undo_move(&position, delta);

    result.best_move = board_core::make_pass();
    result.score = static_cast<Score>(-child.score);
    prepend_move(board_core::make_pass(), child.pv, &result.pv);
    result.root_moves.push_back(RootMoveInfo{
        .move = board_core::make_pass(),
        .score = result.score,
        .bound = BoundType::exact,
        .depth = completed_depth,
        .nodes = nodes - before_nodes,
        .pv = result.pv,
        .exact = false,
        .selective = false,
    });

    result.nodes = nodes;
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
    const bool applied_delta = made_delta && board_core::apply_move_delta(&position, delta);
    require_invariant(applied_delta);

    const NodeCount before_nodes = nodes;
    const NegamaxResult child =
        negamax(&position, evaluator, static_cast<Depth>(completed_depth - 1), &nodes);
    board_core::undo_move(&position, delta);

    const Score score = static_cast<Score>(-child.score);
    Line line{};
    prepend_move(move, child.pv, &line);

    result.root_moves.push_back(RootMoveInfo{
        .move = move,
        .score = score,
        .bound = BoundType::exact,
        .depth = completed_depth,
        .nodes = nodes - before_nodes,
        .pv = line,
        .exact = false,
        .selective = false,
    });

    if (!result.best_move.has_value() || score > best_score) {
      result.best_move = move;
      best_score = score;
      best_line = line;
    }
  }

  result.score = best_score;
  result.nodes = nodes;
  result.pv = best_line;
  result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  return result;
}

} // namespace vibe_othello::search
