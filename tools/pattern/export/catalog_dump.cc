#include "index_mode.h"
#include "pattern_set_options.h"
#include "schema_validation.h"
#include "vibe_othello/board_core/coordinates.h"
#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/pattern_feature_set.h"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr int kSchemaVersion = 1;
constexpr std::uint64_t kFnv1a64Offset = 14695981039346656037ull;
constexpr std::uint64_t kFnv1a64Prime = 1099511628211ull;

struct Args {
  vibe_othello::tools::pattern::IndexMode index_mode = vibe_othello::tools::pattern::IndexMode::raw;
  std::vector<std::string> pattern_sets;
};

struct PatternContract {
  std::string pattern_id;
  std::uint8_t length = 0;
  std::string symmetry_policy;
  std::vector<std::string> squares;
  std::vector<std::vector<std::string>> feature_instances;
  std::uint32_t table_size = 0;
};

struct PatternSetContract {
  std::string pattern_set_id;
  std::string index_mode;
  std::vector<PatternContract> patterns;
  std::uint64_t total_table_entries = 0;
  std::size_t feature_instance_count = 0;
  std::string pattern_contract_digest;
};

std::string_view index_mode_name(vibe_othello::tools::pattern::IndexMode mode) noexcept {
  switch (mode) {
  case vibe_othello::tools::pattern::IndexMode::raw:
    return "raw";
  case vibe_othello::tools::pattern::IndexMode::canonical:
    return "canonical";
  }
  return "unknown";
}

std::string_view
symmetry_policy_name(vibe_othello::evaluation::PatternSymmetryPolicy symmetry_policy) noexcept {
  switch (symmetry_policy) {
  case vibe_othello::evaluation::PatternSymmetryPolicy::none:
    return "none";
  case vibe_othello::evaluation::PatternSymmetryPolicy::reverse:
    return "reverse";
  case vibe_othello::evaluation::PatternSymmetryPolicy::square_d4:
    return "square-d4";
  }
  return "unknown";
}

std::optional<Args> parse_args(int argc, char** argv) {
  Args args;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    if (arg == "--pattern-set") {
      if (index + 1 >= argc) {
        std::cerr << "--pattern-set requires a value\n";
        return std::nullopt;
      }
      args.pattern_sets.emplace_back(argv[++index]);
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

  if (args.pattern_sets.empty()) {
    args.pattern_sets = {
        "fixed-pattern-fixture-v1",
        "pattern-v1-buro-lite",
        "pattern-v2-endgame-lite",
    };
  }
  return args;
}

std::string square_name(vibe_othello::board_core::Square square) {
  const int file = vibe_othello::board_core::file_of(square);
  const int rank = vibe_othello::board_core::rank_of(square);
  if (file < 0 || rank < 0) {
    return "invalid";
  }

  std::string name;
  name.push_back(static_cast<char>('a' + file));
  name.push_back(static_cast<char>('1' + rank));
  return name;
}

std::vector<std::string> square_names(std::span<const vibe_othello::board_core::Square> squares) {
  std::vector<std::string> names;
  names.reserve(squares.size());
  for (const vibe_othello::board_core::Square square : squares) {
    names.push_back(square_name(square));
  }
  return names;
}

std::string join_strings(std::span<const std::string> values) {
  std::string joined;
  bool first = true;
  for (const std::string& value : values) {
    if (!first) {
      joined.push_back(',');
    }
    first = false;
    joined += value;
  }
  return joined;
}

void append_digest_line(std::string* input, std::string_view line) {
  input->append(line);
  input->push_back('\n');
}

std::string digest_input(const PatternSetContract& contract) {
  std::string input;
  append_digest_line(&input, "pattern-contract-digest-v1");
  append_digest_line(&input, "pattern_set_id=" + contract.pattern_set_id);
  append_digest_line(&input, "index_mode=" + contract.index_mode);

  std::vector<std::string> ordered_pattern_ids;
  ordered_pattern_ids.reserve(contract.patterns.size());
  for (const PatternContract& pattern : contract.patterns) {
    ordered_pattern_ids.push_back(pattern.pattern_id);
  }
  append_digest_line(&input, "ordered_pattern_ids=" + join_strings(ordered_pattern_ids));
  append_digest_line(&input, "pattern_count=" + std::to_string(contract.patterns.size()));

  for (std::size_t pattern_index = 0; pattern_index < contract.patterns.size(); ++pattern_index) {
    const PatternContract& pattern = contract.patterns[pattern_index];
    const std::string prefix = "pattern[" + std::to_string(pattern_index) + "].";
    append_digest_line(&input, prefix + "id=" + pattern.pattern_id);
    append_digest_line(&input, prefix + "length=" + std::to_string(pattern.length));
    append_digest_line(&input, prefix + "symmetry_policy=" + pattern.symmetry_policy);
    append_digest_line(&input, prefix + "squares=" + join_strings(pattern.squares));
    append_digest_line(&input, prefix + "feature_instance_count=" +
                                   std::to_string(pattern.feature_instances.size()));
    for (std::size_t instance = 0; instance < pattern.feature_instances.size(); ++instance) {
      append_digest_line(&input,
                         prefix + "feature_instance[" + std::to_string(instance) +
                             "].squares=" + join_strings(pattern.feature_instances[instance]));
    }
    append_digest_line(&input, prefix + "table_size=" + std::to_string(pattern.table_size));
  }
  return input;
}

std::string fnv1a64_digest(std::string_view input) {
  std::uint64_t hash = kFnv1a64Offset;
  for (const char character : input) {
    hash ^= static_cast<unsigned char>(character);
    hash *= kFnv1a64Prime;
  }

  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::nouppercase << std::setfill('0') << std::setw(16)
         << hash;
  return output.str();
}

std::optional<PatternSetContract>
make_contract(std::string_view name, vibe_othello::tools::pattern::IndexMode index_mode) {
  namespace eval = vibe_othello::evaluation;
  namespace pattern = vibe_othello::tools::pattern;

  const std::optional<pattern::PatternSetOption> selected =
      pattern::select_pattern_set(name, index_mode);
  if (!selected.has_value() || selected->pattern_set == nullptr) {
    std::cerr << "--pattern-set must be " << pattern::pattern_set_option_names() << '\n';
    return std::nullopt;
  }

  const eval::PatternSet& pattern_set = *selected->pattern_set;
  const eval::PatternFeatureSet& feature_set = selected->feature_set;
  if (!eval::validate_pattern_set(pattern_set).ok()) {
    std::cerr << "runtime pattern set validation failed: " << pattern_set.id << '\n';
    return std::nullopt;
  }
  const pattern::FeatureSetValidationResult validation =
      pattern::validate_feature_set(feature_set, pattern_set);
  if (!validation.valid) {
    std::cerr << validation.error << '\n';
    return std::nullopt;
  }

  PatternSetContract contract{
      .pattern_set_id = pattern_set.id,
      .index_mode = std::string{index_mode_name(index_mode)},
  };
  contract.patterns.reserve(pattern_set.patterns.size());

  for (std::size_t pattern_index = 0; pattern_index < pattern_set.patterns.size();
       ++pattern_index) {
    const eval::PatternDefinition& definition = pattern_set.patterns[pattern_index];
    const eval::PatternFeatureTable& table = feature_set.tables[pattern_index];
    const std::optional<std::uint32_t> table_size = eval::checked_pattern_size(definition.length);
    if (!table_size.has_value()) {
      std::cerr << "invalid pattern size: " << definition.id << '\n';
      return std::nullopt;
    }

    PatternContract pattern_contract{
        .pattern_id = definition.id,
        .length = definition.length,
        .symmetry_policy = std::string{symmetry_policy_name(definition.symmetry_policy)},
        .squares = square_names(definition.squares),
        .table_size = *table_size,
    };
    pattern_contract.feature_instances.reserve(table.instances.size());
    for (const std::vector<vibe_othello::board_core::Square>& instance : table.instances) {
      pattern_contract.feature_instances.push_back(square_names(instance));
    }

    contract.total_table_entries += *table_size;
    contract.feature_instance_count += pattern_contract.feature_instances.size();
    contract.patterns.push_back(std::move(pattern_contract));
  }

  contract.pattern_contract_digest = fnv1a64_digest(digest_input(contract));
  return contract;
}

std::string json_escape(std::string_view text) {
  std::string escaped;
  for (const char character : text) {
    switch (character) {
    case '"':
      escaped += "\\\"";
      break;
    case '\\':
      escaped += "\\\\";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      escaped.push_back(character);
      break;
    }
  }
  return escaped;
}

void write_indent(std::ostream& output, int spaces) {
  for (int index = 0; index < spaces; ++index) {
    output.put(' ');
  }
}

void write_string_array(std::ostream& output, std::span<const std::string> values) {
  output << "[";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) {
      output << ", ";
    }
    output << '"' << json_escape(values[index]) << '"';
  }
  output << "]";
}

void write_instances(std::ostream& output, const std::vector<std::vector<std::string>>& instances,
                     int indent) {
  output << "[\n";
  for (std::size_t index = 0; index < instances.size(); ++index) {
    write_indent(output, indent + 2);
    output << "{ \"instance\": " << index << ", \"squares\": ";
    write_string_array(output, instances[index]);
    output << " }";
    if (index + 1 != instances.size()) {
      output << ",";
    }
    output << "\n";
  }
  write_indent(output, indent);
  output << "]";
}

void write_pattern(std::ostream& output, const PatternContract& pattern, int indent) {
  write_indent(output, indent);
  output << "{\n";
  write_indent(output, indent + 2);
  output << "\"pattern_id\": \"" << json_escape(pattern.pattern_id) << "\",\n";
  write_indent(output, indent + 2);
  output << "\"length\": " << static_cast<int>(pattern.length) << ",\n";
  write_indent(output, indent + 2);
  output << "\"symmetry_policy\": \"" << json_escape(pattern.symmetry_policy) << "\",\n";
  write_indent(output, indent + 2);
  output << "\"squares\": ";
  write_string_array(output, pattern.squares);
  output << ",\n";
  write_indent(output, indent + 2);
  output << "\"feature_instance_count\": " << pattern.feature_instances.size() << ",\n";
  write_indent(output, indent + 2);
  output << "\"feature_instances\": ";
  write_instances(output, pattern.feature_instances, indent + 2);
  output << ",\n";
  write_indent(output, indent + 2);
  output << "\"table_size\": " << pattern.table_size << "\n";
  write_indent(output, indent);
  output << "}";
}

void write_contract(std::ostream& output, const PatternSetContract& contract, int indent) {
  std::vector<std::string> ordered_pattern_ids;
  ordered_pattern_ids.reserve(contract.patterns.size());
  for (const PatternContract& pattern : contract.patterns) {
    ordered_pattern_ids.push_back(pattern.pattern_id);
  }

  write_indent(output, indent);
  output << "{\n";
  write_indent(output, indent + 2);
  output << "\"pattern_set_id\": \"" << json_escape(contract.pattern_set_id) << "\",\n";
  write_indent(output, indent + 2);
  output << "\"index_mode\": \"" << json_escape(contract.index_mode) << "\",\n";
  write_indent(output, indent + 2);
  output << "\"ordered_pattern_ids\": ";
  write_string_array(output, ordered_pattern_ids);
  output << ",\n";
  write_indent(output, indent + 2);
  output << "\"feature_instance_count\": " << contract.feature_instance_count << ",\n";
  write_indent(output, indent + 2);
  output << "\"total_table_entries\": " << contract.total_table_entries << ",\n";
  write_indent(output, indent + 2);
  output << "\"patterns\": [\n";
  for (std::size_t index = 0; index < contract.patterns.size(); ++index) {
    write_pattern(output, contract.patterns[index], indent + 4);
    if (index + 1 != contract.patterns.size()) {
      output << ",";
    }
    output << "\n";
  }
  write_indent(output, indent + 2);
  output << "],\n";
  write_indent(output, indent + 2);
  output << "\"pattern_contract_digest\": \"" << json_escape(contract.pattern_contract_digest)
         << "\"\n";
  write_indent(output, indent);
  output << "}";
}

} // namespace

int main(int argc, char** argv) {
  const std::optional<Args> args = parse_args(argc, argv);
  if (!args.has_value()) {
    return 2;
  }

  std::vector<PatternSetContract> contracts;
  contracts.reserve(args->pattern_sets.size());
  for (const std::string& pattern_set : args->pattern_sets) {
    std::optional<PatternSetContract> contract = make_contract(pattern_set, args->index_mode);
    if (!contract.has_value()) {
      return 1;
    }
    contracts.push_back(std::move(*contract));
  }

  std::cout << "{\n";
  std::cout << "  \"schema_version\": " << kSchemaVersion << ",\n";
  std::cout << "  \"pattern_sets\": [\n";
  for (std::size_t index = 0; index < contracts.size(); ++index) {
    write_contract(std::cout, contracts[index], 4);
    if (index + 1 != contracts.size()) {
      std::cout << ",";
    }
    std::cout << "\n";
  }
  std::cout << "  ]\n";
  std::cout << "}\n";
  return 0;
}
