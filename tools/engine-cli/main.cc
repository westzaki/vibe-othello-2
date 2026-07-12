#include "vibe_othello/board_core/board.h"
#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/pattern_artifact.h"
#include "vibe_othello/evaluation/pattern_evaluator.h"
#include "vibe_othello/evaluation/pattern_feature_set.h"
#include "vibe_othello/evaluation/pattern_weights.h"
#include "vibe_othello/evaluation/phase_aware_evaluator.h"
#include "vibe_othello/search/search.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using vibe_othello::board_core::Move;
using vibe_othello::board_core::MoveDelta;
using vibe_othello::board_core::MoveKind;
using vibe_othello::board_core::Position;
using vibe_othello::board_core::Square;
using vibe_othello::search::Depth;
using vibe_othello::search::Evaluator;
using vibe_othello::search::Score;

#ifndef VIBE_OTHELLO_SOURCE_DIR
#define VIBE_OTHELLO_SOURCE_DIR "."
#endif

class DiscDifferenceEvaluator final : public Evaluator {
public:
  Score evaluate(const Position& position) const noexcept override {
    return static_cast<Score>(std::popcount(position.player)) -
           static_cast<Score>(std::popcount(position.opponent));
  }
};

enum class EvaluatorChoice : std::uint8_t {
  default_artifact,
  static_eval,
  explicit_artifact,
  legacy_pattern_weights,
};

struct Args {
  std::string moves;
  std::string legacy_eval;
  std::string pattern_set = "tiny";
  std::string pattern_weights_path;
  std::filesystem::path eval_artifact_path;
  EvaluatorChoice evaluator = EvaluatorChoice::default_artifact;
  Depth depth = 0;
};

struct PatternRuntime {
  const vibe_othello::evaluation::PatternSet* pattern_set = nullptr;
  vibe_othello::evaluation::PatternFeatureSet feature_set;
};

std::optional<int> parse_int(std::string_view text) {
  int value = 0;
  const char* begin = text.data();
  const char* end = text.data() + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

std::optional<Square> parse_square(std::string_view text) {
  if (text.size() != 2) {
    return std::nullopt;
  }
  const char file_char = text[0];
  const char rank_char = text[1];
  if (file_char < 'a' || file_char > 'h' || rank_char < '1' || rank_char > '8') {
    return std::nullopt;
  }
  return vibe_othello::board_core::square_from_file_rank(file_char - 'a', rank_char - '1');
}

std::string format_move(Move move) {
  if (move.kind == MoveKind::pass) {
    return "pass";
  }
  const int file = vibe_othello::board_core::file_of(move.square);
  const int rank = vibe_othello::board_core::rank_of(move.square);
  std::string text;
  text.push_back(static_cast<char>('a' + file));
  text.push_back(static_cast<char>('1' + rank));
  return text;
}

std::string format_best_move(const std::optional<Move>& move) {
  if (!move.has_value()) {
    return "none";
  }
  return format_move(*move);
}

std::vector<std::string_view> split_words(std::string_view text) {
  std::vector<std::string_view> words;
  std::size_t pos = 0;
  while (pos < text.size()) {
    while (pos < text.size() && text[pos] == ' ') {
      ++pos;
    }
    const std::size_t begin = pos;
    while (pos < text.size() && text[pos] != ' ') {
      ++pos;
    }
    if (begin != pos) {
      words.push_back(text.substr(begin, pos - begin));
    }
  }
  return words;
}

bool replay_moves(std::string_view moves, Position* position, std::string* error) {
  *position = vibe_othello::board_core::initial_position();
  for (std::string_view token : split_words(moves)) {
    Move move = vibe_othello::board_core::make_pass();
    if (token == "pass") {
      if (vibe_othello::board_core::has_legal_move(*position)) {
        *error = "pass is illegal while legal moves exist";
        return false;
      }
    } else {
      const std::optional<Square> square = parse_square(token);
      if (!square.has_value()) {
        *error = "invalid move coordinate";
        return false;
      }
      move = vibe_othello::board_core::make_move(*square);
    }

    MoveDelta delta{};
    const bool ok = move.kind == MoveKind::pass
                        ? vibe_othello::board_core::apply_pass(position, &delta)
                        : vibe_othello::board_core::apply_move(position, move, &delta);
    if (!ok) {
      *error = "illegal move sequence";
      return false;
    }
  }
  return true;
}

std::optional<Args> parse_args(int argc, char** argv) {
  if (argc < 2 || std::string_view{argv[1]} != "bestmove") {
    std::cerr << "usage: vibe-othello-engine-cli bestmove --moves \"d3 c3\" --depth 4 "
                 "[--eval-mode static] [--eval-artifact MANIFEST] "
                 "[--eval disc-diff|pattern --pattern-set tiny|buro-lite|endgame-lite "
                 "--pattern-weights PATH]\n";
    return std::nullopt;
  }

  Args args;
  bool saw_depth = false;
  bool saw_eval_mode = false;
  bool static_mode_requested = false;
  for (int index = 2; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    if (arg == "--moves") {
      if (index + 1 >= argc) {
        std::cerr << "--moves requires a value\n";
        return std::nullopt;
      }
      args.moves = argv[++index];
    } else if (arg == "--eval") {
      if (index + 1 >= argc) {
        std::cerr << "--eval requires a value\n";
        return std::nullopt;
      }
      args.legacy_eval = argv[++index];
    } else if (arg == "--eval-mode") {
      if (index + 1 >= argc) {
        std::cerr << "--eval-mode requires a value\n";
        return std::nullopt;
      }
      const std::string_view mode{argv[++index]};
      if (mode == "static") {
        args.evaluator = EvaluatorChoice::static_eval;
        static_mode_requested = true;
      } else if (mode == "default" || mode == "artifact") {
        args.evaluator = EvaluatorChoice::default_artifact;
      } else {
        std::cerr << "--eval-mode must be static, default, or artifact\n";
        return std::nullopt;
      }
      saw_eval_mode = true;
    } else if (arg == "--eval-artifact") {
      if (index + 1 >= argc) {
        std::cerr << "--eval-artifact requires a value\n";
        return std::nullopt;
      }
      args.eval_artifact_path = argv[++index];
      args.evaluator = EvaluatorChoice::explicit_artifact;
    } else if (arg == "--pattern-set") {
      if (index + 1 >= argc) {
        std::cerr << "--pattern-set requires a value\n";
        return std::nullopt;
      }
      args.pattern_set = argv[++index];
    } else if (arg == "--pattern-weights") {
      if (index + 1 >= argc) {
        std::cerr << "--pattern-weights requires a value\n";
        return std::nullopt;
      }
      args.pattern_weights_path = argv[++index];
      args.evaluator = EvaluatorChoice::legacy_pattern_weights;
    } else if (arg == "--depth") {
      if (index + 1 >= argc) {
        std::cerr << "--depth requires a value\n";
        return std::nullopt;
      }
      const std::optional<int> depth = parse_int(argv[++index]);
      if (!depth.has_value() || *depth < 0) {
        std::cerr << "--depth must be a non-negative integer\n";
        return std::nullopt;
      }
      args.depth = static_cast<Depth>(*depth);
      saw_depth = true;
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      return std::nullopt;
    }
  }
  if (!saw_depth) {
    std::cerr << "--depth is required\n";
    return std::nullopt;
  }
  if (!args.legacy_eval.empty()) {
    if (args.legacy_eval == "disc-diff" || args.legacy_eval == "static") {
      args.evaluator = EvaluatorChoice::static_eval;
      static_mode_requested = true;
    } else if (args.legacy_eval == "pattern") {
      if (args.eval_artifact_path.empty() && args.pattern_weights_path.empty()) {
        std::cerr << "--eval pattern requires --eval-artifact or --pattern-weights\n";
        return std::nullopt;
      }
      if (!args.pattern_weights_path.empty()) {
        args.evaluator = EvaluatorChoice::legacy_pattern_weights;
      }
    } else {
      std::cerr << "--eval must be disc-diff, static, or pattern\n";
      return std::nullopt;
    }
  }
  if (saw_eval_mode && static_mode_requested && !args.eval_artifact_path.empty()) {
    std::cerr << "--eval-mode static cannot be combined with --eval-artifact\n";
    return std::nullopt;
  }
  if (static_mode_requested && !args.pattern_weights_path.empty()) {
    std::cerr << "--eval-mode static cannot be combined with --pattern-weights\n";
    return std::nullopt;
  }
  if (!args.eval_artifact_path.empty() && !args.pattern_weights_path.empty()) {
    std::cerr << "--eval-artifact cannot be combined with --pattern-weights\n";
    return std::nullopt;
  }
  return args;
}

std::optional<std::vector<std::uint8_t>> read_binary(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    std::cerr << "cannot read pattern weights: " << path << '\n';
    return std::nullopt;
  }

  input.seekg(0, std::ios::end);
  const std::streamoff size = input.tellg();
  if (size < 0) {
    std::cerr << "cannot determine pattern weights size: " << path << '\n';
    return std::nullopt;
  }
  input.seekg(0, std::ios::beg);

  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  if (!input) {
    std::cerr << "failed while reading pattern weights: " << path << '\n';
    return std::nullopt;
  }
  return bytes;
}

std::array<std::uint8_t, vibe_othello::evaluation::PatternWeights::kDiscCountEntries>
phase_by_disc_count_13() {
  std::array<std::uint8_t, vibe_othello::evaluation::PatternWeights::kDiscCountEntries> phases{};
  for (std::uint8_t disc_count = 0; disc_count < phases.size(); ++disc_count) {
    const int normalized_count = disc_count < 4 ? 0 : static_cast<int>(disc_count) - 4;
    const int phase = std::min(12, (normalized_count * 13) / 60);
    phases[disc_count] = static_cast<std::uint8_t>(phase);
  }
  return phases;
}

std::optional<PatternRuntime> select_pattern_runtime(std::string_view name) {
  namespace eval = vibe_othello::evaluation;

  if (name == "tiny" || name == "fixed-pattern-fixture-v1") {
    return PatternRuntime{
        .pattern_set = &eval::fixed_pattern_set_fixture(),
        .feature_set = eval::tiny_pattern_feature_set_fixture(),
    };
  }
  if (name == "buro-lite" || name == "pattern-v1-buro-lite") {
    return PatternRuntime{
        .pattern_set = &eval::buro_lite_pattern_set(),
        .feature_set = eval::buro_lite_pattern_feature_set(),
    };
  }
  if (name == "endgame-lite" || name == "pattern-v2-endgame-lite") {
    return PatternRuntime{
        .pattern_set = &eval::endgame_lite_pattern_set(),
        .feature_set = eval::endgame_lite_pattern_feature_set(),
    };
  }
  return std::nullopt;
}

std::optional<vibe_othello::evaluation::PatternWeights>
load_pattern_weights_for_runtime(const std::string& path,
                                 const vibe_othello::evaluation::PatternSet& pattern_set) {
  namespace eval = vibe_othello::evaluation;

  const std::optional<std::vector<std::uint8_t>> artifact = read_binary(path);
  if (!artifact.has_value()) {
    return std::nullopt;
  }

  const eval::PatternManifest manifest{
      .format_version = eval::kPatternWeightFormatVersion,
      .bit_order = eval::PatternBitOrder::a1_lsb,
      .score_unit = eval::PatternScoreUnit::disc_diff,
      .score_scale = 1,
      .phase_count = 13,
      .pattern_set_id = pattern_set.id,
      .patterns = pattern_set.patterns,
  };
  const eval::PatternWeightsLoadResult loaded = eval::load_pattern_weights(manifest, *artifact);
  if (!loaded.ok()) {
    std::cerr << "runtime loader rejected pattern weights\n";
    return std::nullopt;
  }

  const std::optional<eval::PatternWeights> weights =
      eval::make_pattern_weights(*loaded.weights, phase_by_disc_count_13());
  if (!weights.has_value()) {
    std::cerr << "loaded pattern weights could not be converted to runtime weights\n";
    return std::nullopt;
  }
  return weights;
}

} // namespace

int main(int argc, char** argv) {
  const std::optional<Args> args = parse_args(argc, argv);
  if (!args.has_value()) {
    return 2;
  }

  Position position{};
  std::string error;
  if (!replay_moves(args->moves, &position, &error)) {
    std::cerr << error << '\n';
    return 2;
  }

  DiscDifferenceEvaluator disc_difference_evaluator;
  std::optional<vibe_othello::evaluation::PatternEvaluator> pattern_evaluator;
  std::optional<vibe_othello::evaluation::PhaseAwareEvaluator> phase_aware_evaluator;
  const Evaluator* evaluator = &disc_difference_evaluator;
  if (args->evaluator == EvaluatorChoice::default_artifact ||
      args->evaluator == EvaluatorChoice::explicit_artifact) {
    namespace eval = vibe_othello::evaluation;
    const eval::PatternArtifactLoadResult result =
        args->evaluator == EvaluatorChoice::default_artifact
            ? eval::load_default_pattern_artifact(
                  eval::default_eval_root(std::filesystem::path{VIBE_OTHELLO_SOURCE_DIR}))
            : eval::load_pattern_artifact(args->eval_artifact_path);
    if (!result.ok()) {
      std::cerr << result.error << '\n';
      return 2;
    }
    eval::LoadedPatternArtifact artifact = std::move(*result.artifact);
    try {
      phase_aware_evaluator.emplace(std::move(artifact.weights), std::move(artifact.feature_set),
                                    std::move(artifact.trained_phases),
                                    artifact.fallback_additive_through_phase);
    } catch (const std::exception& error) {
      std::cerr << "phase-aware evaluator rejected artifact: " << error.what() << '\n';
      return 2;
    }
    evaluator = &*phase_aware_evaluator;
  } else if (args->evaluator == EvaluatorChoice::legacy_pattern_weights) {
    const std::optional<PatternRuntime> runtime = select_pattern_runtime(args->pattern_set);
    if (!runtime.has_value()) {
      std::cerr << "--pattern-set must be tiny, fixed-pattern-fixture-v1, buro-lite, "
                   "pattern-v1-buro-lite, endgame-lite, or pattern-v2-endgame-lite\n";
      return 2;
    }
    std::optional<vibe_othello::evaluation::PatternWeights> weights =
        load_pattern_weights_for_runtime(args->pattern_weights_path, *runtime->pattern_set);
    if (!weights.has_value()) {
      return 2;
    }
    try {
      pattern_evaluator.emplace(std::move(*weights), runtime->feature_set);
    } catch (const std::exception& error) {
      std::cerr << "pattern evaluator rejected weights: " << error.what() << '\n';
      return 2;
    }
    evaluator = &*pattern_evaluator;
  }

  const vibe_othello::search::SearchResult result =
      vibe_othello::search::search_fixed_depth(position, *evaluator, args->depth);

  std::cout << "bestmove " << format_best_move(result.best_move) << " score " << result.score
            << " depth " << result.completed_depth << '\n';
  return 0;
}
