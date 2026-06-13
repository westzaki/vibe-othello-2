#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/hash.h"
#include "vibe_othello/search/search.h"

#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <optional>

namespace vibe_othello::search {
namespace {

struct SearchValue {
  Score score = 0;
  Line pv{};
};

struct MoveList {
  std::array<board_core::Move, board_core::kSquareCount> moves{};
  std::uint8_t size = 0;
};

struct MoveOrderingHints {
  std::optional<board_core::Move> first_move;
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
}

MoveList move_list_from_legal_mask(board_core::Bitboard legal_moves) noexcept {
  MoveList list{};
  for (int square_index = 0; square_index < board_core::kSquareCount; ++square_index) {
    const board_core::Square square = board_core::square_from_index(square_index);
    if ((legal_moves & board_core::bit(square)) != 0) {
      list.moves[list.size] = board_core::make_move(square);
      ++list.size;
    }
  }
  return list;
}

void move_to_front(MoveList* list, board_core::Move move) noexcept {
  for (std::uint8_t index = 0; index < list->size; ++index) {
    if (list->moves[index] != move) {
      continue;
    }

    for (std::uint8_t shift = index; shift > 0; --shift) {
      list->moves[shift] = list->moves[shift - 1];
    }
    list->moves[0] = move;
    return;
  }
}

MoveList ordered_moves(board_core::Position position, MoveOrderingHints hints) noexcept {
  MoveList list = move_list_from_legal_mask(board_core::legal_moves(position));
  if (hints.first_move.has_value() &&
      hints.first_move->kind == board_core::MoveKind::normal) {
    move_to_front(&list, *hints.first_move);
  }
  return list;
}

class TTBestMoveTable {
public:
  std::optional<board_core::Move> probe(board_core::Position position,
                                        SearchStats* stats) const noexcept {
    ++stats->tt_probes;

    const board_core::PositionHash key = board_core::hash_position(position);
    const Entry& entry = entries_[index_for(key)];
    if (!entry.occupied || entry.key != key) {
      return std::nullopt;
    }

    ++stats->tt_hits;
    return entry.best_move;
  }

  void store(board_core::Position position, board_core::Move best_move,
             SearchStats* stats) noexcept {
    if (best_move.kind != board_core::MoveKind::normal) {
      return;
    }

    const board_core::PositionHash key = board_core::hash_position(position);
    entries_[index_for(key)] = Entry{
        .key = key,
        .best_move = best_move,
        .occupied = true,
    };
    ++stats->tt_stores;
  }

private:
  struct Entry {
    board_core::PositionHash key = 0;
    board_core::Move best_move = board_core::make_pass();
    bool occupied = false;
  };

  static constexpr std::size_t kEntryCount = 4096;

  static constexpr std::size_t index_for(board_core::PositionHash key) noexcept {
    return static_cast<std::size_t>(key % kEntryCount);
  }

  std::array<Entry, kEntryCount> entries_{};
};

SearchValue alphabeta(board_core::Position* position, const Evaluator& evaluator, Score alpha,
                      Score beta, Depth depth, SearchStats* stats, TTBestMoveTable* tt) {
  require_invariant(alpha < beta);
  ++stats->nodes;

  if (board_core::is_terminal(*position)) {
    ++stats->terminal_nodes;
    return SearchValue{
        .score = terminal_score(*position),
        .pv = {},
    };
  }

  if (depth <= 0) {
    ++stats->leaf_nodes;
    ++stats->eval_calls;
    const Score score = evaluator.evaluate(*position);
    require_invariant(is_valid_evaluator_score(score));
    return SearchValue{
        .score = score,
        .pv = {},
    };
  }

  const MoveOrderingHints hints{
      .first_move = tt == nullptr ? std::nullopt : tt->probe(*position, stats),
  };
  const MoveList moves = ordered_moves(*position, hints);
  if (moves.size == 0) {
    ++stats->pass_nodes;
    board_core::MoveDelta delta{};
    const bool made_delta = board_core::make_move_delta(*position, board_core::make_pass(), &delta);
    require_invariant(made_delta);
    board_core::apply_move_delta(position, delta);

    const SearchValue child =
        alphabeta(position, evaluator, static_cast<Score>(-beta), static_cast<Score>(-alpha),
                  static_cast<Depth>(depth - 1), stats, tt);
    board_core::undo_move(position, delta);

    SearchValue result{
        .score = static_cast<Score>(-child.score),
        .pv = {},
    };
    prepend_move(board_core::make_pass(), child.pv, &result.pv);
    return result;
  }

  SearchValue best{
      .score = kScoreLoss,
      .pv = {},
  };
  std::optional<board_core::Move> best_move;

  for (std::uint8_t move_index = 0; move_index < moves.size; ++move_index) {
    const board_core::Move move = moves.moves[move_index];
    board_core::MoveDelta delta{};
    const bool made_delta = board_core::make_move_delta(*position, move, &delta);
    require_invariant(made_delta);
    board_core::apply_move_delta(position, delta);

    const SearchValue child =
        alphabeta(position, evaluator, static_cast<Score>(-beta), static_cast<Score>(-alpha),
                  static_cast<Depth>(depth - 1), stats, tt);
    board_core::undo_move(position, delta);

    const Score score = static_cast<Score>(-child.score);
    if (!best_move.has_value() || score > best.score ||
        (score == best.score && move.square.index < best_move->square.index)) {
      best.score = score;
      best_move = move;
      prepend_move(move, child.pv, &best.pv);
    }

    if (score > alpha) {
      ++stats->alpha_updates;
      alpha = score;
    }
    if (alpha >= beta) {
      ++stats->beta_cutoffs;
      break;
    }
  }

  if (tt != nullptr && best_move.has_value()) {
    tt->store(*position, *best_move, stats);
  }

  return best;
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

void search_root_move(board_core::Position position, const Evaluator& evaluator, Depth depth,
                      board_core::Move move, SearchStats* stats, Score* best_score,
                      Line* best_line, SearchResult* result, TTBestMoveTable* tt) {
  board_core::MoveDelta delta{};
  const bool made_delta = board_core::make_move_delta(position, move, &delta);
  require_invariant(made_delta);
  board_core::apply_move_delta(&position, delta);

  const NodeCount before_nodes = stats->nodes;
  const SearchValue child = alphabeta(&position, evaluator, kScoreLoss, kScoreWin,
                                      static_cast<Depth>(depth - 1), stats, tt);
  board_core::undo_move(&position, delta);

  const Score score = static_cast<Score>(-child.score);
  Line line{};
  prepend_move(move, child.pv, &line);

  result->root_moves.push_back(RootMoveInfo{
      .move = move,
      .score = score,
      .bound = BoundType::exact,
      .depth = depth,
      .nodes = stats->nodes - before_nodes,
      .pv = line,
      .exact = false,
      .selective = false,
  });

  if (is_better_root_move(score, move, *best_score, result->best_move)) {
    result->best_move = move;
    *best_score = score;
    *best_line = line;
  }
}

SearchResult search_fixed_depth_with_hint(board_core::Position position, const Evaluator& evaluator,
                                          Depth depth,
                                          MoveOrderingHints root_hints, TTBestMoveTable* tt) {
  const auto start = std::chrono::steady_clock::now();
  const Depth completed_depth = depth < 0 ? Depth{0} : depth;

  SearchResult result{
      .completed_depth = completed_depth,
  };

  SearchStats stats{};
  if (board_core::is_terminal(position) || completed_depth == 0) {
    const SearchValue root =
        alphabeta(&position, evaluator, kScoreLoss, kScoreWin, completed_depth, &stats, tt);
    result.score = root.score;
    result.nodes = stats.nodes;
    result.stats = stats;
    result.pv = root.pv;
    result.exact = board_core::is_terminal(position);
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
  }

  stats.nodes = 1;
  if (!root_hints.first_move.has_value() && tt != nullptr) {
    root_hints.first_move = tt->probe(position, &stats);
  }
  const MoveList root_moves = ordered_moves(position, root_hints);
  if (root_moves.size == 0) {
    ++stats.pass_nodes;
    board_core::MoveDelta delta{};
    const bool made_delta = board_core::make_move_delta(position, board_core::make_pass(), &delta);
    require_invariant(made_delta);
    board_core::apply_move_delta(&position, delta);

    const NodeCount before_nodes = stats.nodes;
    const SearchValue child = alphabeta(&position, evaluator, kScoreLoss, kScoreWin,
                                        static_cast<Depth>(completed_depth - 1), &stats, tt);
    board_core::undo_move(&position, delta);

    result.best_move = board_core::make_pass();
    result.score = static_cast<Score>(-child.score);
    prepend_move(board_core::make_pass(), child.pv, &result.pv);
    result.root_moves.push_back(RootMoveInfo{
        .move = board_core::make_pass(),
        .score = result.score,
        .bound = BoundType::exact,
        .depth = completed_depth,
        .nodes = stats.nodes - before_nodes,
        .pv = result.pv,
        .exact = false,
        .selective = false,
    });
    stats.root_moves_searched = 1;

    result.nodes = stats.nodes;
    result.stats = stats;
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
  }

  Score best_score = kScoreLoss;
  Line best_line{};
  for (std::uint8_t move_index = 0; move_index < root_moves.size; ++move_index) {
    const board_core::Move move = root_moves.moves[move_index];
    search_root_move(position, evaluator, completed_depth, move, &stats, &best_score, &best_line,
                     &result, tt);
    ++stats.root_moves_searched;
  }

  if (tt != nullptr && result.best_move.has_value()) {
    tt->store(position, *result.best_move, &stats);
  }

  result.score = best_score;
  result.nodes = stats.nodes;
  result.stats = stats;
  result.pv = best_line;
  result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  return result;
}

} // namespace

SearchResult search_fixed_depth(board_core::Position position, const Evaluator& evaluator,
                                Depth depth) {
  return search_fixed_depth_with_hint(position, evaluator, depth, MoveOrderingHints{}, nullptr);
}

SearchResult search_iterative(board_core::Position position, const Evaluator& evaluator,
                              SearchLimits limits) {
  return search_iterative(position, evaluator, limits, SearchOptions{});
}

SearchResult search_iterative(board_core::Position position, const Evaluator& evaluator,
                              SearchLimits limits, SearchOptions options) {
  const auto start = std::chrono::steady_clock::now();
  const Depth max_depth = limits.max_depth < 0 ? Depth{0} : limits.max_depth;

  if (max_depth == 0) {
    SearchResult result = search_fixed_depth(position, evaluator, Depth{0});
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
  }

  SearchResult best_completed{};
  SearchStats total_stats{};
  TTBestMoveTable tt_storage{};
  TTBestMoveTable* tt = options.use_tt_best_move_ordering ? &tt_storage : nullptr;
  std::optional<board_core::Move> previous_best_move;
  for (Depth depth = 1; depth <= max_depth; ++depth) {
    SearchResult current =
        search_fixed_depth_with_hint(position, evaluator, depth,
                                     MoveOrderingHints{.first_move = previous_best_move}, tt);
    add_stats(&total_stats, current.stats);
    previous_best_move = current.best_move;
    best_completed = current;
  }

  best_completed.nodes = total_stats.nodes;
  best_completed.stats = total_stats;
  best_completed.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  return best_completed;
}

} // namespace vibe_othello::search
