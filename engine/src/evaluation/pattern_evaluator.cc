#include "vibe_othello/evaluation/pattern_evaluator.h"

#include "vibe_othello/evaluation/pattern.h"

#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

namespace vibe_othello::evaluation {
namespace {

using board_core::square_from_file_rank;

constexpr std::uint8_t kEdge8Length = 8;
constexpr std::uint8_t kCorner3x3Length = 9;

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

static_assert(kRank1Edge.size() == kEdge8Length);
static_assert(kRank8Edge.size() == kEdge8Length);
static_assert(kFileAEdge.size() == kEdge8Length);
static_assert(kFileHEdge.size() == kEdge8Length);
static_assert(kA1Corner.size() == kCorner3x3Length);
static_assert(kH1Corner.size() == kCorner3x3Length);
static_assert(kA8Corner.size() == kCorner3x3Length);
static_assert(kH8Corner.size() == kCorner3x3Length);

std::vector<board_core::Square> to_vector(std::span<const board_core::Square> squares) {
  return std::vector<board_core::Square>{squares.begin(), squares.end()};
}

[[nodiscard]] std::optional<std::size_t>
expected_table_weight_count(std::uint8_t phase_count, std::uint8_t pattern_length) noexcept {
  if (pattern_length == 0) {
    return std::nullopt;
  }
  const std::optional<std::uint32_t> pattern_count = checked_pattern_size(pattern_length);
  if (!pattern_count.has_value()) {
    return std::nullopt;
  }
  if (phase_count == 0) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(*pattern_count) * phase_count;
}

[[nodiscard]] std::int64_t max_abs_weight(std::span<const search::Score> weights) noexcept {
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

void validate_instance_squares(const PatternFeatureTable& feature_table) {
  for (const std::vector<board_core::Square>& squares : feature_table.instances) {
    if (squares.size() != feature_table.pattern_length) {
      throw std::invalid_argument("pattern feature instance length does not match schema");
    }

    std::array<bool, board_core::kSquareCount> seen{};
    for (const board_core::Square square : squares) {
      if (!board_core::is_valid(square)) {
        throw std::invalid_argument("pattern feature instance uses an invalid square");
      }
      if (seen[square.index]) {
        throw std::invalid_argument("pattern feature instance uses a duplicate square");
      }
      seen[square.index] = true;
    }
  }
}

void validate_score_range(const PatternWeights& weights, const PatternFeatureSet& feature_set) {
  std::int64_t max_score = 0;
  for (std::size_t table_index = 0; table_index < feature_set.tables.size(); ++table_index) {
    const std::int64_t table_max_weight = max_abs_weight(weights.tables()[table_index].weights);
    if (table_max_weight == 0 || feature_set.tables[table_index].instances.empty()) {
      continue;
    }

    const std::size_t max_safe_instances =
        static_cast<std::size_t>((search::kScoreWin - 1) / table_max_weight);
    if (feature_set.tables[table_index].instances.size() > max_safe_instances) {
      throw std::invalid_argument("pattern evaluator weights can produce search sentinel scores");
    }

    const std::int64_t contribution =
        table_max_weight *
        static_cast<std::int64_t>(feature_set.tables[table_index].instances.size());
    if (max_score >= search::kScoreWin - contribution) {
      throw std::invalid_argument("pattern evaluator weights can produce search sentinel scores");
    }
    max_score += contribution;
  }
}

void validate_pattern_evaluator_inputs(const PatternWeights& weights,
                                       const PatternFeatureSet& feature_set) {
  if (weights.tables().size() != feature_set.tables.size()) {
    throw std::invalid_argument("pattern evaluator table count does not match weights");
  }

  for (std::uint8_t disc_count = 0; disc_count < PatternWeights::kDiscCountEntries; ++disc_count) {
    if (weights.phase_for_disc_count(disc_count) >= weights.phase_count()) {
      throw std::invalid_argument("pattern evaluator weights use an invalid phase mapping");
    }
  }

  for (std::size_t table_index = 0; table_index < feature_set.tables.size(); ++table_index) {
    const PatternFeatureTable& feature_table = feature_set.tables[table_index];
    const PatternWeightTable& weight_table = weights.tables()[table_index];

    if (feature_table.pattern_id != weight_table.pattern_id) {
      throw std::invalid_argument("pattern evaluator table id does not match weights");
    }
    if (feature_table.pattern_length != weight_table.pattern_length) {
      throw std::invalid_argument("pattern evaluator table length does not match weights");
    }

    const std::optional<std::size_t> expected_weight_count =
        expected_table_weight_count(weights.phase_count(), weight_table.pattern_length);
    if (!expected_weight_count.has_value() ||
        weight_table.weights.size() != *expected_weight_count) {
      throw std::invalid_argument("pattern evaluator table weight count does not match schema");
    }

    validate_instance_squares(feature_table);
  }

  validate_score_range(weights, feature_set);
}

} // namespace

PatternFeatureSet tiny_pattern_feature_set_fixture() {
  return PatternFeatureSet{
      .id = "tiny-pattern-v1",
      .tables =
          {
              PatternFeatureTable{
                  .pattern_id = "edge-8",
                  .pattern_length = kEdge8Length,
                  .instances =
                      {
                          to_vector(kRank1Edge),
                          to_vector(kRank8Edge),
                          to_vector(kFileAEdge),
                          to_vector(kFileHEdge),
                      },
              },
              PatternFeatureTable{
                  .pattern_id = "corner-3x3",
                  .pattern_length = kCorner3x3Length,
                  .instances =
                      {
                          to_vector(kA1Corner),
                          to_vector(kH1Corner),
                          to_vector(kA8Corner),
                          to_vector(kH8Corner),
                      },
              },
          },
  };
}

PatternEvaluator::PatternEvaluator(PatternWeights weights, PatternFeatureSet feature_set)
    : weights_(std::move(weights)), feature_set_(std::move(feature_set)) {
  validate_pattern_evaluator_inputs(weights_, feature_set_);
}

search::Score PatternEvaluator::evaluate(const board_core::Position& position) const noexcept {
  const int discs = std::popcount(board_core::occupied(position));
  const std::uint8_t phase = weights_.phase_for_disc_count(discs);

  search::Score score = 0;
  for (std::size_t table_index = 0; table_index < feature_set_.tables.size(); ++table_index) {
    for (const std::vector<board_core::Square>& squares :
         feature_set_.tables[table_index].instances) {
      score += weights_.weight(table_index, phase, ternary_pattern_index(position, squares));
    }
  }
  return score;
}

} // namespace vibe_othello::evaluation
