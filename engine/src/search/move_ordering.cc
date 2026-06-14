#include "search_internal.h"

#include <algorithm>
#include <array>
#include <bit>

namespace vibe_othello::search::internal {

namespace {

struct ScoredMove {
  board_core::Move move;
  int score = 0;
};

struct EmptyRegionMap {
  std::array<std::uint8_t, board_core::kSquareCount> region_for_square{};
  std::array<std::uint8_t, board_core::kSquareCount> region_sizes{};
  std::uint8_t size = 0;
};

struct CornerAdjacency {
  board_core::Square corner;
  board_core::Square x_square;
  std::array<board_core::Square, 2> c_squares;
};

constexpr std::uint8_t kNoEmptyRegion = board_core::kSquareCount;

// Square index follows board_core's a1=0, h1=7, a8=56, h8=63 bit order.
// Define corner/X/C-square tables from coordinates so a bit-order change fails
// close to the ordering logic instead of silently changing heuristics.
constexpr std::array<CornerAdjacency, 4> kCornerAdjacency{{
    {
        .corner = board_core::square_from_file_rank(0, 0),   // a1
        .x_square = board_core::square_from_file_rank(1, 1), // b2
        .c_squares =
            {
                board_core::square_from_file_rank(1, 0), // b1
                board_core::square_from_file_rank(0, 1), // a2
            },
    },
    {
        .corner = board_core::square_from_file_rank(7, 0),   // h1
        .x_square = board_core::square_from_file_rank(6, 1), // g2
        .c_squares =
            {
                board_core::square_from_file_rank(6, 0), // g1
                board_core::square_from_file_rank(7, 1), // h2
            },
    },
    {
        .corner = board_core::square_from_file_rank(0, 7),   // a8
        .x_square = board_core::square_from_file_rank(1, 6), // b7
        .c_squares =
            {
                board_core::square_from_file_rank(0, 6), // a7
                board_core::square_from_file_rank(1, 7), // b8
            },
    },
    {
        .corner = board_core::square_from_file_rank(7, 7),   // h8
        .x_square = board_core::square_from_file_rank(6, 6), // g7
        .c_squares =
            {
                board_core::square_from_file_rank(7, 6), // h7
                board_core::square_from_file_rank(6, 7), // g8
            },
    },
}};

constexpr bool is_corner(board_core::Square square) noexcept {
  for (const CornerAdjacency adjacency : kCornerAdjacency) {
    if (square == adjacency.corner) {
      return true;
    }
  }
  return false;
}

constexpr bool is_edge(board_core::Square square) noexcept {
  const int file = board_core::file_of(square);
  const int rank = board_core::rank_of(square);
  return file == 0 || file == board_core::kBoardSize - 1 || rank == 0 ||
         rank == board_core::kBoardSize - 1;
}

constexpr bool is_normal_move(board_core::Move move) noexcept {
  return move.kind == board_core::MoveKind::normal;
}

bool is_move(board_core::Move candidate, board_core::Move move) noexcept {
  return is_normal_move(candidate) && candidate == move;
}

bool is_dangerous_x_square(board_core::Position position, board_core::Square square) noexcept {
  for (const CornerAdjacency adjacency : kCornerAdjacency) {
    if (square != adjacency.x_square) {
      continue;
    }

    return (board_core::occupied(position) & board_core::bit(adjacency.corner)) == 0;
  }
  return false;
}

bool is_dangerous_c_square(board_core::Position position, board_core::Square square) noexcept {
  for (const CornerAdjacency adjacency : kCornerAdjacency) {
    if (square != adjacency.c_squares[0] && square != adjacency.c_squares[1]) {
      continue;
    }

    return (board_core::occupied(position) & board_core::bit(adjacency.corner)) == 0;
  }
  return false;
}

bool is_stable_like_edge(board_core::Position position, board_core::Square square) noexcept {
  if (!is_edge(square)) {
    return false;
  }

  for (const CornerAdjacency adjacency : kCornerAdjacency) {
    if ((position.player & board_core::bit(adjacency.corner)) == 0) {
      continue;
    }

    if (square == adjacency.c_squares[0] || square == adjacency.c_squares[1]) {
      return true;
    }
  }
  return false;
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

void push_empty_neighbor(board_core::Bitboard* remaining_empties, int file, int rank,
                         std::array<board_core::Square, board_core::kSquareCount>* pending,
                         std::uint8_t* pending_size) noexcept {
  const board_core::Square neighbor = board_core::square_from_file_rank(file, rank);
  const board_core::Bitboard neighbor_bit = board_core::bit(neighbor);
  if ((*remaining_empties & neighbor_bit) == 0) {
    return;
  }

  (*pending)[*pending_size] = neighbor;
  ++(*pending_size);
  *remaining_empties &= ~neighbor_bit;
}

EmptyRegionMap build_empty_region_map(board_core::Position position) noexcept {
  EmptyRegionMap map{};
  map.region_for_square.fill(kNoEmptyRegion);

  board_core::Bitboard remaining_empties = ~board_core::occupied(position);
  for (int square_index = 0; square_index < board_core::kSquareCount; ++square_index) {
    const board_core::Square start = board_core::square_from_index(square_index);
    if ((remaining_empties & board_core::bit(start)) == 0) {
      continue;
    }

    std::array<board_core::Square, board_core::kSquareCount> pending{};
    std::array<board_core::Square, board_core::kSquareCount> region_squares{};
    std::uint8_t pending_size = 0;
    std::uint8_t region_size = 0;
    pending[pending_size] = start;
    ++pending_size;
    remaining_empties &= ~board_core::bit(start);

    while (pending_size > 0) {
      --pending_size;
      const board_core::Square square = pending[pending_size];
      region_squares[region_size] = square;
      ++region_size;

      const int file = board_core::file_of(square);
      const int rank = board_core::rank_of(square);
      push_empty_neighbor(&remaining_empties, file - 1, rank, &pending, &pending_size);
      push_empty_neighbor(&remaining_empties, file + 1, rank, &pending, &pending_size);
      push_empty_neighbor(&remaining_empties, file, rank - 1, &pending, &pending_size);
      push_empty_neighbor(&remaining_empties, file, rank + 1, &pending, &pending_size);
    }

    const std::uint8_t region_id = map.size;
    map.region_sizes[region_id] = region_size;
    for (std::uint8_t index = 0; index < region_size; ++index) {
      map.region_for_square[region_squares[index].index] = region_id;
    }
    ++map.size;
  }

  return map;
}

int parity_region_order_score(board_core::Move move, const EmptyRegionMap& regions) noexcept {
  if (!is_normal_move(move)) {
    return 0;
  }

  const std::uint8_t region_id = regions.region_for_square[move.square.index];
  if (region_id == kNoEmptyRegion) {
    return 0;
  }

  // First policy: with fixed 4-neighbor regions, odd empty regions are treated
  // as favorable ordering hints and even regions receive no bonus.
  return (regions.region_sizes[region_id] % 2) == 1 ? 10'000 : 0;
}

int opponent_mobility_after(board_core::Position position, board_core::Move move) noexcept {
  board_core::MoveDelta delta{};
  if (!board_core::make_move_delta(position, move, &delta)) {
    return board_core::kSquareCount;
  }

  board_core::apply_move_delta(&position, delta);
  return std::popcount(board_core::legal_moves(position));
}

int static_othello_order_score(board_core::Position position, board_core::Move move) noexcept {
  int score = 0;

  if (is_corner(move.square)) {
    score += 100'000;
  }

  if (is_dangerous_x_square(position, move.square)) {
    score -= 50'000;
  }
  if (is_dangerous_c_square(position, move.square)) {
    score -= 20'000;
  }

  if (is_edge(move.square)) {
    score += 2'000;
  }
  if (is_stable_like_edge(position, move.square)) {
    score += 1'000;
  }

  return score;
}

int midgame_order_score(board_core::Position position, board_core::Move move,
                        MidgameOrderingHints hints) noexcept {
  int score = 0;

  if (is_move(move, hints.root_best_move.value_or(board_core::make_pass()))) {
    score += 2'000'000;
  }
  if (is_move(move, hints.tt_best_move.value_or(board_core::make_pass()))) {
    score += 1'000'000;
  }
  if (is_move(move, hints.iid_best_move.value_or(board_core::make_pass()))) {
    score += 500'000;
  }

  score += static_othello_order_score(position, move);

  if (hints.use_opponent_mobility) {
    score += (board_core::kSquareCount - opponent_mobility_after(position, move)) * 10;
  }

  for (const board_core::Move killer : hints.killer_moves) {
    if (is_move(move, killer)) {
      score += 100;
    }
  }
  if (hints.history != nullptr) {
    score += std::clamp((*hints.history)[move.square.index], -99, 99);
  }

  return score;
}

int endgame_order_score(board_core::Position position, board_core::Move move,
                        EndgameOrderingHints hints, const EmptyRegionMap* regions) noexcept {
  int score = 0;

  if (is_move(move, hints.tt_best_move.value_or(board_core::make_pass()))) {
    score += 2'000'000;
  }
  if (is_move(move, hints.root_best_move.value_or(board_core::make_pass()))) {
    score += 1'000'000;
  }

  score += static_othello_order_score(position, move);
  score += (board_core::kSquareCount - opponent_mobility_after(position, move)) * 10;
  if (hints.use_parity_ordering && regions != nullptr) {
    score += parity_region_order_score(move, *regions);
  }

  return score;
}

bool comes_before(ScoredMove left, ScoredMove right) noexcept {
  if (left.score != right.score) {
    return left.score > right.score;
  }
  return left.move.square.index < right.move.square.index;
}

void insertion_sort_scored_moves(std::array<ScoredMove, board_core::kSquareCount>* moves,
                                 std::uint8_t size) noexcept {
  for (std::uint8_t index = 1; index < size; ++index) {
    const ScoredMove current = (*moves)[index];
    std::uint8_t shift = index;
    while (shift > 0 && comes_before(current, (*moves)[shift - 1])) {
      (*moves)[shift] = (*moves)[shift - 1];
      --shift;
    }
    (*moves)[shift] = current;
  }
}

template <typename ScoreMove>
MoveList order_moves_by_policy(board_core::Position position, ScoreMove score_move) noexcept {
  MoveList list = move_list_from_legal_mask(board_core::legal_moves(position));
  std::array<ScoredMove, board_core::kSquareCount> scored_moves{};
  for (std::uint8_t index = 0; index < list.size; ++index) {
    scored_moves[index] = ScoredMove{
        .move = list.moves[index],
        .score = score_move(position, list.moves[index]),
    };
  }

  insertion_sort_scored_moves(&scored_moves, list.size);

  for (std::uint8_t index = 0; index < list.size; ++index) {
    list.moves[index] = scored_moves[index].move;
  }
  return list;
}

} // namespace

MoveList order_midgame_moves(board_core::Position position, MidgameOrderingHints hints) noexcept {
  return order_moves_by_policy(
      position, [hints](board_core::Position current_position, board_core::Move move) noexcept {
        return midgame_order_score(current_position, move, hints);
      });
}

MoveList ordered_moves(board_core::Position position, MoveOrderingHints hints) noexcept {
  return order_midgame_moves(position, hints);
}

MoveList order_endgame_moves(board_core::Position position, EndgameOrderingHints hints) noexcept {
  const EmptyRegionMap regions =
      hints.use_parity_ordering ? build_empty_region_map(position) : EmptyRegionMap{};
  return order_moves_by_policy(position, [hints, &regions](board_core::Position current_position,
                                                           board_core::Move move) noexcept {
    return endgame_order_score(current_position, move, hints,
                               hints.use_parity_ordering ? &regions : nullptr);
  });
}

} // namespace vibe_othello::search::internal
