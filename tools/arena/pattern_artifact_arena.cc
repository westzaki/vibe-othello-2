#include "arena_core.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/position.h"
#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/pattern_evaluator.h"
#include "vibe_othello/evaluation/pattern_feature_set.h"
#include "vibe_othello/evaluation/pattern_weights.h"
#include "vibe_othello/search/search.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

namespace arena = vibe_othello::tools::arena;
namespace board_core = vibe_othello::board_core;
namespace eval = vibe_othello::evaluation;
namespace search = vibe_othello::search;

constexpr std::string_view kArenaVersion = "pattern-artifact-arena-v1";
constexpr std::uint32_t kCrc32Initial = 0xFFFF'FFFFU;
constexpr std::uint32_t kCrc32Polynomial = 0xEDB8'8320U;

struct Args {
  std::string positions_tsv_path;
  std::string candidate_weights_path;
  std::string candidate_manifest_path;
  std::string candidate_name = "pattern-v2-endgame-lite";
  std::string baseline_weights_path;
  std::string baseline_manifest_path;
  std::string baseline_name = "pattern-v1-buro-lite";
  std::string report_out_path;
  std::string summary_out_path;
  std::string diagnostics_out_path;
  int max_empty = 12;
  int max_positions = 0;
  std::uint64_t seed = 0;
  bool side_swap = false;
  int progress_every = 0;
  search::Depth depth = 3;
  bool compare_static_scores = false;
  bool compare_best_moves = false;
  std::vector<search::Depth> depth_sweep;
  bool exact_adjudicate_disagreements = false;
  int max_disagreements = 200;
};

struct PatternRuntime {
  const eval::PatternSet* pattern_set = nullptr;
  eval::PatternFeatureSet feature_set;
};

struct LoadedEvaluator {
  std::string name;
  std::string weights_path;
  std::string manifest_path;
  std::string manifest_pattern_set_id;
  std::string runtime_pattern_set_id;
  std::string artifact_checksum;
  eval::PatternFeatureSet feature_set;
  eval::PatternEvaluator evaluator;
};

struct PositionRow {
  std::string board_id;
  std::string board_text;
  std::string split;
  std::string label_kind;
  std::string label_perspective;
  std::optional<int> label_score_side_to_move;
  int empty_count = 0;
  int phase = 0;
  std::uint64_t selection_hash = 0;
};

struct PositionLoadResult {
  int input_rows = 0;
  int eligible_rows = 0;
  int duplicate_board_id_rows = 0;
  std::vector<PositionRow> selected;
};

struct GameResult {
  std::string board_id;
  std::string split;
  std::string side_assignment;
  int empty_count = 0;
  int phase = 0;
  int candidate_disc_diff = 0;
  bool failed = false;
  std::string failure_reason;
};

struct BucketStats {
  int games = 0;
  int candidate_wins = 0;
  int baseline_wins = 0;
  int draws = 0;
  double candidate_score = 0.0;
  int disc_diff_sum = 0;
  int illegal_or_failed_games = 0;
};

struct ArenaStats {
  int games_played = 0;
  int candidate_wins = 0;
  int baseline_wins = 0;
  int draws = 0;
  double candidate_score = 0.0;
  double baseline_score = 0.0;
  double candidate_score_rate = 0.0;
  double average_disc_diff = 0.0;
  double median_disc_diff = 0.0;
  double score_interval_low = 0.0;
  double score_interval_high = 0.0;
  int illegal_or_failed_games = 0;
  std::map<int, int> disc_diff_histogram;
  std::map<std::string, BucketStats> by_empty_count;
  std::map<std::string, BucketStats> by_phase;
  std::map<std::string, BucketStats> by_split;
  std::map<std::string, BucketStats> by_side_assignment;
};

struct ScoreRange {
  int min = 0;
  int max = 0;
  bool has_value = false;
};

struct FeatureFamilyActivation {
  int instances_evaluated = 0;
  int non_empty_activations = 0;
  int all_empty_activations = 0;
  std::unordered_set<std::uint32_t> distinct_indices;
};

struct FeatureActivationReport {
  std::string pattern_set_id;
  int added_family_count = 0;
  int added_family_active_count = 0;
  std::map<std::string, FeatureFamilyActivation> by_family;
};

struct PositionDiagnostics {
  std::string board_id;
  std::string split;
  std::string label_kind;
  std::string label_perspective;
  std::optional<int> label_score_side_to_move;
  int empty_count = 0;
  int phase = 0;
  int runtime_phase = 0;
  std::optional<int> candidate_static_score;
  std::optional<int> baseline_static_score;
  std::optional<int> static_score_diff;
  std::string candidate_best_move;
  std::string baseline_best_move;
  std::optional<int> candidate_search_score;
  std::optional<int> baseline_search_score;
  std::optional<int> search_score_diff;
  bool best_moves_differ = false;
  std::optional<bool> static_score_order_agrees_with_search;
  std::optional<int> arena_candidate_side_to_move_disc_diff;
  std::optional<int> arena_candidate_opponent_disc_diff;
  bool exact_adjudicated = false;
  std::optional<int> candidate_move_exact_score;
  std::optional<int> baseline_move_exact_score;
  std::string disagreement_adjudication;
};

struct DepthDiagnostic {
  ArenaStats arena_stats;
  int best_move_disagreement_count = 0;
  double best_move_disagreement_rate = 0.0;
  double search_score_diff_mean = 0.0;
  double search_score_diff_abs_mean = 0.0;
};

struct DiagnosticsReport {
  std::vector<PositionDiagnostics> rows;
  int selected_positions = 0;
  int phase_mapping_mismatch_count = 0;
  int label_perspective_mismatch_count = 0;
  int static_score_diff_zero_count = 0;
  int static_score_diff_nonzero_count = 0;
  double static_score_diff_mean = 0.0;
  double static_score_diff_abs_mean = 0.0;
  double static_score_diff_abs_median = 0.0;
  ScoreRange candidate_static_score_range;
  ScoreRange baseline_static_score_range;
  double candidate_static_abs_mean = 0.0;
  double baseline_static_abs_mean = 0.0;
  int labeled_static_sign_checked_count = 0;
  int candidate_static_label_sign_agreement_count = 0;
  int candidate_static_label_sign_opposition_count = 0;
  int best_move_disagreement_count = 0;
  double best_move_disagreement_rate = 0.0;
  int static_search_order_checked_count = 0;
  int static_search_order_agreement_count = 0;
  double static_search_order_agreement_rate = 0.0;
  double search_score_diff_mean = 0.0;
  double search_score_diff_abs_mean = 0.0;
  int disagreement_exact_checked_count = 0;
  int candidate_better_on_disagreements = 0;
  int baseline_better_on_disagreements = 0;
  int draw_on_disagreements = 0;
  FeatureActivationReport candidate_feature_activation;
  FeatureActivationReport baseline_feature_activation;
  std::map<int, DepthDiagnostic> results_by_depth;
  std::vector<std::string> suspicious_sign_or_perspective_indicators;
  std::vector<std::string> notes;
};

void print_usage() {
  std::cerr << "usage: vibe-othello-pattern-artifact-arena "
               "--positions-tsv PATH "
               "--candidate-weights PATH --candidate-manifest PATH --candidate-name NAME "
               "--baseline-weights PATH --baseline-manifest PATH --baseline-name NAME "
               "--report-out PATH --summary-out PATH "
               "[--max-empty 12] [--max-positions N] [--seed 0] [--side-swap] "
               "[--depth 3] [--progress-every N] "
               "[--diagnostics-out PATH] [--compare-static-scores] [--compare-best-moves] "
               "[--depth-sweep 1,3,5] [--exact-adjudicate-disagreements] "
               "[--max-disagreements N]\n";
}

std::optional<int> parse_int(std::string_view text) noexcept {
  int value = 0;
  const char* begin = text.data();
  const char* end = text.data() + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::uint64_t> parse_u64(std::string_view text) noexcept {
  std::uint64_t value = 0;
  const char* begin = text.data();
  const char* end = text.data() + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::vector<search::Depth>> parse_depth_list(std::string_view text) {
  std::vector<search::Depth> depths;
  std::set<int> seen;
  std::size_t offset = 0;
  while (offset <= text.size()) {
    const std::size_t comma = text.find(',', offset);
    const std::string_view token =
        comma == std::string_view::npos ? text.substr(offset) : text.substr(offset, comma - offset);
    if (token.empty()) {
      return std::nullopt;
    }
    const std::optional<int> value = parse_int(token);
    if (!value.has_value() || *value <= 0) {
      return std::nullopt;
    }
    if (seen.insert(*value).second) {
      depths.push_back(static_cast<search::Depth>(*value));
    }
    if (comma == std::string_view::npos) {
      break;
    }
    offset = comma + 1;
  }
  if (depths.empty()) {
    return std::nullopt;
  }
  return depths;
}

std::optional<Args> parse_args(int argc, char** argv) {
  Args args;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    auto next_value = [&](std::string* output) -> bool {
      if (index + 1 >= argc) {
        std::cerr << arg << " requires a value\n";
        return false;
      }
      *output = argv[++index];
      return true;
    };

    if (arg == "--positions-tsv") {
      if (!next_value(&args.positions_tsv_path)) {
        return std::nullopt;
      }
    } else if (arg == "--candidate-weights") {
      if (!next_value(&args.candidate_weights_path)) {
        return std::nullopt;
      }
    } else if (arg == "--candidate-manifest") {
      if (!next_value(&args.candidate_manifest_path)) {
        return std::nullopt;
      }
    } else if (arg == "--candidate-name") {
      if (!next_value(&args.candidate_name)) {
        return std::nullopt;
      }
    } else if (arg == "--baseline-weights") {
      if (!next_value(&args.baseline_weights_path)) {
        return std::nullopt;
      }
    } else if (arg == "--baseline-manifest") {
      if (!next_value(&args.baseline_manifest_path)) {
        return std::nullopt;
      }
    } else if (arg == "--baseline-name") {
      if (!next_value(&args.baseline_name)) {
        return std::nullopt;
      }
    } else if (arg == "--report-out") {
      if (!next_value(&args.report_out_path)) {
        return std::nullopt;
      }
    } else if (arg == "--summary-out") {
      if (!next_value(&args.summary_out_path)) {
        return std::nullopt;
      }
    } else if (arg == "--diagnostics-out") {
      if (!next_value(&args.diagnostics_out_path)) {
        return std::nullopt;
      }
    } else if (arg == "--max-empty") {
      if (index + 1 >= argc) {
        std::cerr << "--max-empty requires a value\n";
        return std::nullopt;
      }
      const std::optional<int> value = parse_int(argv[++index]);
      if (!value.has_value() || *value < 0 || *value > 64) {
        std::cerr << "--max-empty must be an integer in [0, 64]\n";
        return std::nullopt;
      }
      args.max_empty = *value;
    } else if (arg == "--max-positions") {
      if (index + 1 >= argc) {
        std::cerr << "--max-positions requires a value\n";
        return std::nullopt;
      }
      const std::optional<int> value = parse_int(argv[++index]);
      if (!value.has_value() || *value <= 0) {
        std::cerr << "--max-positions must be a positive integer\n";
        return std::nullopt;
      }
      args.max_positions = *value;
    } else if (arg == "--seed") {
      if (index + 1 >= argc) {
        std::cerr << "--seed requires a value\n";
        return std::nullopt;
      }
      const std::optional<std::uint64_t> value = parse_u64(argv[++index]);
      if (!value.has_value()) {
        std::cerr << "--seed must be a non-negative integer\n";
        return std::nullopt;
      }
      args.seed = *value;
    } else if (arg == "--side-swap") {
      args.side_swap = true;
    } else if (arg == "--progress-every") {
      if (index + 1 >= argc) {
        std::cerr << "--progress-every requires a value\n";
        return std::nullopt;
      }
      const std::optional<int> value = parse_int(argv[++index]);
      if (!value.has_value() || *value < 0) {
        std::cerr << "--progress-every must be a non-negative integer\n";
        return std::nullopt;
      }
      args.progress_every = *value;
    } else if (arg == "--depth") {
      if (index + 1 >= argc) {
        std::cerr << "--depth requires a value\n";
        return std::nullopt;
      }
      const std::optional<int> value = parse_int(argv[++index]);
      if (!value.has_value() || *value <= 0) {
        std::cerr << "--depth must be a positive integer\n";
        return std::nullopt;
      }
      args.depth = static_cast<search::Depth>(*value);
    } else if (arg == "--compare-static-scores") {
      args.compare_static_scores = true;
    } else if (arg == "--compare-best-moves") {
      args.compare_best_moves = true;
    } else if (arg == "--depth-sweep") {
      if (index + 1 >= argc) {
        std::cerr << "--depth-sweep requires a comma-separated value\n";
        return std::nullopt;
      }
      const std::optional<std::vector<search::Depth>> depths = parse_depth_list(argv[++index]);
      if (!depths.has_value()) {
        std::cerr << "--depth-sweep must be a comma-separated list of positive integers\n";
        return std::nullopt;
      }
      args.depth_sweep = *depths;
    } else if (arg == "--exact-adjudicate-disagreements") {
      args.exact_adjudicate_disagreements = true;
    } else if (arg == "--max-disagreements") {
      if (index + 1 >= argc) {
        std::cerr << "--max-disagreements requires a value\n";
        return std::nullopt;
      }
      const std::optional<int> value = parse_int(argv[++index]);
      if (!value.has_value() || *value < 0) {
        std::cerr << "--max-disagreements must be a non-negative integer\n";
        return std::nullopt;
      }
      args.max_disagreements = *value;
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      return std::nullopt;
    }
  }

  if (args.positions_tsv_path.empty() || args.candidate_weights_path.empty() ||
      args.candidate_manifest_path.empty() || args.candidate_name.empty() ||
      args.baseline_weights_path.empty() || args.baseline_manifest_path.empty() ||
      args.baseline_name.empty() || args.report_out_path.empty() || args.summary_out_path.empty()) {
    print_usage();
    return std::nullopt;
  }
  if (args.diagnostics_out_path.empty() &&
      (args.compare_static_scores || args.compare_best_moves || !args.depth_sweep.empty() ||
       args.exact_adjudicate_disagreements)) {
    std::cerr << "--diagnostics-out is required when diagnostics flags are used\n";
    return std::nullopt;
  }
  return args;
}

std::string shell_quote(std::string_view text) {
  std::string quoted = "'";
  for (const char ch : text) {
    if (ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted.push_back(ch);
    }
  }
  quoted.push_back('\'');
  return quoted;
}

std::string command_line(int argc, char** argv) {
  std::string result;
  for (int index = 0; index < argc; ++index) {
    if (!result.empty()) {
      result.push_back(' ');
    }
    result += shell_quote(argv[index]);
  }
  return result;
}

void mix_fnv1a(std::string_view text, std::uint64_t* hash) noexcept {
  for (const char character : text) {
    *hash ^= static_cast<unsigned char>(character);
    *hash *= 1099511628211ull;
  }
}

std::string checksum_for(std::string_view text) {
  std::uint64_t hash = 14695981039346656037ull;
  mix_fnv1a(text, &hash);
  std::ostringstream output;
  output << "fnv1a64:0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(16)
         << hash;
  return output.str();
}

std::uint64_t selection_hash(std::uint64_t seed, std::string_view board_id) noexcept {
  std::uint64_t hash = 14695981039346656037ull;
  mix_fnv1a(std::to_string(seed), &hash);
  mix_fnv1a("\0", &hash);
  mix_fnv1a(board_id, &hash);
  return hash;
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

std::string crc32_checksum_string(std::span<const std::uint8_t> bytes_without_checksum) {
  std::ostringstream output;
  output << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(8)
         << crc32(bytes_without_checksum);
  return output.str();
}

std::optional<std::string> read_text_file(const std::string& path, std::string_view label) {
  std::ifstream input(path);
  if (!input) {
    std::cerr << "cannot read " << label << ": " << path << '\n';
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (!input.good() && !input.eof()) {
    std::cerr << "failed while reading " << label << ": " << path << '\n';
    return std::nullopt;
  }
  return buffer.str();
}

std::optional<std::vector<std::uint8_t>> read_binary_file(const std::string& path,
                                                          std::string_view label) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    std::cerr << "cannot read " << label << ": " << path << '\n';
    return std::nullopt;
  }
  input.seekg(0, std::ios::end);
  const std::streamoff size = input.tellg();
  if (size < 0) {
    std::cerr << "cannot determine " << label << " size: " << path << '\n';
    return std::nullopt;
  }
  input.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  if (!input) {
    std::cerr << "failed while reading " << label << ": " << path << '\n';
    return std::nullopt;
  }
  return bytes;
}

std::optional<std::string> json_string_field(std::string_view json, std::string_view field) {
  const std::string quoted_field = "\"" + std::string(field) + "\"";
  std::size_t pos = json.find(quoted_field);
  while (pos != std::string_view::npos) {
    pos += quoted_field.size();
    while (pos < json.size() &&
           (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) {
      ++pos;
    }
    if (pos < json.size() && json[pos] == ':') {
      ++pos;
      while (pos < json.size() &&
             (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) {
        ++pos;
      }
      if (pos >= json.size() || json[pos] != '"') {
        return std::nullopt;
      }
      ++pos;
      std::string value;
      while (pos < json.size()) {
        const char ch = json[pos++];
        if (ch == '"') {
          return value;
        }
        if (ch == '\\') {
          if (pos >= json.size()) {
            return std::nullopt;
          }
          value.push_back(json[pos++]);
        } else {
          value.push_back(ch);
        }
      }
      return std::nullopt;
    }
    pos = json.find(quoted_field, pos);
  }
  return std::nullopt;
}

std::optional<PatternRuntime> select_pattern_runtime(std::string_view name) {
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

std::array<std::uint8_t, eval::PatternWeights::kDiscCountEntries> phase_by_disc_count_13() {
  std::array<std::uint8_t, eval::PatternWeights::kDiscCountEntries> phases{};
  for (std::uint8_t disc_count = 0; disc_count < phases.size(); ++disc_count) {
    const int normalized_count = disc_count < 4 ? 0 : static_cast<int>(disc_count) - 4;
    const int phase = std::min(12, (normalized_count * 13) / 60);
    phases[disc_count] = static_cast<std::uint8_t>(phase);
  }
  return phases;
}

std::optional<LoadedEvaluator> load_evaluator(std::string name, std::string weights_path,
                                              std::string manifest_path) {
  const std::optional<PatternRuntime> runtime = select_pattern_runtime(name);
  if (!runtime.has_value()) {
    std::cerr << "unknown pattern set name: " << name << '\n';
    return std::nullopt;
  }

  const std::optional<std::string> manifest_text = read_text_file(manifest_path, "manifest");
  if (!manifest_text.has_value()) {
    return std::nullopt;
  }
  const std::optional<std::string> manifest_pattern_set =
      json_string_field(*manifest_text, "pattern_set_id");
  if (!manifest_pattern_set.has_value()) {
    std::cerr << "manifest is missing string field pattern_set_id: " << manifest_path << '\n';
    return std::nullopt;
  }
  if (*manifest_pattern_set != runtime->pattern_set->id) {
    std::cerr << "manifest pattern_set_id mismatch for " << name << ": expected "
              << runtime->pattern_set->id << ", got " << *manifest_pattern_set << '\n';
    return std::nullopt;
  }
  const std::optional<std::string> manifest_checksum =
      json_string_field(*manifest_text, "weights_checksum");
  if (!manifest_checksum.has_value()) {
    std::cerr << "manifest is missing string field weights_checksum: " << manifest_path << '\n';
    return std::nullopt;
  }

  const std::optional<std::vector<std::uint8_t>> artifact =
      read_binary_file(weights_path, "artifact weights");
  if (!artifact.has_value()) {
    return std::nullopt;
  }
  if (artifact->size() < sizeof(std::uint32_t)) {
    std::cerr << "artifact is too small: " << weights_path << '\n';
    return std::nullopt;
  }
  const std::string artifact_checksum = crc32_checksum_string(
      std::span<const std::uint8_t>(*artifact).first(artifact->size() - sizeof(std::uint32_t)));
  if (artifact_checksum != *manifest_checksum) {
    std::cerr << "artifact checksum mismatch for " << name << ": manifest " << *manifest_checksum
              << ", actual " << artifact_checksum << '\n';
    return std::nullopt;
  }

  const eval::PatternManifest manifest{
      .format_version = eval::kPatternWeightFormatVersion,
      .bit_order = eval::PatternBitOrder::a1_lsb,
      .score_unit = eval::PatternScoreUnit::disc_diff,
      .score_scale = 1,
      .phase_count = 13,
      .pattern_set_id = runtime->pattern_set->id,
      .patterns = runtime->pattern_set->patterns,
  };
  const eval::PatternWeightsLoadResult loaded = eval::load_pattern_weights(manifest, *artifact);
  if (!loaded.ok()) {
    std::cerr << "runtime loader rejected artifact for " << name << '\n';
    return std::nullopt;
  }
  std::optional<eval::PatternWeights> weights =
      eval::make_pattern_weights(*loaded.weights, phase_by_disc_count_13());
  if (!weights.has_value()) {
    std::cerr << "loaded artifact could not be converted to runtime weights for " << name << '\n';
    return std::nullopt;
  }

  try {
    eval::PatternFeatureSet feature_set = std::move(runtime->feature_set);
    return LoadedEvaluator{
        .name = std::move(name),
        .weights_path = std::move(weights_path),
        .manifest_path = std::move(manifest_path),
        .manifest_pattern_set_id = *manifest_pattern_set,
        .runtime_pattern_set_id = runtime->pattern_set->id,
        .artifact_checksum = artifact_checksum,
        .feature_set = feature_set,
        .evaluator = eval::PatternEvaluator{std::move(*weights), std::move(feature_set)},
    };
  } catch (const std::exception& error) {
    std::cerr << "pattern evaluator rejected artifact for " << name << ": " << error.what() << '\n';
    return std::nullopt;
  }
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

std::string_view trim_trailing_cr(std::string_view text) noexcept {
  if (!text.empty() && text.back() == '\r') {
    text.remove_suffix(1);
  }
  return text;
}

std::optional<std::size_t> column_index(std::span<const std::string_view> header,
                                        std::string_view name) {
  for (std::size_t index = 0; index < header.size(); ++index) {
    if (header[index] == name) {
      return index;
    }
  }
  return std::nullopt;
}

std::optional<board_core::Position> position_from_relative_board(std::string_view board) noexcept {
  if (board.size() != board_core::kSquareCount) {
    return std::nullopt;
  }
  board_core::Bitboard player = 0;
  board_core::Bitboard opponent = 0;
  for (std::size_t index = 0; index < board.size(); ++index) {
    const board_core::Square square = board_core::square_from_index(static_cast<int>(index));
    const board_core::Bitboard bit = board_core::bit(square);
    if (board[index] == 'X') {
      player |= bit;
    } else if (board[index] == 'O') {
      opponent |= bit;
    } else if (board[index] != '-') {
      return std::nullopt;
    }
  }
  board_core::Position position{
      .player = player,
      .opponent = opponent,
      .side_to_move = board_core::Color::black,
  };
  if (!board_core::is_valid(position)) {
    return std::nullopt;
  }
  return position;
}

void add_or_replace_selected(std::vector<PositionRow>* selected, PositionRow row,
                             int max_positions) {
  if (max_positions <= 0 || selected->size() < static_cast<std::size_t>(max_positions)) {
    selected->push_back(std::move(row));
    return;
  }
  auto worst =
      std::max_element(selected->begin(), selected->end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.selection_hash != rhs.selection_hash) {
          return lhs.selection_hash < rhs.selection_hash;
        }
        return lhs.board_id < rhs.board_id;
      });
  if (worst == selected->end()) {
    return;
  }
  if (row.selection_hash < worst->selection_hash ||
      (row.selection_hash == worst->selection_hash && row.board_id < worst->board_id)) {
    *worst = std::move(row);
  }
}

std::optional<PositionLoadResult> load_positions(const Args& args) {
  std::ifstream input(args.positions_tsv_path);
  if (!input) {
    std::cerr << "cannot read positions TSV: " << args.positions_tsv_path << '\n';
    return std::nullopt;
  }

  std::string line;
  if (!std::getline(input, line)) {
    std::cerr << "positions TSV is empty: " << args.positions_tsv_path << '\n';
    return std::nullopt;
  }
  const std::vector<std::string_view> header = split_tabs(trim_trailing_cr(line));
  const std::optional<std::size_t> board_id_index = column_index(header, "board_id");
  const std::optional<std::size_t> board_index = column_index(header, "board_a1_to_h8");
  const std::optional<std::size_t> empty_index = column_index(header, "empty_count");
  const std::optional<std::size_t> phase_index = column_index(header, "phase");
  const std::optional<std::size_t> split_index = column_index(header, "split");
  const std::optional<std::size_t> label_kind_index = column_index(header, "label_kind");
  const std::optional<std::size_t> label_perspective_index =
      column_index(header, "label_perspective");
  const std::optional<std::size_t> label_score_index =
      column_index(header, "label_score_side_to_move");
  if (!board_id_index.has_value() || !board_index.has_value() || !empty_index.has_value() ||
      !phase_index.has_value() || !split_index.has_value()) {
    std::cerr << "positions TSV must contain board_id, board_a1_to_h8, empty_count, phase, and "
                 "split columns\n";
    return std::nullopt;
  }

  PositionLoadResult result;
  std::set<std::string> seen_board_ids;
  while (std::getline(input, line)) {
    ++result.input_rows;
    const std::vector<std::string_view> fields = split_tabs(trim_trailing_cr(line));
    const std::size_t required =
        std::max({*board_id_index, *board_index, *empty_index, *phase_index, *split_index});
    if (fields.size() <= required) {
      std::cerr << "malformed positions TSV row " << result.input_rows + 1 << '\n';
      return std::nullopt;
    }
    const std::optional<int> empty_count = parse_int(fields[*empty_index]);
    const std::optional<int> phase = parse_int(fields[*phase_index]);
    if (!empty_count.has_value() || !phase.has_value()) {
      std::cerr << "positions TSV row " << result.input_rows + 1
                << " has invalid empty_count or phase\n";
      return std::nullopt;
    }
    if (*empty_count > args.max_empty) {
      continue;
    }
    ++result.eligible_rows;
    const std::string board_id{fields[*board_id_index]};
    if (!seen_board_ids.insert(board_id).second) {
      ++result.duplicate_board_id_rows;
      continue;
    }
    const std::string board_text{fields[*board_index]};
    if (!position_from_relative_board(board_text).has_value()) {
      std::cerr << "positions TSV row " << result.input_rows + 1 << " has invalid board_a1_to_h8\n";
      return std::nullopt;
    }
    std::optional<int> label_score;
    if (label_score_index.has_value() && fields.size() > *label_score_index &&
        !fields[*label_score_index].empty()) {
      label_score = parse_int(fields[*label_score_index]);
      if (!label_score.has_value()) {
        std::cerr << "positions TSV row " << result.input_rows + 1
                  << " has invalid label_score_side_to_move\n";
        return std::nullopt;
      }
    }
    add_or_replace_selected(
        &result.selected,
        PositionRow{
            .board_id = board_id,
            .board_text = board_text,
            .split = std::string{fields[*split_index]},
            .label_kind = label_kind_index.has_value() && fields.size() > *label_kind_index
                              ? std::string{fields[*label_kind_index]}
                              : std::string{},
            .label_perspective =
                label_perspective_index.has_value() && fields.size() > *label_perspective_index
                    ? std::string{fields[*label_perspective_index]}
                    : std::string{},
            .label_score_side_to_move = label_score,
            .empty_count = *empty_count,
            .phase = *phase,
            .selection_hash = selection_hash(args.seed, board_id),
        },
        args.max_positions);
  }

  std::sort(result.selected.begin(), result.selected.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.selection_hash != rhs.selection_hash) {
      return lhs.selection_hash < rhs.selection_hash;
    }
    return lhs.board_id < rhs.board_id;
  });

  if (result.selected.empty()) {
    std::cerr << "no positions remained after max-empty filtering and deterministic selection\n";
    return std::nullopt;
  }
  return result;
}

int disc_count(board_core::Bitboard bits) noexcept {
  return std::popcount(bits);
}

GameResult adjudicated_failure(const PositionRow& row, std::string side_assignment,
                               bool candidate_offender, std::string reason) {
  return GameResult{
      .board_id = row.board_id,
      .split = row.split,
      .side_assignment = std::move(side_assignment),
      .empty_count = row.empty_count,
      .phase = row.phase,
      .candidate_disc_diff = candidate_offender ? -64 : 64,
      .failed = true,
      .failure_reason = std::move(reason),
  };
}

GameResult play_game(const PositionRow& row, const LoadedEvaluator& candidate,
                     const LoadedEvaluator& baseline, bool candidate_is_black,
                     search::Depth depth) {
  std::optional<board_core::Position> maybe_position = position_from_relative_board(row.board_text);
  if (!maybe_position.has_value()) {
    return adjudicated_failure(row,
                               candidate_is_black ? "candidate_side_to_move" : "candidate_opponent",
                               true, "invalid_position");
  }
  board_core::Position position = *maybe_position;
  while (!board_core::is_terminal(position)) {
    const board_core::Bitboard legal_moves = board_core::legal_moves(position);
    if (legal_moves == 0) {
      board_core::MoveDelta delta{};
      if (!board_core::apply_pass(&position, &delta)) {
        return adjudicated_failure(
            row, candidate_is_black ? "candidate_side_to_move" : "candidate_opponent", true,
            "illegal_pass");
      }
      continue;
    }

    const bool candidate_to_move = position.side_to_move == board_core::Color::black
                                       ? candidate_is_black
                                       : !candidate_is_black;
    const search::Evaluator& evaluator =
        candidate_to_move ? static_cast<const search::Evaluator&>(candidate.evaluator)
                          : static_cast<const search::Evaluator&>(baseline.evaluator);
    const search::SearchResult search_result =
        search::search_fixed_depth(position, evaluator, depth);
    if (!search_result.best_move.has_value() ||
        search_result.best_move->kind != board_core::MoveKind::normal ||
        (legal_moves & board_core::bit(search_result.best_move->square)) == 0) {
      return adjudicated_failure(
          row, candidate_is_black ? "candidate_side_to_move" : "candidate_opponent",
          candidate_to_move, "illegal_or_missing_best_move");
    }

    board_core::MoveDelta delta{};
    if (!board_core::apply_move(&position, *search_result.best_move, &delta)) {
      return adjudicated_failure(
          row, candidate_is_black ? "candidate_side_to_move" : "candidate_opponent",
          candidate_to_move, "apply_move_failed");
    }
  }

  const int black_discs = disc_count(board_core::black_discs(position));
  const int white_discs = disc_count(board_core::white_discs(position));
  const int black_diff = black_discs - white_discs;
  return GameResult{
      .board_id = row.board_id,
      .split = row.split,
      .side_assignment = candidate_is_black ? "candidate_side_to_move" : "candidate_opponent",
      .empty_count = row.empty_count,
      .phase = row.phase,
      .candidate_disc_diff = candidate_is_black ? black_diff : -black_diff,
  };
}

std::vector<GameResult> play_selected_games(std::span<const PositionRow> rows,
                                            const LoadedEvaluator& candidate,
                                            const LoadedEvaluator& baseline, bool side_swap,
                                            search::Depth depth, int progress_every,
                                            std::string_view progress_prefix) {
  std::vector<GameResult> results;
  results.reserve(rows.size() * (side_swap ? 2U : 1U));
  int games_played = 0;
  for (const PositionRow& row : rows) {
    results.push_back(play_game(row, candidate, baseline, true, depth));
    ++games_played;
    if (progress_every > 0 && games_played % progress_every == 0) {
      std::cerr << progress_prefix << " progress games=" << games_played << '\n';
    }
    if (side_swap) {
      results.push_back(play_game(row, candidate, baseline, false, depth));
      ++games_played;
      if (progress_every > 0 && games_played % progress_every == 0) {
        std::cerr << progress_prefix << " progress games=" << games_played << '\n';
      }
    }
  }
  if (progress_every > 0) {
    std::cerr << progress_prefix << " complete games=" << results.size() << '\n';
  }
  return results;
}

void add_to_bucket(BucketStats* bucket, const GameResult& result) {
  ++bucket->games;
  bucket->disc_diff_sum += result.candidate_disc_diff;
  if (result.failed) {
    ++bucket->illegal_or_failed_games;
  }
  if (result.candidate_disc_diff > 0) {
    ++bucket->candidate_wins;
    bucket->candidate_score += 1.0;
  } else if (result.candidate_disc_diff < 0) {
    ++bucket->baseline_wins;
  } else {
    ++bucket->draws;
    bucket->candidate_score += 0.5;
  }
}

ArenaStats summarize_results(std::span<const GameResult> results) {
  ArenaStats stats;
  std::vector<int> disc_diffs;
  disc_diffs.reserve(results.size());
  for (const GameResult& result : results) {
    ++stats.games_played;
    disc_diffs.push_back(result.candidate_disc_diff);
    ++stats.disc_diff_histogram[result.candidate_disc_diff];
    if (result.failed) {
      ++stats.illegal_or_failed_games;
    }
    if (result.candidate_disc_diff > 0) {
      ++stats.candidate_wins;
      stats.candidate_score += 1.0;
    } else if (result.candidate_disc_diff < 0) {
      ++stats.baseline_wins;
    } else {
      ++stats.draws;
      stats.candidate_score += 0.5;
    }
    add_to_bucket(&stats.by_empty_count[std::to_string(result.empty_count)], result);
    add_to_bucket(&stats.by_phase[std::to_string(result.phase)], result);
    add_to_bucket(&stats.by_split[result.split], result);
    add_to_bucket(&stats.by_side_assignment[result.side_assignment], result);
  }

  if (stats.games_played > 0) {
    stats.baseline_score = static_cast<double>(stats.games_played) - stats.candidate_score;
    stats.candidate_score_rate = stats.candidate_score / static_cast<double>(stats.games_played);
    const int disc_diff_sum = std::accumulate(disc_diffs.begin(), disc_diffs.end(), 0);
    stats.average_disc_diff =
        static_cast<double>(disc_diff_sum) / static_cast<double>(stats.games_played);
    std::sort(disc_diffs.begin(), disc_diffs.end());
    const std::size_t middle = disc_diffs.size() / 2;
    if (disc_diffs.size() % 2 == 0) {
      stats.median_disc_diff =
          (static_cast<double>(disc_diffs[middle - 1]) + static_cast<double>(disc_diffs[middle])) /
          2.0;
    } else {
      stats.median_disc_diff = static_cast<double>(disc_diffs[middle]);
    }
    const double p = stats.candidate_score_rate;
    const double stderr =
        std::sqrt(std::max(0.0, p * (1.0 - p)) / static_cast<double>(stats.games_played));
    stats.score_interval_low = std::clamp(p - 1.96 * stderr, 0.0, 1.0);
    stats.score_interval_high = std::clamp(p + 1.96 * stderr, 0.0, 1.0);
  }
  return stats;
}

int sign_of(int value) noexcept {
  if (value > 0) {
    return 1;
  }
  if (value < 0) {
    return -1;
  }
  return 0;
}

void update_range(ScoreRange* range, int value) noexcept {
  if (!range->has_value) {
    range->min = value;
    range->max = value;
    range->has_value = true;
    return;
  }
  range->min = std::min(range->min, value);
  range->max = std::max(range->max, value);
}

double average_or_zero(std::span<const int> values) {
  if (values.empty()) {
    return 0.0;
  }
  const int sum = std::accumulate(values.begin(), values.end(), 0);
  return static_cast<double>(sum) / static_cast<double>(values.size());
}

double absolute_average_or_zero(std::span<const int> values) {
  if (values.empty()) {
    return 0.0;
  }
  std::int64_t sum = 0;
  for (const int value : values) {
    sum += std::abs(value);
  }
  return static_cast<double>(sum) / static_cast<double>(values.size());
}

double absolute_median_or_zero(std::vector<int> values) {
  if (values.empty()) {
    return 0.0;
  }
  for (int& value : values) {
    value = std::abs(value);
  }
  std::sort(values.begin(), values.end());
  const std::size_t middle = values.size() / 2;
  if (values.size() % 2 == 0) {
    return (static_cast<double>(values[middle - 1]) + static_cast<double>(values[middle])) / 2.0;
  }
  return static_cast<double>(values[middle]);
}

std::string move_to_string(board_core::Move move) {
  if (move.kind == board_core::MoveKind::pass) {
    return "pass";
  }
  const int file = board_core::file_of(move.square);
  const int rank = board_core::rank_of(move.square);
  if (file < 0 || rank < 0) {
    return "none";
  }
  std::string text;
  text.push_back(static_cast<char>('a' + file));
  text.push_back(static_cast<char>('1' + rank));
  return text;
}

std::string best_move_to_string(const search::SearchResult& result) {
  if (!result.best_move.has_value()) {
    return "none";
  }
  return move_to_string(*result.best_move);
}

std::optional<int> arena_disc_diff_for(std::span<const GameResult> results,
                                       std::string_view board_id,
                                       std::string_view side_assignment) {
  for (const GameResult& result : results) {
    if (result.board_id == board_id && result.side_assignment == side_assignment) {
      return result.candidate_disc_diff;
    }
  }
  return std::nullopt;
}

std::optional<int> exact_score_after_root_move(board_core::Position root, board_core::Move move) {
  board_core::MoveDelta delta{};
  bool applied = false;
  if (move.kind == board_core::MoveKind::pass) {
    applied = board_core::apply_pass(&root, &delta);
  } else {
    applied = board_core::apply_move(&root, move, &delta);
  }
  if (!applied) {
    return std::nullopt;
  }
  const search::SearchResult child = search::solve_exact_endgame(root);
  return -child.score;
}

std::uint8_t runtime_phase_for_position(board_core::Position position) noexcept {
  const int discs = disc_count(board_core::occupied(position));
  return phase_by_disc_count_13()[static_cast<std::size_t>(discs)];
}

bool is_side_to_move_perspective(std::string_view text) noexcept {
  return text.empty() || text == "side-to-move" || text == "side_to_move";
}

FeatureActivationReport feature_activation_for(std::span<const PositionRow> rows,
                                               const LoadedEvaluator& evaluator,
                                               const LoadedEvaluator& comparison) {
  std::set<std::string> comparison_families;
  for (const eval::PatternFeatureTable& table : comparison.feature_set.tables) {
    comparison_families.insert(table.pattern_id);
  }

  FeatureActivationReport report{
      .pattern_set_id = evaluator.runtime_pattern_set_id,
  };
  for (const eval::PatternFeatureTable& table : evaluator.feature_set.tables) {
    if (!comparison_families.contains(table.pattern_id)) {
      ++report.added_family_count;
    }
  }

  for (const PositionRow& row : rows) {
    const std::optional<board_core::Position> position =
        position_from_relative_board(row.board_text);
    if (!position.has_value()) {
      continue;
    }
    for (const eval::PatternFeatureTable& table : evaluator.feature_set.tables) {
      FeatureFamilyActivation& activation = report.by_family[table.pattern_id];
      for (const std::vector<board_core::Square>& squares : table.instances) {
        const std::uint32_t index = eval::ternary_pattern_index(*position, squares);
        ++activation.instances_evaluated;
        activation.distinct_indices.insert(index);
        if (index == 0) {
          ++activation.all_empty_activations;
        } else {
          ++activation.non_empty_activations;
        }
      }
    }
  }

  for (const auto& [family, activation] : report.by_family) {
    if (!comparison_families.contains(family) && activation.non_empty_activations > 0) {
      ++report.added_family_active_count;
    }
  }
  return report;
}

DepthDiagnostic compare_roots_at_depth(std::span<const PositionRow> rows,
                                       const LoadedEvaluator& candidate,
                                       const LoadedEvaluator& baseline, search::Depth depth) {
  DepthDiagnostic diagnostic;
  std::vector<int> search_diffs;
  search_diffs.reserve(rows.size());
  for (const PositionRow& row : rows) {
    const std::optional<board_core::Position> position =
        position_from_relative_board(row.board_text);
    if (!position.has_value()) {
      continue;
    }
    const search::SearchResult candidate_search =
        search::search_fixed_depth(*position, candidate.evaluator, depth);
    const search::SearchResult baseline_search =
        search::search_fixed_depth(*position, baseline.evaluator, depth);
    if (best_move_to_string(candidate_search) != best_move_to_string(baseline_search)) {
      ++diagnostic.best_move_disagreement_count;
    }
    search_diffs.push_back(candidate_search.score - baseline_search.score);
  }
  if (!rows.empty()) {
    diagnostic.best_move_disagreement_rate =
        static_cast<double>(diagnostic.best_move_disagreement_count) /
        static_cast<double>(rows.size());
  }
  diagnostic.search_score_diff_mean = average_or_zero(search_diffs);
  diagnostic.search_score_diff_abs_mean = absolute_average_or_zero(search_diffs);
  return diagnostic;
}

DiagnosticsReport build_diagnostics(const Args& args, const LoadedEvaluator& candidate,
                                    const LoadedEvaluator& baseline,
                                    const PositionLoadResult& positions,
                                    std::span<const GameResult> arena_results,
                                    const ArenaStats& arena_stats,
                                    const std::map<int, ArenaStats>& depth_arena_stats) {
  DiagnosticsReport report;
  report.selected_positions = static_cast<int>(positions.selected.size());
  report.rows.reserve(positions.selected.size());

  std::vector<int> static_diffs;
  std::vector<int> search_diffs;
  int candidate_static_abs_sum = 0;
  int baseline_static_abs_sum = 0;
  int exact_attempts = 0;
  constexpr int kMaxExactAdjudicationEmpties = 14;

  for (const PositionRow& row : positions.selected) {
    PositionDiagnostics diagnostics{
        .board_id = row.board_id,
        .split = row.split,
        .label_kind = row.label_kind,
        .label_perspective = row.label_perspective,
        .label_score_side_to_move = row.label_score_side_to_move,
        .empty_count = row.empty_count,
        .phase = row.phase,
        .runtime_phase = row.phase,
        .arena_candidate_side_to_move_disc_diff =
            arena_disc_diff_for(arena_results, row.board_id, "candidate_side_to_move"),
        .arena_candidate_opponent_disc_diff =
            arena_disc_diff_for(arena_results, row.board_id, "candidate_opponent"),
    };

    if (!is_side_to_move_perspective(row.label_perspective)) {
      ++report.label_perspective_mismatch_count;
    }

    const std::optional<board_core::Position> position =
        position_from_relative_board(row.board_text);
    if (!position.has_value()) {
      report.rows.push_back(std::move(diagnostics));
      continue;
    }
    diagnostics.runtime_phase = runtime_phase_for_position(*position);
    if (diagnostics.runtime_phase != row.phase) {
      ++report.phase_mapping_mismatch_count;
    }

    if (args.compare_static_scores) {
      const int candidate_score = candidate.evaluator.evaluate(*position);
      const int baseline_score = baseline.evaluator.evaluate(*position);
      const int diff = candidate_score - baseline_score;
      diagnostics.candidate_static_score = candidate_score;
      diagnostics.baseline_static_score = baseline_score;
      diagnostics.static_score_diff = diff;
      update_range(&report.candidate_static_score_range, candidate_score);
      update_range(&report.baseline_static_score_range, baseline_score);
      static_diffs.push_back(diff);
      candidate_static_abs_sum += std::abs(candidate_score);
      baseline_static_abs_sum += std::abs(baseline_score);
      if (diff == 0) {
        ++report.static_score_diff_zero_count;
      } else {
        ++report.static_score_diff_nonzero_count;
      }
      if (row.label_score_side_to_move.has_value() && *row.label_score_side_to_move != 0 &&
          candidate_score != 0) {
        ++report.labeled_static_sign_checked_count;
        if (sign_of(*row.label_score_side_to_move) == sign_of(candidate_score)) {
          ++report.candidate_static_label_sign_agreement_count;
        } else {
          ++report.candidate_static_label_sign_opposition_count;
        }
      }
    }

    if (args.compare_best_moves) {
      const search::SearchResult candidate_search =
          search::search_fixed_depth(*position, candidate.evaluator, args.depth);
      const search::SearchResult baseline_search =
          search::search_fixed_depth(*position, baseline.evaluator, args.depth);
      diagnostics.candidate_best_move = best_move_to_string(candidate_search);
      diagnostics.baseline_best_move = best_move_to_string(baseline_search);
      diagnostics.candidate_search_score = candidate_search.score;
      diagnostics.baseline_search_score = baseline_search.score;
      diagnostics.search_score_diff = candidate_search.score - baseline_search.score;
      diagnostics.best_moves_differ =
          diagnostics.candidate_best_move != diagnostics.baseline_best_move;
      search_diffs.push_back(*diagnostics.search_score_diff);
      if (diagnostics.best_moves_differ) {
        ++report.best_move_disagreement_count;
      }
      if (diagnostics.static_score_diff.has_value()) {
        const bool agrees =
            sign_of(*diagnostics.static_score_diff) == sign_of(*diagnostics.search_score_diff);
        diagnostics.static_score_order_agrees_with_search = agrees;
        ++report.static_search_order_checked_count;
        if (agrees) {
          ++report.static_search_order_agreement_count;
        }
      }

      if (args.exact_adjudicate_disagreements && diagnostics.best_moves_differ &&
          exact_attempts < args.max_disagreements &&
          row.empty_count <= kMaxExactAdjudicationEmpties &&
          candidate_search.best_move.has_value() && baseline_search.best_move.has_value()) {
        ++exact_attempts;
        const std::optional<int> candidate_exact =
            exact_score_after_root_move(*position, *candidate_search.best_move);
        const std::optional<int> baseline_exact =
            exact_score_after_root_move(*position, *baseline_search.best_move);
        if (candidate_exact.has_value() && baseline_exact.has_value()) {
          diagnostics.exact_adjudicated = true;
          diagnostics.candidate_move_exact_score = *candidate_exact;
          diagnostics.baseline_move_exact_score = *baseline_exact;
          ++report.disagreement_exact_checked_count;
          if (*candidate_exact > *baseline_exact) {
            diagnostics.disagreement_adjudication = "candidate_better";
            ++report.candidate_better_on_disagreements;
          } else if (*candidate_exact < *baseline_exact) {
            diagnostics.disagreement_adjudication = "baseline_better";
            ++report.baseline_better_on_disagreements;
          } else {
            diagnostics.disagreement_adjudication = "draw";
            ++report.draw_on_disagreements;
          }
        } else {
          diagnostics.disagreement_adjudication = "exact_failed";
        }
      } else if (diagnostics.best_moves_differ) {
        diagnostics.disagreement_adjudication = "not_checked";
      }
    }

    report.rows.push_back(std::move(diagnostics));
  }

  report.static_score_diff_mean = average_or_zero(static_diffs);
  report.static_score_diff_abs_mean = absolute_average_or_zero(static_diffs);
  report.static_score_diff_abs_median = absolute_median_or_zero(static_diffs);
  if (!static_diffs.empty()) {
    report.candidate_static_abs_mean =
        static_cast<double>(candidate_static_abs_sum) / static_cast<double>(static_diffs.size());
    report.baseline_static_abs_mean =
        static_cast<double>(baseline_static_abs_sum) / static_cast<double>(static_diffs.size());
  }
  if (!positions.selected.empty()) {
    report.best_move_disagreement_rate = static_cast<double>(report.best_move_disagreement_count) /
                                         static_cast<double>(positions.selected.size());
  }
  report.search_score_diff_mean = average_or_zero(search_diffs);
  report.search_score_diff_abs_mean = absolute_average_or_zero(search_diffs);
  if (report.static_search_order_checked_count > 0) {
    report.static_search_order_agreement_rate =
        static_cast<double>(report.static_search_order_agreement_count) /
        static_cast<double>(report.static_search_order_checked_count);
  }

  report.candidate_feature_activation =
      feature_activation_for(positions.selected, candidate, baseline);
  report.baseline_feature_activation =
      feature_activation_for(positions.selected, baseline, candidate);

  for (const auto& [depth, stats] : depth_arena_stats) {
    DepthDiagnostic diagnostic =
        compare_roots_at_depth(positions.selected, candidate, baseline, depth);
    diagnostic.arena_stats = stats;
    report.results_by_depth[depth] = std::move(diagnostic);
  }

  report.notes = {
      "diagnostics compare selected root positions before and after fixed-depth search",
      "static and search scores are side-to-move relative",
      "arena disc diffs are reported from candidate perspective",
      "feature activation counts use runtime feature geometry and do not inspect learned weights",
      "local diagnostic only; not Elo, not self-play, not production strength, not a publication "
      "gate",
  };

  const bool same_artifact = candidate.artifact_checksum == baseline.artifact_checksum &&
                             candidate.runtime_pattern_set_id == baseline.runtime_pattern_set_id;
  if (same_artifact && args.side_swap &&
      (std::abs(arena_stats.candidate_score_rate - 0.5) > 0.000001 ||
       std::abs(arena_stats.average_disc_diff) > 0.000001)) {
    report.suspicious_sign_or_perspective_indicators.push_back(
        "identical artifact side-swapped arena did not tie exactly");
  }
  if (!same_artifact && args.compare_static_scores && report.selected_positions > 0 &&
      report.static_score_diff_zero_count * 100 >= report.selected_positions * 95) {
    report.suspicious_sign_or_perspective_indicators.push_back(
        "candidate/baseline static score diff is zero for at least 95% of selected positions");
  }
  if (args.compare_best_moves && report.best_move_disagreement_rate <= 0.01 &&
      report.static_score_diff_nonzero_count > 0) {
    report.suspicious_sign_or_perspective_indicators.push_back(
        "static score differences exist but best-move disagreement is near zero");
  }
  if (args.compare_best_moves && args.depth == 3 && report.static_score_diff_nonzero_count > 0 &&
      report.best_move_disagreement_count == 0) {
    report.suspicious_sign_or_perspective_indicators.push_back(
        "static score differences exist but do not affect move selection at depth 3");
  }
  if (report.labeled_static_sign_checked_count > 0 &&
      report.candidate_static_label_sign_opposition_count * 10 >=
          report.labeled_static_sign_checked_count * 6) {
    report.suspicious_sign_or_perspective_indicators.push_back(
        "candidate static score is opposite sign from side-to-move labels on most checked rows");
  }
  const auto side_to_move_bucket = arena_stats.by_side_assignment.find("candidate_side_to_move");
  const auto opponent_bucket = arena_stats.by_side_assignment.find("candidate_opponent");
  if (!same_artifact && side_to_move_bucket != arena_stats.by_side_assignment.end() &&
      opponent_bucket != arena_stats.by_side_assignment.end()) {
    const double stm_rate = side_to_move_bucket->second.games == 0
                                ? 0.0
                                : side_to_move_bucket->second.candidate_score /
                                      static_cast<double>(side_to_move_bucket->second.games);
    const double opponent_rate = opponent_bucket->second.games == 0
                                     ? 0.0
                                     : opponent_bucket->second.candidate_score /
                                           static_cast<double>(opponent_bucket->second.games);
    if (std::abs(stm_rate - opponent_rate) > 0.10) {
      report.suspicious_sign_or_perspective_indicators.push_back(
          "candidate side-to-move and candidate opponent buckets differ by more than 0.10 score "
          "rate");
    }
  }
  if (report.phase_mapping_mismatch_count > 0) {
    report.suspicious_sign_or_perspective_indicators.push_back(
        "runtime phase mapping differs from normalized row phase on selected positions");
  }
  if (report.label_perspective_mismatch_count > 0) {
    report.suspicious_sign_or_perspective_indicators.push_back(
        "selected rows contain labels whose perspective is not side-to-move");
  }
  if (args.compare_static_scores && report.candidate_static_abs_mean > 0.0 &&
      report.baseline_static_abs_mean > 0.0) {
    const double ratio = report.candidate_static_abs_mean / report.baseline_static_abs_mean;
    if (ratio > 10.0 || ratio < 0.1) {
      report.suspicious_sign_or_perspective_indicators.push_back(
          "candidate and baseline static score magnitudes differ by more than 10x");
    }
  }
  if (report.candidate_feature_activation.added_family_count > 0 &&
      report.candidate_feature_activation.added_family_active_count == 0) {
    report.suspicious_sign_or_perspective_indicators.push_back(
        "candidate-added feature families did not activate on selected positions");
  }
  if (report.results_by_depth.size() >= 2) {
    double min_rate = 1.0;
    double max_rate = 0.0;
    for (const auto& [depth, diagnostic] : report.results_by_depth) {
      min_rate = std::min(min_rate, diagnostic.arena_stats.candidate_score_rate);
      max_rate = std::max(max_rate, diagnostic.arena_stats.candidate_score_rate);
    }
    if (max_rate - min_rate > 0.04) {
      report.suspicious_sign_or_perspective_indicators.push_back(
          "depth sweep score rates differ by more than 0.04");
    }
  }
  return report;
}

void write_json_string(std::ostream& output, std::string_view text) {
  output << '"' << arena::json_escape(text) << '"';
}

void write_notes(std::ostream& output) {
  output << "[\n";
  const std::array<std::string_view, 6> notes{
      "local artifact-vs-artifact late-game diagnostic",
      "not an Elo result",
      "not a production strength claim",
      "not self-play",
      "not a publication gate",
      "generated arena reports/logs must not be committed",
  };
  for (std::size_t index = 0; index < notes.size(); ++index) {
    output << "    ";
    write_json_string(output, notes[index]);
    output << (index + 1 == notes.size() ? "\n" : ",\n");
  }
  output << "  ]";
}

void write_bucket(std::ostream& output, const BucketStats& bucket) {
  const double score_rate =
      bucket.games == 0 ? 0.0 : bucket.candidate_score / static_cast<double>(bucket.games);
  const double average_disc_diff =
      bucket.games == 0 ? 0.0 : static_cast<double>(bucket.disc_diff_sum) / bucket.games;
  output << "{";
  output << "\"games\": " << bucket.games;
  output << ", \"candidate_wins\": " << bucket.candidate_wins;
  output << ", \"baseline_wins\": " << bucket.baseline_wins;
  output << ", \"draws\": " << bucket.draws;
  output << ", \"candidate_score\": " << bucket.candidate_score;
  output << ", \"candidate_score_rate\": " << score_rate;
  output << ", \"average_disc_diff_candidate_perspective\": " << average_disc_diff;
  output << ", \"illegal_or_failed_games\": " << bucket.illegal_or_failed_games;
  output << "}";
}

void write_bucket_map(std::ostream& output, const std::map<std::string, BucketStats>& buckets) {
  output << "{";
  bool first = true;
  for (const auto& [key, bucket] : buckets) {
    if (!first) {
      output << ", ";
    }
    first = false;
    write_json_string(output, key);
    output << ": ";
    write_bucket(output, bucket);
  }
  output << "}";
}

void write_histogram(std::ostream& output, const std::map<int, int>& histogram) {
  output << "{";
  bool first = true;
  for (const auto& [disc_diff, count] : histogram) {
    if (!first) {
      output << ", ";
    }
    first = false;
    write_json_string(output, std::to_string(disc_diff));
    output << ": " << count;
  }
  output << "}";
}

void write_optional_int(std::ostream& output, const std::optional<int>& value) {
  if (value.has_value()) {
    output << *value;
  } else {
    output << "null";
  }
}

void write_optional_bool(std::ostream& output, const std::optional<bool>& value) {
  if (value.has_value()) {
    output << (*value ? "true" : "false");
  } else {
    output << "null";
  }
}

void write_string_array(std::ostream& output, std::span<const std::string> values) {
  output << "[";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) {
      output << ", ";
    }
    write_json_string(output, values[index]);
  }
  output << "]";
}

void write_score_range(std::ostream& output, const ScoreRange& range) {
  if (!range.has_value) {
    output << "null";
    return;
  }
  output << "{\"min\": " << range.min << ", \"max\": " << range.max << "}";
}

void write_feature_activation_report(std::ostream& output, const FeatureActivationReport& report) {
  output << "{";
  output << "\"pattern_set_id\": ";
  write_json_string(output, report.pattern_set_id);
  output << ", \"added_family_count\": " << report.added_family_count;
  output << ", \"added_family_active_count\": " << report.added_family_active_count;
  output << ", \"added_family_inactive_count\": "
         << report.added_family_count - report.added_family_active_count;
  output << ", \"by_family\": {";
  bool first = true;
  for (const auto& [family, activation] : report.by_family) {
    if (!first) {
      output << ", ";
    }
    first = false;
    write_json_string(output, family);
    output << ": {";
    output << "\"instances_evaluated\": " << activation.instances_evaluated;
    output << ", \"non_empty_activations\": " << activation.non_empty_activations;
    output << ", \"all_empty_activations\": " << activation.all_empty_activations;
    output << ", \"distinct_ternary_indices\": " << activation.distinct_indices.size();
    output << "}";
  }
  output << "}}";
}

void write_depth_diagnostics(std::ostream& output,
                             const std::map<int, DepthDiagnostic>& diagnostics) {
  output << "{";
  bool first = true;
  for (const auto& [depth, diagnostic] : diagnostics) {
    if (!first) {
      output << ", ";
    }
    first = false;
    write_json_string(output, std::to_string(depth));
    output << ": {";
    output << "\"games_played\": " << diagnostic.arena_stats.games_played;
    output << ", \"candidate_wins\": " << diagnostic.arena_stats.candidate_wins;
    output << ", \"baseline_wins\": " << diagnostic.arena_stats.baseline_wins;
    output << ", \"draws\": " << diagnostic.arena_stats.draws;
    output << ", \"candidate_score_rate\": " << diagnostic.arena_stats.candidate_score_rate;
    output << ", \"average_disc_diff_candidate_perspective\": "
           << diagnostic.arena_stats.average_disc_diff;
    output << ", \"best_move_disagreement_count\": " << diagnostic.best_move_disagreement_count;
    output << ", \"best_move_disagreement_rate\": " << diagnostic.best_move_disagreement_rate;
    output << ", \"search_score_diff_mean\": " << diagnostic.search_score_diff_mean;
    output << ", \"search_score_diff_abs_mean\": " << diagnostic.search_score_diff_abs_mean;
    output << "}";
  }
  output << "}";
}

void write_position_diagnostics(std::ostream& output, std::span<const PositionDiagnostics> rows) {
  output << "[";
  for (std::size_t index = 0; index < rows.size(); ++index) {
    if (index != 0) {
      output << ", ";
    }
    const PositionDiagnostics& row = rows[index];
    output << "{";
    output << "\"board_id\": ";
    write_json_string(output, row.board_id);
    output << ", \"split\": ";
    write_json_string(output, row.split);
    output << ", \"empty_count\": " << row.empty_count;
    output << ", \"phase\": " << row.phase;
    output << ", \"runtime_phase\": " << row.runtime_phase;
    output << ", \"label_kind\": ";
    write_json_string(output, row.label_kind);
    output << ", \"label_perspective\": ";
    write_json_string(output, row.label_perspective);
    output << ", \"label_score_side_to_move\": ";
    write_optional_int(output, row.label_score_side_to_move);
    output << ", \"candidate_static_score\": ";
    write_optional_int(output, row.candidate_static_score);
    output << ", \"baseline_static_score\": ";
    write_optional_int(output, row.baseline_static_score);
    output << ", \"static_score_diff\": ";
    write_optional_int(output, row.static_score_diff);
    output << ", \"candidate_best_move\": ";
    write_json_string(output, row.candidate_best_move);
    output << ", \"baseline_best_move\": ";
    write_json_string(output, row.baseline_best_move);
    output << ", \"candidate_search_score\": ";
    write_optional_int(output, row.candidate_search_score);
    output << ", \"baseline_search_score\": ";
    write_optional_int(output, row.baseline_search_score);
    output << ", \"search_score_diff\": ";
    write_optional_int(output, row.search_score_diff);
    output << ", \"best_moves_differ\": " << (row.best_moves_differ ? "true" : "false");
    output << ", \"static_score_order_agrees_with_search_score_order\": ";
    write_optional_bool(output, row.static_score_order_agrees_with_search);
    output << ", \"arena_candidate_side_to_move_disc_diff\": ";
    write_optional_int(output, row.arena_candidate_side_to_move_disc_diff);
    output << ", \"arena_candidate_opponent_disc_diff\": ";
    write_optional_int(output, row.arena_candidate_opponent_disc_diff);
    output << ", \"exact_adjudicated\": " << (row.exact_adjudicated ? "true" : "false");
    output << ", \"candidate_move_exact_score\": ";
    write_optional_int(output, row.candidate_move_exact_score);
    output << ", \"baseline_move_exact_score\": ";
    write_optional_int(output, row.baseline_move_exact_score);
    output << ", \"disagreement_adjudication\": ";
    write_json_string(output, row.disagreement_adjudication);
    output << "}";
  }
  output << "]";
}

bool write_diagnostics_report(const Args& args, const LoadedEvaluator& candidate,
                              const LoadedEvaluator& baseline, const PositionLoadResult& positions,
                              const ArenaStats& arena_stats, const DiagnosticsReport& diagnostics,
                              std::string_view positions_checksum, std::string_view command) {
  const std::filesystem::path parent =
      std::filesystem::path(args.diagnostics_out_path).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream output(args.diagnostics_out_path);
  if (!output) {
    std::cerr << "cannot write diagnostics report: " << args.diagnostics_out_path << '\n';
    return false;
  }
  output << std::fixed << std::setprecision(6);
  output << "{\n";
  output << "  \"schema_version\": 1,\n";
  output << "  \"diagnostics_version\": \"pattern-artifact-arena-diagnostics-v1\",\n";
  output << "  \"positions_tsv\": ";
  write_json_string(output, args.positions_tsv_path);
  output << ",\n";
  output << "  \"positions_tsv_checksum\": ";
  write_json_string(output, positions_checksum);
  output << ",\n";
  output << "  \"candidate_name\": ";
  write_json_string(output, candidate.name);
  output << ",\n";
  output << "  \"candidate_manifest_pattern_set_id\": ";
  write_json_string(output, candidate.manifest_pattern_set_id);
  output << ",\n";
  output << "  \"candidate_runtime_pattern_set_id\": ";
  write_json_string(output, candidate.runtime_pattern_set_id);
  output << ",\n";
  output << "  \"candidate_artifact_checksum\": ";
  write_json_string(output, candidate.artifact_checksum);
  output << ",\n";
  output << "  \"baseline_name\": ";
  write_json_string(output, baseline.name);
  output << ",\n";
  output << "  \"baseline_manifest_pattern_set_id\": ";
  write_json_string(output, baseline.manifest_pattern_set_id);
  output << ",\n";
  output << "  \"baseline_runtime_pattern_set_id\": ";
  write_json_string(output, baseline.runtime_pattern_set_id);
  output << ",\n";
  output << "  \"baseline_artifact_checksum\": ";
  write_json_string(output, baseline.artifact_checksum);
  output << ",\n";
  output << "  \"max_empty\": " << args.max_empty << ",\n";
  if (args.max_positions > 0) {
    output << "  \"max_positions\": " << args.max_positions << ",\n";
  } else {
    output << "  \"max_positions\": null,\n";
  }
  output << "  \"seed\": " << args.seed << ",\n";
  output << "  \"side_swap\": " << (args.side_swap ? "true" : "false") << ",\n";
  output << "  \"search_depth\": " << args.depth << ",\n";
  output << "  \"compare_static_scores\": " << (args.compare_static_scores ? "true" : "false")
         << ",\n";
  output << "  \"compare_best_moves\": " << (args.compare_best_moves ? "true" : "false") << ",\n";
  output << "  \"exact_adjudicate_disagreements\": "
         << (args.exact_adjudicate_disagreements ? "true" : "false") << ",\n";
  output << "  \"max_disagreements\": " << args.max_disagreements << ",\n";
  const bool same_artifact = candidate.artifact_checksum == baseline.artifact_checksum &&
                             candidate.runtime_pattern_set_id == baseline.runtime_pattern_set_id;
  output << "  \"sanity_checks\": {\"same_artifact_mirror\": " << (same_artifact ? "true" : "false")
         << ", \"same_artifact_side_swapped_tie\": "
         << (same_artifact && args.side_swap &&
                     std::abs(arena_stats.candidate_score_rate - 0.5) <= 0.000001 &&
                     std::abs(arena_stats.average_disc_diff) <= 0.000001
                 ? "true"
                 : "false")
         << "},\n";
  output << "  \"input_rows\": " << positions.input_rows << ",\n";
  output << "  \"eligible_rows\": " << positions.eligible_rows << ",\n";
  output << "  \"duplicate_board_id_rows\": " << positions.duplicate_board_id_rows << ",\n";
  output << "  \"selected_positions\": " << diagnostics.selected_positions << ",\n";
  output << "  \"phase_mapping_mismatch_count\": " << diagnostics.phase_mapping_mismatch_count
         << ",\n";
  output << "  \"label_perspective_mismatch_count\": "
         << diagnostics.label_perspective_mismatch_count << ",\n";
  output << "  \"static_score_diff_mean\": " << diagnostics.static_score_diff_mean << ",\n";
  output << "  \"static_score_diff_abs_mean\": " << diagnostics.static_score_diff_abs_mean << ",\n";
  output << "  \"static_score_diff_abs_median\": " << diagnostics.static_score_diff_abs_median
         << ",\n";
  output << "  \"static_score_diff_zero_count\": " << diagnostics.static_score_diff_zero_count
         << ",\n";
  output << "  \"static_score_diff_nonzero_count\": " << diagnostics.static_score_diff_nonzero_count
         << ",\n";
  output << "  \"candidate_static_score_range\": ";
  write_score_range(output, diagnostics.candidate_static_score_range);
  output << ",\n";
  output << "  \"baseline_static_score_range\": ";
  write_score_range(output, diagnostics.baseline_static_score_range);
  output << ",\n";
  output << "  \"candidate_static_abs_mean\": " << diagnostics.candidate_static_abs_mean << ",\n";
  output << "  \"baseline_static_abs_mean\": " << diagnostics.baseline_static_abs_mean << ",\n";
  output << "  \"labeled_static_sign_checked_count\": "
         << diagnostics.labeled_static_sign_checked_count << ",\n";
  output << "  \"candidate_static_label_sign_agreement_count\": "
         << diagnostics.candidate_static_label_sign_agreement_count << ",\n";
  output << "  \"candidate_static_label_sign_opposition_count\": "
         << diagnostics.candidate_static_label_sign_opposition_count << ",\n";
  output << "  \"best_move_disagreement_count\": " << diagnostics.best_move_disagreement_count
         << ",\n";
  output << "  \"best_move_disagreement_rate\": " << diagnostics.best_move_disagreement_rate
         << ",\n";
  output << "  \"static_search_order_checked_count\": "
         << diagnostics.static_search_order_checked_count << ",\n";
  output << "  \"static_search_order_agreement_count\": "
         << diagnostics.static_search_order_agreement_count << ",\n";
  output << "  \"static_search_order_agreement_rate\": "
         << diagnostics.static_search_order_agreement_rate << ",\n";
  output << "  \"search_score_diff_mean\": " << diagnostics.search_score_diff_mean << ",\n";
  output << "  \"search_score_diff_abs_mean\": " << diagnostics.search_score_diff_abs_mean << ",\n";
  output << "  \"candidate_better_by_arena_count\": " << arena_stats.candidate_wins << ",\n";
  output << "  \"baseline_better_by_arena_count\": " << arena_stats.baseline_wins << ",\n";
  output << "  \"draw_by_arena_count\": " << arena_stats.draws << ",\n";
  output << "  \"disagreement_exact_checked_count\": "
         << diagnostics.disagreement_exact_checked_count << ",\n";
  output << "  \"candidate_better_on_disagreements\": "
         << diagnostics.candidate_better_on_disagreements << ",\n";
  output << "  \"baseline_better_on_disagreements\": "
         << diagnostics.baseline_better_on_disagreements << ",\n";
  output << "  \"draw_on_disagreements\": " << diagnostics.draw_on_disagreements << ",\n";
  output << "  \"results_by_empty_count\": ";
  write_bucket_map(output, arena_stats.by_empty_count);
  output << ",\n";
  output << "  \"results_by_phase\": ";
  write_bucket_map(output, arena_stats.by_phase);
  output << ",\n";
  output << "  \"results_by_split\": ";
  write_bucket_map(output, arena_stats.by_split);
  output << ",\n";
  output << "  \"results_by_side_assignment\": ";
  write_bucket_map(output, arena_stats.by_side_assignment);
  output << ",\n";
  output << "  \"results_by_depth\": ";
  write_depth_diagnostics(output, diagnostics.results_by_depth);
  output << ",\n";
  output << "  \"feature_activation\": {\"candidate\": ";
  write_feature_activation_report(output, diagnostics.candidate_feature_activation);
  output << ", \"baseline\": ";
  write_feature_activation_report(output, diagnostics.baseline_feature_activation);
  output << "},\n";
  output << "  \"suspicious_sign_or_perspective_indicators\": ";
  write_string_array(output, diagnostics.suspicious_sign_or_perspective_indicators);
  output << ",\n";
  output << "  \"notes\": ";
  write_string_array(output, diagnostics.notes);
  output << ",\n";
  output << "  \"position_diagnostics\": ";
  write_position_diagnostics(output, diagnostics.rows);
  output << ",\n";
  output << "  \"command\": ";
  write_json_string(output, command);
  output << "\n";
  output << "}\n";
  return true;
}

std::string
make_report_without_checksum(const Args& args, const LoadedEvaluator& candidate,
                             const LoadedEvaluator& baseline, const PositionLoadResult& positions,
                             const ArenaStats& stats, std::string_view positions_checksum,
                             std::string_view command, double wall_time_sec, double games_per_sec) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(6);
  output << "{\n";
  output << "  \"schema_version\": 1,\n";
  output << "  \"arena_version\": ";
  write_json_string(output, kArenaVersion);
  output << ",\n";
  output << "  \"positions_tsv\": ";
  write_json_string(output, args.positions_tsv_path);
  output << ",\n";
  output << "  \"positions_tsv_checksum\": ";
  write_json_string(output, positions_checksum);
  output << ",\n";
  output << "  \"candidate_name\": ";
  write_json_string(output, candidate.name);
  output << ",\n";
  output << "  \"candidate_weights_path\": ";
  write_json_string(output, candidate.weights_path);
  output << ",\n";
  output << "  \"candidate_manifest_path\": ";
  write_json_string(output, candidate.manifest_path);
  output << ",\n";
  output << "  \"candidate_artifact_checksum\": ";
  write_json_string(output, candidate.artifact_checksum);
  output << ",\n";
  output << "  \"baseline_name\": ";
  write_json_string(output, baseline.name);
  output << ",\n";
  output << "  \"baseline_weights_path\": ";
  write_json_string(output, baseline.weights_path);
  output << ",\n";
  output << "  \"baseline_manifest_path\": ";
  write_json_string(output, baseline.manifest_path);
  output << ",\n";
  output << "  \"baseline_artifact_checksum\": ";
  write_json_string(output, baseline.artifact_checksum);
  output << ",\n";
  output << "  \"max_empty\": " << args.max_empty << ",\n";
  if (args.max_positions > 0) {
    output << "  \"max_positions\": " << args.max_positions << ",\n";
  } else {
    output << "  \"max_positions\": null,\n";
  }
  output << "  \"seed\": " << args.seed << ",\n";
  output << "  \"side_swap\": " << (args.side_swap ? "true" : "false") << ",\n";
  output << "  \"search_depth\": " << args.depth << ",\n";
  output << "  \"input_rows\": " << positions.input_rows << ",\n";
  output << "  \"eligible_rows\": " << positions.eligible_rows << ",\n";
  output << "  \"duplicate_board_id_rows\": " << positions.duplicate_board_id_rows << ",\n";
  output << "  \"selected_positions\": " << positions.selected.size() << ",\n";
  output << "  \"games_played\": " << stats.games_played << ",\n";
  output << "  \"candidate_wins\": " << stats.candidate_wins << ",\n";
  output << "  \"baseline_wins\": " << stats.baseline_wins << ",\n";
  output << "  \"draws\": " << stats.draws << ",\n";
  output << "  \"candidate_score\": " << stats.candidate_score << ",\n";
  output << "  \"baseline_score\": " << stats.baseline_score << ",\n";
  output << "  \"candidate_score_rate\": " << stats.candidate_score_rate << ",\n";
  output << "  \"candidate_score_rate_interval_95\": {\"low\": " << stats.score_interval_low
         << ", \"high\": " << stats.score_interval_high
         << ", \"method\": \"normal approximation over game scores\"},\n";
  output << "  \"average_disc_diff_candidate_perspective\": " << stats.average_disc_diff << ",\n";
  output << "  \"median_disc_diff_candidate_perspective\": " << stats.median_disc_diff << ",\n";
  output << "  \"disc_diff_histogram\": ";
  write_histogram(output, stats.disc_diff_histogram);
  output << ",\n";
  output << "  \"results_by_empty_count\": ";
  write_bucket_map(output, stats.by_empty_count);
  output << ",\n";
  output << "  \"results_by_phase\": ";
  write_bucket_map(output, stats.by_phase);
  output << ",\n";
  output << "  \"results_by_split\": ";
  write_bucket_map(output, stats.by_split);
  output << ",\n";
  output << "  \"results_by_side_assignment\": ";
  write_bucket_map(output, stats.by_side_assignment);
  output << ",\n";
  output << "  \"illegal_or_failed_games\": " << stats.illegal_or_failed_games << ",\n";
  output << "  \"wall_time_sec\": " << wall_time_sec << ",\n";
  output << "  \"games_per_sec\": " << games_per_sec << ",\n";
  output << "  \"command\": ";
  write_json_string(output, command);
  output << ",\n";
  output << "  \"notes\": ";
  write_notes(output);
  output << ",\n";
  return output.str();
}

bool write_report(const Args& args, const LoadedEvaluator& candidate,
                  const LoadedEvaluator& baseline, const PositionLoadResult& positions,
                  const ArenaStats& stats, std::string_view positions_checksum,
                  std::string_view command, double wall_time_sec, double games_per_sec) {
  const std::string report_without_checksum =
      make_report_without_checksum(args, candidate, baseline, positions, stats, positions_checksum,
                                   command, wall_time_sec, games_per_sec);
  const std::string report_checksum = checksum_for(report_without_checksum);
  std::ofstream output(args.report_out_path);
  if (!output) {
    std::cerr << "cannot write report: " << args.report_out_path << '\n';
    return false;
  }
  output << report_without_checksum;
  output << "  \"checksum\": ";
  write_json_string(output, report_checksum);
  output << "\n}\n";
  return true;
}

std::string interpretation_for_rate(double rate, int games) {
  if (games < 500) {
    return "N is small; interpretation is weak";
  }
  if (rate > 0.55) {
    return "strong local positive signal";
  }
  if (rate >= 0.52) {
    return "weak positive signal";
  }
  if (rate >= 0.48) {
    return "inconclusive";
  }
  return "negative signal";
}

void write_summary_bucket_table(std::ostream& output,
                                const std::map<std::string, BucketStats>& buckets) {
  output << "| Bucket | Games | W-L-D | Score rate | Avg disc diff |\n";
  output << "| --- | ---: | ---: | ---: | ---: |\n";
  for (const auto& [key, bucket] : buckets) {
    const double score_rate =
        bucket.games == 0 ? 0.0 : bucket.candidate_score / static_cast<double>(bucket.games);
    const double average_disc_diff =
        bucket.games == 0 ? 0.0 : static_cast<double>(bucket.disc_diff_sum) / bucket.games;
    output << "| `" << key << "` | " << bucket.games << " | " << bucket.candidate_wins << "-"
           << bucket.baseline_wins << "-" << bucket.draws << " | " << std::fixed
           << std::setprecision(6) << score_rate << " | " << average_disc_diff << " |\n";
  }
}

bool write_summary(const Args& args, const LoadedEvaluator& candidate,
                   const LoadedEvaluator& baseline, const PositionLoadResult& positions,
                   const ArenaStats& stats, std::string_view command) {
  std::ofstream output(args.summary_out_path);
  if (!output) {
    std::cerr << "cannot write summary: " << args.summary_out_path << '\n';
    return false;
  }

  output << "# Pattern Artifact Arena Summary\n\n";
  output << "Candidate: `" << candidate.name << "`\n\n";
  output << "Baseline: `" << baseline.name << "`\n\n";
  output << "Settings: max-empty `" << args.max_empty << "`, max-positions `";
  if (args.max_positions > 0) {
    output << args.max_positions;
  } else {
    output << "none";
  }
  output << "`, side-swap `" << (args.side_swap ? "true" : "false") << "`, depth `" << args.depth
         << "`, seed `" << args.seed << "`.\n\n";
  output << "Selected positions: `" << positions.selected.size() << "` from `"
         << positions.eligible_rows << "` eligible rows. Duplicate eligible board rows skipped: `"
         << positions.duplicate_board_id_rows << "`.\n\n";
  output << "Games played: `" << stats.games_played << "`\n\n";
  output << "W-L-D: `" << stats.candidate_wins << "-" << stats.baseline_wins << "-" << stats.draws
         << "`\n\n";
  output << "Score: `" << stats.candidate_score << "/" << stats.games_played << "`\n\n";
  output << "Score rate: `" << std::fixed << std::setprecision(6) << stats.candidate_score_rate
         << "`\n\n";
  output << "Approx. 95% score interval: `[" << stats.score_interval_low << ", "
         << stats.score_interval_high << "]`\n\n";
  output << "Average disc diff: `" << stats.average_disc_diff << "`\n\n";
  output << "Median disc diff: `" << stats.median_disc_diff << "`\n\n";
  output << "Interpretation guide: `"
         << interpretation_for_rate(stats.candidate_score_rate, stats.games_played) << "`.\n\n";
  output << "## Results By Empty Count\n\n";
  write_summary_bucket_table(output, stats.by_empty_count);
  output << "\n## Results By Phase\n\n";
  write_summary_bucket_table(output, stats.by_phase);
  output << "\n## Caveats\n\n";
  output << "- This is a local artifact-vs-artifact late-game diagnostic.\n";
  output << "- This is not an Elo result.\n";
  output << "- This is not a production strength claim.\n";
  output << "- This is not self-play.\n";
  output << "- This is not a publication gate.\n";
  output << "- Generated arena reports, logs, weights, artifacts, and corpus payloads must not be "
            "committed.\n";
  output << "- Treat small N as weak even when the score rate is positive.\n\n";
  output << "## Command\n\n";
  output << "```sh\n" << command << "\n```\n\n";
  output << "## Next Recommendation\n\n";
  output << "Use the result as a local diagnostic gate only. A positive signal should be repeated "
            "with a larger selected position set and reviewed by split, phase, and empty-count "
            "buckets before making any production-facing claim.\n";
  return true;
}

} // namespace

int main(int argc, char** argv) {
  const std::optional<Args> args = parse_args(argc, argv);
  if (!args.has_value()) {
    return 2;
  }
  const std::string command = command_line(argc, argv);

  const std::optional<std::string> positions_text =
      read_text_file(args->positions_tsv_path, "positions TSV");
  if (!positions_text.has_value()) {
    return 1;
  }
  const std::string positions_checksum = checksum_for(*positions_text);

  std::optional<LoadedEvaluator> candidate = load_evaluator(
      args->candidate_name, args->candidate_weights_path, args->candidate_manifest_path);
  if (!candidate.has_value()) {
    return 1;
  }
  std::optional<LoadedEvaluator> baseline = load_evaluator(
      args->baseline_name, args->baseline_weights_path, args->baseline_manifest_path);
  if (!baseline.has_value()) {
    return 1;
  }

  const std::optional<PositionLoadResult> positions = load_positions(*args);
  if (!positions.has_value()) {
    return 1;
  }

  const auto start = std::chrono::steady_clock::now();
  const std::vector<GameResult> results =
      play_selected_games(positions->selected, *candidate, *baseline, args->side_swap, args->depth,
                          args->progress_every, "pattern-artifact-arena");
  const auto end = std::chrono::steady_clock::now();
  const double wall_time_sec = std::chrono::duration<double>(end - start).count();
  const double games_per_sec =
      wall_time_sec > 0.0 ? static_cast<double>(results.size()) / wall_time_sec : 0.0;

  const ArenaStats stats = summarize_results(results);
  std::map<int, ArenaStats> depth_arena_stats;
  for (const search::Depth depth : args->depth_sweep) {
    if (depth == args->depth) {
      depth_arena_stats[depth] = stats;
      continue;
    }
    const std::vector<GameResult> depth_results =
        play_selected_games(positions->selected, *candidate, *baseline, args->side_swap, depth, 0,
                            "pattern-artifact-arena-depth-sweep");
    depth_arena_stats[depth] = summarize_results(depth_results);
  }
  const std::filesystem::path report_parent =
      std::filesystem::path(args->report_out_path).parent_path();
  if (!report_parent.empty()) {
    std::filesystem::create_directories(report_parent);
  }
  const std::filesystem::path summary_parent =
      std::filesystem::path(args->summary_out_path).parent_path();
  if (!summary_parent.empty()) {
    std::filesystem::create_directories(summary_parent);
  }
  if (!write_report(*args, *candidate, *baseline, *positions, stats, positions_checksum, command,
                    wall_time_sec, games_per_sec)) {
    return 1;
  }
  if (!write_summary(*args, *candidate, *baseline, *positions, stats, command)) {
    return 1;
  }
  if (!args->diagnostics_out_path.empty()) {
    const DiagnosticsReport diagnostics = build_diagnostics(
        *args, *candidate, *baseline, *positions, results, stats, depth_arena_stats);
    if (!write_diagnostics_report(*args, *candidate, *baseline, *positions, stats, diagnostics,
                                  positions_checksum, command)) {
      return 1;
    }
  }

  std::cout << "games_played=" << stats.games_played << '\n';
  std::cout << "candidate_score_rate=" << std::fixed << std::setprecision(6)
            << stats.candidate_score_rate << '\n';
  std::cout << "average_disc_diff_candidate_perspective=" << stats.average_disc_diff << '\n';
  return 0;
}
