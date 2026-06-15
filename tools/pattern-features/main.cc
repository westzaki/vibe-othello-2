#include "replay_records.h"
#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/pattern_feature_set.h"
#include "vibe_othello/evaluation/pattern_weights.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

enum class IndexMode {
  raw,
  canonical,
};

struct Args {
  std::string records_path;
  std::string manifest_path;
  IndexMode index_mode = IndexMode::raw;
};

std::optional<IndexMode> parse_index_mode(std::string_view text) noexcept {
  if (text == "raw") {
    return IndexMode::raw;
  }
  if (text == "canonical") {
    return IndexMode::canonical;
  }
  return std::nullopt;
}

std::optional<Args> parse_args(int argc, char** argv) {
  Args args;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    if (arg == "--records") {
      if (index + 1 >= argc) {
        std::cerr << "--records requires a value\n";
        return std::nullopt;
      }
      args.records_path = argv[++index];
    } else if (arg == "--manifest") {
      if (index + 1 >= argc) {
        std::cerr << "--manifest requires a value\n";
        return std::nullopt;
      }
      args.manifest_path = argv[++index];
    } else if (arg == "--index-mode") {
      if (index + 1 >= argc) {
        std::cerr << "--index-mode requires a value\n";
        return std::nullopt;
      }
      const std::optional<IndexMode> mode = parse_index_mode(argv[++index]);
      if (!mode.has_value()) {
        std::cerr << "--index-mode must be raw or canonical\n";
        return std::nullopt;
      }
      args.index_mode = *mode;
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      return std::nullopt;
    }
  }

  if (args.records_path.empty()) {
    std::cerr << "usage: vibe-othello-pattern-features-smoke --records PATH [--manifest PATH] "
                 "[--index-mode raw|canonical]\n";
    return std::nullopt;
  }
  return args;
}

const vibe_othello::evaluation::PatternSet& pattern_set_for_index_mode(IndexMode mode) noexcept {
  switch (mode) {
  case IndexMode::raw:
    return vibe_othello::evaluation::fixed_pattern_set_fixture();
  case IndexMode::canonical:
    return vibe_othello::evaluation::symmetry_aware_fixed_pattern_set_fixture();
  }
  return vibe_othello::evaluation::fixed_pattern_set_fixture();
}

std::array<std::uint8_t, vibe_othello::evaluation::PatternWeights::kDiscCountEntries>
tiny_fixture_phase_by_disc_count() {
  std::array<std::uint8_t, vibe_othello::evaluation::PatternWeights::kDiscCountEntries> phases{};
  for (std::uint8_t disc_count = 0; disc_count < phases.size(); ++disc_count) {
    phases[disc_count] = disc_count < 20 ? 0 : 1;
  }
  return phases;
}

std::uint8_t tiny_fixture_phase(vibe_othello::board_core::Position position) {
  static const vibe_othello::evaluation::PatternWeights phase_selector{
      2,
      tiny_fixture_phase_by_disc_count(),
      {0, 0},
      {},
  };
  const int discs = std::popcount(vibe_othello::board_core::occupied(position));
  return phase_selector.phase_for_disc_count(discs);
}

bool validate_feature_set(const vibe_othello::evaluation::PatternFeatureSet& feature_set,
                          const vibe_othello::evaluation::PatternSet& pattern_set) {
  namespace board = vibe_othello::board_core;
  namespace eval = vibe_othello::evaluation;

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

std::optional<std::uint32_t>
index_for_mode(vibe_othello::board_core::Position position,
               std::span<const vibe_othello::board_core::Square> squares,
               vibe_othello::evaluation::PatternSymmetryPolicy symmetry_policy,
               IndexMode mode) noexcept {
  namespace eval = vibe_othello::evaluation;

  const std::uint32_t raw_index = eval::ternary_pattern_index(position, squares);
  switch (mode) {
  case IndexMode::raw:
    return raw_index;
  case IndexMode::canonical:
    return eval::canonical_ternary_pattern_index(
        raw_index, static_cast<std::uint8_t>(squares.size()), symmetry_policy);
  }
  return std::nullopt;
}

} // namespace

int main(int argc, char** argv) {
  namespace importer = vibe_othello::tools::data_import;
  namespace eval = vibe_othello::evaluation;

  const std::optional<Args> args = parse_args(argc, argv);
  if (!args.has_value()) {
    return 2;
  }
  if (!importer::manifest_is_readable(args->manifest_path)) {
    return 1;
  }

  const eval::PatternSet& pattern_set = pattern_set_for_index_mode(args->index_mode);
  const eval::PatternFeatureSet feature_set = eval::tiny_pattern_feature_set_fixture();
  if (!validate_feature_set(feature_set, pattern_set)) {
    return 1;
  }

  importer::Summary summary;
  std::vector<importer::Record> records;
  if (!importer::load_records(args->records_path, &records, &summary)) {
    return 1;
  }

  std::cout << "record_id\tply\tpattern_id\tinstance\tphase\tternary_index\n";

  int emitted_features = 0;
  for (const importer::Record& record : records) {
    const importer::ReplayResult result = importer::replay_record(record, true);
    if (result.accepted) {
      ++summary.accepted_records;
    } else {
      ++summary.rejected_records;
      std::cerr << record.id << ": " << result.error << '\n';
    }

    if (result.accepted != record.expect_accept) {
      ++summary.expectation_failures;
      std::cerr << record.id << ": expected " << (record.expect_accept ? "accept" : "reject")
                << " but got " << (result.accepted ? "accept" : "reject");
      if (!result.error.empty()) {
        std::cerr << ": " << result.error;
      }
      std::cerr << '\n';
      continue;
    }

    if (!record.expect_accept) {
      continue;
    }

    for (std::size_t ply = 0; ply < result.positions.size(); ++ply) {
      const vibe_othello::board_core::Position position = result.positions[ply];
      const std::uint8_t phase = tiny_fixture_phase(position);
      for (std::size_t table_index = 0; table_index < feature_set.tables.size(); ++table_index) {
        const eval::PatternFeatureTable& table = feature_set.tables[table_index];
        const eval::PatternDefinition& definition = pattern_set.patterns[table_index];
        for (std::size_t instance = 0; instance < table.instances.size(); ++instance) {
          const std::optional<std::uint32_t> index = index_for_mode(
              position, table.instances[instance], definition.symmetry_policy, args->index_mode);
          if (!index.has_value()) {
            std::cerr << "failed to encode pattern index: " << table.pattern_id << '\n';
            return 1;
          }
          std::cout << record.id << '\t' << (ply + 1) << '\t' << table.pattern_id << '\t'
                    << instance << '\t' << static_cast<int>(phase) << '\t' << *index << '\n';
          ++emitted_features;
        }
      }
    }
  }

  if (emitted_features == 0) {
    std::cerr << "no pattern features emitted from expected-good records\n";
    return 1;
  }

  return summary.expectation_failures == 0 ? 0 : 1;
}
