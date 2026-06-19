#include "index_mode.h"
#include "replay_records.h"
#include "schema_validation.h"
#include "smoke_fixture.h"
#include "vibe_othello/evaluation/pattern_feature_set.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Args {
  std::string records_path;
  std::string manifest_path;
  vibe_othello::tools::pattern::IndexMode index_mode = vibe_othello::tools::pattern::IndexMode::raw;
};

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
      const std::optional<vibe_othello::tools::pattern::IndexMode> mode =
          vibe_othello::tools::pattern::parse_index_mode(argv[++index]);
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

} // namespace

int main(int argc, char** argv) {
  namespace importer = vibe_othello::tools::data_import;
  namespace eval = vibe_othello::evaluation;
  namespace pattern = vibe_othello::tools::pattern;

  const std::optional<Args> args = parse_args(argc, argv);
  if (!args.has_value()) {
    return 2;
  }
  if (!importer::manifest_is_readable(args->manifest_path)) {
    return 1;
  }

  const eval::PatternSet& pattern_set =
      pattern::smoke::pattern_set_for_index_mode(args->index_mode);
  const eval::PatternFeatureSet feature_set = eval::tiny_pattern_feature_set_fixture();
  const pattern::FeatureSetValidationResult validation =
      pattern::validate_feature_set(feature_set, pattern_set);
  if (!validation.valid) {
    std::cerr << validation.error << '\n';
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
      const std::uint8_t phase = pattern::smoke::tiny_fixture_phase(position);
      for (std::size_t table_index = 0; table_index < feature_set.tables.size(); ++table_index) {
        const eval::PatternFeatureTable& table = feature_set.tables[table_index];
        const eval::PatternDefinition& definition = pattern_set.patterns[table_index];
        for (std::size_t instance = 0; instance < table.instances.size(); ++instance) {
          const std::optional<std::uint32_t> index = pattern::index_for_mode(
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
