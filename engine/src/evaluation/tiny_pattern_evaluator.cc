#include "vibe_othello/evaluation/tiny_pattern_evaluator.h"

#include "vibe_othello/evaluation/pattern.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace vibe_othello::evaluation {
namespace {

using board_core::square_from_file_rank;

constexpr PatternSchema kEdge8{
    .name = "edge-8",
    .length = 8,
};

constexpr PatternSchema kCorner3x3{
    .name = "corner-3x3",
    .length = 9,
};

constexpr std::array<board_core::Square, 8> kRank1Edge{
    square_from_file_rank(0, 0), square_from_file_rank(1, 0), square_from_file_rank(2, 0),
    square_from_file_rank(3, 0), square_from_file_rank(4, 0), square_from_file_rank(5, 0),
    square_from_file_rank(6, 0), square_from_file_rank(7, 0),
};

constexpr std::array<board_core::Square, 8> kRank8Edge{
    square_from_file_rank(0, 7), square_from_file_rank(1, 7), square_from_file_rank(2, 7),
    square_from_file_rank(3, 7), square_from_file_rank(4, 7), square_from_file_rank(5, 7),
    square_from_file_rank(6, 7), square_from_file_rank(7, 7),
};

constexpr std::array<board_core::Square, 8> kFileAEdge{
    square_from_file_rank(0, 0), square_from_file_rank(0, 1), square_from_file_rank(0, 2),
    square_from_file_rank(0, 3), square_from_file_rank(0, 4), square_from_file_rank(0, 5),
    square_from_file_rank(0, 6), square_from_file_rank(0, 7),
};

constexpr std::array<board_core::Square, 8> kFileHEdge{
    square_from_file_rank(7, 0), square_from_file_rank(7, 1), square_from_file_rank(7, 2),
    square_from_file_rank(7, 3), square_from_file_rank(7, 4), square_from_file_rank(7, 5),
    square_from_file_rank(7, 6), square_from_file_rank(7, 7),
};

constexpr std::array<board_core::Square, 9> kA1Corner{
    square_from_file_rank(0, 0), square_from_file_rank(1, 0), square_from_file_rank(2, 0),
    square_from_file_rank(0, 1), square_from_file_rank(1, 1), square_from_file_rank(2, 1),
    square_from_file_rank(0, 2), square_from_file_rank(1, 2), square_from_file_rank(2, 2),
};

constexpr std::array<board_core::Square, 9> kH1Corner{
    square_from_file_rank(7, 0), square_from_file_rank(6, 0), square_from_file_rank(5, 0),
    square_from_file_rank(7, 1), square_from_file_rank(6, 1), square_from_file_rank(5, 1),
    square_from_file_rank(7, 2), square_from_file_rank(6, 2), square_from_file_rank(5, 2),
};

constexpr std::array<board_core::Square, 9> kA8Corner{
    square_from_file_rank(0, 7), square_from_file_rank(1, 7), square_from_file_rank(2, 7),
    square_from_file_rank(0, 6), square_from_file_rank(1, 6), square_from_file_rank(2, 6),
    square_from_file_rank(0, 5), square_from_file_rank(1, 5), square_from_file_rank(2, 5),
};

constexpr std::array<board_core::Square, 9> kH8Corner{
    square_from_file_rank(7, 7), square_from_file_rank(6, 7), square_from_file_rank(5, 7),
    square_from_file_rank(7, 6), square_from_file_rank(6, 6), square_from_file_rank(5, 6),
    square_from_file_rank(7, 5), square_from_file_rank(6, 5), square_from_file_rank(5, 5),
};

static_assert(kRank1Edge.size() == kEdge8.length);
static_assert(kRank8Edge.size() == kEdge8.length);
static_assert(kFileAEdge.size() == kEdge8.length);
static_assert(kFileHEdge.size() == kEdge8.length);
static_assert(kA1Corner.size() == kCorner3x3.length);
static_assert(kH1Corner.size() == kCorner3x3.length);
static_assert(kA8Corner.size() == kCorner3x3.length);
static_assert(kH8Corner.size() == kCorner3x3.length);

constexpr std::array<std::span<const board_core::Square>, 4> kEdgeInstances{
    std::span<const board_core::Square>{kRank1Edge},
    std::span<const board_core::Square>{kRank8Edge},
    std::span<const board_core::Square>{kFileAEdge},
    std::span<const board_core::Square>{kFileHEdge},
};

constexpr std::array<std::span<const board_core::Square>, 4> kCornerInstances{
    std::span<const board_core::Square>{kA1Corner},
    std::span<const board_core::Square>{kH1Corner},
    std::span<const board_core::Square>{kA8Corner},
    std::span<const board_core::Square>{kH8Corner},
};

constexpr int kEarlyPhaseDiscBoundary = 20;
constexpr search::Score kScoreScale = 2;

search::Score tiny_weight(std::uint32_t index, std::uint8_t length, bool early_phase) noexcept {
  search::Score score = 0;
  for (std::uint8_t digit = 0; digit < length; ++digit) {
    const std::uint32_t cell = index % 3;
    index /= 3;
    const search::Score positional_weight = static_cast<search::Score>(digit + 1);
    if (cell == static_cast<std::uint32_t>(PatternCell::player)) {
      score += positional_weight;
    } else if (cell == static_cast<std::uint32_t>(PatternCell::opponent)) {
      score -= positional_weight;
    }
  }

  return early_phase ? score : static_cast<search::Score>(score * kScoreScale);
}

search::Score evaluate_instances(board_core::Position position,
                                 std::span<const std::span<const board_core::Square>> instances,
                                 std::uint8_t length, bool early_phase) noexcept {
  search::Score score = 0;
  for (std::span<const board_core::Square> squares : instances) {
    score += tiny_weight(ternary_pattern_index(position, squares), length, early_phase);
  }
  return score;
}

} // namespace

std::uint32_t ternary_pattern_index(board_core::Position position,
                                    std::span<const board_core::Square> squares) noexcept {
  std::uint32_t index = 0;
  std::uint32_t place = 1;
  for (board_core::Square square : squares) {
    index += static_cast<std::uint32_t>(cell_at(position, square)) * place;
    place *= 3;
  }
  return index;
}

search::Score TinyPatternEvaluator::evaluate(const board_core::Position& position) const noexcept {
  const int discs = std::popcount(board_core::occupied(position));
  const bool early_phase = discs < kEarlyPhaseDiscBoundary;

  search::Score score = 0;
  score += evaluate_instances(position,
                              std::span<const std::span<const board_core::Square>>{kEdgeInstances},
                              kEdge8.length, early_phase);
  score += evaluate_instances(
      position, std::span<const std::span<const board_core::Square>>{kCornerInstances},
      kCorner3x3.length, early_phase);
  return score;
}

} // namespace vibe_othello::evaluation
