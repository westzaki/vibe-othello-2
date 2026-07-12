#include "vibe_othello/evaluation/pattern_evaluator.h"

#include "vibe_othello/evaluation/pattern.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
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

std::vector<board_core::Square> line(int file, int rank, int file_delta, int rank_delta,
                                     std::uint8_t length) {
  std::vector<board_core::Square> result;
  result.reserve(length);
  for (std::uint8_t index = 0; index < length; ++index) {
    result.push_back(square_from_file_rank(file + file_delta * index, rank + rank_delta * index));
  }
  return result;
}

std::vector<board_core::Square> band(int file, int rank, int primary_file_delta,
                                     int primary_rank_delta, int secondary_file_delta,
                                     int secondary_rank_delta, std::uint8_t primary_length,
                                     std::uint8_t secondary_length) {
  std::vector<board_core::Square> result;
  result.reserve(static_cast<std::size_t>(primary_length) * secondary_length);
  for (std::uint8_t secondary = 0; secondary < secondary_length; ++secondary) {
    for (std::uint8_t primary = 0; primary < primary_length; ++primary) {
      result.push_back(square_from_file_rank(
          file + primary_file_delta * primary + secondary_file_delta * secondary,
          rank + primary_rank_delta * primary + secondary_rank_delta * secondary));
    }
  }
  return result;
}

std::vector<board_core::Square> edge_plus_x(int file, int rank, int file_delta, int rank_delta,
                                            int first_x_file, int first_x_rank, int second_x_file,
                                            int second_x_rank) {
  std::vector<board_core::Square> result = line(file, rank, file_delta, rank_delta, 8);
  result.push_back(square_from_file_rank(first_x_file, first_x_rank));
  result.push_back(square_from_file_rank(second_x_file, second_x_rank));
  return result;
}

std::vector<board_core::Square> corner_wing(int file, int rank, int edge_file_delta,
                                            int edge_rank_delta, int inside_file_delta,
                                            int inside_rank_delta) {
  return {
      square_from_file_rank(file, rank),
      square_from_file_rank(file + edge_file_delta, rank + edge_rank_delta),
      square_from_file_rank(file + edge_file_delta * 2, rank + edge_rank_delta * 2),
      square_from_file_rank(file + edge_file_delta * 3, rank + edge_rank_delta * 3),
      square_from_file_rank(file + inside_file_delta, rank + inside_rank_delta),
      square_from_file_rank(file + edge_file_delta + inside_file_delta,
                            rank + edge_rank_delta + inside_rank_delta),
      square_from_file_rank(file + edge_file_delta * 2 + inside_file_delta,
                            rank + edge_rank_delta * 2 + inside_rank_delta),
      square_from_file_rank(file + edge_file_delta + inside_file_delta * 2,
                            rank + edge_rank_delta + inside_rank_delta * 2),
  };
}

std::vector<board_core::Square> diagonal_corner(int file, int rank, int diagonal_file_delta,
                                                int diagonal_rank_delta, int edge_file_delta,
                                                int edge_rank_delta, int side_file_delta,
                                                int side_rank_delta) {
  return {
      square_from_file_rank(file, rank),
      square_from_file_rank(file + diagonal_file_delta, rank + diagonal_rank_delta),
      square_from_file_rank(file + diagonal_file_delta * 2, rank + diagonal_rank_delta * 2),
      square_from_file_rank(file + diagonal_file_delta * 3, rank + diagonal_rank_delta * 3),
      square_from_file_rank(file + diagonal_file_delta * 4, rank + diagonal_rank_delta * 4),
      square_from_file_rank(file + diagonal_file_delta * 5, rank + diagonal_rank_delta * 5),
      square_from_file_rank(file + edge_file_delta, rank + edge_rank_delta),
      square_from_file_rank(file + side_file_delta, rank + side_rank_delta),
  };
}

PatternFeatureTable table(std::string pattern_id, std::uint8_t length,
                          std::vector<std::vector<board_core::Square>> instances) {
  return PatternFeatureTable{
      .pattern_id = std::move(pattern_id),
      .pattern_length = length,
      .instances = std::move(instances),
  };
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
  if (weights.score_scale() == 0) {
    throw std::invalid_argument("pattern evaluator score scale must be positive");
  }
  const std::int64_t scaled_score_limit =
      static_cast<std::int64_t>(search::kScoreWin - 1) * weights.score_scale();
  std::int64_t max_score = max_abs_weight(weights.phase_biases());
  if (max_score > scaled_score_limit) {
    throw std::invalid_argument("pattern evaluator weights can produce search sentinel scores");
  }
  for (std::size_t table_index = 0; table_index < feature_set.tables.size(); ++table_index) {
    const std::int64_t table_max_weight = max_abs_weight(weights.tables()[table_index].weights);
    if (table_max_weight == 0 || feature_set.tables[table_index].instances.empty()) {
      continue;
    }

    const std::size_t max_safe_instances =
        static_cast<std::size_t>(scaled_score_limit / table_max_weight);
    if (feature_set.tables[table_index].instances.size() > max_safe_instances) {
      throw std::invalid_argument("pattern evaluator weights can produce search sentinel scores");
    }

    const std::int64_t contribution =
        table_max_weight *
        static_cast<std::int64_t>(feature_set.tables[table_index].instances.size());
    if (max_score > scaled_score_limit - contribution) {
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
  if (weights.phase_biases().size() != weights.phase_count()) {
    throw std::invalid_argument("pattern evaluator bias count does not match phase count");
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

PatternFeatureSet buro_lite_pattern_feature_set() {
  return PatternFeatureSet{
      .id = "pattern-v1-buro-lite",
      .tables =
          {
              table("edge-8", 8,
                    {
                        line(0, 0, 1, 0, 8),
                        line(0, 7, 1, 0, 8),
                        line(0, 0, 0, 1, 8),
                        line(7, 0, 0, 1, 8),
                    }),
              table("near-edge-8", 8,
                    {
                        line(0, 1, 1, 0, 8),
                        line(0, 6, 1, 0, 8),
                        line(1, 0, 0, 1, 8),
                        line(6, 0, 0, 1, 8),
                    }),
              table("diagonal-8", 8,
                    {
                        line(0, 0, 1, 1, 8),
                        line(7, 0, -1, 1, 8),
                    }),
              table("diagonal-7", 7,
                    {
                        line(1, 0, 1, 1, 7),
                        line(0, 1, 1, 1, 7),
                        line(6, 0, -1, 1, 7),
                        line(7, 1, -1, 1, 7),
                    }),
              table("corner-2x5", 10,
                    {
                        band(0, 0, 1, 0, 0, 1, 5, 2),
                        band(0, 0, 0, 1, 1, 0, 5, 2),
                        band(7, 0, -1, 0, 0, 1, 5, 2),
                        band(7, 0, 0, 1, -1, 0, 5, 2),
                        band(0, 7, 1, 0, 0, -1, 5, 2),
                        band(0, 7, 0, -1, 1, 0, 5, 2),
                        band(7, 7, -1, 0, 0, -1, 5, 2),
                        band(7, 7, 0, -1, -1, 0, 5, 2),
                    }),
              table("corner-3x3", 9,
                    {
                        band(0, 0, 1, 0, 0, 1, 3, 3),
                        band(7, 0, -1, 0, 0, 1, 3, 3),
                        band(0, 7, 1, 0, 0, -1, 3, 3),
                        band(7, 7, -1, 0, 0, -1, 3, 3),
                    }),
          },
  };
}

PatternFeatureSet endgame_lite_pattern_feature_set() {
  return PatternFeatureSet{
      .id = "pattern-v2-endgame-lite",
      .tables =
          {
              table("edge-8", 8,
                    {
                        line(0, 0, 1, 0, 8),
                        line(0, 7, 1, 0, 8),
                        line(0, 0, 0, 1, 8),
                        line(7, 0, 0, 1, 8),
                    }),
              table("near-edge-8", 8,
                    {
                        line(0, 1, 1, 0, 8),
                        line(0, 6, 1, 0, 8),
                        line(1, 0, 0, 1, 8),
                        line(6, 0, 0, 1, 8),
                    }),
              table("diagonal-8", 8,
                    {
                        line(0, 0, 1, 1, 8),
                        line(7, 0, -1, 1, 8),
                    }),
              table("diagonal-7", 7,
                    {
                        line(1, 0, 1, 1, 7),
                        line(0, 1, 1, 1, 7),
                        line(6, 0, -1, 1, 7),
                        line(7, 1, -1, 1, 7),
                    }),
              table("corner-2x5", 10,
                    {
                        band(0, 0, 1, 0, 0, 1, 5, 2),
                        band(0, 0, 0, 1, 1, 0, 5, 2),
                        band(7, 0, -1, 0, 0, 1, 5, 2),
                        band(7, 0, 0, 1, -1, 0, 5, 2),
                        band(0, 7, 1, 0, 0, -1, 5, 2),
                        band(0, 7, 0, -1, 1, 0, 5, 2),
                        band(7, 7, -1, 0, 0, -1, 5, 2),
                        band(7, 7, 0, -1, -1, 0, 5, 2),
                    }),
              table("corner-3x3", 9,
                    {
                        band(0, 0, 1, 0, 0, 1, 3, 3),
                        band(7, 0, -1, 0, 0, 1, 3, 3),
                        band(0, 7, 1, 0, 0, -1, 3, 3),
                        band(7, 7, -1, 0, 0, -1, 3, 3),
                    }),
              table("corner-2x4-8", 8,
                    {
                        band(0, 0, 1, 0, 0, 1, 4, 2),
                        band(0, 0, 0, 1, 1, 0, 4, 2),
                        band(7, 0, -1, 0, 0, 1, 4, 2),
                        band(7, 0, 0, 1, -1, 0, 4, 2),
                        band(0, 7, 1, 0, 0, -1, 4, 2),
                        band(0, 7, 0, -1, 1, 0, 4, 2),
                        band(7, 7, -1, 0, 0, -1, 4, 2),
                        band(7, 7, 0, -1, -1, 0, 4, 2),
                    }),
              table("edge-plus-x-10", 10,
                    {
                        edge_plus_x(0, 0, 1, 0, 1, 1, 6, 1),
                        edge_plus_x(0, 7, 1, 0, 1, 6, 6, 6),
                        edge_plus_x(0, 0, 0, 1, 1, 1, 1, 6),
                        edge_plus_x(7, 0, 0, 1, 6, 1, 6, 6),
                    }),
              table("corner-wing-8", 8,
                    {
                        corner_wing(0, 0, 1, 0, 0, 1),
                        corner_wing(0, 0, 0, 1, 1, 0),
                        corner_wing(7, 0, -1, 0, 0, 1),
                        corner_wing(7, 0, 0, 1, -1, 0),
                        corner_wing(0, 7, 1, 0, 0, -1),
                        corner_wing(0, 7, 0, -1, 1, 0),
                        corner_wing(7, 7, -1, 0, 0, -1),
                        corner_wing(7, 7, 0, -1, -1, 0),
                    }),
              table("near-edge-segment-8", 8,
                    {
                        band(1, 1, 1, 0, 0, 1, 4, 2),
                        band(3, 1, 1, 0, 0, 1, 4, 2),
                        band(1, 6, 1, 0, 0, -1, 4, 2),
                        band(3, 6, 1, 0, 0, -1, 4, 2),
                        band(1, 1, 0, 1, 1, 0, 4, 2),
                        band(1, 3, 0, 1, 1, 0, 4, 2),
                        band(6, 1, 0, 1, -1, 0, 4, 2),
                        band(6, 3, 0, 1, -1, 0, 4, 2),
                    }),
              table("diagonal-corner-8", 8,
                    {
                        diagonal_corner(0, 0, 1, 1, 1, 0, 0, 1),
                        diagonal_corner(7, 0, -1, 1, -1, 0, 0, 1),
                        diagonal_corner(0, 7, 1, -1, 1, 0, 0, -1),
                        diagonal_corner(7, 7, -1, -1, -1, 0, 0, -1),
                    }),
          },
  };
}

PatternEvaluator::PatternEvaluator(PatternWeights weights, PatternFeatureSet feature_set)
    : feature_set_(std::move(feature_set)) {
  validate_pattern_evaluator_inputs(weights, feature_set_);

  const std::uint8_t phase_count = weights.phase_count();
  score_scale_ = weights.score_scale();
  phase_biases_.assign(weights.phase_biases().begin(), weights.phase_biases().end());
  for (std::uint8_t discs = 0; discs < phase_by_disc_count_.size(); ++discs) {
    phase_by_disc_count_[discs] = weights.phase_for_disc_count(discs);
  }

  const std::span<const PatternWeightTable> weight_tables = weights.tables();
  std::size_t total_weight_count = 0;
  for (const PatternWeightTable& table : weight_tables) {
    total_weight_count += table.weights.size();
  }
  flat_weights_.reserve(total_weight_count);
  table_phase_offsets_.resize(static_cast<std::size_t>(phase_count) * weight_tables.size());
  for (std::size_t table_index = 0; table_index < weight_tables.size(); ++table_index) {
    const PatternWeightTable& table = weight_tables[table_index];
    const std::size_t table_start = flat_weights_.size();
    const std::uint32_t table_size = pattern_size(table.pattern_length);
    flat_weights_.insert(flat_weights_.end(), table.weights.begin(), table.weights.end());
    for (std::uint8_t phase = 0; phase < phase_count; ++phase) {
      table_phase_offsets_[static_cast<std::size_t>(phase) * weight_tables.size() + table_index] =
          table_start + static_cast<std::size_t>(phase) * table_size;
    }
  }

  std::array<std::vector<SquareContribution>, board_core::kSquareCount> contributions_by_square;
  for (std::size_t table_index = 0; table_index < feature_set_.tables.size(); ++table_index) {
    const PatternFeatureTable& table = feature_set_.tables[table_index];
    for (const std::vector<board_core::Square>& squares : table.instances) {
      const std::uint32_t instance_id = static_cast<std::uint32_t>(instances_.size());
      instances_.push_back(InstanceDescriptor{
          .square_offset = flat_squares_.size(),
          .table_index = table_index,
          .pattern_size = pattern_size(table.pattern_length),
          .pattern_length = table.pattern_length,
      });

      std::uint32_t power = 1;
      for (const board_core::Square square : squares) {
        flat_squares_.push_back(square);
        flat_powers_of_three_.push_back(power);
        contributions_by_square[square.index].push_back(SquareContribution{
            .instance_id = instance_id,
            .power_of_three = power,
        });
        power *= 3;
      }
    }
  }

  instance_phase_offsets_.resize(static_cast<std::size_t>(phase_count) * instances_.size());
  for (std::uint8_t phase = 0; phase < phase_count; ++phase) {
    for (std::size_t instance_id = 0; instance_id < instances_.size(); ++instance_id) {
      instance_phase_offsets_[static_cast<std::size_t>(phase) * instances_.size() + instance_id] =
          table_phase_offsets_[static_cast<std::size_t>(phase) * weight_tables.size() +
                               instances_[instance_id].table_index];
    }
  }

  for (std::size_t square = 0; square < contributions_by_square.size(); ++square) {
    square_ranges_[square] = SquareContributionRange{
        .offset = square_contributions_.size(),
        .count = contributions_by_square[square].size(),
    };
    square_contributions_.insert(square_contributions_.end(),
                                 contributions_by_square[square].begin(),
                                 contributions_by_square[square].end());
  }
}

search::Score PatternEvaluator::finish_score(std::int64_t scaled_score) const noexcept {
  const std::int64_t scale = score_scale_;
  const std::int64_t rounded =
      scaled_score >= 0 ? (scaled_score + scale / 2) / scale : (scaled_score - scale / 2) / scale;
  return static_cast<search::Score>(
      std::clamp(rounded, -static_cast<std::int64_t>(board_core::kSquareCount),
                 static_cast<std::int64_t>(board_core::kSquareCount)));
}

search::Score PatternEvaluator::evaluate(const board_core::Position& position) const noexcept {
  const std::uint8_t discs =
      static_cast<std::uint8_t>(std::popcount(board_core::occupied(position)));
  const std::uint8_t phase = phase_by_disc_count_[discs];
  const std::size_t* phase_offsets =
      instance_phase_offsets_.data() + static_cast<std::size_t>(phase) * instances_.size();

  std::int64_t scaled_score = phase_biases_[phase];
  for (std::size_t instance_id = 0; instance_id < instances_.size(); ++instance_id) {
    const InstanceDescriptor& instance = instances_[instance_id];
    std::uint32_t pattern_index = 0;
    for (std::uint8_t cell = 0; cell < instance.pattern_length; ++cell) {
      const std::size_t offset = instance.square_offset + cell;
      const board_core::Bitboard mask = board_core::bit(flat_squares_[offset]);
      if ((position.player & mask) != 0) {
        pattern_index += flat_powers_of_three_[offset];
      } else if ((position.opponent & mask) != 0) {
        pattern_index += 2 * flat_powers_of_three_[offset];
      }
    }
    scaled_score += flat_weights_.data()[phase_offsets[instance_id] + pattern_index];
  }
  return finish_score(scaled_score);
}

search::Score
PatternEvaluator::evaluate_reference(const board_core::Position& position) const noexcept {
  const std::uint8_t discs =
      static_cast<std::uint8_t>(std::popcount(board_core::occupied(position)));
  const std::uint8_t phase = phase_by_disc_count_[discs];
  std::int64_t scaled_score = phase_biases_[phase];
  for (std::size_t table_index = 0; table_index < feature_set_.tables.size(); ++table_index) {
    const std::size_t base =
        table_phase_offsets_[static_cast<std::size_t>(phase) * feature_set_.tables.size() +
                             table_index];
    for (const std::vector<board_core::Square>& squares :
         feature_set_.tables[table_index].instances) {
      scaled_score += flat_weights_[base + ternary_pattern_index(position, squares)];
    }
  }
  return finish_score(scaled_score);
}

PatternEvaluator::IncrementalState
PatternEvaluator::make_incremental_state(const board_core::Position& position) const {
  return IncrementalState{this, position};
}

search::Score
PatternEvaluator::evaluate_indices(std::uint8_t occupied_count, board_core::Color side_to_move,
                                   const std::uint32_t* black_indices,
                                   const std::uint32_t* white_indices) const noexcept {
  const std::uint8_t phase = phase_by_disc_count_[occupied_count];
  const std::size_t* phase_offsets =
      instance_phase_offsets_.data() + static_cast<std::size_t>(phase) * instances_.size();
  const std::uint32_t* indices =
      side_to_move == board_core::Color::black ? black_indices : white_indices;

  std::int64_t scaled_score = phase_biases_[phase];
  for (std::size_t instance_id = 0; instance_id < instances_.size(); ++instance_id) {
    assert(indices[instance_id] < instances_[instance_id].pattern_size);
    scaled_score += flat_weights_.data()[phase_offsets[instance_id] + indices[instance_id]];
  }
  return finish_score(scaled_score);
}

PatternEvaluator::IncrementalState::IncrementalState(const PatternEvaluator* evaluator,
                                                     board_core::Position position)
    : evaluator_(evaluator), black_indices_(evaluator->instances_.size()),
      white_indices_(evaluator->instances_.size()),
      touched_instances_(evaluator->instances_.size()),
      touched_generation_(evaluator->instances_.size()),
      pending_black_delta_(evaluator->instances_.size()),
      pending_white_delta_(evaluator->instances_.size()),
      occupied_count_(static_cast<std::uint8_t>(std::popcount(board_core::occupied(position)))),
      side_to_move_(position.side_to_move) {
  const board_core::Bitboard black = board_core::black_discs(position);
  const board_core::Bitboard white = board_core::white_discs(position);
  for (std::size_t instance_id = 0; instance_id < evaluator_->instances_.size(); ++instance_id) {
    const InstanceDescriptor& instance = evaluator_->instances_[instance_id];
    std::uint32_t black_index = 0;
    std::uint32_t white_index = 0;
    for (std::uint8_t cell = 0; cell < instance.pattern_length; ++cell) {
      const std::size_t offset = instance.square_offset + cell;
      const board_core::Bitboard mask = board_core::bit(evaluator_->flat_squares_[offset]);
      const std::uint32_t power = evaluator_->flat_powers_of_three_[offset];
      if ((black & mask) != 0) {
        black_index += power;
        white_index += 2 * power;
      } else if ((white & mask) != 0) {
        black_index += 2 * power;
        white_index += power;
      }
    }
    black_indices_[instance_id] = black_index;
    white_indices_[instance_id] = white_index;
  }
}

search::Score PatternEvaluator::IncrementalState::evaluate() const noexcept {
  return evaluator_->evaluate_indices(occupied_count_, side_to_move_, black_indices_.data(),
                                      white_indices_.data());
}

std::uint32_t PatternEvaluator::IncrementalState::update_normal_move(board_core::MoveDelta delta,
                                                                     board_core::Color mover,
                                                                     int direction) noexcept {
  ++generation_;
  if (generation_ == 0) {
    std::fill(touched_generation_.begin(), touched_generation_.end(), 0);
    generation_ = 1;
  }
  std::size_t touched_count = 0;

  const auto record_square = [&](board_core::Square square, int black_digit_delta,
                                 int white_digit_delta) {
    const SquareContributionRange range = evaluator_->square_ranges_[square.index];
    const SquareContribution* contributions =
        evaluator_->square_contributions_.data() + range.offset;
    for (std::size_t offset = 0; offset < range.count; ++offset) {
      const SquareContribution contribution = contributions[offset];
      const std::size_t instance_id = contribution.instance_id;
      if (touched_generation_[instance_id] != generation_) {
        touched_generation_[instance_id] = generation_;
        touched_instances_[touched_count++] = contribution.instance_id;
        pending_black_delta_[instance_id] = 0;
        pending_white_delta_[instance_id] = 0;
      }
      pending_black_delta_[instance_id] +=
          direction * black_digit_delta * static_cast<std::int32_t>(contribution.power_of_three);
      pending_white_delta_[instance_id] +=
          direction * white_digit_delta * static_cast<std::int32_t>(contribution.power_of_three);
    }
  };

  if (mover == board_core::Color::black) {
    record_square(delta.move.square, 1, 2);
  } else {
    record_square(delta.move.square, 2, 1);
  }

  board_core::Bitboard flipped = delta.flipped;
  while (flipped != 0) {
    const auto square_index = static_cast<std::uint8_t>(std::countr_zero(flipped));
    record_square(board_core::Square{square_index}, mover == board_core::Color::black ? -1 : 1,
                  mover == board_core::Color::black ? 1 : -1);
    flipped &= flipped - 1;
  }

  for (std::size_t touched = 0; touched < touched_count; ++touched) {
    const std::size_t instance_id = touched_instances_[touched];
    const std::int64_t black_index =
        static_cast<std::int64_t>(black_indices_[instance_id]) + pending_black_delta_[instance_id];
    const std::int64_t white_index =
        static_cast<std::int64_t>(white_indices_[instance_id]) + pending_white_delta_[instance_id];
    assert(black_index >= 0);
    assert(white_index >= 0);
    assert(black_index < evaluator_->instances_[instance_id].pattern_size);
    assert(white_index < evaluator_->instances_[instance_id].pattern_size);
    black_indices_[instance_id] = static_cast<std::uint32_t>(black_index);
    white_indices_[instance_id] = static_cast<std::uint32_t>(white_index);
  }
  return static_cast<std::uint32_t>(touched_count);
}

void PatternEvaluator::IncrementalState::apply_move(board_core::MoveDelta delta) noexcept {
  if (delta.move.kind == board_core::MoveKind::pass) {
    side_to_move_ = board_core::opposite(side_to_move_);
    last_touched_instances_ = 0;
    return;
  }
  last_touched_instances_ = update_normal_move(delta, side_to_move_, 1);
  ++occupied_count_;
  side_to_move_ = board_core::opposite(side_to_move_);
}

void PatternEvaluator::IncrementalState::undo_move(board_core::MoveDelta delta) noexcept {
  side_to_move_ = board_core::opposite(side_to_move_);
  if (delta.move.kind == board_core::MoveKind::pass) {
    last_touched_instances_ = 0;
    return;
  }
  last_touched_instances_ = update_normal_move(delta, side_to_move_, -1);
  --occupied_count_;
}

} // namespace vibe_othello::evaluation
