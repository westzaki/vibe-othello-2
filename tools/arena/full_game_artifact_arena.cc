#include "arena_core.h"
#include "full_game_artifact_arena_core.h"
#include "vibe_othello/board_core/position.h"
#include "vibe_othello/evaluation/pattern_artifact.h"
#include "vibe_othello/evaluation/phase_aware_evaluator.h"
#include "vibe_othello/search/search.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace arena = vibe_othello::tools::arena;
namespace board_core = vibe_othello::board_core;
namespace evaluation = vibe_othello::evaluation;
namespace search = vibe_othello::search;
namespace full_arena = vibe_othello::tools::full_game_arena;

constexpr std::string_view kArenaVersion = "full-game-artifact-arena-v4";
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

#ifndef VIBE_OTHELLO_ARENA_REPOSITORY_SHA
#define VIBE_OTHELLO_ARENA_REPOSITORY_SHA "unknown"
#endif
#ifndef VIBE_OTHELLO_ARENA_COMPILER_ID
#define VIBE_OTHELLO_ARENA_COMPILER_ID "unknown"
#endif
#ifndef VIBE_OTHELLO_ARENA_COMPILER_VERSION
#define VIBE_OTHELLO_ARENA_COMPILER_VERSION "unknown"
#endif
#ifndef VIBE_OTHELLO_ARENA_BUILD_TYPE
#define VIBE_OTHELLO_ARENA_BUILD_TYPE "unknown"
#endif
#ifndef VIBE_OTHELLO_ARENA_SOURCE_DIRTY
#define VIBE_OTHELLO_ARENA_SOURCE_DIRTY 0
#endif

enum class SearchPreset {
  basic,
  full,
};

enum class LimitMode {
  fixed_depth,
  fixed_nodes,
  fixed_time,
};

enum class ProbCutMode {
  off,
  shadow,
  single,
  multi,
};

struct Args {
  std::filesystem::path candidate_manifest;
  std::optional<std::filesystem::path> candidate_weights;
  std::string candidate_name = "candidate";
  std::filesystem::path baseline_manifest;
  std::optional<std::filesystem::path> baseline_weights;
  std::string baseline_name = "baseline";
  std::filesystem::path openings_path;
  std::filesystem::path report_out;
  search::Depth depth = 0;
  bool depth_specified = false;
  search::NodeCount max_nodes = 0;
  bool nodes_specified = false;
  std::chrono::milliseconds max_time{0};
  bool time_specified = false;
  std::optional<LimitMode> limit_mode;
  SearchPreset search_preset = SearchPreset::full;
  std::uint8_t exact_endgame_empties = 0;
  std::uint64_t seed = 0;
  int opening_limit = 0;
  int minimum_opening_pairs = 1;
  int progress_every = 0;
  std::uint64_t bootstrap_seed = 0;
  std::uint32_t bootstrap_samples = 10000;
  bool persistent_session = false;
  std::uint64_t tt_bytes = 16 * 1024 * 1024;
  ProbCutMode candidate_probcut = ProbCutMode::off;
  ProbCutMode baseline_probcut = ProbCutMode::off;
  std::optional<std::filesystem::path> probcut_profile_path;
  search::Score probcut_minimum_margin = 0;
  search::Score probcut_maximum_margin = 0;
  double probcut_minimum_confidence = 0.0;
  std::uint8_t probcut_maximum_probes = 2;
  double probcut_maximum_shallow_overhead_ratio = 0.0;
};

struct SearchConfig {
  SearchPreset preset = SearchPreset::full;
  search::SearchLimits limits;
  search::SearchOptions candidate_requested_options;
  search::SearchOptions baseline_requested_options;
  search::SearchOptions candidate_options;
  search::SearchOptions baseline_options;
  ProbCutMode candidate_probcut = ProbCutMode::off;
  ProbCutMode baseline_probcut = ProbCutMode::off;
  std::uint8_t exact_endgame_empties = 0;
  LimitMode limit_mode = LimitMode::fixed_depth;
  bool persistent_session = false;
  std::uint64_t tt_bytes = 0;
};

struct LoadedProbCutProfile {
  std::uint32_t schema_version = 0;
  std::string profile_id;
  std::string source_checksum;
  std::string joint_holdout_checksum;
  std::string evaluator_family;
  std::string artifact_family;
  search::ProbCutNodeClassV1 node_class = search::ProbCutNodeClassV1::unspecified;
  std::vector<search::ProbCutDepthPairV1> validated_pair_order;
  std::uint8_t validated_maximum_probes_per_node = 0;
  search::NodeCount joint_false_cut_count = 0;
  search::NodeCount joint_cut_candidate_count = 0;
  double joint_false_cut_rate_upper_bound = 1.0;
  std::string scheduler_evidence_serialized;
  std::vector<search::ProbCutSchedulerEvidenceV1> scheduler_evidence;
  std::vector<search::ProbCutCalibrationEntryV1> entries;

  [[nodiscard]] search::ProbCutCalibrationProfileV1 view() const noexcept {
    return search::ProbCutCalibrationProfileV1{
        .schema_version = schema_version,
        .profile_id = profile_id,
        .source_calibration_report_checksum_sha256 = source_checksum,
        .evaluator_family = evaluator_family,
        .artifact_family = artifact_family,
        .node_class = node_class,
        .validated_pair_order = validated_pair_order,
        .validated_maximum_probes_per_node = validated_maximum_probes_per_node,
        .joint_holdout_checksum_sha256 = joint_holdout_checksum,
        .joint_false_cut_count = joint_false_cut_count,
        .joint_cut_candidate_count = joint_cut_candidate_count,
        .joint_false_cut_rate_upper_bound = joint_false_cut_rate_upper_bound,
        .scheduler_evidence = scheduler_evidence,
        .entries = entries,
    };
  }
};

struct ArtifactIdentity {
  std::string display_name;
  std::string artifact_id;
  std::string pattern_set_id;
  std::string weights_checksum;
  std::vector<std::uint8_t> trained_phases;
  bool trained_phases_reported = false;
  std::optional<std::uint8_t> fallback_additive_through_phase;
  std::string evaluator_policy;
  std::string runtime_identity_checksum;
  std::string manifest_content_checksum;
  std::string weights_file_checksum;
  std::filesystem::path manifest_path;
  std::filesystem::path weights_path;
};

struct LoadedEvaluator {
  ArtifactIdentity identity;
  std::array<std::uint8_t, 65> phase_by_occupied_count{};
  evaluation::PhaseAwareEvaluator evaluator;
};

struct SelectedOpening {
  arena::Opening opening;
  int source_index = 0;
  std::uint64_t selection_hash = 0;
  std::string key;
};

struct GameRecord {
  int game_id = 0;
  std::string opening_key;
  std::string opening_id;
  int opening_source_index = 0;
  std::string side_assignment;
  int black_discs = 0;
  int white_discs = 0;
  int candidate_disc_diff = 0;
  int normal_moves_after_opening = 0;
  int passes_after_opening = 0;
  std::string candidate_result;
  std::string reason;
  bool failed = false;
  bool illegal = false;
  struct SearchCall {
    full_arena::SearchTelemetry telemetry;
    std::string best_move;
    std::string best_move_status;
  };
  std::vector<SearchCall> search_calls;
};

struct BucketStats {
  int games = 0;
  int candidate_wins = 0;
  int baseline_wins = 0;
  int draws = 0;
  int failed_games = 0;
  int illegal_games = 0;
  int normal_moves_after_opening = 0;
  int passes_after_opening = 0;
  int disc_diff_sum = 0;
  std::vector<int> disc_diffs;
};

struct ArenaStats {
  BucketStats overall;
  std::map<std::string, BucketStats> by_side_assignment;
  std::map<std::string, BucketStats> by_opening;
};

void print_usage() {
  std::cerr << "usage: vibe-othello-full-game-artifact-arena "
               "--candidate-manifest PATH --baseline-manifest PATH --openings FILE "
               "--report-out PATH [--candidate-weights PATH] [--baseline-weights PATH] "
               "[--candidate-name NAME] [--baseline-name NAME] "
               "--limit-mode depth|nodes|time [--depth N | --nodes N | --time-ms N] "
               "[--search-preset basic|full] [--exact-endgame-empties 0] "
               "[--seed 0] [--bootstrap-seed 0] [--bootstrap-samples 10000] "
               "[--opening-limit 0] [--minimum-opening-pairs 1] [--progress-every 0] "
               "[--persistent-session] [--tt-bytes 16777216] "
               "[--candidate-probcut off|shadow|single|multi] "
               "[--baseline-probcut off|shadow|single|multi] [--probcut-profile PATH] "
               "[--probcut-minimum-margin N] [--probcut-maximum-margin N] "
               "[--probcut-minimum-confidence K] [--probcut-maximum-probes N] "
               "[--probcut-maximum-shallow-overhead-ratio R]\n";
}

std::optional<int> parse_int(std::string_view text) noexcept {
  int value = 0;
  const auto [pointer, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || pointer != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::uint64_t> parse_u64(std::string_view text) noexcept {
  std::uint64_t value = 0;
  const auto [pointer, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || pointer != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<double> parse_double(std::string_view text) noexcept {
  if (text.empty()) {
    return std::nullopt;
  }
  const std::string owned{text};
  char* end = nullptr;
  errno = 0;
  const double parsed = std::strtod(owned.c_str(), &end);
  if (errno != 0 || end != owned.c_str() + owned.size() || !std::isfinite(parsed)) {
    return std::nullopt;
  }
  return parsed;
}

std::optional<ProbCutMode> parse_probcut_mode(std::string_view value) noexcept {
  if (value == "off") {
    return ProbCutMode::off;
  }
  if (value == "shadow") {
    return ProbCutMode::shadow;
  }
  if (value == "single") {
    return ProbCutMode::single;
  }
  if (value == "multi" || value == "on") {
    return ProbCutMode::multi;
  }
  return std::nullopt;
}

std::optional<Args> parse_args(int argc, char** argv) {
  Args args;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    auto next_value = [&](std::string* value) {
      if (index + 1 >= argc) {
        std::cerr << arg << " requires a value\n";
        return false;
      }
      *value = argv[++index];
      return true;
    };

    std::string value;
    if (arg == "--candidate-manifest") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      args.candidate_manifest = value;
    } else if (arg == "--candidate-weights") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      args.candidate_weights = std::filesystem::path{value};
    } else if (arg == "--candidate-name") {
      if (!next_value(&args.candidate_name)) {
        return std::nullopt;
      }
    } else if (arg == "--baseline-manifest") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      args.baseline_manifest = value;
    } else if (arg == "--baseline-weights") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      args.baseline_weights = std::filesystem::path{value};
    } else if (arg == "--baseline-name") {
      if (!next_value(&args.baseline_name)) {
        return std::nullopt;
      }
    } else if (arg == "--openings") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      args.openings_path = value;
    } else if (arg == "--report-out") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      args.report_out = value;
    } else if (arg == "--depth") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<int> depth = parse_int(value);
      if (!depth.has_value() || *depth <= 0) {
        std::cerr << "--depth must be a positive integer\n";
        return std::nullopt;
      }
      args.depth = static_cast<search::Depth>(*depth);
      args.depth_specified = true;
    } else if (arg == "--nodes") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<std::uint64_t> nodes = parse_u64(value);
      if (!nodes.has_value()) {
        std::cerr << "--nodes must be a non-negative integer\n";
        return std::nullopt;
      }
      args.max_nodes = static_cast<search::NodeCount>(*nodes);
      args.nodes_specified = true;
    } else if (arg == "--time-ms") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<std::uint64_t> time_ms = parse_u64(value);
      if (!time_ms.has_value() ||
          *time_ms > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        std::cerr << "--time-ms must be a non-negative integer within milliseconds range\n";
        return std::nullopt;
      }
      args.max_time = std::chrono::milliseconds{static_cast<std::int64_t>(*time_ms)};
      args.time_specified = true;
    } else if (arg == "--limit-mode") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      if (value == "depth") {
        args.limit_mode = LimitMode::fixed_depth;
      } else if (value == "nodes") {
        args.limit_mode = LimitMode::fixed_nodes;
      } else if (value == "time") {
        args.limit_mode = LimitMode::fixed_time;
      } else {
        std::cerr << "--limit-mode must be depth, nodes, or time\n";
        return std::nullopt;
      }
    } else if (arg == "--search-preset") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      if (value == "basic") {
        args.search_preset = SearchPreset::basic;
      } else if (value == "full") {
        args.search_preset = SearchPreset::full;
      } else {
        std::cerr << "--search-preset must be basic or full\n";
        return std::nullopt;
      }
    } else if (arg == "--exact-endgame-empties") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<int> empties = parse_int(value);
      if (!empties.has_value() || *empties < 0 || *empties > 64) {
        std::cerr << "--exact-endgame-empties must be an integer in [0, 64]\n";
        return std::nullopt;
      }
      args.exact_endgame_empties = static_cast<std::uint8_t>(*empties);
    } else if (arg == "--seed") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<std::uint64_t> seed = parse_u64(value);
      if (!seed.has_value()) {
        std::cerr << "--seed must be a non-negative integer\n";
        return std::nullopt;
      }
      args.seed = *seed;
    } else if (arg == "--bootstrap-seed") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<std::uint64_t> seed = parse_u64(value);
      if (!seed.has_value()) {
        std::cerr << "--bootstrap-seed must be a non-negative integer\n";
        return std::nullopt;
      }
      args.bootstrap_seed = *seed;
    } else if (arg == "--bootstrap-samples") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<std::uint64_t> samples = parse_u64(value);
      if (!samples.has_value() || *samples == 0 ||
          *samples > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        std::cerr << "--bootstrap-samples must be a positive 32-bit integer\n";
        return std::nullopt;
      }
      args.bootstrap_samples = static_cast<std::uint32_t>(*samples);
    } else if (arg == "--opening-limit") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<int> limit = parse_int(value);
      if (!limit.has_value() || *limit < 0) {
        std::cerr << "--opening-limit must be a non-negative integer\n";
        return std::nullopt;
      }
      args.opening_limit = *limit;
    } else if (arg == "--minimum-opening-pairs") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<int> minimum = parse_int(value);
      if (!minimum.has_value() || *minimum <= 0) {
        std::cerr << "--minimum-opening-pairs must be a positive integer\n";
        return std::nullopt;
      }
      args.minimum_opening_pairs = *minimum;
    } else if (arg == "--progress-every") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<int> progress_every = parse_int(value);
      if (!progress_every.has_value() || *progress_every < 0) {
        std::cerr << "--progress-every must be a non-negative integer\n";
        return std::nullopt;
      }
      args.progress_every = *progress_every;
    } else if (arg == "--persistent-session") {
      args.persistent_session = true;
    } else if (arg == "--tt-bytes") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<std::uint64_t> bytes = parse_u64(value);
      if (!bytes.has_value()) {
        std::cerr << "--tt-bytes must be a non-negative integer\n";
        return std::nullopt;
      }
      args.tt_bytes = *bytes;
    } else if (arg == "--candidate-probcut" || arg == "--baseline-probcut") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<ProbCutMode> mode = parse_probcut_mode(value);
      if (!mode.has_value()) {
        std::cerr << arg << " must be off, shadow, single, or multi\n";
        return std::nullopt;
      }
      if (arg == "--candidate-probcut") {
        args.candidate_probcut = *mode;
      } else {
        args.baseline_probcut = *mode;
      }
    } else if (arg == "--probcut-profile") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      args.probcut_profile_path = std::filesystem::path{value};
    } else if (arg == "--probcut-minimum-margin" || arg == "--probcut-maximum-margin") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<int> margin = parse_int(value);
      if (!margin.has_value() || *margin < 0 || *margin >= search::kScoreWin) {
        std::cerr << arg << " must be a non-negative score margin\n";
        return std::nullopt;
      }
      if (arg == "--probcut-minimum-margin") {
        args.probcut_minimum_margin = static_cast<search::Score>(*margin);
      } else {
        args.probcut_maximum_margin = static_cast<search::Score>(*margin);
      }
    } else if (arg == "--probcut-minimum-confidence") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<double> confidence = parse_double(value);
      if (!confidence.has_value() || *confidence < 0.0) {
        std::cerr << arg << " must be non-negative\n";
        return std::nullopt;
      }
      args.probcut_minimum_confidence = *confidence;
    } else if (arg == "--probcut-maximum-probes") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<int> probes = parse_int(value);
      if (!probes.has_value() || *probes <= 0 || *probes > 255) {
        std::cerr << arg << " must be in [1, 255]\n";
        return std::nullopt;
      }
      args.probcut_maximum_probes = static_cast<std::uint8_t>(*probes);
    } else if (arg == "--probcut-maximum-shallow-overhead-ratio") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<double> ratio = parse_double(value);
      if (!ratio.has_value() || *ratio < 0.0) {
        std::cerr << arg << " must be non-negative\n";
        return std::nullopt;
      }
      args.probcut_maximum_shallow_overhead_ratio = *ratio;
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      return std::nullopt;
    }
  }

  if (args.candidate_manifest.empty() || args.baseline_manifest.empty() ||
      args.openings_path.empty() || args.report_out.empty()) {
    print_usage();
    return std::nullopt;
  }
  if (!args.limit_mode.has_value()) {
    std::cerr << "--limit-mode is required\n";
    return std::nullopt;
  }
  const bool valid_depth = *args.limit_mode == LimitMode::fixed_depth && args.depth_specified &&
                           !args.nodes_specified && !args.time_specified;
  const bool valid_nodes = *args.limit_mode == LimitMode::fixed_nodes && args.nodes_specified &&
                           args.max_nodes != 0 && !args.depth_specified && !args.time_specified;
  const bool valid_time = *args.limit_mode == LimitMode::fixed_time && args.time_specified &&
                          args.max_time.count() > 0 && !args.depth_specified &&
                          !args.nodes_specified;
  if (!valid_depth && !valid_nodes && !valid_time) {
    std::cerr << "--limit-mode requires exactly its corresponding non-zero limit\n";
    return std::nullopt;
  }
  if (args.exact_endgame_empties != 0 && args.max_nodes == 0 && args.max_time.count() == 0) {
    std::cerr << "--exact-endgame-empties requires --nodes or --time-ms because exact root "
                 "search ignores depth\n";
    return std::nullopt;
  }
  const bool probcut_requested =
      args.candidate_probcut != ProbCutMode::off || args.baseline_probcut != ProbCutMode::off;
  if (probcut_requested &&
      (!args.probcut_profile_path.has_value() || args.probcut_maximum_margin <= 0 ||
       args.probcut_maximum_margin < args.probcut_minimum_margin)) {
    std::cerr << "ProbCut requires --probcut-profile and a valid positive maximum margin\n";
    return std::nullopt;
  }
  return args;
}

void mix_fnv1a(std::string_view text, std::uint64_t* hash) noexcept {
  for (const unsigned char byte : text) {
    *hash ^= byte;
    *hash *= kFnvPrime;
  }
}

std::string checksum_for(std::string_view text) {
  std::uint64_t hash = kFnvOffsetBasis;
  mix_fnv1a(text, &hash);
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}

std::string checksum_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return "unavailable";
  }
  const std::string contents{std::istreambuf_iterator<char>{input},
                             std::istreambuf_iterator<char>{}};
  return checksum_for(contents);
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

std::vector<std::string_view> split_delimited(std::string_view value, char delimiter) {
  std::vector<std::string_view> fields;
  std::size_t begin = 0;
  while (begin <= value.size()) {
    const std::size_t separator = value.find(delimiter, begin);
    if (separator == std::string_view::npos) {
      fields.push_back(value.substr(begin));
      break;
    }
    fields.push_back(value.substr(begin, separator - begin));
    begin = separator + 1;
  }
  return fields;
}

std::optional<std::vector<search::ProbCutSchedulerEvidenceV1>>
parse_scheduler_evidence(std::string_view serialized) {
  if (serialized.empty()) {
    return std::nullopt;
  }
  std::vector<search::ProbCutSchedulerEvidenceV1> result;
  for (const std::string_view record : split_delimited(serialized, ';')) {
    const std::vector<std::string_view> fields = split_delimited(record, ':');
    if (fields.size() != 15) {
      return std::nullopt;
    }
    const auto prefix_length = parse_int(fields[0]);
    const auto maximum_probes = parse_int(fields[1]);
    const auto phase = parse_int(fields[2]);
    const std::optional<search::SearchMode> search_mode =
        fields[3] == "move"            ? std::optional{search::SearchMode::move}
        : fields[3] == "analyze"       ? std::optional{search::SearchMode::analyze}
        : fields[3] == "exact_score"   ? std::optional{search::SearchMode::exact_score}
        : fields[3] == "win_loss_draw" ? std::optional{search::SearchMode::win_loss_draw}
                                       : std::nullopt;
    const auto minimum_empties = parse_int(fields[4]);
    const auto maximum_empties = parse_int(fields[5]);
    const auto deep = parse_int(fields[6]);
    const bool valid_handoff = fields[7] == "true" || fields[7] == "false";
    const auto exact_handoff_threshold = parse_int(fields[8]);
    const auto minimum_handoff = parse_int(fields[9]);
    const auto maximum_handoff = parse_int(fields[10]);
    const auto holdout_nodes = parse_u64(fields[11]);
    const auto false_cuts = parse_u64(fields[12]);
    const auto candidates = parse_u64(fields[13]);
    const auto upper = parse_double(fields[14]);
    if (!prefix_length.has_value() || *prefix_length <= 0 || *prefix_length > 65535 ||
        !maximum_probes.has_value() || *maximum_probes <= 0 || *maximum_probes > 255 ||
        !phase.has_value() || *phase < 0 || *phase > 12 || !search_mode.has_value() ||
        !minimum_empties.has_value() || !maximum_empties.has_value() || *minimum_empties < 0 ||
        *minimum_empties > *maximum_empties || *maximum_empties > 60 || !deep.has_value() ||
        *deep <= 0 || *deep > std::numeric_limits<search::Depth>::max() || !valid_handoff ||
        !exact_handoff_threshold.has_value() || *exact_handoff_threshold < 0 ||
        *exact_handoff_threshold > 60 ||
        ((fields[7] == "true") != (*exact_handoff_threshold != 0)) ||
        !minimum_handoff.has_value() || !maximum_handoff.has_value() || *minimum_handoff < 0 ||
        *minimum_handoff > *maximum_handoff || *maximum_handoff > 60 ||
        !holdout_nodes.has_value() || *holdout_nodes == 0 || !false_cuts.has_value() ||
        !candidates.has_value() || *candidates == 0 || *false_cuts > *candidates ||
        !upper.has_value() || *upper < 0.0 || *upper > 1.0) {
      return std::nullopt;
    }
    result.push_back(search::ProbCutSchedulerEvidenceV1{
        .pair_prefix_length = static_cast<std::uint16_t>(*prefix_length),
        .maximum_probes_per_node = static_cast<std::uint8_t>(*maximum_probes),
        .phase = static_cast<std::uint8_t>(*phase),
        .search_mode = *search_mode,
        .minimum_empties = static_cast<std::uint8_t>(*minimum_empties),
        .maximum_empties = static_cast<std::uint8_t>(*maximum_empties),
        .deep_depth = static_cast<search::Depth>(*deep),
        .exact_handoff_enabled = fields[7] == "true",
        .exact_handoff_threshold = static_cast<std::uint8_t>(*exact_handoff_threshold),
        .minimum_exact_handoff_distance = static_cast<std::uint8_t>(*minimum_handoff),
        .maximum_exact_handoff_distance = static_cast<std::uint8_t>(*maximum_handoff),
        .holdout_node_count = *holdout_nodes,
        .false_cut_count = *false_cuts,
        .cut_candidate_count = *candidates,
        .false_cut_rate_upper_bound = *upper,
    });
  }
  return result.empty() ? std::nullopt : std::optional{std::move(result)};
}

std::optional<LoadedProbCutProfile> load_probcut_profile(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    std::cerr << "cannot read ProbCut profile: " << path << '\n';
    return std::nullopt;
  }
  const std::array<std::string_view, 30> expected_header{
      "schema_version",
      "profile_id",
      "source_checksum_sha256",
      "joint_holdout_checksum_sha256",
      "evaluator_family",
      "artifact_family",
      "node_class",
      "validated_maximum_probes_per_node",
      "joint_false_cut_count",
      "joint_cut_candidate_count",
      "joint_false_cut_rate_upper_bound",
      "scheduler_domain_evidence",
      "phase",
      "search_mode",
      "minimum_empties",
      "maximum_empties",
      "deep_depth",
      "shallow_depth",
      "exact_handoff_enabled",
      "exact_handoff_threshold",
      "minimum_exact_handoff_distance",
      "maximum_exact_handoff_distance",
      "regression_slope",
      "intercept",
      "residual_sigma",
      "confidence_multiplier",
      "minimum_shallow_score",
      "maximum_shallow_score",
      "minimum_beta",
      "maximum_beta",
  };
  LoadedProbCutProfile profile;
  std::string line;
  bool saw_header = false;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const std::vector<std::string_view> fields = split_tabs(line);
    if (!saw_header) {
      if (fields.size() != expected_header.size() ||
          !std::equal(fields.begin(), fields.end(), expected_header.begin())) {
        std::cerr << "invalid ProbCut profile header: " << path << '\n';
        return std::nullopt;
      }
      saw_header = true;
      continue;
    }
    if (fields.size() != expected_header.size()) {
      std::cerr << "invalid ProbCut profile row: " << path << '\n';
      return std::nullopt;
    }
    const auto schema = parse_int(fields[0]);
    const auto validated_maximum_probes = parse_int(fields[7]);
    const auto joint_false_cuts = parse_u64(fields[8]);
    const auto joint_candidates = parse_u64(fields[9]);
    const auto joint_upper = parse_double(fields[10]);
    const auto scheduler_evidence = parse_scheduler_evidence(fields[11]);
    const auto phase = parse_int(fields[12]);
    const std::optional<search::SearchMode> search_mode =
        fields[13] == "move"            ? std::optional{search::SearchMode::move}
        : fields[13] == "analyze"       ? std::optional{search::SearchMode::analyze}
        : fields[13] == "exact_score"   ? std::optional{search::SearchMode::exact_score}
        : fields[13] == "win_loss_draw" ? std::optional{search::SearchMode::win_loss_draw}
                                        : std::nullopt;
    const auto minimum_empties = parse_int(fields[14]);
    const auto maximum_empties = parse_int(fields[15]);
    const auto deep = parse_int(fields[16]);
    const auto shallow = parse_int(fields[17]);
    const bool valid_handoff = fields[18] == "true" || fields[18] == "false";
    const auto exact_handoff_threshold = parse_int(fields[19]);
    const auto minimum_handoff = parse_int(fields[20]);
    const auto maximum_handoff = parse_int(fields[21]);
    const auto slope = parse_double(fields[22]);
    const auto intercept = parse_double(fields[23]);
    const auto sigma = parse_double(fields[24]);
    const auto confidence = parse_double(fields[25]);
    const auto minimum_shallow = parse_int(fields[26]);
    const auto maximum_shallow = parse_int(fields[27]);
    const auto minimum_beta = parse_int(fields[28]);
    const auto maximum_beta = parse_int(fields[29]);
    if (!schema.has_value() || *schema != 3 || !validated_maximum_probes.has_value() ||
        *validated_maximum_probes <= 0 || *validated_maximum_probes > 255 ||
        !joint_false_cuts.has_value() || !joint_candidates.has_value() || *joint_candidates == 0 ||
        *joint_false_cuts > *joint_candidates || !joint_upper.has_value() || *joint_upper < 0.0 ||
        *joint_upper > 1.0 || !scheduler_evidence.has_value() || !search_mode.has_value() ||
        !phase.has_value() || *phase < 0 || *phase > 12 || !minimum_empties.has_value() ||
        !maximum_empties.has_value() || *minimum_empties < 0 ||
        *minimum_empties > *maximum_empties || *maximum_empties > 60 || !deep.has_value() ||
        !shallow.has_value() || *shallow <= 0 || *deep <= *shallow ||
        *deep > std::numeric_limits<search::Depth>::max() || !valid_handoff ||
        !exact_handoff_threshold.has_value() || *exact_handoff_threshold < 0 ||
        *exact_handoff_threshold > 60 ||
        ((fields[18] == "true") != (*exact_handoff_threshold != 0)) ||
        !minimum_handoff.has_value() || !maximum_handoff.has_value() || *minimum_handoff < 0 ||
        *minimum_handoff > *maximum_handoff || *maximum_handoff > 60 || !slope.has_value() ||
        *slope <= 0.0 || !intercept.has_value() || !sigma.has_value() || *sigma < 0.0 ||
        !confidence.has_value() || *confidence <= 0.0 || !minimum_shallow.has_value() ||
        !maximum_shallow.has_value() || *minimum_shallow > *maximum_shallow ||
        *minimum_shallow <= search::kScoreLoss || *maximum_shallow >= search::kScoreWin ||
        !minimum_beta.has_value() || !maximum_beta.has_value() || *minimum_beta > *maximum_beta ||
        *minimum_beta <= search::kScoreLoss || *maximum_beta >= search::kScoreWin) {
      std::cerr << "invalid ProbCut profile value: " << path << '\n';
      return std::nullopt;
    }
    if (profile.entries.empty()) {
      profile.schema_version = static_cast<std::uint32_t>(*schema);
      profile.profile_id = fields[1];
      profile.source_checksum = fields[2];
      profile.joint_holdout_checksum = fields[3];
      profile.evaluator_family = fields[4];
      profile.artifact_family = fields[5];
      profile.validated_maximum_probes_per_node =
          static_cast<std::uint8_t>(*validated_maximum_probes);
      profile.joint_false_cut_count = *joint_false_cuts;
      profile.joint_cut_candidate_count = *joint_candidates;
      profile.joint_false_cut_rate_upper_bound = *joint_upper;
      profile.scheduler_evidence_serialized = fields[11];
      profile.scheduler_evidence = *scheduler_evidence;
      if (fields[6] != "non_pv_scout_beta_only") {
        std::cerr << "unsupported ProbCut node class: " << path << '\n';
        return std::nullopt;
      }
      profile.node_class = search::ProbCutNodeClassV1::non_pv_scout_beta_only;
    } else if (profile.profile_id != fields[1] || profile.source_checksum != fields[2] ||
               profile.joint_holdout_checksum != fields[3] ||
               profile.evaluator_family != fields[4] || profile.artifact_family != fields[5] ||
               fields[6] != "non_pv_scout_beta_only" ||
               profile.validated_maximum_probes_per_node != *validated_maximum_probes ||
               profile.joint_false_cut_count != *joint_false_cuts ||
               profile.joint_cut_candidate_count != *joint_candidates ||
               profile.joint_false_cut_rate_upper_bound != *joint_upper ||
               profile.scheduler_evidence_serialized != fields[11]) {
      std::cerr << "mixed ProbCut profile identity: " << path << '\n';
      return std::nullopt;
    }
    const search::ProbCutDepthPairV1 pair{
        .deep_depth = static_cast<search::Depth>(*deep),
        .shallow_depth = static_cast<search::Depth>(*shallow),
    };
    if (std::find(profile.validated_pair_order.begin(), profile.validated_pair_order.end(), pair) ==
        profile.validated_pair_order.end()) {
      profile.validated_pair_order.push_back(pair);
    }
    profile.entries.push_back(search::ProbCutCalibrationEntryV1{
        .phase = static_cast<std::uint8_t>(*phase),
        .search_mode = *search_mode,
        .minimum_empties = static_cast<std::uint8_t>(*minimum_empties),
        .maximum_empties = static_cast<std::uint8_t>(*maximum_empties),
        .deep_depth = pair.deep_depth,
        .shallow_depth = pair.shallow_depth,
        .exact_handoff_enabled = fields[18] == "true",
        .exact_handoff_threshold = static_cast<std::uint8_t>(*exact_handoff_threshold),
        .minimum_exact_handoff_distance = static_cast<std::uint8_t>(*minimum_handoff),
        .maximum_exact_handoff_distance = static_cast<std::uint8_t>(*maximum_handoff),
        .regression_slope = *slope,
        .intercept = *intercept,
        .residual_sigma = *sigma,
        .confidence_multiplier = *confidence,
        .minimum_shallow_score = static_cast<search::Score>(*minimum_shallow),
        .maximum_shallow_score = static_cast<search::Score>(*maximum_shallow),
        .minimum_beta = static_cast<search::Score>(*minimum_beta),
        .maximum_beta = static_cast<search::Score>(*maximum_beta),
    });
  }
  const auto checksum_valid = [](const std::string& checksum) {
    return checksum.size() == 64 && std::all_of(checksum.begin(), checksum.end(), [](char value) {
             return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
           });
  };
  if (!saw_header || profile.entries.empty() || profile.profile_id.empty() ||
      profile.evaluator_family.empty() || profile.artifact_family.empty() ||
      !checksum_valid(profile.source_checksum) || !checksum_valid(profile.joint_holdout_checksum) ||
      profile.validated_pair_order.empty() || profile.scheduler_evidence.empty() ||
      profile.validated_maximum_probes_per_node > profile.validated_pair_order.size()) {
    std::cerr << "incomplete ProbCut profile: " << path << '\n';
    return std::nullopt;
  }
  for (std::size_t index = 0; index < profile.entries.size(); ++index) {
    const search::ProbCutCalibrationEntryV1& entry = profile.entries[index];
    for (std::size_t previous = 0; previous < index; ++previous) {
      const search::ProbCutCalibrationEntryV1& other = profile.entries[previous];
      const bool empties_overlap = other.minimum_empties <= entry.maximum_empties &&
                                   entry.minimum_empties <= other.maximum_empties;
      const bool proximity_overlap =
          other.minimum_exact_handoff_distance <= entry.maximum_exact_handoff_distance &&
          entry.minimum_exact_handoff_distance <= other.maximum_exact_handoff_distance;
      if (other.phase == entry.phase && other.search_mode == entry.search_mode &&
          other.deep_depth == entry.deep_depth && other.shallow_depth == entry.shallow_depth &&
          other.exact_handoff_enabled == entry.exact_handoff_enabled &&
          other.exact_handoff_threshold == entry.exact_handoff_threshold && empties_overlap &&
          proximity_overlap) {
        std::cerr << "overlapping ProbCut profile domain: " << path << '\n';
        return std::nullopt;
      }
    }
  }
  return profile;
}

std::string search_preset_name(SearchPreset preset) {
  return preset == SearchPreset::basic ? "basic" : "full";
}

std::string_view limit_mode_name(LimitMode mode) {
  switch (mode) {
  case LimitMode::fixed_depth:
    return "fixed_depth";
  case LimitMode::fixed_nodes:
    return "fixed_nodes";
  case LimitMode::fixed_time:
    return "fixed_wall_time";
  }
  return "unknown";
}

std::string_view probcut_mode_name(ProbCutMode mode) noexcept {
  switch (mode) {
  case ProbCutMode::off:
    return "off";
  case ProbCutMode::shadow:
    return "shadow";
  case ProbCutMode::single:
    return "single";
  case ProbCutMode::multi:
    return "multi";
  }
  return "unknown";
}

void configure_probcut(search::SearchOptions* options, ProbCutMode mode, const Args& args,
                       const search::ProbCutCalibrationProfileV1* profile,
                       std::string_view evaluator_family, std::string_view artifact_family) {
  if (mode == ProbCutMode::off || profile == nullptr || profile->entries.empty()) {
    return;
  }
  const std::span<const search::ProbCutDepthPairV1> selected_pairs =
      mode == ProbCutMode::single && !profile->validated_pair_order.empty()
          ? profile->validated_pair_order.first(1)
          : profile->validated_pair_order;
  const auto minimum_deep = std::min_element(
      profile->entries.begin(), profile->entries.end(),
      [](const auto& lhs, const auto& rhs) { return lhs.deep_depth < rhs.deep_depth; });
  const auto selected_minimum_deep = std::min_element(
      selected_pairs.begin(), selected_pairs.end(),
      [](const auto& lhs, const auto& rhs) { return lhs.deep_depth < rhs.deep_depth; });
  const search::ProbCutCalibrationEntryV1& first = profile->entries.front();
  options->probcut_options = search::ProbCutOptionsV1{
      .use_probcut = true,
      .minimum_depth =
          selected_pairs.empty() ? minimum_deep->deep_depth : selected_minimum_deep->deep_depth,
      .shallow_depth_reduction = static_cast<search::Depth>(first.deep_depth - first.shallow_depth),
      .maximum_probes_per_node =
          mode == ProbCutMode::single ? std::uint8_t{1} : args.probcut_maximum_probes,
      .ordered_depth_pairs = selected_pairs,
      .stop_after_first_success = true,
      .confidence_multiplier = args.probcut_minimum_confidence,
      .minimum_confidence = args.probcut_minimum_confidence,
      .minimum_margin = args.probcut_minimum_margin,
      .maximum_margin = args.probcut_maximum_margin,
      .maximum_shallow_overhead_ratio = args.probcut_maximum_shallow_overhead_ratio,
      .enabled_phase_mask = search::kAllProbCutPhasesMask,
      .non_pv_only = true,
      .beta_only = true,
      .disable_near_exact = true,
      .near_exact_disable_empties = args.exact_endgame_empties,
      .shadow_verify = mode == ProbCutMode::shadow,
      .evaluator_family = evaluator_family,
      .artifact_family = artifact_family,
      .calibration_profile_id = profile->profile_id,
      .calibration_profile = profile,
  };
}

std::optional<SearchConfig>
make_search_config(const Args& args, const search::ProbCutCalibrationProfileV1* probcut_profile,
                   const ArtifactIdentity& candidate_identity,
                   const ArtifactIdentity& baseline_identity) {
  const bool full = args.search_preset == SearchPreset::full;
  const LimitMode mode = *args.limit_mode;
  const bool fixed_nodes_or_time = mode == LimitMode::fixed_nodes || mode == LimitMode::fixed_time;
  search::SearchOptions base_options{
      .midgame =
          search::MidgameSearchOptions{
              .use_pvs = full,
              .use_aspiration = full,
              .use_iid = full,
              .use_midgame_tt = full,
          },
      .ordering =
          search::MoveOrderingOptions{
              .use_tt_best_move_ordering = full,
              .use_history = full,
              .use_killers = full,
              .use_midgame_mobility_ordering = full,
              .use_endgame_parity_ordering = true,
          },
      .endgame =
          search::EndgameSearchOptions{
              .exact_endgame = args.exact_endgame_empties != 0,
              .use_endgame_tt = full,
              .endgame_exact_empties = args.exact_endgame_empties,
              .endgame_wld_empties = 0,
          },
      .reporting = search::SearchReportingOptions{.multi_pv = 1},
      .mode = search::SearchMode::move,
  };
  search::SearchOptions candidate_requested_options = base_options;
  search::SearchOptions baseline_requested_options = base_options;
  configure_probcut(&candidate_requested_options, args.candidate_probcut, args, probcut_profile,
                    candidate_identity.pattern_set_id, candidate_identity.artifact_id);
  configure_probcut(&baseline_requested_options, args.baseline_probcut, args, probcut_profile,
                    baseline_identity.pattern_set_id, baseline_identity.artifact_id);
  const search::ResolvedProbCutConfigurationV1 candidate_probcut =
      search::resolve_probcut_configuration(candidate_requested_options.probcut_options);
  const search::ResolvedProbCutConfigurationV1 baseline_probcut =
      search::resolve_probcut_configuration(baseline_requested_options.probcut_options);
  if (args.candidate_probcut != ProbCutMode::off && !candidate_probcut.enabled()) {
    std::cerr << "candidate requested ProbCut mode is not effective under the reviewed profile\n";
    return std::nullopt;
  }
  if (args.baseline_probcut != ProbCutMode::off && !baseline_probcut.enabled()) {
    std::cerr << "baseline requested ProbCut mode is not effective under the reviewed profile\n";
    return std::nullopt;
  }
  search::SearchOptions candidate_options = candidate_requested_options;
  candidate_options.probcut_options = candidate_probcut.options;
  search::SearchOptions baseline_options = baseline_requested_options;
  baseline_options.probcut_options = baseline_probcut.options;
  return SearchConfig{
      .preset = args.search_preset,
      .limits =
          search::SearchLimits{
              .max_depth = fixed_nodes_or_time ? search::Depth{0} : args.depth,
              .max_nodes = args.max_nodes,
              .max_time = args.max_time,
              .infinite = fixed_nodes_or_time,
          },
      .candidate_requested_options = candidate_requested_options,
      .baseline_requested_options = baseline_requested_options,
      .candidate_options = candidate_options,
      .baseline_options = baseline_options,
      .candidate_probcut = args.candidate_probcut,
      .baseline_probcut = args.baseline_probcut,
      .exact_endgame_empties = args.exact_endgame_empties,
      .limit_mode = mode,
      .persistent_session = args.persistent_session,
      .tt_bytes = args.tt_bytes,
  };
}

std::string runtime_identity_checksum(const ArtifactIdentity& identity) {
  std::ostringstream payload;
  payload << identity.pattern_set_id << '\n'
          << identity.weights_checksum << '\n'
          << identity.evaluator_policy << '\n'
          << (identity.trained_phases_reported ? "1" : "0") << '\n';
  if (identity.fallback_additive_through_phase.has_value()) {
    payload << static_cast<int>(*identity.fallback_additive_through_phase);
  } else {
    payload << "none";
  }
  for (const std::uint8_t phase : identity.trained_phases) {
    payload << ',' << static_cast<int>(phase);
  }
  return checksum_for(payload.str());
}

std::optional<LoadedEvaluator>
load_evaluator(std::string display_name, const std::filesystem::path& manifest_path,
               const std::optional<std::filesystem::path>& weights_override) {
  evaluation::PatternArtifactLoadResult result =
      weights_override.has_value()
          ? evaluation::load_pattern_artifact(manifest_path, *weights_override)
          : evaluation::load_pattern_artifact(manifest_path);
  if (!result.ok()) {
    std::cerr << result.error << '\n';
    return std::nullopt;
  }

  try {
    evaluation::LoadedPatternArtifact artifact = std::move(*result.artifact);
    std::array<std::uint8_t, 65> phase_by_occupied_count{};
    for (int occupied_count = 0; occupied_count <= 64; ++occupied_count) {
      phase_by_occupied_count[static_cast<std::size_t>(occupied_count)] =
          artifact.weights.phase_for_disc_count(occupied_count);
    }
    ArtifactIdentity identity{
        .display_name = std::move(display_name),
        .artifact_id = artifact.artifact_id,
        .pattern_set_id = artifact.pattern_set_id,
        .weights_checksum = artifact.weights_checksum,
        .trained_phases = artifact.trained_phases.value_or(std::vector<std::uint8_t>{}),
        .trained_phases_reported = artifact.trained_phases.has_value(),
        .fallback_additive_through_phase = artifact.fallback_additive_through_phase,
        .evaluator_policy = artifact.fallback_additive_through_phase.has_value()
                                ? "phase-aware-covered-phases-with-fallback-residual"
                            : artifact.trained_phases.has_value()
                                ? "phase-aware-covered-phases"
                                : "phase-aware-legacy-all-phase-learned",
        .manifest_path = artifact.manifest_path,
        .weights_path = artifact.weights_path,
    };
    identity.manifest_content_checksum = checksum_file(identity.manifest_path);
    identity.weights_file_checksum = checksum_file(identity.weights_path);
    identity.runtime_identity_checksum = runtime_identity_checksum(identity);
    return LoadedEvaluator{
        .identity = std::move(identity),
        .phase_by_occupied_count = phase_by_occupied_count,
        .evaluator = evaluation::PhaseAwareEvaluator{std::move(artifact.weights),
                                                     std::move(artifact.feature_set),
                                                     std::move(artifact.trained_phases),
                                                     artifact.fallback_additive_through_phase},
    };
  } catch (const std::exception& error) {
    std::cerr << "phase-aware evaluator rejected artifact for " << display_name << ": "
              << error.what() << '\n';
    return std::nullopt;
  }
}

std::optional<std::string> read_text_file(const std::filesystem::path& path,
                                          std::string_view description) {
  std::ifstream input(path);
  if (!input) {
    std::cerr << "cannot read " << description << ": " << path << '\n';
    return std::nullopt;
  }
  return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

std::uint64_t opening_selection_hash(std::uint64_t seed, const arena::Opening& opening,
                                     int source_index) noexcept {
  std::uint64_t hash = kFnvOffsetBasis;
  const std::string seed_text = std::to_string(seed);
  mix_fnv1a(seed_text, &hash);
  mix_fnv1a("\n", &hash);
  mix_fnv1a(opening.id, &hash);
  mix_fnv1a("\n", &hash);
  const std::string moves = arena::format_moves(opening.moves);
  mix_fnv1a(moves, &hash);
  mix_fnv1a("\n", &hash);
  mix_fnv1a(std::to_string(source_index), &hash);
  return hash;
}

std::vector<SelectedOpening> select_openings(std::span<const arena::Opening> openings,
                                             std::uint64_t seed, int opening_limit) {
  std::vector<SelectedOpening> selected;
  selected.reserve(openings.size());
  for (std::size_t index = 0; index < openings.size(); ++index) {
    const arena::Opening& opening = openings[index];
    const int source_index = static_cast<int>(index) + 1;
    selected.push_back(SelectedOpening{
        .opening = opening,
        .source_index = source_index,
        .selection_hash = opening_selection_hash(seed, opening, source_index),
        .key = opening.id + "#" + std::to_string(source_index),
    });
  }
  if (opening_limit <= 0 || selected.size() <= static_cast<std::size_t>(opening_limit)) {
    return selected;
  }
  std::sort(selected.begin(), selected.end(),
            [](const SelectedOpening& lhs, const SelectedOpening& rhs) {
              if (lhs.selection_hash != rhs.selection_hash) {
                return lhs.selection_hash < rhs.selection_hash;
              }
              return lhs.source_index < rhs.source_index;
            });
  selected.resize(static_cast<std::size_t>(opening_limit));
  return selected;
}

int disc_count(board_core::Bitboard discs) noexcept {
  return std::popcount(discs);
}

GameRecord adjudicated_failure(int game_id, const SelectedOpening& opening, bool candidate_is_black,
                               bool candidate_offender, std::string reason, bool illegal,
                               int normal_moves_after_opening, int passes_after_opening,
                               std::vector<GameRecord::SearchCall> search_calls = {}) {
  const bool offender_is_black = candidate_offender == candidate_is_black;
  const int black_discs = offender_is_black ? 0 : 64;
  const int white_discs = offender_is_black ? 64 : 0;
  return GameRecord{
      .game_id = game_id,
      .opening_key = opening.key,
      .opening_id = opening.opening.id,
      .opening_source_index = opening.source_index,
      .side_assignment = candidate_is_black ? "candidate_black" : "candidate_white",
      .black_discs = black_discs,
      .white_discs = white_discs,
      .candidate_disc_diff =
          candidate_is_black ? black_discs - white_discs : white_discs - black_discs,
      .normal_moves_after_opening = normal_moves_after_opening,
      .passes_after_opening = passes_after_opening,
      .candidate_result = candidate_offender ? "loss" : "win",
      .reason = std::move(reason),
      .failed = true,
      .illegal = illegal,
      .search_calls = std::move(search_calls),
  };
}

GameRecord play_game(int game_id, const SelectedOpening& opening, bool candidate_is_black,
                     const LoadedEvaluator& candidate, const LoadedEvaluator& baseline,
                     const SearchConfig& search_config) {
  board_core::Position position{};
  std::string replay_error;
  if (!arena::replay_moves(opening.opening.moves, &position, &replay_error)) {
    return adjudicated_failure(game_id, opening, candidate_is_black, true, "invalid_opening", false,
                               0, 0);
  }

  int normal_moves_after_opening = 0;
  int passes_after_opening = 0;
  std::vector<GameRecord::SearchCall> search_calls;
  const search::SearchSessionConfig session_config{
      .profile = search::SearchPlatformProfile::native,
      .transposition_table =
          search::TranspositionTableConfig{
              .capacity = static_cast<std::size_t>(search_config.tt_bytes),
              .unit = search::TranspositionTableCapacityUnit::bytes,
          },
  };
  search::SearchSession candidate_session{session_config};
  search::SearchSession baseline_session{session_config};
  while (!board_core::is_terminal(position)) {
    const board_core::Bitboard legal_moves = board_core::legal_moves(position);
    if (legal_moves == 0) {
      board_core::MoveDelta delta{};
      if (!board_core::apply_pass(&position, &delta)) {
        return adjudicated_failure(game_id, opening, candidate_is_black, true, "illegal_pass", true,
                                   normal_moves_after_opening, passes_after_opening,
                                   std::move(search_calls));
      }
      ++passes_after_opening;
      continue;
    }

    const bool candidate_to_move =
        (position.side_to_move == board_core::Color::black) == candidate_is_black;
    const LoadedEvaluator& active = candidate_to_move ? candidate : baseline;
    const search::Evaluator& evaluator = static_cast<const search::Evaluator&>(active.evaluator);
    const int occupied_count = std::popcount(position.player) + std::popcount(position.opponent);
    const auto search_started = std::chrono::steady_clock::now();
    search::SearchSession& active_session =
        candidate_to_move ? candidate_session : baseline_session;
    if (!search_config.persistent_session) {
      active_session.clear();
    }
    const search::SearchResult result = search::search_iterative(
        active_session, position, evaluator, search_config.limits,
        candidate_to_move ? search_config.candidate_options : search_config.baseline_options);
    const auto measured_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - search_started);
    const std::uint64_t elapsed_ns =
        measured_elapsed.count() > 0 ? static_cast<std::uint64_t>(measured_elapsed.count()) : 0;
    const std::int64_t timer_accounting_delta_ns =
        measured_elapsed.count() -
        std::chrono::duration_cast<std::chrono::nanoseconds>(result.elapsed).count();
    const bool exact_root_search =
        search_config.exact_endgame_empties != 0 &&
        64 - occupied_count <= static_cast<int>(search_config.exact_endgame_empties);
    const std::uint64_t time_budget_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(search_config.limits.max_time)
            .count());
    GameRecord::SearchCall search_call{
        .telemetry = full_arena::make_search_telemetry(
            candidate_to_move ? full_arena::EngineRole::candidate
                              : full_arena::EngineRole::baseline,
            position.side_to_move == board_core::Color::black ? "black" : "white", occupied_count,
            active.phase_by_occupied_count[static_cast<std::size_t>(occupied_count)], elapsed_ns,
            timer_accounting_delta_ns, exact_root_search,
            search_config.limits.max_time.count() > 0 ? std::optional<std::uint64_t>{time_budget_ns}
                                                      : std::nullopt,
            result),
        .best_move = result.best_move.has_value() ? arena::format_move(*result.best_move) : "none",
        .best_move_status = "legal",
    };
    if (result.nodes != result.stats.nodes) {
      search_call.best_move_status = "malformed";
      search_calls.push_back(std::move(search_call));
      return adjudicated_failure(game_id, opening, candidate_is_black, candidate_to_move,
                                 "malformed_search_result", false, normal_moves_after_opening,
                                 passes_after_opening, std::move(search_calls));
    }
    if (!result.best_move.has_value()) {
      search_call.best_move_status = result.stopped ? "stopped_without_best_move" : "missing";
      search_calls.push_back(std::move(search_call));
      return adjudicated_failure(
          game_id, opening, candidate_is_black, candidate_to_move,
          result.stopped ? "search_stopped_without_best_move" : "missing_best_move", false,
          normal_moves_after_opening, passes_after_opening, std::move(search_calls));
    }
    if (result.best_move->kind != board_core::MoveKind::normal ||
        (legal_moves & board_core::bit(result.best_move->square)) == 0) {
      search_call.best_move_status = "illegal";
      search_calls.push_back(std::move(search_call));
      return adjudicated_failure(game_id, opening, candidate_is_black, candidate_to_move,
                                 "illegal_best_move", true, normal_moves_after_opening,
                                 passes_after_opening, std::move(search_calls));
    }

    board_core::MoveDelta delta{};
    if (!board_core::apply_move(&position, *result.best_move, &delta)) {
      search_call.best_move_status = "apply_failed";
      search_calls.push_back(std::move(search_call));
      return adjudicated_failure(game_id, opening, candidate_is_black, candidate_to_move,
                                 "apply_move_failed", true, normal_moves_after_opening,
                                 passes_after_opening, std::move(search_calls));
    }
    search_calls.push_back(std::move(search_call));
    ++normal_moves_after_opening;
  }

  const int black_discs = disc_count(board_core::black_discs(position));
  const int white_discs = disc_count(board_core::white_discs(position));
  const int candidate_disc_diff =
      candidate_is_black ? black_discs - white_discs : white_discs - black_discs;
  return GameRecord{
      .game_id = game_id,
      .opening_key = opening.key,
      .opening_id = opening.opening.id,
      .opening_source_index = opening.source_index,
      .side_assignment = candidate_is_black ? "candidate_black" : "candidate_white",
      .black_discs = black_discs,
      .white_discs = white_discs,
      .candidate_disc_diff = candidate_disc_diff,
      .normal_moves_after_opening = normal_moves_after_opening,
      .passes_after_opening = passes_after_opening,
      .candidate_result = candidate_disc_diff > 0   ? "win"
                          : candidate_disc_diff < 0 ? "loss"
                                                    : "draw",
      .reason = "terminal",
      .search_calls = std::move(search_calls),
  };
}

void add_to_bucket(BucketStats* bucket, const GameRecord& game) {
  ++bucket->games;
  bucket->disc_diff_sum += game.candidate_disc_diff;
  bucket->disc_diffs.push_back(game.candidate_disc_diff);
  bucket->normal_moves_after_opening += game.normal_moves_after_opening;
  bucket->passes_after_opening += game.passes_after_opening;
  if (game.candidate_result == "win") {
    ++bucket->candidate_wins;
  } else if (game.candidate_result == "loss") {
    ++bucket->baseline_wins;
  } else {
    ++bucket->draws;
  }
  if (game.failed) {
    ++bucket->failed_games;
  }
  if (game.illegal) {
    ++bucket->illegal_games;
  }
}

ArenaStats summarize(std::span<const GameRecord> games) {
  ArenaStats stats;
  for (const GameRecord& game : games) {
    add_to_bucket(&stats.overall, game);
    add_to_bucket(&stats.by_side_assignment[game.side_assignment], game);
    add_to_bucket(&stats.by_opening[game.opening_key], game);
  }
  return stats;
}

double score_rate(const BucketStats& bucket) noexcept {
  if (bucket.games == 0) {
    return 0.0;
  }
  return static_cast<double>(bucket.candidate_wins + bucket.draws * 0.5) /
         static_cast<double>(bucket.games);
}

double average_disc_diff(const BucketStats& bucket) noexcept {
  if (bucket.games == 0) {
    return 0.0;
  }
  return static_cast<double>(bucket.disc_diff_sum) / static_cast<double>(bucket.games);
}

double median_disc_diff(const BucketStats& bucket) {
  if (bucket.disc_diffs.empty()) {
    return 0.0;
  }
  std::vector<int> values = bucket.disc_diffs;
  std::sort(values.begin(), values.end());
  const std::size_t middle = values.size() / 2;
  if (values.size() % 2 != 0) {
    return static_cast<double>(values[middle]);
  }
  return (static_cast<double>(values[middle - 1]) + static_cast<double>(values[middle])) / 2.0;
}

void write_json_string(std::ostream& output, std::string_view value) {
  output << '"' << arena::json_escape(value) << '"';
}

void write_bool(std::ostream& output, bool value) {
  output << (value ? "true" : "false");
}

void write_trained_phases(std::ostream& output, const ArtifactIdentity& identity) {
  if (!identity.trained_phases_reported) {
    output << "null";
    return;
  }
  output << '[';
  for (std::size_t index = 0; index < identity.trained_phases.size(); ++index) {
    if (index != 0) {
      output << ", ";
    }
    output << static_cast<int>(identity.trained_phases[index]);
  }
  output << ']';
}

void write_artifact_identity(std::ostream& output, const ArtifactIdentity& identity) {
  output << "{";
  output << "\"name\": ";
  write_json_string(output, identity.display_name);
  output << ", \"artifact_id\": ";
  write_json_string(output, identity.artifact_id);
  output << ", \"pattern_set_id\": ";
  write_json_string(output, identity.pattern_set_id);
  output << ", \"weights_checksum\": ";
  write_json_string(output, identity.weights_checksum);
  output << ", \"manifest_content_checksum\": ";
  write_json_string(output, identity.manifest_content_checksum);
  output << ", \"weights_file_checksum\": ";
  write_json_string(output, identity.weights_file_checksum);
  output << ", \"trained_phases\": ";
  write_trained_phases(output, identity);
  output << ", \"fallback_additive_through_phase\": ";
  if (identity.fallback_additive_through_phase.has_value()) {
    output << static_cast<int>(*identity.fallback_additive_through_phase);
  } else {
    output << "null";
  }
  output << ", \"evaluator_policy\": ";
  write_json_string(output, identity.evaluator_policy);
  output << ", \"runtime_identity_checksum\": ";
  write_json_string(output, identity.runtime_identity_checksum);
  output << ", \"manifest_path\": ";
  write_json_string(output, identity.manifest_path.string());
  output << ", \"weights_path\": ";
  write_json_string(output, identity.weights_path.string());
  output << "}";
}

void write_probcut_pairs(std::ostream& output, std::span<const search::ProbCutDepthPairV1> pairs) {
  output << '[';
  for (std::size_t index = 0; index < pairs.size(); ++index) {
    if (index != 0) {
      output << ", ";
    }
    const search::ProbCutDepthPairV1 pair = pairs[index];
    output << "{\"deep_depth\": " << pair.deep_depth
           << ", \"shallow_depth\": " << pair.shallow_depth << "}";
  }
  output << ']';
}

void write_requested_probcut_options(std::ostream& output, const search::SearchOptions& options,
                                     ProbCutMode requested_mode) {
  const search::ProbCutOptionsV1& probcut = options.probcut_options;
  output << "{\"requested_mode\": ";
  write_json_string(output, probcut_mode_name(requested_mode));
  output << ", \"requested_enabled\": ";
  write_bool(output, probcut.use_probcut);
  output << ", \"requested_maximum_probes_per_node\": "
         << static_cast<int>(probcut.maximum_probes_per_node);
  output << ", \"requested_ordered_depth_pairs\": ";
  write_probcut_pairs(output, probcut.ordered_depth_pairs);
  output << '}';
}

void write_search_options(std::ostream& output, const search::SearchOptions& options,
                          ProbCutMode requested_mode) {
  output << "{";
  output << "\"use_pvs\": ";
  write_bool(output, options.midgame.use_pvs);
  output << ", \"use_aspiration\": ";
  write_bool(output, options.midgame.use_aspiration);
  output << ", \"use_iid\": ";
  write_bool(output, options.midgame.use_iid);
  output << ", \"use_midgame_tt\": ";
  write_bool(output, options.midgame.use_midgame_tt);
  output << ", \"use_tt_best_move_ordering\": ";
  write_bool(output, options.ordering.use_tt_best_move_ordering);
  output << ", \"use_history\": ";
  write_bool(output, options.ordering.use_history);
  output << ", \"use_killers\": ";
  write_bool(output, options.ordering.use_killers);
  output << ", \"use_midgame_mobility_ordering\": ";
  write_bool(output, options.ordering.use_midgame_mobility_ordering);
  output << ", \"use_endgame_parity_ordering\": ";
  write_bool(output, options.ordering.use_endgame_parity_ordering);
  output << ", \"exact_endgame\": ";
  write_bool(output, options.endgame.exact_endgame);
  output << ", \"use_endgame_tt\": ";
  write_bool(output, options.endgame.use_endgame_tt);
  output << ", \"endgame_exact_empties\": "
         << static_cast<int>(options.endgame.endgame_exact_empties);
  output << ", \"endgame_wld_empties\": " << static_cast<int>(options.endgame.endgame_wld_empties);
  output << ", \"probcut\": ";
  write_bool(output, false);
  output << ", \"use_pv_table\": ";
  write_bool(output, false);
  output << ", \"use_parallel\": ";
  write_bool(output, false);
  output << ", \"selectivity_level\": 0";
  const search::ProbCutOptionsV1& probcut = options.probcut_options;
  output << ", \"multi_probcut\": {\"requested_mode\": ";
  write_json_string(output, probcut_mode_name(requested_mode));
  output << ", \"enabled\": ";
  write_bool(output, probcut.use_probcut);
  output << ", \"effective_enabled\": ";
  write_bool(output, probcut.use_probcut);
  output << ", \"profile_id\": ";
  write_json_string(output, probcut.use_probcut ? probcut.calibration_profile_id : "none");
  output << ", \"source_checksum_sha256\": ";
  write_json_string(output,
                    probcut.use_probcut && probcut.calibration_profile != nullptr
                        ? probcut.calibration_profile->source_calibration_report_checksum_sha256
                        : std::string_view{"none"});
  output << ", \"joint_holdout_checksum_sha256\": ";
  write_json_string(output, probcut.use_probcut && probcut.calibration_profile != nullptr
                                ? probcut.calibration_profile->joint_holdout_checksum_sha256
                                : std::string_view{"none"});
  output << ", \"validated_maximum_probes_per_node\": "
         << (probcut.use_probcut && probcut.calibration_profile != nullptr
                 ? static_cast<int>(probcut.calibration_profile->validated_maximum_probes_per_node)
                 : 0);
  output << ", \"joint_false_cut_count\": "
         << (probcut.use_probcut && probcut.calibration_profile != nullptr
                 ? probcut.calibration_profile->joint_false_cut_count
                 : 0);
  output << ", \"joint_cut_candidate_count\": "
         << (probcut.use_probcut && probcut.calibration_profile != nullptr
                 ? probcut.calibration_profile->joint_cut_candidate_count
                 : 0);
  output << ", \"joint_false_cut_rate_upper_bound\": "
         << (probcut.use_probcut && probcut.calibration_profile != nullptr
                 ? probcut.calibration_profile->joint_false_cut_rate_upper_bound
                 : 1.0);
  output << ", \"scheduler_domain_evidence_count\": "
         << (probcut.use_probcut && probcut.calibration_profile != nullptr
                 ? probcut.calibration_profile->scheduler_evidence.size()
                 : 0);
  output << ", \"evaluator_family\": ";
  write_json_string(output, probcut.evaluator_family);
  output << ", \"artifact_family\": ";
  write_json_string(output, probcut.artifact_family);
  output << ", \"minimum_depth\": " << probcut.minimum_depth;
  output << ", \"shallow_depth_reduction\": " << probcut.shallow_depth_reduction;
  output << ", \"maximum_probes_per_node\": "
         << (probcut.use_probcut ? static_cast<int>(probcut.maximum_probes_per_node) : 0);
  output << ", \"effective_maximum_probes_per_node\": "
         << (probcut.use_probcut ? static_cast<int>(probcut.maximum_probes_per_node) : 0);
  output << ", \"stop_after_first_success\": ";
  write_bool(output, probcut.stop_after_first_success);
  output << ", \"confidence_multiplier\": " << probcut.confidence_multiplier;
  output << ", \"minimum_confidence\": " << probcut.minimum_confidence;
  output << ", \"minimum_margin\": " << probcut.minimum_margin;
  output << ", \"maximum_margin\": " << probcut.maximum_margin;
  output << ", \"maximum_shallow_overhead_ratio\": " << probcut.maximum_shallow_overhead_ratio;
  output << ", \"enabled_phase_mask\": " << probcut.enabled_phase_mask;
  output << ", \"non_pv_only\": ";
  write_bool(output, probcut.non_pv_only);
  output << ", \"beta_only\": ";
  write_bool(output, probcut.beta_only);
  output << ", \"disable_near_exact\": ";
  write_bool(output, probcut.disable_near_exact);
  output << ", \"near_exact_disable_empties\": "
         << static_cast<int>(probcut.near_exact_disable_empties);
  output << ", \"shadow_verify\": ";
  write_bool(output, probcut.shadow_verify);
  output << ", \"ordered_depth_pairs\": ";
  write_probcut_pairs(output, probcut.ordered_depth_pairs);
  output << ", \"effective_ordered_depth_pairs\": ";
  write_probcut_pairs(output, probcut.ordered_depth_pairs);
  output << '}';
  output << "}";
}

void write_search_config(std::ostream& output, const SearchConfig& config) {
  output << "{";
  output << "\"entrypoint\": \"search_iterative\"";
  output << ", \"preset\": ";
  write_json_string(output, search_preset_name(config.preset));
  output << ", \"limit_scope\": \"per-move\"";
  output << ", \"limit_mode\": ";
  write_json_string(output, limit_mode_name(config.limit_mode));
  output << ", \"pure_limit_mode\": true";
  output << ", \"infinite\": ";
  write_bool(output, config.limits.infinite);
  output << ", \"depth\": " << config.limits.max_depth;
  output << ", \"nodes\": " << config.limits.max_nodes;
  output << ", \"time_ms\": " << config.limits.max_time.count();
  output << ", \"exact_endgame_empties\": " << static_cast<int>(config.exact_endgame_empties);
  output << ", \"persistent_session\": ";
  write_bool(output, config.persistent_session);
  output << ", \"tt_requested_bytes\": " << config.tt_bytes;
  const search::SearchSession allocation_session{search::SearchSessionConfig{
      .profile = search::SearchPlatformProfile::native,
      .transposition_table =
          search::TranspositionTableConfig{
              .capacity = static_cast<std::size_t>(config.tt_bytes),
              .unit = search::TranspositionTableCapacityUnit::bytes,
          },
  }};
  const search::TranspositionTableAllocation allocation =
      allocation_session.transposition_table_allocation();
  output << ", \"tt_actual_bytes\": " << allocation.actual_bytes;
  output << ", \"tt_entry_count\": " << allocation.entry_count;
  output << ", \"tt_bucket_count\": " << allocation.bucket_count;
  output << ", \"tt_entry_size\": " << allocation.entry_size;
  output << ", \"tt_enabled\": ";
  write_bool(output, allocation.enabled);
  output << ", \"tt_allocation_succeeded\": ";
  write_bool(output, allocation.allocation_succeeded);
  output << ", \"candidate_probcut_mode\": ";
  write_json_string(output, probcut_mode_name(config.candidate_probcut));
  output << ", \"baseline_probcut_mode\": ";
  write_json_string(output, probcut_mode_name(config.baseline_probcut));
  output << ", \"candidate_requested_probcut_mode\": ";
  write_json_string(output, probcut_mode_name(config.candidate_probcut));
  output << ", \"baseline_requested_probcut_mode\": ";
  write_json_string(output, probcut_mode_name(config.baseline_probcut));
  output << ", \"candidate_requested_probcut_options\": ";
  write_requested_probcut_options(output, config.candidate_requested_options,
                                  config.candidate_probcut);
  output << ", \"baseline_requested_probcut_options\": ";
  write_requested_probcut_options(output, config.baseline_requested_options,
                                  config.baseline_probcut);
  output << ", \"resolved_options\": ";
  write_search_options(output, config.candidate_options, config.candidate_probcut);
  output << ", \"candidate_resolved_options\": ";
  write_search_options(output, config.candidate_options, config.candidate_probcut);
  output << ", \"baseline_resolved_options\": ";
  write_search_options(output, config.baseline_options, config.baseline_probcut);
  output << "}";
}

void write_bucket(std::ostream& output, const BucketStats& bucket) {
  output << "{";
  output << "\"games\": " << bucket.games;
  output << ", \"candidate_wins\": " << bucket.candidate_wins;
  output << ", \"candidate_losses\": " << bucket.baseline_wins;
  output << ", \"draws\": " << bucket.draws;
  output << ", \"candidate_score_rate\": " << score_rate(bucket);
  output << ", \"average_disc_diff_candidate_perspective\": " << average_disc_diff(bucket);
  output << ", \"median_disc_diff_candidate_perspective\": " << median_disc_diff(bucket);
  output << ", \"failed_games\": " << bucket.failed_games;
  output << ", \"illegal_games\": " << bucket.illegal_games;
  output << ", \"normal_moves_after_opening\": " << bucket.normal_moves_after_opening;
  output << ", \"passes_after_opening\": " << bucket.passes_after_opening;
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

void write_optional_rate(std::ostream& output, std::uint64_t numerator, std::uint64_t denominator) {
  if (denominator == 0) {
    output << "null";
    return;
  }
  output << static_cast<double>(numerator) / static_cast<double>(denominator);
}

void write_histogram(std::ostream& output, std::span<const std::uint64_t> values) {
  std::map<std::uint64_t, std::uint64_t> histogram;
  for (const std::uint64_t value : values) {
    ++histogram[value];
  }
  output << "{";
  bool first = true;
  for (const auto& [value, count] : histogram) {
    if (!first) {
      output << ", ";
    }
    first = false;
    write_json_string(output, std::to_string(value));
    output << ": " << count;
  }
  output << "}";
}

void write_distribution(std::ostream& output, std::span<const std::uint64_t> values,
                        double divisor = 1.0) {
  output << "{";
  output << "\"count\": " << values.size();
  if (values.empty()) {
    output << ", \"mean\": null, \"p50\": null, \"p90\": null, \"p99\": null, \"max\": null";
  } else {
    const std::uint64_t total = std::accumulate(values.begin(), values.end(), std::uint64_t{0});
    output << ", \"mean\": "
           << static_cast<double>(total) / static_cast<double>(values.size()) / divisor;
    output << ", \"p50\": " << full_arena::nearest_rank_percentile(values, 0.50) / divisor;
    output << ", \"p90\": " << full_arena::nearest_rank_percentile(values, 0.90) / divisor;
    output << ", \"p99\": " << full_arena::nearest_rank_percentile(values, 0.99) / divisor;
    output << ", \"max\": "
           << static_cast<double>(*std::max_element(values.begin(), values.end())) / divisor;
  }
  output << "}";
}

void write_probcut_pair_telemetry(std::ostream& output,
                                  std::span<const search::ProbCutDepthPairStats> entries) {
  output << "[";
  for (std::size_t index = 0; index < entries.size(); ++index) {
    if (index != 0) {
      output << ", ";
    }
    const search::ProbCutDepthPairStats& entry = entries[index];
    output << "{\"phase\": " << static_cast<int>(entry.phase)
           << ", \"deep_depth\": " << entry.deep_depth
           << ", \"shallow_depth\": " << entry.shallow_depth << ", \"attempts\": " << entry.attempts
           << ", \"shallow_nodes\": " << entry.shallow_nodes
           << ", \"successes\": " << entry.successes
           << ", \"confidence_rejections\": " << entry.confidence_rejections
           << ", \"unsupported_profile\": " << entry.unsupported_profile
           << ", \"near_exact_rejections\": " << entry.near_exact_rejections
           << ", \"pass_rejections\": " << entry.pass_rejections
           << ", \"pv_rejections\": " << entry.pv_rejections
           << ", \"root_rejections\": " << entry.root_rejections
           << ", \"beta_cuts\": " << entry.beta_cuts
           << ", \"cut_low_attempts\": " << entry.cut_low_attempts
           << ", \"shadow_candidates\": " << entry.shadow_candidates
           << ", \"shadow_verifications\": " << entry.shadow_verifications
           << ", \"shadow_false_cuts\": " << entry.shadow_false_cuts
           << ", \"average_shallow_overhead\": ";
    if (entry.attempts == 0) {
      output << "null";
    } else {
      output << static_cast<double>(entry.shallow_nodes) / static_cast<double>(entry.attempts);
    }
    output << ", \"cut_success_rate\": ";
    write_optional_rate(output, entry.successes, entry.attempts);
    output << "}";
  }
  output << "]";
}

void write_search_stats_identity(std::ostream& output, const search::SearchStats& stats) {
  output << stats.nodes << '\n'
         << stats.leaf_nodes << '\n'
         << stats.eval_calls << '\n'
         << stats.incremental_eval_enabled << '\n'
         << stats.incremental_state_initializations << '\n'
         << stats.incremental_eval_calls << '\n'
         << stats.stateless_eval_calls << '\n'
         << stats.incremental_updates << '\n'
         << stats.incremental_touched_instances << '\n'
         << stats.terminal_nodes << '\n'
         << stats.pass_nodes << '\n'
         << stats.beta_cutoffs << '\n'
         << stats.alpha_updates << '\n'
         << stats.root_moves_searched << '\n'
         << stats.tt_probes << '\n'
         << stats.tt_hits << '\n'
         << stats.tt_stores << '\n'
         << stats.tt_cutoffs << '\n'
         << stats.tt_replacements << '\n'
         << stats.tt_bucket_conflicts << '\n'
         << stats.tt_same_key_updates << '\n'
         << stats.tt_probe_slots << '\n'
         << stats.tt_generation_age_hits << '\n'
         << stats.tt_rejected_stores << '\n'
         << stats.tt_invalid_best_move_stores << '\n'
         << stats.pvs_researches << '\n'
         << stats.aspiration_fail_lows << '\n'
         << stats.aspiration_fail_highs << '\n'
         << stats.iid_searches << '\n'
         << stats.endgame_nodes << '\n'
         << stats.selective_cuts << '\n'
         << stats.probcut_attempts << '\n'
         << stats.probcut_shallow_nodes << '\n'
         << stats.probcut_successes << '\n'
         << stats.probcut_unsupported_profile << '\n'
         << stats.probcut_rejected_by_phase << '\n'
         << stats.probcut_rejected_by_depth << '\n'
         << stats.probcut_rejected_near_exact << '\n'
         << stats.probcut_rejected_pass << '\n'
         << stats.probcut_rejected_pv << '\n'
         << stats.probcut_rejected_root << '\n'
         << stats.probcut_rejected_overhead << '\n'
         << stats.probcut_probe_limit_reached << '\n'
         << stats.probcut_rejected_confidence << '\n'
         << stats.probcut_beta_cutoffs << '\n'
         << stats.probcut_cut_low_attempts << '\n'
         << stats.probcut_shadow_candidates << '\n'
         << stats.probcut_shadow_verifications << '\n'
         << stats.probcut_shadow_false_cuts << '\n'
         << stats.probcut_estimated_saved_nodes << '\n'
         << stats.probcut_estimated_saved_nodes_available << '\n';
  for (const search::ProbCutDepthPairStats& pair : stats.probcut_by_phase_depth_pair) {
    output << static_cast<int>(pair.phase) << '\n'
           << pair.deep_depth << '\n'
           << pair.shallow_depth << '\n'
           << pair.attempts << '\n'
           << pair.shallow_nodes << '\n'
           << pair.successes << '\n'
           << pair.confidence_rejections << '\n'
           << pair.unsupported_profile << '\n'
           << pair.near_exact_rejections << '\n'
           << pair.pass_rejections << '\n'
           << pair.pv_rejections << '\n'
           << pair.root_rejections << '\n'
           << pair.beta_cuts << '\n'
           << pair.cut_low_attempts << '\n'
           << pair.shadow_candidates << '\n'
           << pair.shadow_verifications << '\n'
           << pair.shadow_false_cuts << '\n';
  }
}

void write_telemetry_summary(std::ostream& output, const full_arena::TelemetrySummary& summary) {
  output << "{";
  output << "\"search_calls\": " << summary.search_calls;
  output << ", \"elapsed_ns\": " << summary.elapsed_ns;
  output << ", \"elapsed_ms\": " << static_cast<double>(summary.elapsed_ns) / 1'000'000.0;
  output << ", \"engine_elapsed_ms\": " << summary.engine_elapsed_ms;
  output << ", \"timer_accounting_delta_ns\": " << summary.timer_accounting_delta_ns;
  output << ", \"nodes\": {\"total\": " << summary.stats.nodes << ", \"mean\": ";
  if (summary.search_calls == 0) {
    output << "null";
  } else {
    output << static_cast<double>(summary.stats.nodes) / static_cast<double>(summary.search_calls);
  }
  output << "}";
  output << ", \"eval_calls\": {\"total\": " << summary.stats.eval_calls << ", \"mean\": ";
  if (summary.search_calls == 0) {
    output << "null";
  } else {
    output << static_cast<double>(summary.stats.eval_calls) /
                  static_cast<double>(summary.search_calls);
  }
  output << "}";
  output << ", \"incremental_eval_enabled\": "
         << (summary.incremental_eval_enabled_searches != 0 ? "true" : "false");
  output << ", \"incremental_eval_enabled_searches\": "
         << summary.incremental_eval_enabled_searches;
  output << ", \"incremental_state_initializations\": "
         << summary.stats.incremental_state_initializations;
  output << ", \"incremental_eval_calls\": " << summary.stats.incremental_eval_calls;
  output << ", \"stateless_eval_calls\": " << summary.stats.stateless_eval_calls;
  output << ", \"incremental_updates\": " << summary.stats.incremental_updates;
  output << ", \"incremental_touched_instances\": " << summary.stats.incremental_touched_instances;
  output << ", \"nodes_per_sec\": ";
  const std::optional<double> nodes_per_sec =
      full_arena::events_per_second(summary.stats.nodes, summary.elapsed_ns);
  if (!nodes_per_sec.has_value()) {
    output << "null";
  } else {
    output << *nodes_per_sec;
  }
  output << ", \"evals_per_sec\": ";
  const std::optional<double> evals_per_sec =
      full_arena::events_per_second(summary.stats.eval_calls, summary.elapsed_ns);
  if (!evals_per_sec.has_value()) {
    output << "null";
  } else {
    output << *evals_per_sec;
  }
  output << ", \"completed_depth_histogram\": ";
  write_histogram(output, summary.completed_depths);
  output << ", \"completed_depth_percentiles\": {\"p10\": ";
  if (summary.completed_depths.empty()) {
    output << "null, \"p50\": null, \"p90\": null";
  } else {
    output << full_arena::nearest_rank_percentile(summary.completed_depths, 0.10)
           << ", \"p50\": " << full_arena::nearest_rank_percentile(summary.completed_depths, 0.50)
           << ", \"p90\": " << full_arena::nearest_rank_percentile(summary.completed_depths, 0.90);
  }
  output << "}";
  output << ", \"leaf_nodes\": " << summary.stats.leaf_nodes;
  output << ", \"terminal_nodes\": " << summary.stats.terminal_nodes;
  output << ", \"pass_nodes\": " << summary.stats.pass_nodes;
  output << ", \"beta_cutoffs\": " << summary.stats.beta_cutoffs;
  output << ", \"alpha_updates\": " << summary.stats.alpha_updates;
  output << ", \"root_moves_searched\": " << summary.stats.root_moves_searched;
  output << ", \"tt\": {\"probes\": " << summary.stats.tt_probes
         << ", \"hits\": " << summary.stats.tt_hits << ", \"cutoffs\": " << summary.stats.tt_cutoffs
         << ", \"stores\": " << summary.stats.tt_stores
         << ", \"replacements\": " << summary.stats.tt_replacements
         << ", \"bucket_conflicts\": " << summary.stats.tt_bucket_conflicts
         << ", \"same_key_updates\": " << summary.stats.tt_same_key_updates
         << ", \"probe_slots\": " << summary.stats.tt_probe_slots
         << ", \"generation_age_hits\": " << summary.stats.tt_generation_age_hits
         << ", \"rejected_stores\": " << summary.stats.tt_rejected_stores
         << ", \"invalid_best_move_stores\": " << summary.stats.tt_invalid_best_move_stores
         << ", \"hit_rate\": ";
  write_optional_rate(output, summary.stats.tt_hits, summary.stats.tt_probes);
  output << ", \"cutoff_rate_per_probe\": ";
  write_optional_rate(output, summary.stats.tt_cutoffs, summary.stats.tt_probes);
  output << ", \"cutoff_rate_per_hit\": ";
  write_optional_rate(output, summary.stats.tt_cutoffs, summary.stats.tt_hits);
  output << ", \"average_probe_slots\": ";
  write_optional_rate(output, summary.stats.tt_probe_slots, summary.stats.tt_probes);
  output << "}";
  output << ", \"pvs_researches\": " << summary.stats.pvs_researches;
  output << ", \"aspiration_fail_lows\": " << summary.stats.aspiration_fail_lows;
  output << ", \"aspiration_fail_highs\": " << summary.stats.aspiration_fail_highs;
  output << ", \"iid_searches\": " << summary.stats.iid_searches;
  output << ", \"endgame_nodes\": " << summary.stats.endgame_nodes;
  output << ", \"selective_cuts\": " << summary.stats.selective_cuts;
  output << ", \"probcut\": {\"attempts\": " << summary.stats.probcut_attempts
         << ", \"shallow_nodes\": " << summary.stats.probcut_shallow_nodes
         << ", \"successes\": " << summary.stats.probcut_successes
         << ", \"confidence_rejections\": " << summary.stats.probcut_rejected_confidence
         << ", \"unsupported_profile\": " << summary.stats.probcut_unsupported_profile
         << ", \"phase_rejections\": " << summary.stats.probcut_rejected_by_phase
         << ", \"depth_rejections\": " << summary.stats.probcut_rejected_by_depth
         << ", \"near_exact_rejections\": " << summary.stats.probcut_rejected_near_exact
         << ", \"pass_rejections\": " << summary.stats.probcut_rejected_pass
         << ", \"pv_rejections\": " << summary.stats.probcut_rejected_pv
         << ", \"root_rejections\": " << summary.stats.probcut_rejected_root
         << ", \"overhead_rejections\": " << summary.stats.probcut_rejected_overhead
         << ", \"probe_limit_reached\": " << summary.stats.probcut_probe_limit_reached
         << ", \"beta_cuts\": " << summary.stats.probcut_beta_cutoffs
         << ", \"cut_low_attempts\": " << summary.stats.probcut_cut_low_attempts
         << ", \"shadow_candidates\": " << summary.stats.probcut_shadow_candidates
         << ", \"shadow_verifications\": " << summary.stats.probcut_shadow_verifications
         << ", \"shadow_false_cuts\": " << summary.stats.probcut_shadow_false_cuts
         << ", \"estimated_saved_nodes\": " << summary.stats.probcut_estimated_saved_nodes
         << ", \"estimated_saved_nodes_available\": "
         << (summary.stats.probcut_estimated_saved_nodes_available ? "true" : "false")
         << ", \"average_shallow_overhead\": ";
  if (summary.stats.probcut_attempts == 0) {
    output << "null";
  } else {
    output << static_cast<double>(summary.stats.probcut_shallow_nodes) /
                  static_cast<double>(summary.stats.probcut_attempts);
  }
  output << ", \"cut_success_rate\": ";
  write_optional_rate(output, summary.stats.probcut_successes, summary.stats.probcut_attempts);
  output << ", \"by_phase_depth_pair\": ";
  write_probcut_pair_telemetry(output, summary.stats.probcut_by_phase_depth_pair);
  output << "}";
  output << ", \"stopped_searches\": " << summary.stopped_searches;
  output << ", \"exact_handoff_uses\": " << summary.exact_handoff_uses;
  output << ", \"exact_root_searches\": " << summary.exact_root_searches;
  output << ", \"exact_searches\": " << summary.exact_searches;
  output << ", \"time_budget_overshoot_ns\": ";
  write_distribution(output, summary.time_overshoot_ns);
  output << ", \"time_budget_overshoot_ms\": ";
  write_distribution(output, summary.time_overshoot_ns, 1'000'000.0);
  output << "}";
}

void write_telemetry_map(
    std::ostream& output,
    const std::map<std::string, std::vector<full_arena::SearchTelemetry>>& buckets) {
  output << "{";
  bool first = true;
  for (const auto& [key, records] : buckets) {
    if (!first) {
      output << ", ";
    }
    first = false;
    write_json_string(output, key);
    output << ": ";
    write_telemetry_summary(output, full_arena::summarize_telemetry(records));
  }
  output << "}";
}

void write_selected_openings(std::ostream& output, std::span<const SelectedOpening> openings) {
  output << "[";
  for (std::size_t index = 0; index < openings.size(); ++index) {
    if (index != 0) {
      output << ", ";
    }
    const SelectedOpening& opening = openings[index];
    output << "{\"key\": ";
    write_json_string(output, opening.key);
    output << ", \"id\": ";
    write_json_string(output, opening.opening.id);
    output << ", \"source_index\": " << opening.source_index;
    output << ", \"moves\": ";
    write_json_string(output, arena::format_moves(opening.opening.moves));
    output << ", \"selection_hash\": \"0x" << std::hex << std::setfill('0') << std::setw(16)
           << opening.selection_hash << std::dec << "\"}";
  }
  output << "]";
}

void write_search_call(std::ostream& output, const GameRecord::SearchCall& call) {
  const full_arena::SearchTelemetry& record = call.telemetry;
  output << "{\"engine_role\": ";
  write_json_string(output, full_arena::engine_role_name(record.role));
  output << ", \"side_to_move\": ";
  write_json_string(output, record.side_to_move);
  output << ", \"occupied_count\": " << record.occupied_count;
  output << ", \"phase\": " << record.phase;
  output << ", \"completed_depth\": " << record.completed_depth;
  output << ", \"elapsed_ns\": " << record.elapsed_ns;
  output << ", \"elapsed_ms\": " << static_cast<double>(record.elapsed_ns) / 1'000'000.0;
  output << ", \"engine_elapsed_ms\": " << record.engine_elapsed_ms;
  output << ", \"timer_accounting_delta_ns\": " << record.timer_accounting_delta_ns;
  output << ", \"nodes\": " << record.stats.nodes;
  output << ", \"eval_calls\": " << record.stats.eval_calls;
  output << ", \"incremental_eval_enabled\": ";
  write_bool(output, record.stats.incremental_eval_enabled);
  output << ", \"incremental_state_initializations\": "
         << record.stats.incremental_state_initializations;
  output << ", \"incremental_eval_calls\": " << record.stats.incremental_eval_calls;
  output << ", \"stateless_eval_calls\": " << record.stats.stateless_eval_calls;
  output << ", \"incremental_updates\": " << record.stats.incremental_updates;
  output << ", \"incremental_touched_instances\": " << record.stats.incremental_touched_instances;
  output << ", \"leaf_nodes\": " << record.stats.leaf_nodes;
  output << ", \"terminal_nodes\": " << record.stats.terminal_nodes;
  output << ", \"pass_nodes\": " << record.stats.pass_nodes;
  output << ", \"beta_cutoffs\": " << record.stats.beta_cutoffs;
  output << ", \"alpha_updates\": " << record.stats.alpha_updates;
  output << ", \"root_moves_searched\": " << record.stats.root_moves_searched;
  output << ", \"tt_probes\": " << record.stats.tt_probes;
  output << ", \"tt_hits\": " << record.stats.tt_hits;
  output << ", \"tt_cutoffs\": " << record.stats.tt_cutoffs;
  output << ", \"tt_stores\": " << record.stats.tt_stores;
  output << ", \"tt_replacements\": " << record.stats.tt_replacements;
  output << ", \"tt_bucket_conflicts\": " << record.stats.tt_bucket_conflicts;
  output << ", \"tt_same_key_updates\": " << record.stats.tt_same_key_updates;
  output << ", \"tt_probe_slots\": " << record.stats.tt_probe_slots;
  output << ", \"tt_generation_age_hits\": " << record.stats.tt_generation_age_hits;
  output << ", \"tt_rejected_stores\": " << record.stats.tt_rejected_stores;
  output << ", \"tt_invalid_best_move_stores\": " << record.stats.tt_invalid_best_move_stores;
  output << ", \"pvs_researches\": " << record.stats.pvs_researches;
  output << ", \"aspiration_fail_lows\": " << record.stats.aspiration_fail_lows;
  output << ", \"aspiration_fail_highs\": " << record.stats.aspiration_fail_highs;
  output << ", \"iid_searches\": " << record.stats.iid_searches;
  output << ", \"endgame_nodes\": " << record.stats.endgame_nodes;
  output << ", \"selective_cuts\": " << record.stats.selective_cuts;
  output << ", \"probcut\": {\"attempts\": " << record.stats.probcut_attempts
         << ", \"shallow_nodes\": " << record.stats.probcut_shallow_nodes
         << ", \"successes\": " << record.stats.probcut_successes
         << ", \"confidence_rejections\": " << record.stats.probcut_rejected_confidence
         << ", \"unsupported_profile\": " << record.stats.probcut_unsupported_profile
         << ", \"phase_rejections\": " << record.stats.probcut_rejected_by_phase
         << ", \"depth_rejections\": " << record.stats.probcut_rejected_by_depth
         << ", \"near_exact_rejections\": " << record.stats.probcut_rejected_near_exact
         << ", \"pass_rejections\": " << record.stats.probcut_rejected_pass
         << ", \"pv_rejections\": " << record.stats.probcut_rejected_pv
         << ", \"root_rejections\": " << record.stats.probcut_rejected_root
         << ", \"overhead_rejections\": " << record.stats.probcut_rejected_overhead
         << ", \"probe_limit_reached\": " << record.stats.probcut_probe_limit_reached
         << ", \"beta_cuts\": " << record.stats.probcut_beta_cutoffs
         << ", \"cut_low_attempts\": " << record.stats.probcut_cut_low_attempts
         << ", \"shadow_candidates\": " << record.stats.probcut_shadow_candidates
         << ", \"shadow_verifications\": " << record.stats.probcut_shadow_verifications
         << ", \"shadow_false_cuts\": " << record.stats.probcut_shadow_false_cuts
         << ", \"estimated_saved_nodes\": " << record.stats.probcut_estimated_saved_nodes
         << ", \"estimated_saved_nodes_available\": ";
  write_bool(output, record.stats.probcut_estimated_saved_nodes_available);
  output << ", \"by_phase_depth_pair\": ";
  write_probcut_pair_telemetry(output, record.stats.probcut_by_phase_depth_pair);
  output << "}";
  output << ", \"exact\": ";
  write_bool(output, record.exact);
  output << ", \"stopped\": ";
  write_bool(output, record.stopped);
  output << ", \"exact_handoff_used\": ";
  write_bool(output, record.exact_handoff_used);
  output << ", \"exact_root_search\": ";
  write_bool(output, record.exact_root_search);
  output << ", \"time_budget_ns\": ";
  if (record.time_budget_applies) {
    output << record.time_budget_ns;
  } else {
    output << "null";
  }
  output << ", \"time_budget_ms\": ";
  if (record.time_budget_applies) {
    output << static_cast<double>(record.time_budget_ns) / 1'000'000.0;
  } else {
    output << "null";
  }
  output << ", \"best_move\": ";
  write_json_string(output, call.best_move);
  output << ", \"best_move_status\": ";
  write_json_string(output, call.best_move_status);
  output << "}";
}

void write_search_calls(std::ostream& output, std::span<const GameRecord::SearchCall> calls) {
  output << "[";
  for (std::size_t index = 0; index < calls.size(); ++index) {
    if (index != 0) {
      output << ", ";
    }
    write_search_call(output, calls[index]);
  }
  output << "]";
}

void write_games(std::ostream& output, std::span<const GameRecord> games) {
  output << "[";
  for (std::size_t index = 0; index < games.size(); ++index) {
    if (index != 0) {
      output << ",\n    ";
    }
    const GameRecord& game = games[index];
    output << "{\"game_id\": " << game.game_id;
    output << ", \"opening_key\": ";
    write_json_string(output, game.opening_key);
    output << ", \"opening_id\": ";
    write_json_string(output, game.opening_id);
    output << ", \"opening_source_index\": " << game.opening_source_index;
    output << ", \"side_assignment\": ";
    write_json_string(output, game.side_assignment);
    output << ", \"black_discs\": " << game.black_discs;
    output << ", \"white_discs\": " << game.white_discs;
    output << ", \"candidate_disc_diff\": " << game.candidate_disc_diff;
    output << ", \"normal_moves_after_opening\": " << game.normal_moves_after_opening;
    output << ", \"passes_after_opening\": " << game.passes_after_opening;
    output << ", \"candidate_result\": ";
    write_json_string(output, game.candidate_result);
    output << ", \"reason\": ";
    write_json_string(output, game.reason);
    output << ", \"failed\": ";
    write_bool(output, game.failed);
    output << ", \"illegal\": ";
    write_bool(output, game.illegal);
    output << ", \"search_calls\": ";
    write_search_calls(output, game.search_calls);
    output << "}";
  }
  output << "]";
}

bool same_runtime_artifact(const LoadedEvaluator& candidate, const LoadedEvaluator& baseline) {
  return candidate.identity.runtime_identity_checksum ==
         baseline.identity.runtime_identity_checksum;
}

bool same_engine_configuration(const LoadedEvaluator& candidate, const LoadedEvaluator& baseline,
                               const SearchConfig& search_config) {
  return same_runtime_artifact(candidate, baseline) &&
         search_config.candidate_probcut == search_config.baseline_probcut;
}

struct TelemetryBuckets {
  std::vector<full_arena::SearchTelemetry> candidate;
  std::vector<full_arena::SearchTelemetry> baseline;
  std::map<std::string, std::vector<full_arena::SearchTelemetry>> candidate_by_phase;
  std::map<std::string, std::vector<full_arena::SearchTelemetry>> baseline_by_phase;
  std::map<std::string, std::vector<full_arena::SearchTelemetry>> candidate_by_side_to_move;
  std::map<std::string, std::vector<full_arena::SearchTelemetry>> baseline_by_side_to_move;
};

TelemetryBuckets collect_telemetry(std::span<const GameRecord> games) {
  TelemetryBuckets buckets;
  for (const GameRecord& game : games) {
    for (const GameRecord::SearchCall& call : game.search_calls) {
      const full_arena::SearchTelemetry& telemetry = call.telemetry;
      const std::string phase = std::to_string(telemetry.phase);
      if (telemetry.role == full_arena::EngineRole::candidate) {
        buckets.candidate.push_back(telemetry);
        buckets.candidate_by_phase[phase].push_back(telemetry);
        buckets.candidate_by_side_to_move[telemetry.side_to_move].push_back(telemetry);
      } else {
        buckets.baseline.push_back(telemetry);
        buckets.baseline_by_phase[phase].push_back(telemetry);
        buckets.baseline_by_side_to_move[telemetry.side_to_move].push_back(telemetry);
      }
    }
  }
  return buckets;
}

std::vector<full_arena::PairGameResult> pair_games(std::span<const GameRecord> games) {
  std::vector<full_arena::PairGameResult> results;
  results.reserve(games.size());
  for (const GameRecord& game : games) {
    const double score = game.candidate_result == "win"    ? 1.0
                         : game.candidate_result == "draw" ? 0.5
                                                           : 0.0;
    results.push_back(full_arena::PairGameResult{
        .opening_key = game.opening_key,
        .side_assignment = game.side_assignment,
        .candidate_score = score,
        .candidate_disc_diff = game.candidate_disc_diff,
        .failed = game.failed,
        .illegal = game.illegal,
    });
  }
  return results;
}

std::string selected_openings_checksum(std::span<const SelectedOpening> openings) {
  std::ostringstream payload;
  for (const SelectedOpening& opening : openings) {
    payload << opening.key << '\n'
            << opening.opening.id << '\n'
            << arena::format_moves(opening.opening.moves) << '\n'
            << opening.selection_hash << '\n';
  }
  return checksum_for(payload.str());
}

std::string deterministic_payload(const LoadedEvaluator& candidate, const LoadedEvaluator& baseline,
                                  std::string_view openings_checksum,
                                  std::span<const SelectedOpening> openings,
                                  const SearchConfig& search_config, std::uint64_t seed,
                                  int opening_limit, std::uint64_t bootstrap_seed,
                                  std::uint32_t bootstrap_samples, int minimum_opening_pairs,
                                  std::span<const GameRecord> games) {
  std::ostringstream output;
  output << kArenaVersion << '\n';
  output << candidate.identity.runtime_identity_checksum << '\n';
  output << baseline.identity.runtime_identity_checksum << '\n';
  output << openings_checksum << '\n'
         << seed << '\n'
         << opening_limit << '\n'
         << bootstrap_seed << '\n'
         << bootstrap_samples << '\n'
         << minimum_opening_pairs << '\n';
  output << search_preset_name(search_config.preset) << '\n'
         << limit_mode_name(search_config.limit_mode) << '\n'
         << search_config.limits.max_depth << '\n'
         << search_config.limits.max_nodes << '\n'
         << search_config.limits.max_time.count() << '\n'
         << static_cast<int>(search_config.exact_endgame_empties) << '\n'
         << search_config.persistent_session << '\n'
         << search_config.tt_bytes << '\n';
  output << probcut_mode_name(search_config.candidate_probcut) << '\n'
         << probcut_mode_name(search_config.baseline_probcut) << '\n';
  // Keep the retired v4 no-op option slots serialized as false so existing
  // report checksums and schema readers remain compatible.
  const auto write_options_identity = [&output](const search::SearchOptions& options) {
    output << options.midgame.use_pvs << options.midgame.use_aspiration << options.midgame.use_iid
           << options.midgame.use_midgame_tt << options.ordering.use_tt_best_move_ordering
           << options.ordering.use_history << options.ordering.use_killers
           << options.ordering.use_midgame_mobility_ordering
           << options.ordering.use_endgame_parity_ordering << options.endgame.exact_endgame
           << options.endgame.use_endgame_tt << false << false << false << '\n';
    const search::ProbCutOptionsV1& probcut = options.probcut_options;
    output << probcut.use_probcut << '\n'
           << probcut.calibration_profile_id << '\n'
           << probcut.minimum_depth << '\n'
           << static_cast<int>(probcut.maximum_probes_per_node) << '\n'
           << probcut.confidence_multiplier << '\n'
           << probcut.minimum_confidence << '\n'
           << probcut.minimum_margin << '\n'
           << probcut.maximum_margin << '\n'
           << probcut.maximum_shallow_overhead_ratio << '\n'
           << probcut.enabled_phase_mask << '\n'
           << static_cast<int>(probcut.near_exact_disable_empties) << '\n';
    if (probcut.calibration_profile != nullptr) {
      output << probcut.calibration_profile->source_calibration_report_checksum_sha256 << '\n'
             << probcut.calibration_profile->joint_holdout_checksum_sha256 << '\n'
             << static_cast<int>(probcut.calibration_profile->validated_maximum_probes_per_node)
             << '\n'
             << probcut.calibration_profile->joint_false_cut_count << '\n'
             << probcut.calibration_profile->joint_cut_candidate_count << '\n'
             << probcut.calibration_profile->joint_false_cut_rate_upper_bound << '\n';
      for (const search::ProbCutDepthPairV1 pair :
           probcut.calibration_profile->validated_pair_order) {
        output << pair.deep_depth << '\n' << pair.shallow_depth << '\n';
      }
      for (const search::ProbCutSchedulerEvidenceV1& evidence :
           probcut.calibration_profile->scheduler_evidence) {
        output << evidence.pair_prefix_length << '\n'
               << static_cast<int>(evidence.maximum_probes_per_node) << '\n'
               << static_cast<int>(evidence.phase) << '\n'
               << static_cast<int>(evidence.search_mode) << '\n'
               << static_cast<int>(evidence.minimum_empties) << '\n'
               << static_cast<int>(evidence.maximum_empties) << '\n'
               << evidence.deep_depth << '\n'
               << evidence.exact_handoff_enabled << '\n'
               << static_cast<int>(evidence.exact_handoff_threshold) << '\n'
               << static_cast<int>(evidence.minimum_exact_handoff_distance) << '\n'
               << static_cast<int>(evidence.maximum_exact_handoff_distance) << '\n'
               << evidence.holdout_node_count << '\n'
               << evidence.false_cut_count << '\n'
               << evidence.cut_candidate_count << '\n'
               << evidence.false_cut_rate_upper_bound << '\n';
      }
      for (const search::ProbCutCalibrationEntryV1& entry : probcut.calibration_profile->entries) {
        output << static_cast<int>(entry.phase) << '\n'
               << static_cast<int>(entry.search_mode) << '\n'
               << static_cast<int>(entry.minimum_empties) << '\n'
               << static_cast<int>(entry.maximum_empties) << '\n'
               << entry.deep_depth << '\n'
               << entry.shallow_depth << '\n'
               << entry.exact_handoff_enabled << '\n'
               << static_cast<int>(entry.exact_handoff_threshold) << '\n'
               << static_cast<int>(entry.minimum_exact_handoff_distance) << '\n'
               << static_cast<int>(entry.maximum_exact_handoff_distance) << '\n'
               << entry.regression_slope << '\n'
               << entry.intercept << '\n'
               << entry.residual_sigma << '\n'
               << entry.confidence_multiplier << '\n';
      }
    }
  };
  write_options_identity(search_config.candidate_options);
  write_options_identity(search_config.baseline_options);
  for (const SelectedOpening& opening : openings) {
    output << opening.key << '\n'
           << opening.selection_hash << '\n'
           << arena::format_moves(opening.opening.moves) << '\n';
  }
  for (const GameRecord& game : games) {
    output << game.game_id << '\n'
           << game.opening_key << '\n'
           << game.side_assignment << '\n'
           << game.black_discs << '\n'
           << game.white_discs << '\n'
           << game.candidate_disc_diff << '\n'
           << game.normal_moves_after_opening << '\n'
           << game.passes_after_opening << '\n'
           << game.candidate_result << '\n'
           << game.reason << '\n'
           << game.failed << game.illegal << '\n';
    for (const GameRecord::SearchCall& call : game.search_calls) {
      const full_arena::SearchTelemetry& telemetry = call.telemetry;
      output << full_arena::engine_role_name(telemetry.role) << '\n'
             << telemetry.side_to_move << '\n'
             << telemetry.occupied_count << '\n'
             << telemetry.phase << '\n'
             << telemetry.completed_depth << '\n';
      write_search_stats_identity(output, telemetry.stats);
      output << telemetry.exact << telemetry.stopped << telemetry.exact_handoff_used
             << telemetry.exact_root_search << '\n'
             << call.best_move << '\n'
             << call.best_move_status << '\n';
    }
  }
  return output.str();
}

bool write_report(const Args& args, const LoadedEvaluator& candidate,
                  const LoadedEvaluator& baseline, std::string_view openings_checksum,
                  int input_opening_count, std::span<const SelectedOpening> openings,
                  const SearchConfig& search_config, std::span<const GameRecord> games,
                  const ArenaStats& stats, double elapsed_sec,
                  const std::filesystem::path& executable_path) {
  const std::filesystem::path parent = args.report_out.parent_path();
  if (!parent.empty()) {
    std::error_code error;
    std::filesystem::create_directories(parent, error);
    if (error) {
      std::cerr << "cannot create report directory: " << parent << ": " << error.message() << '\n';
      return false;
    }
  }
  std::ofstream output(args.report_out);
  if (!output) {
    std::cerr << "cannot write report: " << args.report_out << '\n';
    return false;
  }
  const bool same_artifact = same_runtime_artifact(candidate, baseline);
  const bool same_configuration = same_engine_configuration(candidate, baseline, search_config);
  const TelemetryBuckets telemetry = collect_telemetry(games);
  const std::vector<full_arena::PairGameResult> pair_game_records = pair_games(games);
  const std::vector<full_arena::PairObservation> pairs =
      full_arena::make_pair_observations(pair_game_records);
  const full_arena::BootstrapInterval paired_bootstrap =
      full_arena::paired_cluster_bootstrap(pairs, args.bootstrap_seed, args.bootstrap_samples);
  const full_arena::SanitySummary paired_sanity =
      full_arena::summarize_sanity(pairs, same_configuration);
  const full_arena::TelemetrySummary candidate_telemetry =
      full_arena::summarize_telemetry(telemetry.candidate);
  const full_arena::TelemetrySummary baseline_telemetry =
      full_arena::summarize_telemetry(telemetry.baseline);
  const full_arena::StrengthGateSummary strength_gate = full_arena::evaluate_strength_gate(
      static_cast<std::size_t>(stats.overall.failed_games),
      static_cast<std::size_t>(stats.overall.illegal_games), paired_sanity.incomplete_pairs,
      pairs.size(), static_cast<std::size_t>(args.minimum_opening_pairs),
      candidate_telemetry.search_calls != 0, baseline_telemetry.search_calls != 0);
  const std::string selected_checksum = selected_openings_checksum(openings);
  const std::string report_checksum = checksum_for(
      deterministic_payload(candidate, baseline, openings_checksum, openings, search_config,
                            args.seed, args.opening_limit, args.bootstrap_seed,
                            args.bootstrap_samples, args.minimum_opening_pairs, games));
  output << std::fixed << std::setprecision(6);
  output << "{\n";
  output << "  \"schema_version\": 4,\n";
  output << "  \"arena_version\": ";
  write_json_string(output, kArenaVersion);
  output << ",\n  \"candidate\": ";
  write_artifact_identity(output, candidate.identity);
  output << ",\n  \"baseline\": ";
  write_artifact_identity(output, baseline.identity);
  output << ",\n  \"inputs\": {\n";
  output << "    \"repository\": {\"configure_time_sha\": ";
  write_json_string(output, VIBE_OTHELLO_ARENA_REPOSITORY_SHA);
  output << ", \"configure_time_dirty\": ";
  write_bool(output, VIBE_OTHELLO_ARENA_SOURCE_DIRTY != 0);
  output << ", \"identity_source\": \"configure_time\"}";
  output << ",\n    \"executable\": {\"path\": ";
  write_json_string(output, executable_path.string());
  output << ", \"checksum\": ";
  write_json_string(output, checksum_file(executable_path));
  output << "},\n    \"build\": {\"compiler_id\": ";
  write_json_string(output, VIBE_OTHELLO_ARENA_COMPILER_ID);
  output << ", \"compiler_version\": ";
  write_json_string(output, VIBE_OTHELLO_ARENA_COMPILER_VERSION);
  output << ", \"build_type\": ";
  write_json_string(output, VIBE_OTHELLO_ARENA_BUILD_TYPE);
  output << "},\n    \"candidate_manifest_checksum\": ";
  write_json_string(output, candidate.identity.manifest_content_checksum);
  output << ",\n    \"candidate_weights_checksum\": ";
  write_json_string(output, candidate.identity.weights_file_checksum);
  output << ",\n    \"baseline_manifest_checksum\": ";
  write_json_string(output, baseline.identity.manifest_content_checksum);
  output << ",\n    \"baseline_weights_checksum\": ";
  write_json_string(output, baseline.identity.weights_file_checksum);
  output << "\n  }";
  output << ",\n  \"search_config\": ";
  write_search_config(output, search_config);
  output << ",\n  \"opening_source_path\": ";
  write_json_string(output, args.openings_path.string());
  output << ",\n  \"opening_source_checksum\": ";
  write_json_string(output, openings_checksum);
  output << ",\n  \"input_opening_count\": " << input_opening_count;
  output << ",\n  \"opening_count\": " << openings.size();
  output << ",\n  \"opening_limit\": ";
  if (args.opening_limit == 0) {
    output << "null";
  } else {
    output << args.opening_limit;
  }
  output << ",\n  \"seed\": " << args.seed;
  output
      << ",\n  \"opening_selection\": \"input-order when unlimited; FNV-1a sample when limited\"";
  output << ",\n  \"selected_openings_checksum\": ";
  write_json_string(output, selected_checksum);
  output << ",\n  \"selected_openings\": ";
  write_selected_openings(output, openings);
  output << ",\n  \"games\": " << games.size();
  output << ",\n  \"game_records\": ";
  write_games(output, games);
  output << ",\n  \"results\": {\n    \"overall\": ";
  write_bucket(output, stats.overall);
  output << ",\n    \"by_side_assignment\": ";
  write_bucket_map(output, stats.by_side_assignment);
  output << ",\n    \"by_opening\": ";
  write_bucket_map(output, stats.by_opening);
  output
      << ",\n    \"paired_score\": {\"method\": \"deterministic-cluster-bootstrap-opening-pair\"";
  output << ", \"point_estimate\": " << paired_bootstrap.point_estimate;
  output << ", \"lower_95\": " << paired_bootstrap.lower_95;
  output << ", \"upper_95\": " << paired_bootstrap.upper_95;
  output << ", \"opening_pair_count\": " << paired_bootstrap.opening_pair_count;
  output << ", \"game_count\": " << paired_bootstrap.game_count;
  output << ", \"candidate_wins\": " << stats.overall.candidate_wins;
  output << ", \"draws\": " << stats.overall.draws;
  output << ", \"candidate_losses\": " << stats.overall.baseline_wins;
  output << ", \"bootstrap_seed\": " << paired_bootstrap.seed;
  output << ", \"bootstrap_samples\": " << paired_bootstrap.samples;
  output << ", \"descriptive_only\": ";
  write_bool(output, !strength_gate.eligible);
  output << "}";
  output << "\n  },\n";
  output << "  \"failed_games\": " << stats.overall.failed_games << ",\n";
  output << "  \"illegal_games\": " << stats.overall.illegal_games << ",\n";
  output << "  \"elapsed_sec\": " << elapsed_sec << ",\n";
  output << "  \"telemetry\": {\n    \"candidate\": {\"overall\": ";
  write_telemetry_summary(output, candidate_telemetry);
  output << ", \"by_phase\": ";
  write_telemetry_map(output, telemetry.candidate_by_phase);
  output << ", \"by_side_to_move\": ";
  write_telemetry_map(output, telemetry.candidate_by_side_to_move);
  output << "},\n    \"baseline\": {\"overall\": ";
  write_telemetry_summary(output, baseline_telemetry);
  output << ", \"by_phase\": ";
  write_telemetry_map(output, telemetry.baseline_by_phase);
  output << ", \"by_side_to_move\": ";
  write_telemetry_map(output, telemetry.baseline_by_side_to_move);
  output << "}\n  },\n";
  output << "  \"same_artifact_sanity\": {\"same_runtime_artifact\": ";
  write_bool(output, same_artifact);
  output << ", \"same_search_configuration\": ";
  write_bool(output, same_configuration);
  output << ", \"applicable\": ";
  write_bool(output, paired_sanity.same_artifact_applicable);
  output << ", \"paired_color_swap\": ";
  write_bool(output, paired_sanity.paired_color_swap_complete);
  output << ", \"neutral\": ";
  write_bool(output, paired_sanity.same_artifact_neutral);
  output << "},\n";
  output << "  \"paired_sanity\": {\"paired_color_swap_complete\": ";
  write_bool(output, paired_sanity.paired_color_swap_complete);
  output << ", \"incomplete_pairs\": " << paired_sanity.incomplete_pairs;
  output << ", \"paired_disc_diff_sum\": " << paired_sanity.paired_disc_diff_sum;
  output << ", \"same_artifact_applicable\": ";
  write_bool(output, paired_sanity.same_artifact_applicable);
  output << ", \"same_artifact_neutral\": ";
  write_bool(output, paired_sanity.same_artifact_neutral);
  output << "},\n";
  output << "  \"strength_gate\": {\"eligible\": ";
  write_bool(output, strength_gate.eligible);
  output << ", \"minimum_opening_pairs\": " << args.minimum_opening_pairs;
  output << ", \"reasons\": [";
  for (std::size_t index = 0; index < strength_gate.reasons.size(); ++index) {
    if (index != 0) {
      output << ", ";
    }
    write_json_string(output, strength_gate.reasons[index]);
  }
  output << "]},\n";
  output << "  \"report_checksum_algorithm\": \"fnv1a64\",\n";
  output << "  \"report_checksum_scope\": \"deterministic config, runtime identities, selected "
            "openings, game results, and non-time telemetry; excludes paths and elapsed time\",\n";
  output << "  \"report_checksum\": ";
  write_json_string(output, report_checksum);
  output << ",\n  \"non_claim_notes\": [\n";
  output << "    \"local-only artifact-vs-artifact full-game harness\",\n";
  output << "    \"not an Elo result\",\n";
  output << "    \"not a production strength or artifact promotion claim\",\n";
  output << "    \"time-limited searches can change completed depths across machines; "
            "deterministic checksum smoke uses no time limit\",\n";
  output << "    \"generated arena reports, artifacts, and logs must not be committed\"\n";
  output << "  ]\n}\n";
  return true;
}

} // namespace

int main(int argc, char** argv) {
  const std::optional<Args> args = parse_args(argc, argv);
  if (!args.has_value()) {
    return 2;
  }
  const std::optional<std::string> openings_text =
      read_text_file(args->openings_path, "openings file");
  if (!openings_text.has_value()) {
    return 1;
  }
  std::string opening_error;
  const std::optional<std::vector<arena::Opening>> parsed_openings =
      arena::parse_openings_file(*openings_text, &opening_error);
  if (!parsed_openings.has_value()) {
    std::cerr << "invalid openings file: " << opening_error << '\n';
    return 1;
  }
  const std::vector<SelectedOpening> openings =
      select_openings(*parsed_openings, args->seed, args->opening_limit);
  if (openings.empty()) {
    std::cerr << "no openings selected\n";
    return 1;
  }

  std::optional<LoadedProbCutProfile> loaded_probcut_profile;
  std::optional<search::ProbCutCalibrationProfileV1> probcut_profile;
  if (args->probcut_profile_path.has_value()) {
    loaded_probcut_profile = load_probcut_profile(*args->probcut_profile_path);
    if (!loaded_probcut_profile.has_value()) {
      return 1;
    }
    if (args->probcut_maximum_probes > loaded_probcut_profile->validated_maximum_probes_per_node) {
      std::cerr << "requested ProbCut maximum probes exceeds reviewed scheduler evidence\n";
      return 1;
    }
    probcut_profile = loaded_probcut_profile->view();
  }

  std::optional<LoadedEvaluator> candidate =
      load_evaluator(args->candidate_name, args->candidate_manifest, args->candidate_weights);
  if (!candidate.has_value()) {
    return 1;
  }
  std::optional<LoadedEvaluator> baseline =
      load_evaluator(args->baseline_name, args->baseline_manifest, args->baseline_weights);
  if (!baseline.has_value()) {
    return 1;
  }
  const std::optional<SearchConfig> search_config =
      make_search_config(*args, probcut_profile.has_value() ? &*probcut_profile : nullptr,
                         candidate->identity, baseline->identity);
  if (!search_config.has_value()) {
    return 1;
  }
  const auto started = std::chrono::steady_clock::now();
  std::vector<GameRecord> games;
  games.reserve(openings.size() * 2U);
  int game_id = 1;
  for (const SelectedOpening& opening : openings) {
    games.push_back(play_game(game_id++, opening, true, *candidate, *baseline, *search_config));
    games.push_back(play_game(game_id++, opening, false, *candidate, *baseline, *search_config));
    if (args->progress_every > 0 &&
        games.size() % static_cast<std::size_t>(args->progress_every) == 0) {
      std::cerr << "full-game-artifact-arena progress games=" << games.size() << '\n';
    }
  }
  const double elapsed_sec =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  const ArenaStats stats = summarize(games);
  if (!write_report(*args, *candidate, *baseline, checksum_for(*openings_text),
                    static_cast<int>(parsed_openings->size()), openings, *search_config, games,
                    stats, elapsed_sec, std::filesystem::path{argv[0]})) {
    return 1;
  }
  std::cout << "games=" << games.size() << '\n';
  std::cout << "candidate_score_rate=" << std::fixed << std::setprecision(6)
            << score_rate(stats.overall) << '\n';
  std::cout << "report=" << args->report_out << '\n';
  return 0;
}
