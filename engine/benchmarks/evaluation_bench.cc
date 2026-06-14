#include "vibe_othello/board_core/serialization.h"
#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/pattern_evaluator.h"
#include "vibe_othello/evaluation/tiny_pattern_evaluator.h"
#include "vibe_othello/search/evaluator.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using vibe_othello::board_core::parse_position;
using vibe_othello::board_core::Position;
using vibe_othello::evaluation::pattern_size;
using vibe_othello::evaluation::PatternCell;
using vibe_othello::evaluation::PatternEvaluator;
using vibe_othello::evaluation::PatternWeights;
using vibe_othello::evaluation::PatternWeightTable;
using vibe_othello::evaluation::tiny_pattern_feature_set_fixture;
using vibe_othello::evaluation::TinyPatternEvaluator;
using vibe_othello::search::Evaluator;
using vibe_othello::search::Score;

constexpr int kDefaultIterations = 500'000;
constexpr std::uint8_t kFixturePhaseCount = 2;
constexpr int kFixtureLatePhaseDiscBoundary = 20;
constexpr Score kFixtureLatePhaseScale = 2;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

volatile std::uint64_t g_sink = 0;

struct Config {
  std::string corpus_path =
      std::string{VIBE_OTHELLO_SOURCE_DIR} + "/engine/fixtures/search/positions.tsv";
  int iterations = kDefaultIterations;
};

struct PositionCase {
  std::string id;
  Position position;
};

struct TimedResult {
  int iterations = 0;
  std::chrono::nanoseconds elapsed{};
  std::uint64_t checksum = 0;
};

void require_condition(bool condition, std::string_view message) {
  if (condition) {
    return;
  }

  std::cerr << "evaluation_bench: " << message << '\n';
  std::exit(1);
}

std::optional<int> parse_positive_int(std::string_view value) noexcept {
  if (value.empty()) {
    return std::nullopt;
  }

  int result = 0;
  const char* begin = value.data();
  const char* end = value.data() + value.size();
  const std::from_chars_result parsed = std::from_chars(begin, end, result);
  if (parsed.ec != std::errc{} || parsed.ptr != end || result <= 0) {
    return std::nullopt;
  }
  return result;
}

bool parse_argument_with_value(std::string_view argument, std::string_view name,
                               std::string_view* value) noexcept {
  if (!argument.starts_with(name) || argument.size() <= name.size() ||
      argument[name.size()] != '=') {
    return false;
  }

  *value = argument.substr(name.size() + 1);
  return true;
}

void print_usage(std::ostream& output, std::string_view program) {
  output << "Usage: " << program << " [--corpus PATH] [--iterations N]\n\n"
         << "Default corpus: " << VIBE_OTHELLO_SOURCE_DIR
         << "/engine/fixtures/search/positions.tsv\n"
         << "Default iterations per evaluator/position: " << kDefaultIterations << '\n';
}

std::optional<Config> parse_config(int argc, char** argv) {
  Config config{};
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    std::string_view value;

    if (argument == "--help" || argument == "-h") {
      print_usage(std::cout, argv[0]);
      return std::nullopt;
    }

    if (argument == "--corpus") {
      require_condition(index + 1 < argc, "--corpus requires a value");
      config.corpus_path = argv[++index];
      continue;
    }
    if (parse_argument_with_value(argument, "--corpus", &value)) {
      require_condition(!value.empty(), "--corpus requires a value");
      config.corpus_path = std::string(value);
      continue;
    }

    if (argument == "--iterations") {
      require_condition(index + 1 < argc, "--iterations requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--iterations", &value)) {
      value = {};
    }
    if (!value.empty()) {
      const std::optional<int> iterations = parse_positive_int(value);
      require_condition(iterations.has_value(), "invalid iteration count");
      config.iterations = *iterations;
      continue;
    }

    std::cerr << "evaluation_bench: unknown argument: " << argument << '\n';
    print_usage(std::cerr, argv[0]);
    std::exit(2);
  }

  return config;
}

std::vector<std::string_view> split_tabs(std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t begin = 0;
  while (begin <= line.size()) {
    const std::size_t tab = line.find('\t', begin);
    if (tab == std::string_view::npos) {
      fields.push_back(line.substr(begin));
      break;
    }
    fields.push_back(line.substr(begin, tab - begin));
    begin = tab + 1;
  }
  return fields;
}

std::vector<PositionCase> load_corpus(std::string_view path) {
  std::ifstream input{std::string(path)};
  require_condition(input.is_open(), "failed to open corpus");

  std::vector<PositionCase> positions;
  std::string line;
  bool saw_header = false;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }

    const std::vector<std::string_view> fields = split_tabs(line);
    if (!saw_header) {
      require_condition(fields.size() == 5 && fields[0] == "id" && fields[1] == "category" &&
                            fields[2] == "position" && fields[3] == "depths" &&
                            fields[4] == "notes",
                        "invalid corpus header");
      saw_header = true;
      continue;
    }

    require_condition(fields.size() == 5, "invalid corpus row");
    const std::optional<Position> position = parse_position(fields[2]);
    require_condition(position.has_value(), "invalid corpus position");
    positions.push_back(PositionCase{
        .id = std::string(fields[0]),
        .position = *position,
    });
  }

  require_condition(saw_header, "missing corpus header");
  require_condition(!positions.empty(), "empty corpus");
  return positions;
}

std::array<std::uint8_t, PatternWeights::kDiscCountEntries> fixture_phase_by_disc_count() {
  std::array<std::uint8_t, PatternWeights::kDiscCountEntries> phases{};
  for (std::uint8_t disc_count = 0; disc_count < phases.size(); ++disc_count) {
    phases[disc_count] = disc_count < kFixtureLatePhaseDiscBoundary ? 0 : 1;
  }
  return phases;
}

Score fixture_weight(std::uint32_t index, std::uint8_t length, std::uint8_t phase) noexcept {
  Score score = 0;
  for (std::uint8_t digit = 0; digit < length; ++digit) {
    const std::uint32_t cell = index % 3;
    index /= 3;
    const Score positional_weight = static_cast<Score>(digit + 1);
    if (cell == static_cast<std::uint32_t>(PatternCell::player)) {
      score += positional_weight;
    } else if (cell == static_cast<std::uint32_t>(PatternCell::opponent)) {
      score -= positional_weight;
    }
  }
  return phase == 0 ? score : static_cast<Score>(score * kFixtureLatePhaseScale);
}

PatternWeightTable make_fixture_table(std::string_view pattern_id, std::uint8_t pattern_length) {
  std::vector<Score> weights;
  weights.reserve(static_cast<std::size_t>(kFixturePhaseCount) * pattern_size(pattern_length));
  for (std::uint8_t phase = 0; phase < kFixturePhaseCount; ++phase) {
    for (std::uint32_t index = 0; index < pattern_size(pattern_length); ++index) {
      weights.push_back(fixture_weight(index, pattern_length, phase));
    }
  }
  return PatternWeightTable{
      .pattern_id = std::string{pattern_id},
      .pattern_length = pattern_length,
      .weights = std::move(weights),
  };
}

PatternWeights make_tiny_pattern_fixture_weights() {
  return PatternWeights{
      kFixturePhaseCount,
      fixture_phase_by_disc_count(),
      {0, 0},
      {
          make_fixture_table("edge-8", 8),
          make_fixture_table("corner-3x3", 9),
      },
  };
}

std::uint64_t mix_checksum(std::uint64_t checksum, std::uint64_t value) noexcept {
  checksum ^= value;
  checksum *= kFnvPrime;
  return checksum;
}

std::uint64_t position_checksum(Position position) noexcept {
  std::uint64_t checksum = kFnvOffset;
  checksum = mix_checksum(checksum, position.player);
  checksum = mix_checksum(checksum, position.opponent);
  checksum = mix_checksum(
      checksum, position.side_to_move == vibe_othello::board_core::Color::black ? 0ULL : 1ULL);
  return checksum;
}

TimedResult run_benchmark(const Evaluator& evaluator, const PositionCase& position_case,
                          int iterations) {
  std::int64_t score_sum = 0;
  const auto start = std::chrono::steady_clock::now();
  for (int iteration = 0; iteration < iterations; ++iteration) {
    score_sum += evaluator.evaluate(position_case.position);
  }
  const auto end = std::chrono::steady_clock::now();

  std::uint64_t checksum = position_checksum(position_case.position);
  checksum = mix_checksum(checksum, static_cast<std::uint64_t>(iterations));
  checksum = mix_checksum(checksum, static_cast<std::uint64_t>(score_sum));
  g_sink = checksum;

  return TimedResult{
      .iterations = iterations,
      .elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start),
      .checksum = checksum,
  };
}

void print_header() {
  std::cout << "evaluator\tposition_id\titerations\telapsed_ns\tns_per_eval\tchecksum\n";
}

void print_result(std::string_view evaluator_name, const PositionCase& position_case,
                  TimedResult result) {
  const double ns_per_eval =
      static_cast<double>(result.elapsed.count()) / static_cast<double>(result.iterations);
  std::cout << evaluator_name << '\t' << position_case.id << '\t' << result.iterations << '\t'
            << result.elapsed.count() << '\t' << std::fixed << std::setprecision(2) << ns_per_eval
            << "\t0x" << std::hex << result.checksum << std::dec << '\n';
}

} // namespace

int main(int argc, char** argv) {
  const std::optional<Config> config = parse_config(argc, argv);
  if (!config.has_value()) {
    return 0;
  }

  const std::vector<PositionCase> positions = load_corpus(config->corpus_path);
  const TinyPatternEvaluator tiny_evaluator{make_tiny_pattern_fixture_weights()};
  const PatternEvaluator pattern_evaluator{make_tiny_pattern_fixture_weights(),
                                           tiny_pattern_feature_set_fixture()};

  print_header();
  for (const PositionCase& position_case : positions) {
    print_result("TinyPatternEvaluator", position_case,
                 run_benchmark(tiny_evaluator, position_case, config->iterations));
    print_result("PatternEvaluator", position_case,
                 run_benchmark(pattern_evaluator, position_case, config->iterations));
  }

  return 0;
}
