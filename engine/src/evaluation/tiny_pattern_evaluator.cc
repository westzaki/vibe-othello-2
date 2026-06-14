#include "vibe_othello/evaluation/tiny_pattern_evaluator.h"

#include "vibe_othello/evaluation/pattern.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>

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

constexpr std::uint8_t kTinyPatternPhaseCount = 2;
constexpr std::size_t kEdgeTableIndex = 0;
constexpr std::size_t kCornerTableIndex = 1;

std::int64_t max_abs_weight(std::span<const search::Score> weights) noexcept {
  std::int64_t max_weight = 0;
  for (search::Score weight : weights) {
    const std::int64_t abs_weight =
        weight < 0 ? -static_cast<std::int64_t>(weight) : static_cast<std::int64_t>(weight);
    if (abs_weight > max_weight) {
      max_weight = abs_weight;
    }
  }
  return max_weight;
}

void validate_table(const PatternWeightTable& table, PatternSchema expected_schema,
                    std::uint8_t phase_count) {
  if (table.pattern_id != expected_schema.name || table.pattern_length != expected_schema.length) {
    throw std::invalid_argument("tiny pattern weights use an incompatible pattern schema");
  }

  const std::size_t expected_size =
      static_cast<std::size_t>(phase_count) * pattern_size(expected_schema.length);
  if (table.weights.size() != expected_size) {
    throw std::invalid_argument("tiny pattern weights have an incompatible table size");
  }
}

void validate_tiny_pattern_weights(const PatternWeights& weights) {
  if (weights.phase_count() != kTinyPatternPhaseCount || weights.tables().size() != 2) {
    throw std::invalid_argument("tiny pattern weights use an incompatible phase or table count");
  }

  for (std::uint8_t disc_count = 0; disc_count < PatternWeights::kDiscCountEntries; ++disc_count) {
    if (weights.phase_for_disc_count(disc_count) >= weights.phase_count()) {
      throw std::invalid_argument("tiny pattern weights use an invalid phase mapping");
    }
  }

  validate_table(weights.tables()[kEdgeTableIndex], kEdge8, weights.phase_count());
  validate_table(weights.tables()[kCornerTableIndex], kCorner3x3, weights.phase_count());

  const std::int64_t max_score = static_cast<std::int64_t>(kEdgeInstances.size()) *
                                     max_abs_weight(weights.tables()[kEdgeTableIndex].weights) +
                                 static_cast<std::int64_t>(kCornerInstances.size()) *
                                     max_abs_weight(weights.tables()[kCornerTableIndex].weights);
  if (max_score >= search::kScoreWin) {
    throw std::invalid_argument("tiny pattern weights can produce search sentinel scores");
  }
}

search::Score
evaluate_instances(const PatternWeights& weights, std::size_t table_index, std::uint8_t phase,
                   board_core::Position position,
                   std::span<const std::span<const board_core::Square>> instances) noexcept {
  search::Score score = 0;
  for (std::span<const board_core::Square> squares : instances) {
    score += weights.weight(table_index, phase, ternary_pattern_index(position, squares));
  }
  return score;
}

} // namespace

TinyPatternEvaluator::TinyPatternEvaluator(PatternWeights weights) : weights_(std::move(weights)) {
  validate_tiny_pattern_weights(weights_);
}

search::Score TinyPatternEvaluator::evaluate(const board_core::Position& position) const noexcept {
  const int discs = std::popcount(board_core::occupied(position));
  const std::uint8_t phase = weights_.phase_for_disc_count(discs);

  search::Score score = 0;
  score += evaluate_instances(weights_, kEdgeTableIndex, phase, position,
                              std::span<const std::span<const board_core::Square>>{kEdgeInstances});
  score +=
      evaluate_instances(weights_, kCornerTableIndex, phase, position,
                         std::span<const std::span<const board_core::Square>>{kCornerInstances});
  return score;
}

} // namespace vibe_othello::evaluation
