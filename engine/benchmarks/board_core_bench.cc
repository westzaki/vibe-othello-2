#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/hash.h"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace {

using vibe_othello::board_core::apply_move;
using vibe_othello::board_core::bit;
using vibe_othello::board_core::Bitboard;
using vibe_othello::board_core::flips_for_move;
using vibe_othello::board_core::hash_position;
using vibe_othello::board_core::initial_position;
using vibe_othello::board_core::legal_moves;
using vibe_othello::board_core::make_move;
using vibe_othello::board_core::MoveDelta;
using vibe_othello::board_core::Position;
using vibe_othello::board_core::Square;
using vibe_othello::board_core::square_from_file_rank;
using vibe_othello::board_core::undo_move;

struct BenchmarkResult {
  std::string_view name;
  int iterations;
  std::chrono::nanoseconds elapsed;
  std::uint64_t checksum;
};

constexpr int kIterations = 1'000'000;
volatile std::uint64_t g_sink = 0;

constexpr Square square(int file, int rank) noexcept {
  return square_from_file_rank(file, rank);
}

template <typename Function>
BenchmarkResult run_benchmark(std::string_view name, int iterations, Function function) {
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
      .elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start),
      .checksum = checksum,
  };
}

void print_result(BenchmarkResult result) {
  const double ns_per_iteration =
      static_cast<double>(result.elapsed.count()) / static_cast<double>(result.iterations);

  std::cout << std::left << std::setw(22) << result.name << std::right << std::setw(12)
            << result.iterations << std::setw(16) << result.elapsed.count() << std::setw(14)
            << std::fixed << std::setprecision(2) << ns_per_iteration << "  0x" << std::hex
            << result.checksum << std::dec << '\n';
}

std::uint64_t bench_legal_moves() noexcept {
  return legal_moves(initial_position());
}

std::uint64_t bench_flips_for_move() noexcept {
  return flips_for_move(initial_position(), square(3, 2));
}

std::uint64_t bench_hash_position() noexcept {
  return hash_position(initial_position());
}

std::uint64_t bench_apply_undo(Position* position) noexcept {
  MoveDelta delta{};
  if (!apply_move(position, make_move(square(3, 2)), &delta)) {
    return 0;
  }

  const Bitboard after_move = bit(square(3, 2)) ^ delta.flipped;
  undo_move(position, delta);
  return after_move ^ hash_position(*position);
}

} // namespace

int main() {
  std::cout << "board-core benchmark baseline\n";
  std::cout << std::left << std::setw(22) << "benchmark" << std::right << std::setw(12)
            << "iterations" << std::setw(16) << "elapsed_ns" << std::setw(14) << "ns/iter"
            << "  checksum" << '\n';

  print_result(run_benchmark("legal_moves", kIterations, bench_legal_moves));
  print_result(run_benchmark("flips_for_move", kIterations, bench_flips_for_move));
  Position apply_undo_position = initial_position();
  print_result(run_benchmark("apply_undo", kIterations, [&apply_undo_position] {
    return bench_apply_undo(&apply_undo_position);
  }));
  print_result(run_benchmark("hash_position", kIterations, bench_hash_position));

  return 0;
}
