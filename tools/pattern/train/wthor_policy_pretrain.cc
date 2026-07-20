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
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace board = vibe_othello::board_core;
namespace eval = vibe_othello::evaluation;

constexpr std::size_t kWthorHeaderSize = 16;
constexpr std::size_t kWthorRecordSize = 68;
constexpr std::uint8_t kPhaseCount = 13;
constexpr std::size_t kMaxFeatureOccurrences = 128;
constexpr std::size_t kMaxLegalMoves = 32;
constexpr std::size_t kMaxNegativeCount = 16;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

struct Args {
  std::vector<std::filesystem::path> inputs;
  std::filesystem::path initial_artifact;
  std::filesystem::path weights_out;
  std::filesystem::path report_out;
  int epochs = 1;
  double learning_rate = 0.0005;
  double temperature = 1.0;
  int negative_count = 2;
  std::uint64_t seed = 0;
  std::optional<std::uint64_t> max_games;
  std::optional<std::uint64_t> progress_every_games;
  int validation_modulus = 20;
  int validation_residue = 0;
  double max_abs_weight = 16.0;
  bool final_validation = true;
  std::array<bool, kPhaseCount> frozen_phases{};
};

struct SourceFile {
  std::filesystem::path path;
  std::vector<std::uint8_t> bytes;
  std::uint64_t checksum = 0;
  std::uint32_t game_count = 0;
  int game_year = 0;
};

struct GameRef {
  std::size_t source_index = 0;
  std::uint32_t record_index = 0;
  std::uint64_t identity = 0;
};

struct FeatureOccurrence {
  std::uint16_t table = 0;
  std::uint32_t index = 0;
  std::uint8_t count = 0;
};

struct FeatureBuffer {
  std::array<FeatureOccurrence, kMaxFeatureOccurrences> values{};
  std::size_t size = 0;
};

struct Child {
  board::Square move{};
  board::Position position{};
  FeatureBuffer features;
  double value = 0.0;
};

struct PhaseMetrics {
  std::uint64_t decision_count = 0;
  std::uint64_t pair_count = 0;
  std::uint64_t pair_correct = 0;
  double pair_loss_sum = 0.0;
  std::uint64_t top1_count = 0;
  std::uint64_t top1_correct = 0;
  double cross_entropy_sum = 0.0;
};

struct EpochMetrics {
  std::uint64_t game_count = 0;
  std::uint64_t incomplete_game_count = 0;
  std::uint64_t normal_move_count = 0;
  std::uint64_t implicit_pass_count = 0;
  std::uint64_t decision_count = 0;
  std::uint64_t forced_choice_count = 0;
  std::uint64_t train_decision_count = 0;
  std::uint64_t validation_decision_count = 0;
  std::uint64_t update_count = 0;
  std::uint64_t clipped_weight_count = 0;
  PhaseMetrics train;
  PhaseMetrics validation;
  std::array<PhaseMetrics, kPhaseCount> train_by_phase{};
  std::array<PhaseMetrics, kPhaseCount> validation_by_phase{};
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

struct PendingUpdate {
  std::uint16_t table = 0;
  std::uint32_t index = 0;
  double gradient = 0.0;
};

std::string usage() {
  return R"(usage: vibe-othello-wthor-policy-pretrain
  --input PATH [--input PATH ...]
  --initial-artifact MANIFEST
  --weights-out PATH
  --report-out PATH
  [--epochs N]                # 0 evaluates the initial artifact only
  [--learning-rate FLOAT]
  [--temperature FLOAT]
  [--negative-count N]        # 0 means every non-played legal move
  [--freeze-phase PHASE ...]  # defaults to 10, 11, and 12
  [--validation-modulus N]    # 0 disables holdout; default 20
  [--validation-residue N]
  [--seed N]
  [--max-games N]
  [--progress-every-games N]
  [--max-abs-weight FLOAT]
  [--no-final-validation]

Inputs may be extracted .wtb files or directories containing .wtb files.
Every non-forced recorded WTHOR decision is used. Training pairs the played
move with deterministic legal alternatives; validation scores every legal move.
)";
}

template <typename Integer> std::optional<Integer> parse_integer(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }
  Integer value = 0;
  for (char character : text) {
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
    } else if (option == "--temperature") {
      const auto parsed = parse_finite_double(*value);
      if (!parsed.has_value() || *parsed <= 0.0) {
        std::cerr << "--temperature must be positive\n";
        return std::nullopt;
      }
      args.temperature = *parsed;
    } else if (option == "--negative-count") {
      const auto parsed = parse_integer<int>(*value);
      if (!parsed.has_value() || *parsed < 0 || *parsed > static_cast<int>(kMaxNegativeCount)) {
        std::cerr << "--negative-count must be in [0, " << kMaxNegativeCount << "]\n";
        return std::nullopt;
      }
      args.negative_count = *parsed;
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
    } else if (option == "--max-games") {
      const auto parsed = parse_integer<std::uint64_t>(*value);
      if (!parsed.has_value() || *parsed == 0) {
        std::cerr << "--max-games must be positive\n";
        return std::nullopt;
      }
      args.max_games = *parsed;
    } else if (option == "--progress-every-games") {
      const auto parsed = parse_integer<std::uint64_t>(*value);
      if (!parsed.has_value() || *parsed == 0) {
        std::cerr << "--progress-every-games must be positive\n";
        return std::nullopt;
      }
      args.progress_every_games = *parsed;
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
  return args;
}

std::uint64_t fnv1a(std::span<const std::uint8_t> bytes, std::uint64_t hash = kFnvOffset) noexcept {
  for (std::uint8_t byte : bytes) {
    hash ^= byte;
    hash *= kFnvPrime;
  }
  return hash;
}

std::uint64_t fnv1a(std::string_view text, std::uint64_t hash = kFnvOffset) noexcept {
  return fnv1a(std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(text.data()),
                                             text.size()},
               hash);
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

std::optional<std::vector<std::uint8_t>> read_bytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  input.seekg(0, std::ios::end);
  const std::streamoff size = input.tellg();
  if (size < 0) {
    return std::nullopt;
  }
  input.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()),
                                    static_cast<std::streamsize>(bytes.size()))) {
    return std::nullopt;
  }
  return bytes;
}

std::uint16_t little_u16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(bytes[offset]) | static_cast<std::uint16_t>(bytes[offset + 1])
                                                         << 8U;
}

std::uint32_t little_u32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
  return static_cast<std::uint32_t>(bytes[offset]) |
         static_cast<std::uint32_t>(bytes[offset + 1]) << 8U |
         static_cast<std::uint32_t>(bytes[offset + 2]) << 16U |
         static_cast<std::uint32_t>(bytes[offset + 3]) << 24U;
}

std::optional<std::vector<SourceFile>> load_sources(const Args& args) {
  std::vector<std::filesystem::path> paths;
  for (const std::filesystem::path& input : args.inputs) {
    std::error_code error;
    if (std::filesystem::is_directory(input, error)) {
      for (std::filesystem::recursive_directory_iterator iterator(input, error), end;
           !error && iterator != end; iterator.increment(error)) {
        if (iterator->is_regular_file() && iterator->path().extension() == ".wtb") {
          paths.push_back(iterator->path());
        }
      }
      if (error) {
        std::cerr << input << ": cannot scan input directory: " << error.message() << '\n';
        return std::nullopt;
      }
    } else if (std::filesystem::is_regular_file(input, error) && input.extension() == ".wtb") {
      paths.push_back(input);
    } else {
      std::cerr << input << ": expected an extracted .wtb file or directory\n";
      return std::nullopt;
    }
  }
  std::sort(paths.begin(), paths.end());
  paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
  if (paths.empty()) {
    std::cerr << "no .wtb inputs found\n";
    return std::nullopt;
  }

  std::vector<SourceFile> sources;
  for (const std::filesystem::path& path : paths) {
    std::optional<std::vector<std::uint8_t>> bytes = read_bytes(path);
    if (!bytes.has_value() || bytes->size() < kWthorHeaderSize) {
      std::cerr << path << ": cannot read a complete WTHOR header\n";
      return std::nullopt;
    }
    const std::uint32_t game_count = little_u32(*bytes, 4);
    const std::uint16_t secondary_count = little_u16(*bytes, 8);
    const std::uint8_t board_size = (*bytes)[12];
    const std::uint8_t game_type = (*bytes)[13];
    const std::size_t expected =
        kWthorHeaderSize + static_cast<std::size_t>(game_count) * kWthorRecordSize;
    if (bytes->size() != expected || secondary_count != 0 || (board_size != 0 && board_size != 8) ||
        game_type != 0) {
      std::cerr << path << ": unsupported or malformed WTHOR game file\n";
      return std::nullopt;
    }
    const int game_year = static_cast<int>(little_u16(*bytes, 10));
    const std::uint64_t checksum = fnv1a(*bytes);
    sources.push_back(SourceFile{
        .path = path,
        .bytes = std::move(*bytes),
        .checksum = checksum,
        .game_count = game_count,
        .game_year = game_year,
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
        continue;
      }
      if (result.size >= result.values.size()) {
        throw std::runtime_error("pattern feature occurrence buffer is too small");
      }
      result.values[result.size++] = FeatureOccurrence{
          .table = static_cast<std::uint16_t>(table_index),
          .index = index,
          .count = 1,
      };
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

Child make_child(board::Position root, board::Square move, const Model& model,
                 const eval::EarlyMidgameHeuristicEvaluator& fallback,
                 std::optional<std::uint8_t> fallback_through_phase) {
  Child child{
      .move = move,
      .position = root,
  };
  board::MoveDelta delta{};
  if (!board::apply_move(&child.position, board::make_move(move), &delta)) {
    throw std::runtime_error("internal error: legal child move could not be applied");
  }
  const std::uint8_t phase = phase_for_position(model, child.position);
  child.features = features_for(child.position, model);
  child.value = learned_value(model, phase, child.features);
  if (fallback_through_phase.has_value() && phase <= *fallback_through_phase) {
    child.value += fallback.evaluate(child.position);
  }
  return child;
}

std::vector<board::Square> squares_in(board::Bitboard moves) {
  std::vector<board::Square> result;
  result.reserve(std::popcount(moves));
  while (moves != 0) {
    const int index = std::countr_zero(moves);
    result.push_back(board::square_from_index(index));
    moves &= moves - 1;
  }
  return result;
}

double logistic_loss(double better_value, double worse_value, double temperature,
                     double* better_gradient) noexcept {
  const double margin = (worse_value - better_value) / temperature;
  const double clamped = std::clamp(margin, -700.0, 700.0);
  *better_gradient = (1.0 / (1.0 + std::exp(clamped))) / temperature;
  if (margin >= 0.0) {
    return std::log1p(std::exp(-margin));
  }
  return -margin + std::log1p(std::exp(margin));
}

void add_feature_gradients(std::vector<PendingUpdate>* updates, const FeatureBuffer& features,
                           double gradient) {
  for (std::size_t feature_index = 0; feature_index < features.size; ++feature_index) {
    const FeatureOccurrence& feature = features.values[feature_index];
    const double contribution = gradient * feature.count;
    auto existing = std::find_if(updates->begin(), updates->end(), [&](const PendingUpdate& item) {
      return item.table == feature.table && item.index == feature.index;
    });
    if (existing == updates->end()) {
      updates->push_back(PendingUpdate{
          .table = feature.table,
          .index = feature.index,
          .gradient = contribution,
      });
    } else {
      existing->gradient += contribution;
    }
  }
}

void record_pair(PhaseMetrics* metrics, double chosen_value, double alternative_value,
                 double loss) noexcept {
  ++metrics->pair_count;
  metrics->pair_correct += chosen_value <= alternative_value ? 1 : 0;
  metrics->pair_loss_sum += loss;
}

void record_validation_top1(PhaseMetrics* metrics, std::span<const Child> children,
                            board::Square chosen_move, double temperature) {
  const auto predicted =
      std::min_element(children.begin(), children.end(), [](const Child& left, const Child& right) {
        if (left.value != right.value) {
          return left.value < right.value;
        }
        return left.move.index < right.move.index;
      });
  ++metrics->top1_count;
  metrics->top1_correct += predicted->move == chosen_move ? 1 : 0;

  double minimum_value = children.front().value;
  for (const Child& child : children) {
    minimum_value = std::min(minimum_value, child.value);
  }
  double normalizer = 0.0;
  double chosen_exponential = 0.0;
  for (const Child& child : children) {
    const double exponential = std::exp(-(child.value - minimum_value) / temperature);
    normalizer += exponential;
    if (child.move == chosen_move) {
      chosen_exponential = exponential;
    }
  }
  metrics->cross_entropy_sum +=
      -std::log(std::max(chosen_exponential / normalizer, std::numeric_limits<double>::min()));
}

std::uint64_t record_identity(std::span<const std::uint8_t> record) noexcept {
  // Transcript identity keeps the same played game in one split even when
  // archives repeat it with different player or tournament metadata.
  return fnv1a(record.subspan(8, 60));
}

bool is_validation(std::uint64_t identity, const Args& args) noexcept {
  return args.validation_modulus > 0 &&
         mix64(identity ^ 0x76616c6964617465ULL) %
                 static_cast<std::uint64_t>(args.validation_modulus) ==
             static_cast<std::uint64_t>(args.validation_residue);
}

std::optional<board::Square> square_from_wthor(std::uint8_t encoded) noexcept {
  const int file = encoded % 10 - 1;
  const int rank = encoded / 10 - 1;
  if (file < 0 || file >= board::kBoardSize || rank < 0 || rank >= board::kBoardSize) {
    return std::nullopt;
  }
  return board::square_from_file_rank(file, rank);
}

bool train_decision(board::Position root, board::Square chosen_move, std::uint64_t identity,
                    std::uint64_t decision_index, int epoch, bool validation, const Args& args,
                    std::optional<std::uint8_t> fallback_through_phase,
                    const eval::EarlyMidgameHeuristicEvaluator& fallback, Model* model,
                    EpochMetrics* epoch_metrics) {
  const std::vector<board::Square> legal = squares_in(board::legal_moves(root));
  if (std::find(legal.begin(), legal.end(), chosen_move) == legal.end()) {
    return false;
  }
  ++epoch_metrics->decision_count;
  if (legal.size() == 1) {
    ++epoch_metrics->forced_choice_count;
    return true;
  }

  Child chosen = make_child(root, chosen_move, *model, fallback, fallback_through_phase);
  const std::uint8_t phase = phase_for_position(*model, chosen.position);
  PhaseMetrics* overall = validation ? &epoch_metrics->validation : &epoch_metrics->train;
  PhaseMetrics* by_phase = validation ? &epoch_metrics->validation_by_phase[phase]
                                      : &epoch_metrics->train_by_phase[phase];
  ++overall->decision_count;
  ++by_phase->decision_count;

  if (validation) {
    ++epoch_metrics->validation_decision_count;
    std::vector<Child> children;
    children.reserve(legal.size());
    for (board::Square move : legal) {
      children.push_back(move == chosen_move
                             ? chosen
                             : make_child(root, move, *model, fallback, fallback_through_phase));
    }
    record_validation_top1(overall, children, chosen_move, args.temperature);
    record_validation_top1(by_phase, children, chosen_move, args.temperature);
    for (const Child& alternative : children) {
      if (alternative.move == chosen_move) {
        continue;
      }
      double ignored_gradient = 0.0;
      const double loss =
          logistic_loss(chosen.value, alternative.value, args.temperature, &ignored_gradient);
      record_pair(overall, chosen.value, alternative.value, loss);
      record_pair(by_phase, chosen.value, alternative.value, loss);
    }
    return true;
  }

  ++epoch_metrics->train_decision_count;
  std::vector<board::Square> alternatives;
  alternatives.reserve(legal.size() - 1);
  for (board::Square move : legal) {
    if (move != chosen_move) {
      alternatives.push_back(move);
    }
  }
  const std::size_t selected_count =
      args.negative_count == 0
          ? alternatives.size()
          : std::min(alternatives.size(), static_cast<std::size_t>(args.negative_count));
  const std::uint64_t selector = mix64(args.seed ^ identity ^ mix64(decision_index) ^
                                       mix64(static_cast<std::uint64_t>(epoch)));
  const std::size_t start = static_cast<std::size_t>(selector % alternatives.size());

  std::vector<PendingUpdate> updates;
  updates.reserve((selected_count + 1) * chosen.features.size);
  double chosen_gradient_sum = 0.0;
  for (std::size_t selected_index = 0; selected_index < selected_count; ++selected_index) {
    const std::size_t alternative_index = (start + selected_index) % alternatives.size();
    const Child alternative =
        make_child(root, alternatives[alternative_index], *model, fallback, fallback_through_phase);
    double chosen_gradient = 0.0;
    const double loss =
        logistic_loss(chosen.value, alternative.value, args.temperature, &chosen_gradient);
    chosen_gradient /= static_cast<double>(selected_count);
    chosen_gradient_sum += chosen_gradient;
    add_feature_gradients(&updates, alternative.features, -chosen_gradient);
    record_pair(overall, chosen.value, alternative.value, loss);
    record_pair(by_phase, chosen.value, alternative.value, loss);
  }
  add_feature_gradients(&updates, chosen.features, chosen_gradient_sum);

  if (args.frozen_phases[phase]) {
    return true;
  }
  for (const PendingUpdate& update : updates) {
    DenseTable& table = model->tables[update.table];
    double& weight =
        table.weights[static_cast<std::size_t>(phase) * table.pattern_size + update.index];
    const double next = std::clamp(weight - args.learning_rate * update.gradient,
                                   -args.max_abs_weight, args.max_abs_weight);
    epoch_metrics->clipped_weight_count +=
        std::abs(next) == args.max_abs_weight && next != weight ? 1 : 0;
    weight = next;
    ++epoch_metrics->update_count;
  }
  return true;
}

bool train_record(std::span<const std::uint8_t> record, int epoch, const Args& args,
                  std::optional<std::uint8_t> fallback_through_phase,
                  const eval::EarlyMidgameHeuristicEvaluator& fallback, Model* model,
                  EpochMetrics* metrics, std::string* error, bool validation_only = false) {
  const std::uint64_t identity = record_identity(record);
  const bool validation = is_validation(identity, args);
  board::Position position = board::initial_position();
  std::uint64_t decision_index = 0;
  std::size_t terminator = 60;
  for (std::size_t move_index = 0; move_index < 60; ++move_index) {
    if (record[8 + move_index] == 0) {
      terminator = move_index;
      break;
    }
  }
  for (std::size_t move_index = terminator; move_index < 60; ++move_index) {
    if (record[8 + move_index] != 0) {
      *error = "non-zero move follows WTHOR move-list terminator";
      return false;
    }
  }

  for (std::size_t move_index = 0; move_index < terminator; ++move_index) {
    const std::optional<board::Square> square = square_from_wthor(record[8 + move_index]);
    if (!square.has_value()) {
      *error = "invalid WTHOR move coordinate";
      return false;
    }
    if (!board::has_legal_move(position)) {
      if (board::is_terminal(position)) {
        *error = "record contains a move after terminal position";
        return false;
      }
      board::MoveDelta pass_delta{};
      if (!board::apply_pass(&position, &pass_delta)) {
        *error = "implicit pass could not be applied";
        return false;
      }
      ++metrics->implicit_pass_count;
    }
    if (!validation_only || validation) {
      if (!train_decision(position, *square, identity, decision_index, epoch, validation, args,
                          fallback_through_phase, fallback, model, metrics)) {
        *error = "recorded WTHOR move is illegal";
        return false;
      }
    }
    ++decision_index;
    board::MoveDelta delta{};
    if (!board::apply_move(&position, board::make_move(*square), &delta)) {
      *error = "recorded WTHOR move could not be applied";
      return false;
    }
    ++metrics->normal_move_count;
  }
  while (!board::is_terminal(position) && !board::has_legal_move(position)) {
    board::MoveDelta pass_delta{};
    if (!board::apply_pass(&position, &pass_delta)) {
      break;
    }
    ++metrics->implicit_pass_count;
  }
  if (!board::is_terminal(position)) {
    ++metrics->incomplete_game_count;
  }
  return true;
}

std::string json_escape(std::string_view value) {
  std::ostringstream output;
  for (char character : value) {
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

void write_metric(std::ostream& output, const PhaseMetrics& metric, int indent) {
  const std::string spaces(static_cast<std::size_t>(indent), ' ');
  output << spaces << "{\n"
         << spaces << "  \"decision_count\": " << metric.decision_count << ",\n"
         << spaces << "  \"pair_count\": " << metric.pair_count << ",\n"
         << spaces << "  \"pairwise_accuracy\": ";
  if (metric.pair_count == 0) {
    output << "null";
  } else {
    output << static_cast<double>(metric.pair_correct) / metric.pair_count;
  }
  output << ",\n" << spaces << "  \"pairwise_logistic_loss\": ";
  if (metric.pair_count == 0) {
    output << "null";
  } else {
    output << metric.pair_loss_sum / metric.pair_count;
  }
  output << ",\n" << spaces << "  \"top1_accuracy\": ";
  if (metric.top1_count == 0) {
    output << "null";
  } else {
    output << static_cast<double>(metric.top1_correct) / metric.top1_count;
  }
  output << ",\n" << spaces << "  \"cross_entropy\": ";
  if (metric.top1_count == 0) {
    output << "null";
  } else {
    output << metric.cross_entropy_sum / metric.top1_count;
  }
  output << "\n" << spaces << '}';
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
         << "  \"trainer_algorithm\": \"wthor-played-move-streaming-rank-v1\",\n"
         << "  \"teacher_semantics\": \"recorded WHTOR move is provisional best move\",\n"
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
         << "  \"learning_rate\": " << args.learning_rate << ",\n"
         << "  \"temperature\": " << args.temperature << ",\n"
         << "  \"negative_count\": " << args.negative_count << ",\n"
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
  output << "],\n  \"recommended_trained_phases\": [";
  for (std::uint8_t phase = 0; phase < kPhaseCount; ++phase) {
    output << (phase == 0 ? "" : ", ") << static_cast<int>(phase);
  }
  output << "],\n  \"source_files\": [\n";
  for (std::size_t index = 0; index < sources.size(); ++index) {
    const SourceFile& source = sources[index];
    output << "    {\"path\": \"" << json_escape(source.path.string()) << "\", \"checksum\": \""
           << hex64(source.checksum) << "\", \"game_count\": " << source.game_count
           << ", \"game_year\": " << source.game_year << '}'
           << (index + 1 == sources.size() ? "\n" : ",\n");
  }
  output << "  ],\n  \"metrics_by_epoch\": [\n";
  for (std::size_t epoch_index = 0; epoch_index < epochs.size(); ++epoch_index) {
    const EpochMetrics& metrics = epochs[epoch_index];
    output << "    {\n"
           << "      \"epoch\": " << epoch_index + 1 << ",\n"
           << "      \"game_count\": " << metrics.game_count << ",\n"
           << "      \"incomplete_game_count\": " << metrics.incomplete_game_count << ",\n"
           << "      \"normal_move_count\": " << metrics.normal_move_count << ",\n"
           << "      \"implicit_pass_count\": " << metrics.implicit_pass_count << ",\n"
           << "      \"decision_count\": " << metrics.decision_count << ",\n"
           << "      \"forced_choice_count\": " << metrics.forced_choice_count << ",\n"
           << "      \"train_decision_count\": " << metrics.train_decision_count << ",\n"
           << "      \"validation_decision_count\": " << metrics.validation_decision_count << ",\n"
           << "      \"updated_weight_count\": " << metrics.update_count << ",\n"
           << "      \"clipped_weight_count\": " << metrics.clipped_weight_count << ",\n"
           << "      \"train\": ";
    write_metric(output, metrics.train, 6);
    output << ",\n      \"validation\": ";
    write_metric(output, metrics.validation, 6);
    output << ",\n      \"train_by_child_phase\": {\n";
    for (std::uint8_t phase = 0; phase < kPhaseCount; ++phase) {
      output << "        \"" << static_cast<int>(phase) << "\": ";
      write_metric(output, metrics.train_by_phase[phase], 8);
      output << (phase + 1 == kPhaseCount ? "\n" : ",\n");
    }
    output << "      },\n      \"validation_by_child_phase\": {\n";
    for (std::uint8_t phase = 0; phase < kPhaseCount; ++phase) {
      output << "        \"" << static_cast<int>(phase) << "\": ";
      write_metric(output, metrics.validation_by_phase[phase], 8);
      output << (phase + 1 == kPhaseCount ? "\n" : ",\n");
    }
    output << "      }\n    }" << (epoch_index + 1 == epochs.size() ? "\n" : ",\n");
  }
  output << "  ],\n  \"final_validation\": ";
  if (!final_validation.has_value()) {
    output << "null";
  } else {
    output << "{\n"
           << "    \"game_count\": " << final_validation->game_count << ",\n"
           << "    \"validation_decision_count\": " << final_validation->validation_decision_count
           << ",\n"
           << "    \"metrics\": ";
    write_metric(output, final_validation->validation, 4);
    output << ",\n    \"by_child_phase\": {\n";
    for (std::uint8_t phase = 0; phase < kPhaseCount; ++phase) {
      output << "      \"" << static_cast<int>(phase) << "\": ";
      write_metric(output, final_validation->validation_by_phase[phase], 6);
      output << (phase + 1 == kPhaseCount ? "\n" : ",\n");
    }
    output << "    }\n  }";
  }
  output << ",\n"
         << "  \"notes\": [\n"
         << "    \"Every recorded non-forced WHTOR decision is visited; no expanded child dataset "
            "is materialized.\",\n"
         << "    \"WHTOR moves are empirical provisional targets, not solved ground truth.\",\n"
         << "    \"Validation games never update weights and score every legal move.\",\n"
         << "    \"A fixed-depth and fixed-time arena remains the promotion gate.\"\n"
         << "  ]\n"
         << "}\n";
  return static_cast<bool>(output);
}

std::uint64_t checksum_file(const std::filesystem::path& path) {
  const std::optional<std::vector<std::uint8_t>> bytes = read_bytes(path);
  return bytes.has_value() ? fnv1a(*bytes) : 0;
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
  std::vector<GameRef> games;
  for (std::size_t source_index = 0; source_index < sources.size(); ++source_index) {
    const SourceFile& source = sources[source_index];
    for (std::uint32_t record_index = 0; record_index < source.game_count; ++record_index) {
      const std::size_t offset =
          kWthorHeaderSize + static_cast<std::size_t>(record_index) * kWthorRecordSize;
      const std::span<const std::uint8_t> record{source.bytes.data() + offset, kWthorRecordSize};
      games.push_back(GameRef{
          .source_index = source_index,
          .record_index = record_index,
          .identity = record_identity(record),
      });
    }
  }
  auto order_games = [&](int epoch) {
    std::vector<GameRef> ordered = games;
    std::sort(ordered.begin(), ordered.end(), [&](const GameRef& left, const GameRef& right) {
      const std::uint64_t left_key =
          mix64(args.seed ^ mix64(static_cast<std::uint64_t>(epoch)) ^ left.identity);
      const std::uint64_t right_key =
          mix64(args.seed ^ mix64(static_cast<std::uint64_t>(epoch)) ^ right.identity);
      if (left_key != right_key) {
        return left_key < right_key;
      }
      if (left.source_index != right.source_index) {
        return left.source_index < right.source_index;
      }
      return left.record_index < right.record_index;
    });
    return ordered;
  };

  for (int epoch = 0; epoch < args.epochs; ++epoch) {
    EpochMetrics metrics;
    const std::vector<GameRef> epoch_games = order_games(epoch);
    for (const GameRef& game : epoch_games) {
      if (args.max_games.has_value() && metrics.game_count >= *args.max_games) {
        break;
      }
      const SourceFile& source = sources[game.source_index];
      const std::size_t offset =
          kWthorHeaderSize + static_cast<std::size_t>(game.record_index) * kWthorRecordSize;
      const std::span<const std::uint8_t> record{source.bytes.data() + offset, kWthorRecordSize};
      std::string error;
      if (!train_record(record, epoch, args, fallback_through_phase, fallback, &model, &metrics,
                        &error)) {
        std::cerr << source.path << ": record " << game.record_index + 1 << ": " << error << '\n';
        return 1;
      }
      ++metrics.game_count;
      if (args.progress_every_games.has_value() &&
          metrics.game_count % *args.progress_every_games == 0) {
        std::cerr << "wthor_policy_progress epoch=" << epoch + 1 << " games=" << metrics.game_count
                  << " decisions=" << metrics.decision_count
                  << " train_decisions=" << metrics.train_decision_count
                  << " validation_decisions=" << metrics.validation_decision_count
                  << " updates=" << metrics.update_count << '\n';
      }
    }
    std::cerr << "wthor_policy_epoch epoch=" << epoch + 1 << " games=" << metrics.game_count
              << " decisions=" << metrics.decision_count << " train_pair_accuracy="
              << (metrics.train.pair_count == 0
                      ? 0.0
                      : static_cast<double>(metrics.train.pair_correct) / metrics.train.pair_count)
              << " validation_top1="
              << (metrics.validation.top1_count == 0
                      ? 0.0
                      : static_cast<double>(metrics.validation.top1_correct) /
                            metrics.validation.top1_count)
              << '\n';
    metrics_by_epoch.push_back(metrics);
  }

  std::optional<EpochMetrics> final_validation;
  if (args.final_validation && args.validation_modulus > 0) {
    final_validation.emplace();
    const std::vector<GameRef> evaluation_games = order_games(0);
    for (const GameRef& game : evaluation_games) {
      if (args.max_games.has_value() && final_validation->game_count >= *args.max_games) {
        break;
      }
      const SourceFile& source = sources[game.source_index];
      const std::size_t offset =
          kWthorHeaderSize + static_cast<std::size_t>(game.record_index) * kWthorRecordSize;
      const std::span<const std::uint8_t> record{source.bytes.data() + offset, kWthorRecordSize};
      std::string error;
      if (!train_record(record, args.epochs, args, fallback_through_phase, fallback, &model,
                        &*final_validation, &error, true)) {
        std::cerr << source.path << ": record " << game.record_index + 1
                  << " during final validation: " << error << '\n';
        return 1;
      }
      ++final_validation->game_count;
    }
    std::cerr << "wthor_policy_final_validation games=" << final_validation->game_count
              << " decisions=" << final_validation->validation_decision_count << " top1="
              << (final_validation->validation.top1_count == 0
                      ? 0.0
                      : static_cast<double>(final_validation->validation.top1_correct) /
                            final_validation->validation.top1_count)
              << " cross_entropy="
              << (final_validation->validation.top1_count == 0
                      ? 0.0
                      : final_validation->validation.cross_entropy_sum /
                            final_validation->validation.top1_count)
              << '\n';
  }

  if (!write_weights(args.weights_out, model)) {
    return 1;
  }
  const std::string weights_checksum = hex64(checksum_file(args.weights_out));
  if (!write_report(args.report_out, args, sources, artifact, fallback_through_phase,
                    metrics_by_epoch, final_validation, weights_checksum)) {
    return 1;
  }
  std::cout << "weights=" << args.weights_out << '\n'
            << "weights_checksum=" << weights_checksum << '\n'
            << "report=" << args.report_out << '\n';
  return 0;
}
