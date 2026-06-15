#include "pattern_common.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace vibe_othello::tools::pattern {

std::optional<IndexMode> parse_index_mode(std::string_view text) noexcept {
  if (text == "raw") {
    return IndexMode::raw;
  }
  if (text == "canonical") {
    return IndexMode::canonical;
  }
  return std::nullopt;
}

const evaluation::PatternSet& pattern_set_for_index_mode(IndexMode mode) noexcept {
  switch (mode) {
  case IndexMode::raw:
    return evaluation::fixed_pattern_set_fixture();
  case IndexMode::canonical:
    return evaluation::symmetry_aware_fixed_pattern_set_fixture();
  }
  return evaluation::fixed_pattern_set_fixture();
}

std::array<std::uint8_t, evaluation::PatternWeights::kDiscCountEntries>
tiny_fixture_phase_by_disc_count() {
  std::array<std::uint8_t, evaluation::PatternWeights::kDiscCountEntries> phases{};
  for (std::uint8_t disc_count = 0; disc_count < phases.size(); ++disc_count) {
    phases[disc_count] = disc_count < 20 ? 0 : 1;
  }
  return phases;
}

std::uint8_t tiny_fixture_phase(board_core::Position position) {
  static const evaluation::PatternWeights phase_selector{
      2,
      tiny_fixture_phase_by_disc_count(),
      {0, 0},
      {},
  };
  const int discs = std::popcount(board_core::occupied(position));
  return phase_selector.phase_for_disc_count(discs);
}

bool validate_feature_set(const evaluation::PatternFeatureSet& feature_set,
                          const evaluation::PatternSet& pattern_set) {
  namespace board = board_core;
  namespace eval = evaluation;

  if (feature_set.tables.size() != pattern_set.patterns.size()) {
    std::cerr << "pattern feature table count does not match runtime schema\n";
    return false;
  }

  for (std::size_t table_index = 0; table_index < feature_set.tables.size(); ++table_index) {
    const eval::PatternFeatureTable& table = feature_set.tables[table_index];
    const eval::PatternDefinition& definition = pattern_set.patterns[table_index];
    if (table.pattern_id != definition.id || table.pattern_length != definition.length) {
      std::cerr << "pattern feature table does not match runtime schema: " << table.pattern_id
                << '\n';
      return false;
    }

    if (table.instances.empty()) {
      std::cerr << "pattern feature table has no instances: " << table.pattern_id << '\n';
      return false;
    }

    for (const std::vector<board::Square>& instance : table.instances) {
      if (instance.size() != table.pattern_length) {
        std::cerr << "pattern feature instance length does not match schema: " << table.pattern_id
                  << '\n';
        return false;
      }

      std::array<bool, board::kSquareCount> seen{};
      for (const board::Square square : instance) {
        if (!board::is_valid(square)) {
          std::cerr << "pattern feature instance uses an invalid square: " << table.pattern_id
                    << '\n';
          return false;
        }
        if (seen[square.index]) {
          std::cerr << "pattern feature instance uses a duplicate square: " << table.pattern_id
                    << '\n';
          return false;
        }
        seen[square.index] = true;
      }
    }
  }

  return true;
}

std::optional<std::uint32_t> index_for_mode(board_core::Position position,
                                            std::span<const board_core::Square> squares,
                                            evaluation::PatternSymmetryPolicy symmetry_policy,
                                            IndexMode mode) noexcept {
  const std::uint32_t raw_index = evaluation::ternary_pattern_index(position, squares);
  switch (mode) {
  case IndexMode::raw:
    return raw_index;
  case IndexMode::canonical:
    return evaluation::canonical_ternary_pattern_index(
        raw_index, static_cast<std::uint8_t>(squares.size()), symmetry_policy);
  }
  return std::nullopt;
}

} // namespace vibe_othello::tools::pattern
