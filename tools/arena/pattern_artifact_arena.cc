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
  int max_empty = 12;
  int max_positions = 0;
  std::uint64_t seed = 0;
  bool side_swap = false;
  int progress_every = 0;
  search::Depth depth = 3;
};

struct PatternRuntime {
  const eval::PatternSet* pattern_set = nullptr;
  eval::PatternFeatureSet feature_set;
};

struct LoadedEvaluator {
  std::string name;
  std::string weights_path;
  std::string manifest_path;
  std::string artifact_checksum;
  eval::PatternEvaluator evaluator;
};

struct PositionRow {
  std::string board_id;
  std::string board_text;
  std::string split;
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

void print_usage() {
  std::cerr << "usage: vibe-othello-pattern-artifact-arena "
               "--positions-tsv PATH "
               "--candidate-weights PATH --candidate-manifest PATH --candidate-name NAME "
               "--baseline-weights PATH --baseline-manifest PATH --baseline-name NAME "
               "--report-out PATH --summary-out PATH "
               "[--max-empty 12] [--max-positions N] [--seed 0] [--side-swap] "
               "[--depth 3] [--progress-every N]\n";
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
    return LoadedEvaluator{
        .name = std::move(name),
        .weights_path = std::move(weights_path),
        .manifest_path = std::move(manifest_path),
        .artifact_checksum = artifact_checksum,
        .evaluator = eval::PatternEvaluator{std::move(*weights), std::move(runtime->feature_set)},
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
    add_or_replace_selected(&result.selected,
                            PositionRow{
                                .board_id = board_id,
                                .board_text = board_text,
                                .split = std::string{fields[*split_index]},
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
  std::vector<GameResult> results;
  results.reserve(positions->selected.size() * (args->side_swap ? 2U : 1U));
  int games_played = 0;
  for (const PositionRow& row : positions->selected) {
    results.push_back(play_game(row, *candidate, *baseline, true, args->depth));
    ++games_played;
    if (args->progress_every > 0 && games_played % args->progress_every == 0) {
      std::cerr << "pattern-artifact-arena progress games=" << games_played << '\n';
    }
    if (args->side_swap) {
      results.push_back(play_game(row, *candidate, *baseline, false, args->depth));
      ++games_played;
      if (args->progress_every > 0 && games_played % args->progress_every == 0) {
        std::cerr << "pattern-artifact-arena progress games=" << games_played << '\n';
      }
    }
  }
  const auto end = std::chrono::steady_clock::now();
  const double wall_time_sec = std::chrono::duration<double>(end - start).count();
  const double games_per_sec =
      wall_time_sec > 0.0 ? static_cast<double>(results.size()) / wall_time_sec : 0.0;
  if (args->progress_every > 0) {
    std::cerr << "pattern-artifact-arena complete games=" << results.size() << '\n';
  }

  const ArenaStats stats = summarize_results(results);
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

  std::cout << "games_played=" << stats.games_played << '\n';
  std::cout << "candidate_score_rate=" << std::fixed << std::setprecision(6)
            << stats.candidate_score_rate << '\n';
  std::cout << "average_disc_diff_candidate_perspective=" << stats.average_disc_diff << '\n';
  return 0;
}
