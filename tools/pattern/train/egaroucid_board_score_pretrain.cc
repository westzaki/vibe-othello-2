#include "vibe_othello/board_core/board.h"
#include "vibe_othello/evaluation/early_midgame_heuristic_evaluator.h"
#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/pattern_artifact.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace board = vibe_othello::board_core;
namespace eval = vibe_othello::evaluation;

constexpr std::uint8_t kPhaseCount = 13;
constexpr std::size_t kMaxFeatureOccurrences = 128;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

struct Args {
  std::vector<std::filesystem::path> inputs;
  std::filesystem::path initial_artifact;
  std::filesystem::path weights_out;
  std::filesystem::path report_out;
  int epochs = 1;
  double learning_rate = 0.0005;
  std::vector<double> learning_rate_schedule;
  double phase_bias_learning_rate = 0.0;
  double huber_delta = 4.0;
  double gradient_clip = 1.0;
  double max_abs_weight = 16.0;
  std::uint64_t seed = 0;
  int validation_modulus = 20;
  int validation_residue = 0;
  std::optional<std::uint64_t> max_positions;
  std::optional<std::uint64_t> progress_every_positions;
  bool final_validation = true;
  bool quantize_between_epochs = false;
  std::array<bool, kPhaseCount> frozen_phases{};
};

struct SourceFile {
  std::filesystem::path path;
  std::uint64_t checksum = 0;
  std::uint64_t byte_count = 0;
};

struct FeatureOccurrence {
  std::uint16_t table = 0;
  std::uint32_t index = 0;
  std::uint8_t count = 0;
};

struct FeatureBuffer {
  std::array<FeatureOccurrence, kMaxFeatureOccurrences> values{};
  std::size_t size = 0;
  std::size_t occurrence_count = 0;
};

struct DenseTable {
  std::string pattern_id;
  std::uint8_t pattern_length = 0;
  std::uint32_t pattern_size = 0;
  std::vector<std::vector<board::Square>> instances;
  std::vector<double> weights;
};

struct Model {
  std::array<double, kPhaseCount> phase_bias{};
  std::array<std::uint8_t, eval::PatternWeights::kDiscCountEntries> phase_by_disc_count{};
  std::vector<DenseTable> tables;
  std::uint16_t source_score_scale = 1;
};

struct ErrorMetrics {
  std::uint64_t position_count = 0;
  std::uint64_t sign_correct_count = 0;
  double absolute_error_sum = 0.0;
  double squared_error_sum = 0.0;
  double huber_loss_sum = 0.0;
};

struct EpochMetrics {
  std::uint64_t input_position_count = 0;
  std::uint64_t train_position_count = 0;
  std::uint64_t validation_position_count = 0;
  std::uint64_t updated_weight_count = 0;
  std::uint64_t updated_phase_bias_count = 0;
  std::uint64_t clipped_weight_count = 0;
  ErrorMetrics train;
  ErrorMetrics validation;
  std::array<ErrorMetrics, kPhaseCount> train_by_phase{};
  std::array<ErrorMetrics, kPhaseCount> validation_by_phase{};
};

struct ParsedPosition {
  board::Position position{};
  int label = 0;
  std::uint64_t identity = 0;
};

std::string usage() {
  return R"(usage: vibe-othello-egaroucid-board-score-pretrain
  --input PATH [--input PATH ...]
  --initial-artifact MANIFEST
  --weights-out PATH
  --report-out PATH
  [--epochs N]                        # 0 evaluates the initial artifact only
  [--learning-rate FLOAT ...]          # one value or one per epoch
  [--quantize-between-epochs]          # reproduce chained runtime artifacts
  [--phase-bias-learning-rate FLOAT]  # defaults to 0 (preserve calibration)
  [--huber-delta FLOAT]
  [--gradient-clip FLOAT]
  [--freeze-phase PHASE ...]          # defaults to 10, 11, and 12
  [--train-all-phases]                 # overrides the default freeze
  [--validation-modulus N]            # 0 trains on every row; default 20
  [--validation-residue N]
  [--seed N]
  [--max-positions N]
  [--progress-every-positions N]
  [--max-abs-weight FLOAT]
  [--no-final-validation]

Inputs may be extracted .txt files or directories containing .txt files.
Each non-empty row must be '<64-character X/O/- board> <integer score>'.
X is the side to move and the score is a teacher value in final-disc-difference
units from X's perspective. Its generation procedure depends on occupied count:
4-15 uses Egaroucid 7.4.0 lv17 enumeration, evaluation, and negamax; 16-63
uses the terminal outcome of Egaroucid 7.5.1 lv17 self-play.
)";
}

template <typename Integer> std::optional<Integer> parse_integer(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }
  Integer value = 0;
  for (const char character : text) {
    if (character < '0' || character > '9') {
      return std::nullopt;
    }
    const Integer digit = static_cast<Integer>(character - '0');
    if (value > (std::numeric_limits<Integer>::max() - digit) / 10) {
      return std::nullopt;
    }
    value = value * 10 + digit;
  }
  return value;
}

std::optional<double> parse_finite_double(std::string_view text) {
  try {
    std::size_t consumed = 0;
    const double value = std::stod(std::string{text}, &consumed);
    if (consumed != text.size() || !std::isfinite(value)) {
      return std::nullopt;
    }
    return value;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<Args> parse_args(int argc, char** argv) {
  Args args;
  bool explicit_freeze = false;
  auto require_value = [&](int* index, std::string_view option) -> std::optional<std::string_view> {
    if (*index + 1 >= argc) {
      std::cerr << option << " requires a value\n";
      return std::nullopt;
    }
    ++*index;
    return std::string_view{argv[*index]};
  };
  for (int index = 1; index < argc; ++index) {
    const std::string_view option{argv[index]};
    if (option == "--help") {
      std::cout << usage();
      std::exit(0);
    }
    if (option == "--no-final-validation") {
      args.final_validation = false;
      continue;
    }
    if (option == "--quantize-between-epochs") {
      args.quantize_between_epochs = true;
      continue;
    }
    if (option == "--train-all-phases") {
      args.frozen_phases.fill(false);
      explicit_freeze = true;
      continue;
    }
    const std::optional<std::string_view> value = require_value(&index, option);
    if (!value.has_value()) {
      return std::nullopt;
    }
    if (option == "--input") {
      args.inputs.emplace_back(*value);
    } else if (option == "--initial-artifact") {
      args.initial_artifact = *value;
    } else if (option == "--weights-out") {
      args.weights_out = *value;
    } else if (option == "--report-out") {
      args.report_out = *value;
    } else if (option == "--epochs") {
      const auto parsed = parse_integer<int>(*value);
      if (!parsed.has_value() || *parsed < 0) {
        std::cerr << "--epochs must be non-negative\n";
        return std::nullopt;
      }
      args.epochs = *parsed;
    } else if (option == "--learning-rate") {
      const auto parsed = parse_finite_double(*value);
      if (!parsed.has_value() || *parsed <= 0.0) {
        std::cerr << "--learning-rate must be positive\n";
        return std::nullopt;
      }
      args.learning_rate = *parsed;
      args.learning_rate_schedule.push_back(*parsed);
    } else if (option == "--phase-bias-learning-rate") {
      const auto parsed = parse_finite_double(*value);
      if (!parsed.has_value() || *parsed < 0.0) {
        std::cerr << "--phase-bias-learning-rate must be non-negative\n";
        return std::nullopt;
      }
      args.phase_bias_learning_rate = *parsed;
    } else if (option == "--huber-delta") {
      const auto parsed = parse_finite_double(*value);
      if (!parsed.has_value() || *parsed <= 0.0) {
        std::cerr << "--huber-delta must be positive\n";
        return std::nullopt;
      }
      args.huber_delta = *parsed;
    } else if (option == "--gradient-clip") {
      const auto parsed = parse_finite_double(*value);
      if (!parsed.has_value() || *parsed <= 0.0) {
        std::cerr << "--gradient-clip must be positive\n";
        return std::nullopt;
      }
      args.gradient_clip = *parsed;
    } else if (option == "--freeze-phase") {
      const auto parsed = parse_integer<int>(*value);
      if (!parsed.has_value() || *parsed < 0 || *parsed >= kPhaseCount) {
        std::cerr << "--freeze-phase must be in [0, 12]\n";
        return std::nullopt;
      }
      if (!explicit_freeze) {
        args.frozen_phases.fill(false);
        explicit_freeze = true;
      }
      args.frozen_phases[static_cast<std::size_t>(*parsed)] = true;
    } else if (option == "--validation-modulus") {
      const auto parsed = parse_integer<int>(*value);
      if (!parsed.has_value() || *parsed < 0) {
        std::cerr << "--validation-modulus must be non-negative\n";
        return std::nullopt;
      }
      args.validation_modulus = *parsed;
    } else if (option == "--validation-residue") {
      const auto parsed = parse_integer<int>(*value);
      if (!parsed.has_value() || *parsed < 0) {
        std::cerr << "--validation-residue must be non-negative\n";
        return std::nullopt;
      }
      args.validation_residue = *parsed;
    } else if (option == "--seed") {
      const auto parsed = parse_integer<std::uint64_t>(*value);
      if (!parsed.has_value()) {
        std::cerr << "--seed must be a non-negative integer\n";
        return std::nullopt;
      }
      args.seed = *parsed;
    } else if (option == "--max-positions") {
      const auto parsed = parse_integer<std::uint64_t>(*value);
      if (!parsed.has_value() || *parsed == 0) {
        std::cerr << "--max-positions must be positive\n";
        return std::nullopt;
      }
      args.max_positions = *parsed;
    } else if (option == "--progress-every-positions") {
      const auto parsed = parse_integer<std::uint64_t>(*value);
      if (!parsed.has_value() || *parsed == 0) {
        std::cerr << "--progress-every-positions must be positive\n";
        return std::nullopt;
      }
      args.progress_every_positions = *parsed;
    } else if (option == "--max-abs-weight") {
      const auto parsed = parse_finite_double(*value);
      if (!parsed.has_value() || *parsed <= 0.0) {
        std::cerr << "--max-abs-weight must be positive\n";
        return std::nullopt;
      }
      args.max_abs_weight = *parsed;
    } else {
      std::cerr << "unknown option: " << option << '\n';
      return std::nullopt;
    }
  }
  if (args.inputs.empty() || args.initial_artifact.empty() || args.weights_out.empty() ||
      args.report_out.empty()) {
    std::cerr << "--input, --initial-artifact, --weights-out, and --report-out are required\n";
    return std::nullopt;
  }
  if (!explicit_freeze) {
    args.frozen_phases[10] = true;
    args.frozen_phases[11] = true;
    args.frozen_phases[12] = true;
  }
  if (args.validation_modulus > 0 && args.validation_residue >= args.validation_modulus) {
    std::cerr << "--validation-residue must be smaller than --validation-modulus\n";
    return std::nullopt;
  }
  if (args.learning_rate_schedule.empty()) {
    args.learning_rate_schedule.push_back(args.learning_rate);
  }
  if (args.learning_rate_schedule.size() != 1 &&
      args.learning_rate_schedule.size() != static_cast<std::size_t>(args.epochs)) {
    std::cerr << "--learning-rate must be supplied once or exactly once per epoch\n";
    return std::nullopt;
  }
  return args;
}

std::uint64_t fnv1a(std::string_view text, std::uint64_t hash = kFnvOffset) noexcept {
  for (const unsigned char byte : text) {
    hash ^= byte;
    hash *= kFnvPrime;
  }
  return hash;
}

std::uint64_t mix64(std::uint64_t value) noexcept {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

std::string hex64(std::uint64_t value) {
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << value;
  return output.str();
}

std::optional<std::uint64_t> checksum_file(const std::filesystem::path& path,
                                           std::uint64_t* byte_count = nullptr) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  std::uint64_t hash = kFnvOffset;
  std::uint64_t size = 0;
  std::array<char, 1 << 16> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize count = input.gcount();
    if (count <= 0) {
      continue;
    }
    hash = fnv1a(std::string_view{buffer.data(), static_cast<std::size_t>(count)}, hash);
    size += static_cast<std::uint64_t>(count);
  }
  if (!input.eof()) {
    return std::nullopt;
  }
  if (byte_count != nullptr) {
    *byte_count = size;
  }
  return hash;
}

std::optional<std::vector<SourceFile>> load_sources(const Args& args) {
  std::vector<std::filesystem::path> paths;
  for (const std::filesystem::path& input : args.inputs) {
    std::error_code error;
    if (std::filesystem::is_directory(input, error)) {
      for (std::filesystem::recursive_directory_iterator iterator(input, error), end;
           !error && iterator != end; iterator.increment(error)) {
        if (iterator->is_regular_file() && iterator->path().extension() == ".txt") {
          paths.push_back(iterator->path());
        }
      }
      if (error) {
        std::cerr << input << ": cannot scan input directory: " << error.message() << '\n';
        return std::nullopt;
      }
    } else if (std::filesystem::is_regular_file(input, error) && input.extension() == ".txt") {
      paths.push_back(input);
    } else {
      std::cerr << input << ": expected an extracted .txt file or directory\n";
      return std::nullopt;
    }
  }
  std::sort(paths.begin(), paths.end());
  paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
  if (paths.empty()) {
    std::cerr << "no .txt inputs found\n";
    return std::nullopt;
  }
  std::vector<SourceFile> sources;
  sources.reserve(paths.size());
  for (const std::filesystem::path& path : paths) {
    std::uint64_t byte_count = 0;
    const std::optional<std::uint64_t> checksum = checksum_file(path, &byte_count);
    if (!checksum.has_value()) {
      std::cerr << path << ": cannot read source\n";
      return std::nullopt;
    }
    sources.push_back(SourceFile{
        .path = path,
        .checksum = *checksum,
        .byte_count = byte_count,
    });
  }
  return sources;
}

std::optional<Model> load_model(const eval::LoadedPatternArtifact& artifact) {
  if (artifact.pattern_set_id != "pattern-v2-endgame-lite" ||
      artifact.weights.phase_count() != kPhaseCount ||
      artifact.weights.tables().size() != artifact.feature_set.tables.size()) {
    std::cerr << "initial artifact must use the 13-phase pattern-v2-endgame-lite contract\n";
    return std::nullopt;
  }
  if (artifact.weights.score_scale() != 100) {
    std::cerr << "initial artifact score_scale must be 100 for v2 trainer-weight export\n";
    return std::nullopt;
  }
  Model model;
  model.source_score_scale = artifact.weights.score_scale();
  for (std::size_t disc_count = 0; disc_count < model.phase_by_disc_count.size(); ++disc_count) {
    model.phase_by_disc_count[disc_count] =
        artifact.weights.phase_for_disc_count(static_cast<int>(disc_count));
  }
  for (std::uint8_t phase = 0; phase < kPhaseCount; ++phase) {
    model.phase_bias[phase] =
        static_cast<double>(artifact.weights.phase_bias(phase)) / model.source_score_scale;
  }
  for (std::size_t table_index = 0; table_index < artifact.weights.tables().size(); ++table_index) {
    const eval::PatternWeightTable& weights = artifact.weights.tables()[table_index];
    const eval::PatternFeatureTable& features = artifact.feature_set.tables[table_index];
    const std::optional<std::uint32_t> pattern_size =
        eval::checked_pattern_size(weights.pattern_length);
    if (!pattern_size.has_value() || weights.pattern_id != features.pattern_id ||
        weights.pattern_length != features.pattern_length ||
        weights.weights.size() !=
            static_cast<std::size_t>(kPhaseCount) * static_cast<std::size_t>(*pattern_size)) {
      std::cerr << "initial artifact pattern table mismatch at table " << table_index << '\n';
      return std::nullopt;
    }
    DenseTable table{
        .pattern_id = weights.pattern_id,
        .pattern_length = weights.pattern_length,
        .pattern_size = *pattern_size,
        .instances = features.instances,
    };
    table.weights.reserve(weights.weights.size());
    for (const vibe_othello::search::Score value : weights.weights) {
      table.weights.push_back(static_cast<double>(value) / model.source_score_scale);
    }
    model.tables.push_back(std::move(table));
  }
  return model;
}

std::uint8_t phase_for_position(const Model& model, board::Position position) noexcept {
  const std::size_t occupied = static_cast<std::size_t>(std::popcount(board::occupied(position)));
  return model.phase_by_disc_count[occupied];
}

FeatureBuffer features_for(board::Position position, const Model& model) {
  FeatureBuffer result;
  for (std::size_t table_index = 0; table_index < model.tables.size(); ++table_index) {
    const DenseTable& table = model.tables[table_index];
    for (const std::vector<board::Square>& instance : table.instances) {
      const std::uint32_t index = eval::ternary_pattern_index(position, instance);
      auto existing = std::find_if(result.values.begin(),
                                   result.values.begin() + static_cast<std::ptrdiff_t>(result.size),
                                   [&](const FeatureOccurrence& feature) {
                                     return feature.table == table_index && feature.index == index;
                                   });
      if (existing != result.values.begin() + static_cast<std::ptrdiff_t>(result.size)) {
        ++existing->count;
      } else {
        if (result.size >= result.values.size()) {
          throw std::runtime_error("pattern feature occurrence buffer is too small");
        }
        result.values[result.size++] = FeatureOccurrence{
            .table = static_cast<std::uint16_t>(table_index),
            .index = index,
            .count = 1,
        };
      }
      ++result.occurrence_count;
    }
  }
  return result;
}

double learned_value(const Model& model, std::uint8_t phase,
                     const FeatureBuffer& features) noexcept {
  double result = model.phase_bias[phase];
  for (std::size_t feature_index = 0; feature_index < features.size; ++feature_index) {
    const FeatureOccurrence& feature = features.values[feature_index];
    const DenseTable& table = model.tables[feature.table];
    const std::size_t offset = static_cast<std::size_t>(phase) * table.pattern_size + feature.index;
    result += table.weights[offset] * feature.count;
  }
  return result;
}

std::optional<ParsedPosition> parse_position(std::string_view line, std::string* error) {
  const std::size_t first_space = line.find_first_of(" \t");
  if (first_space == std::string_view::npos) {
    *error = "expected '<64-character-board> <integer-score>'";
    return std::nullopt;
  }
  const std::string_view board_text = line.substr(0, first_space);
  const std::size_t score_start = line.find_first_not_of(" \t", first_space);
  if (score_start == std::string_view::npos) {
    *error = "missing score";
    return std::nullopt;
  }
  const std::size_t score_end = line.find_first_of(" \t", score_start);
  const std::string_view score_text =
      line.substr(score_start, score_end == std::string_view::npos ? line.size() - score_start
                                                                   : score_end - score_start);
  if (score_end != std::string_view::npos &&
      line.find_first_not_of(" \t", score_end) != std::string_view::npos) {
    *error = "unexpected trailing field";
    return std::nullopt;
  }
  if (board_text.size() != 64) {
    *error = "board must have 64 characters";
    return std::nullopt;
  }
  int label = 0;
  try {
    std::size_t consumed = 0;
    label = std::stoi(std::string{score_text}, &consumed);
    if (consumed != score_text.size() || label < -64 || label > 64) {
      *error = "score must be an integer in [-64, 64]";
      return std::nullopt;
    }
  } catch (const std::exception&) {
    *error = "score must be an integer in [-64, 64]";
    return std::nullopt;
  }
  board::Position position{
      .player = 0,
      .opponent = 0,
      .side_to_move = board::Color::black,
  };
  for (std::size_t index = 0; index < board_text.size(); ++index) {
    const board::Bitboard bit = board::bit(board::square_from_index(static_cast<int>(index)));
    if (board_text[index] == 'X') {
      position.player |= bit;
    } else if (board_text[index] == 'O') {
      position.opponent |= bit;
    } else if (board_text[index] != '-') {
      *error = "board contains a character other than X, O, or -";
      return std::nullopt;
    }
  }
  const int occupied = std::popcount(board::occupied(position));
  if (!board::is_valid(position) || occupied < 4 || occupied > 64) {
    *error = "board must be non-overlapping with occupied count in [4, 64]";
    return std::nullopt;
  }
  return ParsedPosition{
      .position = position,
      .label = label,
      .identity = fnv1a(board_text),
  };
}

bool is_validation(std::uint64_t identity, const Args& args) noexcept {
  return args.validation_modulus > 0 &&
         static_cast<int>(mix64(identity ^ args.seed) %
                          static_cast<std::uint64_t>(args.validation_modulus)) ==
             args.validation_residue;
}

double huber_loss(double error, double delta) noexcept {
  const double absolute = std::abs(error);
  return absolute <= delta ? 0.5 * error * error : delta * (absolute - 0.5 * delta);
}

double huber_gradient(double error, double delta) noexcept {
  return std::clamp(error, -delta, delta);
}

void record_error(ErrorMetrics* metrics, double prediction, double label, double delta) noexcept {
  const double error = prediction - label;
  ++metrics->position_count;
  metrics->sign_correct_count += (prediction == 0.0 && label == 0.0) ||
                                         (prediction > 0.0 && label > 0.0) ||
                                         (prediction < 0.0 && label < 0.0)
                                     ? 1
                                     : 0;
  metrics->absolute_error_sum += std::abs(error);
  metrics->squared_error_sum += error * error;
  metrics->huber_loss_sum += huber_loss(error, delta);
}

void update_model(Model* model, std::uint8_t phase, const FeatureBuffer& features, double gradient,
                  const Args& args, EpochMetrics* metrics) {
  if (args.frozen_phases[phase]) {
    return;
  }
  const double clipped_gradient = std::clamp(gradient, -args.gradient_clip, args.gradient_clip);
  if (args.phase_bias_learning_rate > 0.0) {
    const double previous = model->phase_bias[phase];
    const double next = std::clamp(previous - args.phase_bias_learning_rate * clipped_gradient,
                                   -args.max_abs_weight, args.max_abs_weight);
    metrics->clipped_weight_count +=
        std::abs(next) == args.max_abs_weight && next != previous ? 1 : 0;
    model->phase_bias[phase] = next;
    ++metrics->updated_phase_bias_count;
  }
  if (features.occurrence_count == 0) {
    return;
  }
  for (std::size_t feature_index = 0; feature_index < features.size; ++feature_index) {
    const FeatureOccurrence& feature = features.values[feature_index];
    DenseTable& table = model->tables[feature.table];
    const std::size_t offset = static_cast<std::size_t>(phase) * table.pattern_size + feature.index;
    const double previous = table.weights[offset];
    const double contribution =
        static_cast<double>(feature.count) / static_cast<double>(features.occurrence_count);
    const double next = std::clamp(previous - args.learning_rate * clipped_gradient * contribution,
                                   -args.max_abs_weight, args.max_abs_weight);
    metrics->clipped_weight_count +=
        std::abs(next) == args.max_abs_weight && next != previous ? 1 : 0;
    table.weights[offset] = next;
    ++metrics->updated_weight_count;
  }
}

bool process_sources(const std::vector<SourceFile>& sources, const Args& args, Model* model,
                     std::optional<std::uint8_t> fallback_through_phase,
                     const eval::EarlyMidgameHeuristicEvaluator& fallback, EpochMetrics* metrics,
                     bool validation_only) {
  for (const SourceFile& source : sources) {
    std::ifstream input(source.path);
    if (!input) {
      std::cerr << source.path << ": cannot open source\n";
      return false;
    }
    std::string line;
    std::uint64_t line_number = 0;
    while (std::getline(input, line)) {
      ++line_number;
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (line.empty()) {
        continue;
      }
      if (args.max_positions.has_value() && metrics->input_position_count >= *args.max_positions) {
        return true;
      }
      std::string error;
      const std::optional<ParsedPosition> parsed = parse_position(line, &error);
      if (!parsed.has_value()) {
        std::cerr << source.path << ':' << line_number << ": " << error << '\n';
        return false;
      }
      ++metrics->input_position_count;
      const bool validation = is_validation(parsed->identity, args);
      if (validation_only && !validation) {
        continue;
      }
      const std::uint8_t phase = phase_for_position(*model, parsed->position);
      const FeatureBuffer features = features_for(parsed->position, *model);
      double prediction = learned_value(*model, phase, features);
      if (fallback_through_phase.has_value() && phase <= *fallback_through_phase) {
        prediction += fallback.evaluate(parsed->position);
      }
      if (validation) {
        ++metrics->validation_position_count;
        record_error(&metrics->validation, prediction, parsed->label, args.huber_delta);
        record_error(&metrics->validation_by_phase[phase], prediction, parsed->label,
                     args.huber_delta);
      } else if (!validation_only) {
        ++metrics->train_position_count;
        record_error(&metrics->train, prediction, parsed->label, args.huber_delta);
        record_error(&metrics->train_by_phase[phase], prediction, parsed->label, args.huber_delta);
        update_model(model, phase, features,
                     huber_gradient(prediction - parsed->label, args.huber_delta), args, metrics);
      }
      if (args.progress_every_positions.has_value() &&
          metrics->input_position_count % *args.progress_every_positions == 0) {
        std::cerr << "egaroucid_board_score_progress positions=" << metrics->input_position_count
                  << " train=" << metrics->train_position_count
                  << " validation=" << metrics->validation_position_count
                  << " updates=" << metrics->updated_weight_count << '\n';
      }
    }
    if (!input.eof()) {
      std::cerr << source.path << ": read failed\n";
      return false;
    }
  }
  return true;
}

std::string json_escape(std::string_view value) {
  std::ostringstream output;
  for (const char character : value) {
    switch (character) {
    case '\\':
      output << "\\\\";
      break;
    case '"':
      output << "\\\"";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
    default:
      output << character;
      break;
    }
  }
  return output.str();
}

double quantized_weight(double value, std::uint16_t scale) noexcept {
  const double scaled = value * scale;
  const double rounded = scaled >= 0.0 ? std::floor(scaled + 0.5) : std::ceil(scaled - 0.5);
  return rounded / scale;
}

void quantize_model(Model* model) {
  for (double& bias : model->phase_bias) {
    bias = quantized_weight(bias, model->source_score_scale);
  }
  for (DenseTable& table : model->tables) {
    for (double& weight : table.weights) {
      weight = quantized_weight(weight, model->source_score_scale);
    }
  }
}

bool write_weights(const std::filesystem::path& path, const Model& model) {
  std::ofstream output(path);
  if (!output) {
    std::cerr << path << ": cannot open weights output\n";
    return false;
  }
  output << std::setprecision(12);
  output << "{\n"
         << "  \"index_mode\": \"raw\",\n"
         << "  \"pattern_contract_digest\": \"fnv1a64:05941913c7fe9895\",\n"
         << "  \"pattern_set_id\": \"pattern-v2-endgame-lite\",\n"
         << "  \"pattern_weights\": [\n";
  bool first = true;
  for (std::uint8_t phase = 0; phase < kPhaseCount; ++phase) {
    for (const DenseTable& table : model.tables) {
      for (std::uint32_t index = 0; index < table.pattern_size; ++index) {
        const double value = quantized_weight(
            table.weights[static_cast<std::size_t>(phase) * table.pattern_size + index],
            model.source_score_scale);
        if (value == 0.0) {
          continue;
        }
        if (!first) {
          output << ",\n";
        }
        first = false;
        output << "    {\"pattern_id\": \"" << json_escape(table.pattern_id)
               << "\", \"phase\": " << static_cast<int>(phase) << ", \"ternary_index\": " << index
               << ", \"weight\": " << value << '}';
      }
    }
  }
  output << "\n  ],\n"
         << "  \"phase_bias\": {\n";
  for (std::uint8_t phase = 0; phase < kPhaseCount; ++phase) {
    output << "    \"" << static_cast<int>(phase)
           << "\": " << quantized_weight(model.phase_bias[phase], model.source_score_scale)
           << (phase + 1 == kPhaseCount ? "\n" : ",\n");
  }
  output << "  },\n"
         << "  \"phase_count\": 13,\n"
         << "  \"phase_mapping_id\": \"disc-count-13-v1\",\n"
         << "  \"score_unit\": \"disc-diff\",\n"
         << "  \"weights_schema_version\": \"pattern-eval-weights-v2\"\n"
         << "}\n";
  return static_cast<bool>(output);
}

void write_error_metrics(std::ostream& output, const ErrorMetrics& metrics, int indent) {
  const std::string spaces(static_cast<std::size_t>(indent), ' ');
  output << spaces << "{\n"
         << spaces << "  \"position_count\": " << metrics.position_count << ",\n"
         << spaces << "  \"mean_absolute_error\": ";
  if (metrics.position_count == 0) {
    output << "null";
  } else {
    output << metrics.absolute_error_sum / metrics.position_count;
  }
  output << ",\n" << spaces << "  \"root_mean_squared_error\": ";
  if (metrics.position_count == 0) {
    output << "null";
  } else {
    output << std::sqrt(metrics.squared_error_sum / metrics.position_count);
  }
  output << ",\n" << spaces << "  \"mean_huber_loss\": ";
  if (metrics.position_count == 0) {
    output << "null";
  } else {
    output << metrics.huber_loss_sum / metrics.position_count;
  }
  output << ",\n" << spaces << "  \"sign_accuracy\": ";
  if (metrics.position_count == 0) {
    output << "null";
  } else {
    output << static_cast<double>(metrics.sign_correct_count) / metrics.position_count;
  }
  output << "\n" << spaces << '}';
}

bool write_epoch(std::ostream& output, const EpochMetrics& metrics, int epoch, int indent) {
  const std::string spaces(static_cast<std::size_t>(indent), ' ');
  output << spaces << "{\n"
         << spaces << "  \"epoch\": " << epoch << ",\n"
         << spaces << "  \"input_position_count\": " << metrics.input_position_count << ",\n"
         << spaces << "  \"train_position_count\": " << metrics.train_position_count << ",\n"
         << spaces << "  \"validation_position_count\": " << metrics.validation_position_count
         << ",\n"
         << spaces << "  \"updated_weight_count\": " << metrics.updated_weight_count << ",\n"
         << spaces << "  \"updated_phase_bias_count\": " << metrics.updated_phase_bias_count
         << ",\n"
         << spaces << "  \"clipped_weight_count\": " << metrics.clipped_weight_count << ",\n"
         << spaces << "  \"train\": ";
  write_error_metrics(output, metrics.train, indent + 2);
  output << ",\n" << spaces << "  \"validation\": ";
  write_error_metrics(output, metrics.validation, indent + 2);
  output << ",\n" << spaces << "  \"train_by_phase\": {\n";
  for (std::uint8_t phase = 0; phase < kPhaseCount; ++phase) {
    output << spaces << "    \"" << static_cast<int>(phase) << "\": ";
    write_error_metrics(output, metrics.train_by_phase[phase], indent + 4);
    output << (phase + 1 == kPhaseCount ? "\n" : ",\n");
  }
  output << spaces << "  },\n" << spaces << "  \"validation_by_phase\": {\n";
  for (std::uint8_t phase = 0; phase < kPhaseCount; ++phase) {
    output << spaces << "    \"" << static_cast<int>(phase) << "\": ";
    write_error_metrics(output, metrics.validation_by_phase[phase], indent + 4);
    output << (phase + 1 == kPhaseCount ? "\n" : ",\n");
  }
  output << spaces << "  }\n" << spaces << '}';
  return static_cast<bool>(output);
}

bool write_report(const std::filesystem::path& path, const Args& args,
                  const std::vector<SourceFile>& sources,
                  const eval::LoadedPatternArtifact& artifact,
                  std::optional<std::uint8_t> fallback_through_phase,
                  const std::vector<EpochMetrics>& epochs,
                  const std::optional<EpochMetrics>& final_validation,
                  std::string_view weights_checksum) {
  std::ofstream output(path);
  if (!output) {
    std::cerr << path << ": cannot open report output\n";
    return false;
  }
  output << std::setprecision(12);
  output << "{\n"
         << "  \"schema_version\": 1,\n"
         << "  \"trainer_algorithm\": \"egaroucid-board-score-streaming-huber-v1\",\n"
         << "  \"label_kind\": \"teacher_value_disc_diff\",\n"
         << "  \"teacher_semantics\": \"side-to-move teacher value in final-disc-difference "
            "units; generation procedure depends on occupied count\",\n"
         << "  \"label_generation_by_occupied_range\": [\n"
         << "    {\"occupied_count_min\": 4, \"occupied_count_max\": 15, "
            "\"generation_kind\": \"enumerated_static_eval_negamax\", "
            "\"engine\": \"Egaroucid for Console 7.4.0 lv17\"},\n"
         << "    {\"occupied_count_min\": 16, \"occupied_count_max\": 63, "
            "\"generation_kind\": \"selfplay_terminal_outcome\", "
            "\"engine\": \"Egaroucid for Console 7.5.1 lv17\"}\n"
         << "  ],\n"
         << "  \"source_artifact_id\": \"" << json_escape(artifact.artifact_id) << "\",\n"
         << "  \"source_artifact_weights_checksum\": \"" << json_escape(artifact.weights_checksum)
         << "\",\n"
         << "  \"weights_checksum\": \"" << weights_checksum << "\",\n"
         << "  \"weights_checksum_algorithm\": \"fnv1a64\",\n"
         << "  \"weights_schema_version\": \"pattern-eval-weights-v2\",\n"
         << "  \"pattern_set_id\": \"pattern-v2-endgame-lite\",\n"
         << "  \"pattern_contract_digest\": \"fnv1a64:05941913c7fe9895\",\n"
         << "  \"index_mode\": \"raw\",\n"
         << "  \"phase_mapping_id\": \"disc-count-13-v1\",\n"
         << "  \"score_unit\": \"disc-diff\",\n"
         << "  \"epochs_requested\": " << args.epochs << ",\n"
         << "  \"learning_rate_schedule\": [";
  for (std::size_t index = 0; index < args.learning_rate_schedule.size(); ++index) {
    output << (index == 0 ? "" : ", ") << args.learning_rate_schedule[index];
  }
  output << "],\n"
         << "  \"quantize_between_epochs\": " << (args.quantize_between_epochs ? "true" : "false")
         << ",\n"
         << "  \"phase_bias_learning_rate\": " << args.phase_bias_learning_rate << ",\n"
         << "  \"huber_delta\": " << args.huber_delta << ",\n"
         << "  \"gradient_clip\": " << args.gradient_clip << ",\n"
         << "  \"deterministic_seed\": " << args.seed << ",\n"
         << "  \"validation_modulus\": " << args.validation_modulus << ",\n"
         << "  \"validation_residue\": " << args.validation_residue << ",\n"
         << "  \"fallback_additive_through_phase\": ";
  if (fallback_through_phase.has_value()) {
    output << static_cast<int>(*fallback_through_phase);
  } else {
    output << "null";
  }
  output << ",\n  \"frozen_phases\": [";
  bool first_phase = true;
  for (std::uint8_t phase = 0; phase < kPhaseCount; ++phase) {
    if (!args.frozen_phases[phase]) {
      continue;
    }
    output << (first_phase ? "" : ", ") << static_cast<int>(phase);
    first_phase = false;
  }
  output << "],\n  \"source_files\": [\n";
  for (std::size_t index = 0; index < sources.size(); ++index) {
    const SourceFile& source = sources[index];
    output << "    {\"name\": \"" << json_escape(source.path.filename().string())
           << "\", \"checksum\": \"" << hex64(source.checksum)
           << "\", \"byte_count\": " << source.byte_count << '}'
           << (index + 1 == sources.size() ? "\n" : ",\n");
  }
  output << "  ],\n  \"metrics_by_epoch\": [\n";
  for (std::size_t index = 0; index < epochs.size(); ++index) {
    write_epoch(output, epochs[index], static_cast<int>(index + 1), 4);
    output << (index + 1 == epochs.size() ? "\n" : ",\n");
  }
  output << "  ],\n  \"final_validation\": ";
  if (final_validation.has_value()) {
    write_epoch(output, *final_validation, args.epochs, 2);
  } else {
    output << "null";
  }
  output << "\n}\n";
  return static_cast<bool>(output);
}

} // namespace

int main(int argc, char** argv) {
  const std::optional<Args> parsed = parse_args(argc, argv);
  if (!parsed.has_value()) {
    std::cerr << usage();
    return 2;
  }
  const Args& args = *parsed;
  const std::optional<std::vector<SourceFile>> loaded_sources = load_sources(args);
  if (!loaded_sources.has_value()) {
    return 1;
  }
  const std::vector<SourceFile>& sources = *loaded_sources;
  eval::PatternArtifactLoadResult artifact_result =
      eval::load_pattern_artifact(args.initial_artifact);
  if (!artifact_result.ok()) {
    std::cerr << "cannot load initial artifact: " << artifact_result.error << '\n';
    return 1;
  }
  const eval::LoadedPatternArtifact& artifact = *artifact_result.artifact;
  std::optional<Model> loaded_model = load_model(artifact);
  if (!loaded_model.has_value()) {
    return 1;
  }
  Model model = std::move(*loaded_model);
  const std::optional<std::uint8_t> fallback_through_phase =
      artifact.fallback_additive_through_phase;
  eval::EarlyMidgameHeuristicEvaluator fallback;
  std::vector<EpochMetrics> metrics_by_epoch;
  for (int epoch = 0; epoch < args.epochs; ++epoch) {
    Args epoch_args = args;
    epoch_args.learning_rate = args.learning_rate_schedule.size() == 1
                                   ? args.learning_rate_schedule.front()
                                   : args.learning_rate_schedule[static_cast<std::size_t>(epoch)];
    EpochMetrics metrics;
    if (!process_sources(sources, epoch_args, &model, fallback_through_phase, fallback, &metrics,
                         false)) {
      return 1;
    }
    std::cerr << "egaroucid_board_score_epoch epoch=" << epoch + 1
              << " positions=" << metrics.input_position_count << " train_mae="
              << (metrics.train.position_count == 0
                      ? 0.0
                      : metrics.train.absolute_error_sum / metrics.train.position_count)
              << " validation_mae="
              << (metrics.validation.position_count == 0
                      ? 0.0
                      : metrics.validation.absolute_error_sum / metrics.validation.position_count)
              << '\n';
    metrics_by_epoch.push_back(metrics);
    if (args.quantize_between_epochs && epoch + 1 < args.epochs) {
      quantize_model(&model);
    }
  }
  std::optional<EpochMetrics> final_validation;
  if (args.final_validation && args.validation_modulus > 0) {
    final_validation.emplace();
    if (!process_sources(sources, args, &model, fallback_through_phase, fallback,
                         &*final_validation, true)) {
      return 1;
    }
    std::cerr << "egaroucid_board_score_final_validation positions="
              << final_validation->validation_position_count << " mae="
              << (final_validation->validation.position_count == 0
                      ? 0.0
                      : final_validation->validation.absolute_error_sum /
                            final_validation->validation.position_count)
              << '\n';
  }
  std::error_code error;
  for (const std::filesystem::path& output : std::array{args.weights_out, args.report_out}) {
    const std::filesystem::path parent = output.parent_path();
    if (parent.empty()) {
      continue;
    }
    std::filesystem::create_directories(parent, error);
    if (error) {
      std::cerr << parent << ": cannot create output directory: " << error.message() << '\n';
      return 1;
    }
  }
  if (!write_weights(args.weights_out, model)) {
    return 1;
  }
  const std::optional<std::uint64_t> weights_hash = checksum_file(args.weights_out);
  if (!weights_hash.has_value()) {
    std::cerr << args.weights_out << ": cannot checksum weights output\n";
    return 1;
  }
  const std::string weights_checksum = hex64(*weights_hash);
  if (!write_report(args.report_out, args, sources, artifact, fallback_through_phase,
                    metrics_by_epoch, final_validation, weights_checksum)) {
    return 1;
  }
  std::cout << "weights=" << args.weights_out << '\n'
            << "weights_checksum=" << weights_checksum << '\n'
            << "report=" << args.report_out << '\n';
  return 0;
}
