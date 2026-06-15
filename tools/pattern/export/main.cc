#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/pattern_weights.h"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint16_t kTinyPhaseCount = 2;
constexpr std::string_view kTrainingNote =
    "tiny smoke phase-bias baseline; pattern tables are zero-filled";
constexpr std::uint32_t kCrc32Initial = 0xFFFF'FFFFU;
constexpr std::uint32_t kCrc32Polynomial = 0xEDB8'8320U;

struct Args {
  std::string summary_path;
  std::string weights_out_path;
  std::string manifest_out_path;
};

std::optional<Args> parse_args(int argc, char** argv) {
  Args args;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    if (arg == "--summary") {
      if (index + 1 >= argc) {
        std::cerr << "--summary requires a value\n";
        return std::nullopt;
      }
      args.summary_path = argv[++index];
    } else if (arg == "--weights-out") {
      if (index + 1 >= argc) {
        std::cerr << "--weights-out requires a value\n";
        return std::nullopt;
      }
      args.weights_out_path = argv[++index];
    } else if (arg == "--manifest-out") {
      if (index + 1 >= argc) {
        std::cerr << "--manifest-out requires a value\n";
        return std::nullopt;
      }
      args.manifest_out_path = argv[++index];
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      return std::nullopt;
    }
  }

  if (args.summary_path.empty() || args.weights_out_path.empty() ||
      args.manifest_out_path.empty()) {
    std::cerr << "usage: vibe-othello-pattern-export-smoke --summary PATH --weights-out PATH "
                 "--manifest-out PATH\n";
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

std::optional<std::int32_t> parse_i32(std::string_view text) noexcept {
  std::int32_t value = 0;
  const char* begin = text.data();
  const char* end = text.data() + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

bool load_summary(const std::string& path, std::map<std::string, std::string>* summary) {
  std::ifstream input(path);
  if (!input) {
    std::cerr << "cannot read trainer summary: " << path << '\n';
    return false;
  }

  std::string line;
  int line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    const std::string_view trimmed = trim_trailing_cr(line);
    if (trimmed.empty()) {
      continue;
    }

    const std::size_t separator = trimmed.find('=');
    if (separator == std::string_view::npos) {
      std::cerr << "line " << line_number << ": summary line is missing '='\n";
      return false;
    }
    const std::string key{trimmed.substr(0, separator)};
    const std::string value{trimmed.substr(separator + 1)};
    if (!summary->emplace(key, value).second) {
      std::cerr << "line " << line_number << ": duplicate summary key: " << key << '\n';
      return false;
    }
  }

  if (summary->empty()) {
    std::cerr << "trainer summary is empty\n";
    return false;
  }
  return true;
}

std::optional<std::vector<std::int32_t>>
phase_biases_from_summary(const std::map<std::string, std::string>& summary) {
  const auto model = summary.find("model");
  if (model == summary.end() || model->second != "phase-bias-baseline") {
    std::cerr << "summary model must be phase-bias-baseline\n";
    return std::nullopt;
  }

  std::vector<std::int32_t> phase_biases(kTinyPhaseCount, 0);
  for (std::uint16_t phase = 0; phase < kTinyPhaseCount; ++phase) {
    const std::string key = "phase_bias[" + std::to_string(phase) + "]";
    const auto value = summary.find(key);
    if (value == summary.end()) {
      continue;
    }

    const std::optional<std::int32_t> bias = parse_i32(value->second);
    if (!bias.has_value()) {
      std::cerr << key << " must be an integer for the tiny artifact smoke\n";
      return std::nullopt;
    }
    phase_biases[phase] = *bias;
  }
  return phase_biases;
}

void append_u16(std::vector<std::uint8_t>* bytes, std::uint16_t value) {
  bytes->push_back(static_cast<std::uint8_t>(value & 0xFFU));
  bytes->push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void append_u32(std::vector<std::uint8_t>* bytes, std::uint32_t value) {
  bytes->push_back(static_cast<std::uint8_t>(value & 0xFFU));
  bytes->push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  bytes->push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  bytes->push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

void append_i32(std::vector<std::uint8_t>* bytes, std::int32_t value) {
  append_u32(bytes, static_cast<std::uint32_t>(value));
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes) noexcept {
  std::uint32_t crc = kCrc32Initial;
  for (const std::uint8_t byte : bytes) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      const std::uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (kCrc32Polynomial & mask);
    }
  }
  return ~crc;
}

std::string hex_u32(std::uint32_t value) {
  std::ostringstream output;
  output << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(8) << value;
  return output.str();
}

std::optional<std::uint32_t> phase_stride(const vibe_othello::evaluation::PatternSet& pattern_set) {
  std::uint32_t stride = vibe_othello::evaluation::kPatternPhaseBiasWeightCount;
  for (const vibe_othello::evaluation::PatternDefinition& pattern : pattern_set.patterns) {
    const std::optional<std::uint32_t> table_size =
        vibe_othello::evaluation::checked_pattern_size(pattern.length);
    if (!table_size.has_value()) {
      return std::nullopt;
    }
    stride += *table_size;
  }
  return stride;
}

std::vector<std::uint8_t> make_artifact(const vibe_othello::evaluation::PatternSet& pattern_set,
                                        std::span<const std::int32_t> phase_biases,
                                        std::uint32_t* checksum) {
  const std::uint32_t stride = *phase_stride(pattern_set);
  const std::uint32_t weight_count = stride * static_cast<std::uint32_t>(phase_biases.size());

  std::vector<std::uint8_t> bytes{'V', 'O', 'P', 'W', 'G', 'T', '\0', '\0'};
  append_u16(&bytes, vibe_othello::evaluation::kPatternWeightFormatVersion);
  append_u16(&bytes, static_cast<std::uint16_t>(vibe_othello::evaluation::PatternBitOrder::a1_lsb));
  append_u16(&bytes,
             static_cast<std::uint16_t>(vibe_othello::evaluation::PatternScoreUnit::disc_diff));
  append_u16(&bytes, 1);
  append_u16(&bytes, static_cast<std::uint16_t>(phase_biases.size()));
  append_u16(&bytes, static_cast<std::uint16_t>(pattern_set.patterns.size()));
  append_u16(&bytes, static_cast<std::uint16_t>(pattern_set.id.size()));
  append_u16(&bytes, 0);
  append_u32(&bytes, weight_count);
  bytes.insert(bytes.end(), pattern_set.id.begin(), pattern_set.id.end());

  for (const std::int32_t bias : phase_biases) {
    append_i32(&bytes, bias);
    for (std::uint32_t index = 1; index < stride; ++index) {
      append_i32(&bytes, 0);
    }
  }

  *checksum = crc32(bytes);
  append_u32(&bytes, *checksum);
  return bytes;
}

bool write_bytes(const std::string& path, std::span<const std::uint8_t> bytes) {
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    std::cerr << "cannot write artifact weights: " << path << '\n';
    return false;
  }
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    std::cerr << "failed while writing artifact weights: " << path << '\n';
    return false;
  }
  return true;
}

bool write_manifest(const std::string& path,
                    const vibe_othello::evaluation::PatternSet& pattern_set,
                    std::span<const std::int32_t> phase_biases,
                    const std::filesystem::path& weights_path, std::uint32_t checksum,
                    std::size_t weights_size) {
  std::ofstream output(path);
  if (!output) {
    std::cerr << "cannot write artifact manifest: " << path << '\n';
    return false;
  }

  output << "{\n";
  output << "  \"artifact_id\": \"tiny-smoke-pattern-artifact-v1\",\n";
  output << "  \"format\": \"vibe-othello-pattern-eval\",\n";
  output << "  \"format_version\": " << vibe_othello::evaluation::kPatternWeightFormatVersion
         << ",\n";
  output << "  \"bit_order\": \"a1-lsb\",\n";
  output << "  \"score_unit\": \"disc-diff\",\n";
  output << "  \"score_scale\": 1,\n";
  output << "  \"phase_count\": " << phase_biases.size() << ",\n";
  output << "  \"pattern_set_id\": \"" << pattern_set.id << "\",\n";
  output << "  \"weights_file\": \"" << weights_path.filename().string() << "\",\n";
  output << "  \"weights_size_bytes\": " << weights_size << ",\n";
  output << "  \"weights_checksum\": \"" << hex_u32(checksum) << "\",\n";
  output << "  \"source\": \"tools/pattern/train tiny deterministic smoke summary\",\n";
  output << "  \"training_note\": \"" << kTrainingNote << "\",\n";
  output << "  \"phase_bias\": [";
  for (std::size_t index = 0; index < phase_biases.size(); ++index) {
    if (index != 0) {
      output << ", ";
    }
    output << phase_biases[index];
  }
  output << "],\n";
  output << "  \"patterns\": [\n";
  for (std::size_t index = 0; index < pattern_set.patterns.size(); ++index) {
    const vibe_othello::evaluation::PatternDefinition& pattern = pattern_set.patterns[index];
    output << "    {\"pattern_id\": \"" << pattern.id
           << "\", \"length\": " << static_cast<int>(pattern.length)
           << ", \"weights\": \"all-zero\"}";
    output << (index + 1 == pattern_set.patterns.size() ? "\n" : ",\n");
  }
  output << "  ]\n";
  output << "}\n";
  return static_cast<bool>(output);
}

} // namespace

int main(int argc, char** argv) {
  const std::optional<Args> args = parse_args(argc, argv);
  if (!args.has_value()) {
    return 2;
  }

  std::map<std::string, std::string> summary;
  if (!load_summary(args->summary_path, &summary)) {
    return 1;
  }

  const std::optional<std::vector<std::int32_t>> phase_biases = phase_biases_from_summary(summary);
  if (!phase_biases.has_value()) {
    return 1;
  }

  const vibe_othello::evaluation::PatternSet& pattern_set =
      vibe_othello::evaluation::fixed_pattern_set_fixture();
  if (!phase_stride(pattern_set).has_value()) {
    std::cerr << "fixed pattern set has unsupported table sizes\n";
    return 1;
  }

  std::uint32_t checksum = 0;
  const std::vector<std::uint8_t> artifact = make_artifact(pattern_set, *phase_biases, &checksum);
  if (!write_bytes(args->weights_out_path, artifact)) {
    return 1;
  }
  if (!write_manifest(args->manifest_out_path, pattern_set, *phase_biases, args->weights_out_path,
                      checksum, artifact.size())) {
    return 1;
  }

  std::cout << "format_version=" << vibe_othello::evaluation::kPatternWeightFormatVersion << '\n';
  std::cout << "bit_order=a1-lsb\n";
  std::cout << "score_unit=disc-diff\n";
  std::cout << "score_scale=1\n";
  std::cout << "phase_count=" << phase_biases->size() << '\n';
  std::cout << "pattern_set_id=" << pattern_set.id << '\n';
  std::cout << "weights_checksum=" << hex_u32(checksum) << '\n';
  std::cout << "weights_size_bytes=" << artifact.size() << '\n';
  std::cout << "source=tools/pattern/train tiny deterministic smoke summary\n";
  std::cout << "training_note=" << kTrainingNote << '\n';
  return 0;
}
