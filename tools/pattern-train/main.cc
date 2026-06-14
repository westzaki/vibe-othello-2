#include <charconv>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kExpectedHeader =
    "record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_id\tinstance\tternary_index";

struct Args {
  std::string dataset_path;
};

struct PhaseAggregate {
  std::int64_t label_sum = 0;
  int rows = 0;
};

struct Summary {
  int input_rows = 0;
  int train_rows = 0;
  int validation_rows = 0;
  int test_rows = 0;
  std::map<int, PhaseAggregate> phase_aggregates;
};

std::optional<Args> parse_args(int argc, char** argv) {
  Args args;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    if (arg == "--dataset") {
      if (index + 1 >= argc) {
        std::cerr << "--dataset requires a value\n";
        return std::nullopt;
      }
      args.dataset_path = argv[++index];
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      return std::nullopt;
    }
  }

  if (args.dataset_path.empty()) {
    std::cerr << "usage: vibe-othello-pattern-train-smoke --dataset PATH\n";
    return std::nullopt;
  }
  return args;
}

std::string_view trim_trailing_cr(std::string_view text) noexcept {
  if (!text.empty() && text.back() == '\r') {
    text.remove_suffix(1);
  }
  return text;
}

std::vector<std::string_view> split_tabs(std::string_view text) {
  std::vector<std::string_view> fields;
  std::size_t offset = 0;
  while (offset <= text.size()) {
    const std::size_t next = text.find('\t', offset);
    if (next == std::string_view::npos) {
      fields.push_back(text.substr(offset));
      break;
    }
    fields.push_back(text.substr(offset, next - offset));
    offset = next + 1;
  }
  return fields;
}

std::optional<std::int64_t> parse_i64(std::string_view text) noexcept {
  std::int64_t value = 0;
  const char* begin = text.data();
  const char* end = text.data() + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

bool require_non_negative_integer(std::string_view text, std::string_view field_name,
                                  int line_number) {
  const std::optional<std::int64_t> value = parse_i64(text);
  if (!value.has_value() || *value < 0) {
    std::cerr << "line " << line_number << ": " << field_name
              << " must be a non-negative integer\n";
    return false;
  }
  return true;
}

void count_split(std::string_view split, Summary* summary) {
  if (split == "train") {
    ++summary->train_rows;
  } else if (split == "validation") {
    ++summary->validation_rows;
  } else if (split == "test") {
    ++summary->test_rows;
  }
}

bool parse_dataset_row(std::string_view line, int line_number, Summary* summary) {
  const std::vector<std::string_view> fields = split_tabs(trim_trailing_cr(line));
  if (fields.size() != 8) {
    std::cerr << "line " << line_number << ": expected 8 TSV fields\n";
    return false;
  }

  if (fields[0].empty()) {
    std::cerr << "line " << line_number << ": record_id is empty\n";
    return false;
  }
  if (fields[5].empty()) {
    std::cerr << "line " << line_number << ": pattern_id is empty\n";
    return false;
  }

  if (!require_non_negative_integer(fields[1], "ply", line_number)) {
    return false;
  }

  const std::string_view split = fields[2];
  if (split != "train" && split != "validation" && split != "test") {
    std::cerr << "line " << line_number << ": split must be train, validation, or test\n";
    return false;
  }

  const std::optional<std::int64_t> label = parse_i64(fields[3]);
  if (!label.has_value()) {
    std::cerr << "line " << line_number << ": label_final_disc_diff must be an integer\n";
    return false;
  }

  const std::optional<std::int64_t> phase = parse_i64(fields[4]);
  if (!phase.has_value() || *phase < 0) {
    std::cerr << "line " << line_number << ": phase must be a non-negative integer\n";
    return false;
  }
  if (*phase > 255) {
    std::cerr << "line " << line_number << ": phase is out of range\n";
    return false;
  }

  if (!require_non_negative_integer(fields[6], "instance", line_number) ||
      !require_non_negative_integer(fields[7], "ternary_index", line_number)) {
    return false;
  }

  ++summary->input_rows;
  count_split(split, summary);
  if (split == "train") {
    PhaseAggregate& aggregate = summary->phase_aggregates[static_cast<int>(*phase)];
    aggregate.label_sum += *label;
    ++aggregate.rows;
  }

  return true;
}

bool load_dataset(const std::string& path, Summary* summary) {
  std::ifstream input(path);
  if (!input) {
    std::cerr << "cannot read dataset: " << path << '\n';
    return false;
  }

  std::string line;
  int line_number = 0;
  bool saw_header = false;
  while (std::getline(input, line)) {
    ++line_number;
    if (!saw_header) {
      saw_header = true;
      if (trim_trailing_cr(line) != kExpectedHeader) {
        std::cerr << "line " << line_number << ": unexpected TSV header\n";
        return false;
      }
      continue;
    }

    if (line.empty()) {
      continue;
    }
    if (!parse_dataset_row(line, line_number, summary)) {
      return false;
    }
  }

  if (!saw_header) {
    std::cerr << "dataset file is empty\n";
    return false;
  }
  if (summary->input_rows == 0) {
    std::cerr << "dataset has no rows\n";
    return false;
  }
  if (summary->train_rows == 0) {
    std::cerr << "dataset has no train rows\n";
    return false;
  }
  return true;
}

std::string average_label_string(const PhaseAggregate& aggregate) {
  std::int64_t numerator = aggregate.label_sum;
  std::int64_t denominator = aggregate.rows;
  const std::int64_t divisor = std::gcd(numerator, denominator);
  numerator /= divisor;
  denominator /= divisor;
  if (denominator == 1) {
    return std::to_string(numerator);
  }
  return std::to_string(numerator) + "/" + std::to_string(denominator);
}

std::uint64_t fnv1a64(std::string_view text) noexcept {
  std::uint64_t hash = 14695981039346656037ull;
  for (const char character : text) {
    hash ^= static_cast<unsigned char>(character);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string checksum_string(std::uint64_t checksum) {
  std::ostringstream output;
  output << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << checksum;
  return output.str();
}

std::string join_summary_lines(const std::vector<std::string>& lines) {
  std::string text;
  for (const std::string& line : lines) {
    text += line;
    text += '\n';
  }
  return text;
}

std::vector<std::string> summary_lines(const Summary& summary) {
  std::vector<std::string> lines{
      "model=phase-bias-baseline",
      "input_rows=" + std::to_string(summary.input_rows),
      "train_rows=" + std::to_string(summary.train_rows),
      "validation_rows=" + std::to_string(summary.validation_rows),
      "test_rows=" + std::to_string(summary.test_rows),
      "phases_seen=" + std::to_string(summary.phase_aggregates.size()),
  };

  for (const auto& [phase, aggregate] : summary.phase_aggregates) {
    lines.push_back("phase_bias[" + std::to_string(phase) + "]=" + average_label_string(aggregate));
  }

  return lines;
}

} // namespace

int main(int argc, char** argv) {
  const std::optional<Args> args = parse_args(argc, argv);
  if (!args.has_value()) {
    return 2;
  }

  Summary summary;
  if (!load_dataset(args->dataset_path, &summary)) {
    return 1;
  }

  std::vector<std::string> lines = summary_lines(summary);
  const std::string checksum_input = join_summary_lines(lines);
  lines.push_back("checksum=" + checksum_string(fnv1a64(checksum_input)));

  std::cout << join_summary_lines(lines);
  return 0;
}
