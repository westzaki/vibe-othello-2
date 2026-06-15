#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/coordinates.h"
#include "vibe_othello/board_core/position.h"
#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/pattern_evaluator.h"
#include "vibe_othello/evaluation/pattern_feature_set.h"
#include "vibe_othello/evaluation/pattern_weights.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Args {
  std::string weights_path;
};

std::optional<Args> parse_args(int argc, char** argv) {
  Args args;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    if (arg == "--weights") {
      if (index + 1 >= argc) {
        std::cerr << "--weights requires a value\n";
        return std::nullopt;
      }
      args.weights_path = argv[++index];
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      return std::nullopt;
    }
  }

  if (args.weights_path.empty()) {
    std::cerr << "usage: vibe-othello-pattern-evaluation-roundtrip-smoke --weights PATH\n";
    return std::nullopt;
  }
  return args;
}

std::optional<std::vector<std::uint8_t>> read_binary(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    std::cerr << "cannot read artifact weights: " << path << '\n';
    return std::nullopt;
  }

  input.seekg(0, std::ios::end);
  const std::streamoff size = input.tellg();
  if (size < 0) {
    std::cerr << "cannot determine artifact weights size: " << path << '\n';
    return std::nullopt;
  }
  input.seekg(0, std::ios::beg);

  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  if (!input) {
    std::cerr << "failed while reading artifact weights: " << path << '\n';
    return std::nullopt;
  }
  return bytes;
}

std::array<std::uint8_t, vibe_othello::evaluation::PatternWeights::kDiscCountEntries>
tiny_phase_by_disc_count() {
  std::array<std::uint8_t, vibe_othello::evaluation::PatternWeights::kDiscCountEntries> phases{};
  for (std::uint8_t disc_count = 0; disc_count < phases.size(); ++disc_count) {
    phases[disc_count] = disc_count < 20 ? 0 : 1;
  }
  return phases;
}

vibe_othello::board_core::Position representative_position() {
  namespace board = vibe_othello::board_core;

  board::Position position = board::initial_position();
  board::MoveDelta delta;
  const std::array<board::Square, 3> moves{
      board::square_from_file_rank(3, 2),
      board::square_from_file_rank(2, 2),
      board::square_from_file_rank(2, 3),
  };

  for (const board::Square square : moves) {
    if (!board::apply_move(&position, board::make_move(square), &delta)) {
      std::cerr << "failed to apply representative smoke move\n";
      return board::Position{};
    }
  }
  return position;
}

} // namespace

int main(int argc, char** argv) {
  namespace eval = vibe_othello::evaluation;

  const std::optional<Args> args = parse_args(argc, argv);
  if (!args.has_value()) {
    return 2;
  }

  const std::optional<std::vector<std::uint8_t>> artifact = read_binary(args->weights_path);
  if (!artifact.has_value()) {
    return 1;
  }

  const eval::PatternSet& pattern_set = eval::fixed_pattern_set_fixture();
  const eval::PatternManifest manifest{
      .format_version = eval::kPatternWeightFormatVersion,
      .bit_order = eval::PatternBitOrder::a1_lsb,
      .score_unit = eval::PatternScoreUnit::disc_diff,
      .score_scale = 1,
      .phase_count = 2,
      .pattern_set_id = pattern_set.id,
      .patterns = pattern_set.patterns,
  };

  const eval::PatternWeightsLoadResult loaded = eval::load_pattern_weights(manifest, *artifact);
  if (!loaded.ok()) {
    std::cerr << "runtime loader rejected tiny artifact\n";
    return 1;
  }

  const std::optional<eval::PatternWeights> weights =
      eval::make_pattern_weights(*loaded.weights, tiny_phase_by_disc_count());
  if (!weights.has_value()) {
    std::cerr << "loaded artifact could not be converted to runtime PatternWeights\n";
    return 1;
  }

  const eval::PatternEvaluator evaluator{*weights, eval::tiny_pattern_feature_set_fixture()};
  const vibe_othello::board_core::Position position = representative_position();
  const vibe_othello::search::Score first = evaluator.evaluate(position);
  const vibe_othello::search::Score second = evaluator.evaluate(position);
  if (first != second) {
    std::cerr << "PatternEvaluator result is not deterministic\n";
    return 1;
  }
  if (first != 3) {
    std::cerr << "unexpected representative evaluation score: " << first << '\n';
    return 1;
  }

  std::cout << "loaded_pattern_set_id=" << loaded.weights->manifest.pattern_set_id << '\n';
  std::cout << "phase_stride=" << loaded.weights->phase_stride << '\n';
  std::cout << "phase_bias[0]=" << weights->phase_bias(0) << '\n';
  std::cout << "phase_bias[1]=" << weights->phase_bias(1) << '\n';
  std::cout << "representative_score=" << first << '\n';
  return 0;
}
