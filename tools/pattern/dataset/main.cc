#include "pattern_common.h"
#include "replay_records.h"
#include "vibe_othello/evaluation/pattern_feature_set.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

enum class SplitPolicy {
  record_hash,
  tiny_cycle,
};

struct Args {
  std::string records_path;
  std::string manifest_path;
  SplitPolicy split_policy = SplitPolicy::record_hash;
  vibe_othello::tools::pattern::IndexMode index_mode = vibe_othello::tools::pattern::IndexMode::raw;
};

std::optional<SplitPolicy> parse_split_policy(std::string_view text) {
  if (text == "record-hash") {
    return SplitPolicy::record_hash;
  }
  if (text == "tiny-cycle") {
    return SplitPolicy::tiny_cycle;
  }
  return std::nullopt;
}

std::string_view split_policy_name(SplitPolicy policy) noexcept {
  switch (policy) {
  case SplitPolicy::record_hash:
    return "record-hash";
  case SplitPolicy::tiny_cycle:
    return "tiny-cycle";
  }
  return "unknown";
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
    } else if (arg == "--split-policy") {
      if (index + 1 >= argc) {
        std::cerr << "--split-policy requires a value\n";
        return std::nullopt;
      }
      const std::optional<SplitPolicy> policy = parse_split_policy(argv[++index]);
      if (!policy.has_value()) {
        std::cerr << "--split-policy must be record-hash or tiny-cycle\n";
        return std::nullopt;
      }
      args.split_policy = *policy;
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
    std::cerr << "usage: vibe-othello-pattern-dataset-smoke --records PATH [--manifest PATH] "
                 "[--split-policy record-hash|tiny-cycle] [--index-mode raw|canonical]\n";
    return std::nullopt;
  }
  return args;
}

std::uint64_t fnv1a64(std::string_view text) noexcept {
  std::uint64_t hash = 14695981039346656037ull;
  for (const char character : text) {
    hash ^= static_cast<unsigned char>(character);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string_view split_for_hash(std::string_view record_id) noexcept {
  switch (fnv1a64(record_id) % 10) {
  case 0:
    return "validation";
  case 1:
    return "test";
  default:
    return "train";
  }
}

std::string_view split_for_tiny_cycle(std::size_t accepted_ply_ordinal) noexcept {
  switch (accepted_ply_ordinal % 3) {
  case 0:
    return "train";
  case 1:
    return "validation";
  default:
    return "test";
  }
}

std::string_view split_for(SplitPolicy policy, std::string_view record_id,
                           std::size_t accepted_ply_ordinal) noexcept {
  switch (policy) {
  case SplitPolicy::record_hash:
    return split_for_hash(record_id);
  case SplitPolicy::tiny_cycle:
    return split_for_tiny_cycle(accepted_ply_ordinal);
  }
  return "train";
}

struct EmitSummary {
  int rows = 0;
  int train_rows = 0;
  int validation_rows = 0;
  int test_rows = 0;
};

void count_split(std::string_view split, EmitSummary* summary) noexcept {
  if (split == "train") {
    ++summary->train_rows;
  } else if (split == "validation") {
    ++summary->validation_rows;
  } else if (split == "test") {
    ++summary->test_rows;
  }
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

  const eval::PatternSet& pattern_set = pattern::pattern_set_for_index_mode(args->index_mode);
  const eval::PatternFeatureSet feature_set = eval::tiny_pattern_feature_set_fixture();
  if (!pattern::validate_feature_set(feature_set, pattern_set)) {
    return 1;
  }

  importer::Summary summary;
  std::vector<importer::Record> records;
  if (!importer::load_records(args->records_path, &records, &summary)) {
    return 1;
  }

  std::cout << "record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_id\tinstance\tternary_"
               "index\n";

  EmitSummary emit_summary;
  std::size_t accepted_ply_ordinal = 0;
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

    if (!record.expect_accept || !result.accepted) {
      continue;
    }
    if (!record.expected_final_disc_diff.has_value()) {
      ++summary.expectation_failures;
      std::cerr << record.id << ": accepted record is missing label_final_disc_diff\n";
      continue;
    }

    for (std::size_t ply = 0; ply < result.positions.size(); ++ply) {
      const std::string_view split = split_for(args->split_policy, record.id, accepted_ply_ordinal);
      ++accepted_ply_ordinal;

      const vibe_othello::board_core::Position position = result.positions[ply];
      const std::uint8_t phase = pattern::tiny_fixture_phase(position);
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
          std::cout << record.id << '\t' << (ply + 1) << '\t' << split << '\t'
                    << *record.expected_final_disc_diff << '\t' << static_cast<int>(phase) << '\t'
                    << table.pattern_id << '\t' << instance << '\t' << *index << '\n';
          ++emit_summary.rows;
          count_split(split, &emit_summary);
        }
      }
    }
  }

  std::cerr << "summary total_records=" << summary.total_records
            << " accepted_records=" << summary.accepted_records
            << " rejected_records=" << summary.rejected_records
            << " emitted_rows=" << emit_summary.rows << " train_rows=" << emit_summary.train_rows
            << " validation_rows=" << emit_summary.validation_rows
            << " test_rows=" << emit_summary.test_rows
            << " split_policy=" << split_policy_name(args->split_policy)
            << " duplicate_policy=keep_all_input_order\n";

  if (emit_summary.rows == 0) {
    std::cerr << "no dataset rows emitted from expected-good records\n";
    return 1;
  }

  return summary.expectation_failures == 0 ? 0 : 1;
}
