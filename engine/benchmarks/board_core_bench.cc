#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/hash.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace {

using vibe_othello::board_core::apply_move;
using vibe_othello::board_core::apply_move_delta;
using vibe_othello::board_core::bit;
using vibe_othello::board_core::Bitboard;
using vibe_othello::board_core::flips_for_move;
using vibe_othello::board_core::has_legal_move;
using vibe_othello::board_core::hash_position;
using vibe_othello::board_core::initial_black_discs;
using vibe_othello::board_core::initial_position;
using vibe_othello::board_core::initial_white_discs;
using vibe_othello::board_core::legal_moves;
using vibe_othello::board_core::make_move;
using vibe_othello::board_core::make_move_delta;
using vibe_othello::board_core::Move;
using vibe_othello::board_core::MoveDelta;
using vibe_othello::board_core::Position;
using vibe_othello::board_core::Square;
using vibe_othello::board_core::square_from_file_rank;
using vibe_othello::board_core::undo_move;

struct BenchmarkResult {
  std::string_view name;
  int iterations;
  int operations;
  std::chrono::nanoseconds elapsed;
  std::uint64_t checksum;
};

constexpr int kIterations = 200'000;
volatile std::uint64_t g_sink = 0;

constexpr Square square(int file, int rank) noexcept {
  return square_from_file_rank(file, rank);
}

constexpr Bitboard squares(std::initializer_list<Square> values) noexcept {
  Bitboard result = 0;
  for (Square value : values) {
    result |= bit(value);
  }
  return result;
}

struct PositionCase {
  Position position;
};

struct MoveCase {
  Position position;
  Move move;
};

struct AppliedMoveCase {
  Position before;
  Position after;
  MoveDelta delta;
};

constexpr Position white_to_move_initial() noexcept {
  return Position{
      .player = initial_white_discs(),
      .opponent = initial_black_discs(),
      .side_to_move = vibe_othello::board_core::Color::white,
  };
}

constexpr Position multi_direction_position() noexcept {
  return Position{
      .player = squares({
          square(0, 3),
          square(3, 0),
          square(6, 3),
          square(3, 6),
      }),
      .opponent = squares({
          square(1, 3),
          square(2, 3),
          square(3, 1),
          square(3, 2),
          square(4, 3),
          square(5, 3),
          square(3, 4),
          square(3, 5),
      }),
      .side_to_move = vibe_othello::board_core::Color::black,
  };
}

constexpr Position pass_available_position() noexcept {
  return Position{
      .player = bit(square(1, 0)),
      .opponent = bit(square(0, 0)),
      .side_to_move = vibe_othello::board_core::Color::black,
  };
}

constexpr Position edge_run_position() noexcept {
  return Position{
      .player = bit(square(7, 0)),
      .opponent = bit(square(6, 0)),
      .side_to_move = vibe_othello::board_core::Color::black,
  };
}

constexpr Position dense_late_position() noexcept {
  return Position{
      .player = 0x00F0F7F7F7F70F00ULL,
      .opponent = 0x7F0F08080808F07EULL,
      .side_to_move = vibe_othello::board_core::Color::black,
  };
}

constexpr std::array<PositionCase, 7> kPositionCorpus{{
    PositionCase{.position = initial_position()},
    PositionCase{.position = white_to_move_initial()},
    PositionCase{.position = multi_direction_position()},
    PositionCase{.position = pass_available_position()},
    PositionCase{.position = edge_run_position()},
    PositionCase{.position = dense_late_position()},
    PositionCase{.position =
                     Position{
                         .player = 0x5555555555555555ULL,
                         .opponent = 0xAAAAAAAAAAAAAAAAULL,
                         .side_to_move = vibe_othello::board_core::Color::black,
                     }},
}};

constexpr std::array<MoveCase, 4> kMoveCorpus{{
    MoveCase{.position = initial_position(), .move = make_move(square(3, 2))},
    MoveCase{.position = white_to_move_initial(), .move = make_move(square(2, 4))},
    MoveCase{.position = multi_direction_position(), .move = make_move(square(3, 3))},
    MoveCase{.position = edge_run_position(), .move = make_move(square(5, 0))},
}};

std::uint64_t position_checksum(Position position) noexcept {
  return position.player ^ (position.opponent << 1U) ^
         (position.side_to_move == vibe_othello::board_core::Color::black ? 0ULL
                                                                          : 0x9E3779B97F4A7C15ULL);
}

std::array<AppliedMoveCase, kMoveCorpus.size()> make_applied_move_corpus() noexcept {
  std::array<AppliedMoveCase, kMoveCorpus.size()> result{};
  for (std::size_t index = 0; index < kMoveCorpus.size(); ++index) {
    Position position = kMoveCorpus[index].position;
    MoveDelta delta{};
    if (!apply_move(&position, kMoveCorpus[index].move, &delta)) {
      result[index] = AppliedMoveCase{};
      continue;
    }
    result[index] = AppliedMoveCase{
        .before = kMoveCorpus[index].position,
        .after = position,
        .delta = delta,
    };
  }
  return result;
}

bool applied_move_corpus_is_valid(
    const std::array<AppliedMoveCase, kMoveCorpus.size()>& corpus) noexcept {
  for (AppliedMoveCase entry : corpus) {
    if (entry.delta.flipped == 0) {
      return false;
    }
  }
  return true;
}

template <typename Function>
BenchmarkResult run_benchmark(std::string_view name, int iterations, int operations_per_iteration,
                              Function function) {
  std::uint64_t checksum = 0;
  const auto start = std::chrono::steady_clock::now();
  for (int iteration = 0; iteration < iterations; ++iteration) {
    const std::uint64_t value = function();
    g_sink = value;
    checksum += value ^ static_cast<std::uint64_t>(iteration);
  }
  const auto end = std::chrono::steady_clock::now();

  return BenchmarkResult{
      .name = name,
      .iterations = iterations,
      .operations = iterations * operations_per_iteration,
      .elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start),
      .checksum = checksum,
  };
}

void print_result(BenchmarkResult result) {
  const double ns_per_operation =
      static_cast<double>(result.elapsed.count()) / static_cast<double>(result.operations);

  std::cout << std::left << std::setw(22) << result.name << std::right << std::setw(12)
            << result.operations << std::setw(16) << result.elapsed.count() << std::setw(14)
            << std::fixed << std::setprecision(2) << ns_per_operation << "  0x" << std::hex
            << result.checksum << std::dec << '\n';
}

std::uint64_t bench_legal_moves() noexcept {
  std::uint64_t checksum = 0;
  for (PositionCase entry : kPositionCorpus) {
    checksum ^= legal_moves(entry.position);
  }
  return checksum;
}

std::uint64_t bench_has_legal_move() noexcept {
  std::uint64_t checksum = 0;
  for (PositionCase entry : kPositionCorpus) {
    checksum = (checksum << 1U) ^ (has_legal_move(entry.position) ? 1ULL : 0ULL);
  }
  return checksum;
}

std::uint64_t bench_flips_for_move() noexcept {
  std::uint64_t checksum = 0;
  for (MoveCase entry : kMoveCorpus) {
    checksum ^= flips_for_move(entry.position, entry.move.square);
  }
  return checksum;
}

std::uint64_t bench_hash_position() noexcept {
  std::uint64_t checksum = 0;
  for (PositionCase entry : kPositionCorpus) {
    checksum ^= hash_position(entry.position);
  }
  return checksum;
}

std::uint64_t bench_apply_move() noexcept {
  std::uint64_t checksum = 0;
  for (MoveCase entry : kMoveCorpus) {
    Position position = entry.position;
    MoveDelta delta{};
    if (apply_move(&position, entry.move, &delta)) {
      checksum ^= position_checksum(position) ^ delta.flipped;
    }
  }
  return checksum;
}

std::uint64_t bench_make_move_delta() noexcept {
  std::uint64_t checksum = 0;
  for (MoveCase entry : kMoveCorpus) {
    MoveDelta delta{};
    if (make_move_delta(entry.position, entry.move, &delta)) {
      checksum ^= position_checksum(entry.position) ^ delta.flipped;
    }
  }
  return checksum;
}

std::uint64_t
bench_apply_move_delta(const std::array<AppliedMoveCase, kMoveCorpus.size()>& corpus) noexcept {
  std::uint64_t checksum = 0;
  for (AppliedMoveCase entry : corpus) {
    Position position = entry.before;
    apply_move_delta(&position, entry.delta);
    checksum ^= position_checksum(position) ^ entry.delta.flipped;
  }
  return checksum;
}

std::uint64_t
bench_undo_move(const std::array<AppliedMoveCase, kMoveCorpus.size()>& corpus) noexcept {
  std::uint64_t checksum = 0;
  for (AppliedMoveCase entry : corpus) {
    Position position = entry.after;
    undo_move(&position, entry.delta);
    checksum ^= position_checksum(position);
  }
  return checksum;
}

} // namespace

int main() {
  const std::array<AppliedMoveCase, kMoveCorpus.size()> applied_move_corpus =
      make_applied_move_corpus();
  if (!applied_move_corpus_is_valid(applied_move_corpus)) {
    std::cerr << "invalid board-core benchmark move corpus\n";
    return 1;
  }

  std::cout << "board-core benchmark corpus\n";
  std::cout << std::left << std::setw(22) << "benchmark" << std::right << std::setw(12)
            << "operations" << std::setw(16) << "elapsed_ns" << std::setw(14) << "ns/op"
            << "  checksum" << '\n';

  print_result(run_benchmark("legal_moves", kIterations, static_cast<int>(kPositionCorpus.size()),
                             bench_legal_moves));
  print_result(run_benchmark("has_legal_move", kIterations,
                             static_cast<int>(kPositionCorpus.size()), bench_has_legal_move));
  print_result(run_benchmark("flips_for_move", kIterations, static_cast<int>(kMoveCorpus.size()),
                             bench_flips_for_move));
  print_result(run_benchmark("apply_move", kIterations, static_cast<int>(kMoveCorpus.size()),
                             bench_apply_move));
  print_result(run_benchmark("make_move_delta", kIterations, static_cast<int>(kMoveCorpus.size()),
                             bench_make_move_delta));
  print_result(run_benchmark(
      "apply_move_delta", kIterations, static_cast<int>(kMoveCorpus.size()),
      [&applied_move_corpus] { return bench_apply_move_delta(applied_move_corpus); }));
  print_result(
      run_benchmark("undo_move", kIterations, static_cast<int>(kMoveCorpus.size()),
                    [&applied_move_corpus] { return bench_undo_move(applied_move_corpus); }));
  print_result(run_benchmark("hash_position", kIterations, static_cast<int>(kPositionCorpus.size()),
                             bench_hash_position));

  return 0;
}
