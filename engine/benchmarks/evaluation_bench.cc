#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/serialization.h"
#include "vibe_othello/evaluation/early_midgame_heuristic_evaluator.h"
#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/pattern_artifact.h"
#include "vibe_othello/evaluation/pattern_evaluator.h"
#include "vibe_othello/evaluation/phase_aware_evaluator.h"
#include "vibe_othello/search/evaluator.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
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
using vibe_othello::evaluation::default_eval_root;
using vibe_othello::evaluation::EarlyMidgameHeuristicEvaluator;
using vibe_othello::evaluation::load_default_pattern_artifact;
using vibe_othello::evaluation::LoadedPatternArtifact;
using vibe_othello::evaluation::pattern_size;
using vibe_othello::evaluation::PatternArtifactLoadResult;
using vibe_othello::evaluation::PatternCell;
using vibe_othello::evaluation::PatternEvaluator;
using vibe_othello::evaluation::PatternFeatureSet;
using vibe_othello::evaluation::PatternWeights;
using vibe_othello::evaluation::PatternWeightTable;
using vibe_othello::evaluation::PhaseAwareEvaluator;
using vibe_othello::evaluation::tiny_pattern_feature_set_fixture;
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

template <typename Function>
TimedResult run_benchmark_function(Function&& function, const PositionCase& position_case,
                                   int iterations) {
  std::int64_t score_sum = 0;
  const auto start = std::chrono::steady_clock::now();
  for (int iteration = 0; iteration < iterations; ++iteration) {
    score_sum += function();
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

TimedResult run_benchmark(const Evaluator& evaluator, const PositionCase& position_case,
                          int iterations) {
  return run_benchmark_function(
      [&evaluator, &position_case] { return evaluator.evaluate(position_case.position); },
      position_case, iterations);
}

std::vector<PositionCase> phase_positions(const PatternWeights& weights) {
  std::array<std::optional<Position>, 13> by_phase{};
  std::uint64_t random_state = UINT64_C(0xb6d31f805a4c279e);
  for (int game = 0; game < 32; ++game) {
    Position position = vibe_othello::board_core::initial_position();
    for (;;) {
      const int discs = std::popcount(vibe_othello::board_core::occupied(position));
      const std::uint8_t phase = weights.phase_for_disc_count(discs);
      if (phase < by_phase.size() && !by_phase[phase].has_value() &&
          vibe_othello::board_core::legal_moves(position) != 0) {
        by_phase[phase] = position;
      }

      vibe_othello::board_core::Bitboard legal = vibe_othello::board_core::legal_moves(position);
      vibe_othello::board_core::MoveDelta delta{};
      if (legal == 0) {
        if (!vibe_othello::board_core::has_legal_move(Position{
                .player = position.opponent,
                .opponent = position.player,
                .side_to_move = vibe_othello::board_core::opposite(position.side_to_move),
            })) {
          break;
        }
        require_condition(vibe_othello::board_core::apply_pass(&position, &delta),
                          "failed to build phase corpus pass");
        continue;
      }

      random_state ^= random_state << 13;
      random_state ^= random_state >> 7;
      random_state ^= random_state << 17;
      std::uint8_t choice = static_cast<std::uint8_t>(
          random_state % static_cast<std::uint64_t>(std::popcount(legal)));
      while (choice > 0) {
        legal &= legal - 1;
        --choice;
      }
      const vibe_othello::board_core::Move move = vibe_othello::board_core::make_move(
          vibe_othello::board_core::Square{static_cast<std::uint8_t>(std::countr_zero(legal))});
      require_condition(vibe_othello::board_core::apply_move(&position, move, &delta),
                        "failed to build phase corpus move");
    }
  }

  std::vector<PositionCase> result;
  result.reserve(by_phase.size());
  for (std::size_t phase = 0; phase < by_phase.size(); ++phase) {
    require_condition(by_phase[phase].has_value(), "failed to cover every production phase");
    result.push_back(PositionCase{
        .id = "phase-" + std::to_string(phase),
        .position = *by_phase[phase],
    });
  }
  return result;
}

std::pair<vibe_othello::board_core::MoveDelta, Position> benchmark_child(Position position) {
  const vibe_othello::board_core::Bitboard legal = vibe_othello::board_core::legal_moves(position);
  require_condition(legal != 0, "phase benchmark position has no legal move");
  const vibe_othello::board_core::Move move = vibe_othello::board_core::make_move(
      vibe_othello::board_core::Square{static_cast<std::uint8_t>(std::countr_zero(legal))});
  vibe_othello::board_core::MoveDelta delta{};
  require_condition(vibe_othello::board_core::apply_move(&position, move, &delta),
                    "failed to build benchmark child");
  return {delta, position};
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
  const PatternEvaluator pattern_evaluator{make_tiny_pattern_fixture_weights(),
                                           tiny_pattern_feature_set_fixture()};
  const EarlyMidgameHeuristicEvaluator heuristic_evaluator;
  const PhaseAwareEvaluator phase_aware_evaluator{make_tiny_pattern_fixture_weights(),
                                                  tiny_pattern_feature_set_fixture(),
                                                  std::vector<std::uint8_t>{1}};

  PatternArtifactLoadResult artifact_result =
      load_default_pattern_artifact(default_eval_root(VIBE_OTHELLO_SOURCE_DIR));
  require_condition(artifact_result.ok(), "failed to load committed default artifact");
  LoadedPatternArtifact artifact = std::move(*artifact_result.artifact);
  require_condition(artifact.pattern_set_id == "pattern-v2-endgame-lite",
                    "default artifact does not use pattern-v2-endgame-lite");
  const std::vector<PositionCase> production_positions = phase_positions(artifact.weights);

  PatternWeights stateless_weights = artifact.weights;
  PatternWeights residual_weights = artifact.weights;
  PatternFeatureSet stateless_features = artifact.feature_set;
  PatternFeatureSet residual_features = artifact.feature_set;
  const PatternEvaluator production_pattern{std::move(stateless_weights),
                                            std::move(stateless_features)};
  const PhaseAwareEvaluator production_phase_aware{
      std::move(artifact.weights), std::move(artifact.feature_set),
      std::move(artifact.trained_phases), artifact.fallback_additive_through_phase};
  const PhaseAwareEvaluator production_residual{
      std::move(residual_weights), std::move(residual_features),
      std::vector<std::uint8_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, 12};

  print_header();
  for (const PositionCase& position_case : positions) {
    print_result("PatternEvaluator", position_case,
                 run_benchmark(pattern_evaluator, position_case, config->iterations));
    print_result("EarlyMidgameHeuristicEvaluator", position_case,
                 run_benchmark(heuristic_evaluator, position_case, config->iterations));
    print_result("PhaseAwareEvaluator", position_case,
                 run_benchmark(phase_aware_evaluator, position_case, config->iterations));
  }

  for (std::size_t phase = 0; phase < production_positions.size(); ++phase) {
    const PositionCase& position_case = production_positions[phase];
    print_result("production_pattern_v2_stateless_reference", position_case,
                 run_benchmark_function(
                     [&production_pattern, &position_case] {
                       return production_pattern.evaluate_reference(position_case.position);
                     },
                     position_case, config->iterations));
    print_result("production_pattern_v2_flat_stateless", position_case,
                 run_benchmark(production_pattern, position_case, config->iterations));
    print_result(
        "production_pattern_v2_incremental_root_init", position_case,
        run_benchmark_function(
            [&production_pattern, &position_case] {
              return production_pattern.make_incremental_state(position_case.position).evaluate();
            },
            position_case, config->iterations));

    PatternEvaluator::IncrementalState pattern_state =
        production_pattern.make_incremental_state(position_case.position);
    print_result("production_pattern_v2_incremental_evaluate", position_case,
                 run_benchmark_function([&pattern_state] { return pattern_state.evaluate(); },
                                        position_case, config->iterations));
    const auto [delta, child] = benchmark_child(position_case.position);
    print_result("production_pattern_v2_incremental_make_evaluate_undo", position_case,
                 run_benchmark_function(
                     [&pattern_state, delta] {
                       pattern_state.apply_move(delta);
                       const Score score = pattern_state.evaluate();
                       pattern_state.undo_move(delta);
                       return score;
                     },
                     position_case, config->iterations));

    const std::string default_route = phase <= 9 ? "fallback" : "learned_replacement";
    print_result("production_phase_aware_stateless_" + default_route, position_case,
                 run_benchmark(production_phase_aware, position_case, config->iterations));
    PhaseAwareEvaluator::IncrementalState phase_state =
        production_phase_aware.make_incremental_state(position_case.position);
    print_result("production_phase_aware_incremental_" + default_route, position_case,
                 run_benchmark_function(
                     [&production_phase_aware, &phase_state, &position_case] {
                       return production_phase_aware.evaluate_incremental(phase_state,
                                                                          position_case.position);
                     },
                     position_case, config->iterations));

    PhaseAwareEvaluator::IncrementalState residual_state =
        production_residual.make_incremental_state(position_case.position);
    print_result("production_phase_aware_incremental_fallback_plus_residual", position_case,
                 run_benchmark_function(
                     [&production_residual, &residual_state, &position_case] {
                       return production_residual.evaluate_incremental(residual_state,
                                                                       position_case.position);
                     },
                     position_case, config->iterations));

    if (phase == 0 || phase == 5 || phase == 10) {
      PhaseAwareEvaluator::IncrementalState route_state =
          phase == 5 ? production_residual.make_incremental_state(position_case.position)
                     : production_phase_aware.make_incremental_state(position_case.position);
      const PhaseAwareEvaluator& route_evaluator =
          phase == 5 ? production_residual : production_phase_aware;
      const std::string route = phase == 0   ? "fallback_only"
                                : phase == 5 ? "fallback_plus_learned_residual"
                                             : "learned_replacement";
      print_result("production_phase_aware_incremental_make_evaluate_undo_" + route, position_case,
                   run_benchmark_function(
                       [&route_evaluator, &route_state, delta, &child] {
                         route_state.apply_move(delta);
                         const Score score =
                             route_evaluator.evaluate_incremental(route_state, child);
                         route_state.undo_move(delta);
                         return score;
                       },
                       position_case, config->iterations));
    }
  }

  return 0;
}
