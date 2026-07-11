#include "pattern_set_options.h"
#include "vibe_othello/evaluation/pattern_evaluator.h"
#include "vibe_othello/evaluation/pattern_weights.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
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
#include <vector>

namespace {

namespace board_core = vibe_othello::board_core;
namespace eval = vibe_othello::evaluation;
namespace pattern = vibe_othello::tools::pattern;

constexpr std::string_view kMoveTeacherHeaderV1 =
    "root_board_id\troot_record_id\troot_split\troot_phase\troot_empty_count\tmove\tchild_"
    "board_id\tchild_board_a1_to_h8\tchild_empty_count\tchild_phase\troot_move_score_side_to_"
    "move\tchild_label_score_side_to_move\tis_best_move\tbest_move_tie_count\tmove_rank\tbest_"
    "score_margin\tteacher_source\tteacher_depth\tteacher_nodes";
constexpr std::string_view kMoveTeacherHeaderV2 =
    "root_board_id\troot_record_id\troot_split\troot_phase\troot_empty_count\tmove\tchild_"
    "board_id\tchild_board_a1_to_h8\tchild_empty_count\tchild_phase\troot_move_score_side_to_"
    "move\tchild_label_score_side_to_move\tis_best_move\tbest_move_tie_count\tmove_rank\tbest_"
    "score_margin\tteacher_kind\tteacher_source\tteacher_artifact_id\tteacher_artifact_checksum\t"
    "teacher_depth\tteacher_nodes\tteacher_search_config_id";
constexpr std::uint32_t kCrc32Initial = 0xFFFF'FFFFU;
constexpr std::uint32_t kCrc32Polynomial = 0xEDB8'8320U;

struct Args {
  std::string move_teacher_path;
  std::string weights_path;
  std::string manifest_path;
  std::string pattern_set = "pattern-v2-endgame-lite";
  std::string report_out_path;
  std::string summary_out_path;
  double tie_margin = 0.0;
};

struct MoveTeacherRow {
  std::string root_board_id;
  std::string root_record_id;
  std::string root_split;
  int root_phase = 0;
  int root_empty_count = 0;
  std::string move;
  std::string child_board_id;
  std::string child_board;
  int child_empty_count = 0;
  int child_phase = 0;
  int root_move_score_side_to_move = 0;
  int child_label_score_side_to_move = 0;
  bool is_best_move = false;
  int best_move_tie_count = 0;
  int move_rank = 0;
  int best_score_margin = 0;
  std::string teacher_source;
  std::string teacher_kind;
  std::string teacher_artifact_id;
  std::string teacher_artifact_checksum;
  std::string teacher_search_config_id;
  std::string teacher_depth;
  std::string teacher_nodes;
};

struct LoadedArtifact {
  std::string pattern_set_id;
  std::string artifact_checksum;
  eval::PatternEvaluator evaluator;
};

struct ScoreRange {
  int min = 0;
  int max = 0;
  bool has_value = false;
};

struct MetricsAccumulator {
  int root_count = 0;
  int legal_move_count = 0;
  double top1_correct = 0.0;
  double top1_tie_aware_correct = 0.0;
  double best_in_top2 = 0.0;
  double pairwise_correct = 0.0;
  int pairwise_count = 0;
  int roots_with_all_moves_same_predicted_score = 0;
  std::vector<int> regrets;
  std::vector<int> exact_best_predicted_ranks;
  std::vector<int> predicted_best_exact_margins;
  ScoreRange predicted_score_range;
  double child_value_absolute_error = 0.0;
  double child_value_squared_error = 0.0;
  int child_value_count = 0;
};

struct EvaluationReport {
  std::string move_teacher_path;
  std::string weights_path;
  std::string manifest_path;
  std::string pattern_set;
  std::string artifact_checksum;
  double tie_margin = 0.0;
  MetricsAccumulator overall;
  std::map<std::string, MetricsAccumulator> by_empty_count;
  std::map<std::string, MetricsAccumulator> by_phase;
  std::map<std::string, MetricsAccumulator> by_split;
  ScoreRange static_score_range;
  std::uint64_t checksum = 14695981039346656037ull;
};

std::string_view trim_trailing_cr(std::string_view text) noexcept {
  if (!text.empty() && text.back() == '\r') {
    text.remove_suffix(1);
  }
  return text;
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

std::optional<double> parse_double(std::string_view text) {
  const std::string buffer{text};
  char* end = nullptr;
  errno = 0;
  const double value = std::strtod(buffer.c_str(), &end);
  if (errno == ERANGE || end != buffer.c_str() + buffer.size() || !std::isfinite(value)) {
    return std::nullopt;
  }
  return value;
}

void mix_fnv1a(std::string_view text, std::uint64_t* hash) noexcept {
  for (const char character : text) {
    *hash ^= static_cast<unsigned char>(character);
    *hash *= 1099511628211ull;
  }
}

void mix_checksum(std::string_view text, EvaluationReport* report) noexcept {
  mix_fnv1a(text, &report->checksum);
  report->checksum ^= static_cast<unsigned char>('\n');
  report->checksum *= 1099511628211ull;
}

std::string checksum_string(std::uint64_t checksum) {
  std::ostringstream output;
  output << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << checksum;
  return output.str();
}

std::string canonical_double(double value) {
  std::ostringstream output;
  output << std::setprecision(17) << value;
  return output.str();
}

std::string json_escape(std::string_view text) {
  std::string output;
  for (const char character : text) {
    switch (character) {
    case '\\':
      output += "\\\\";
      break;
    case '"':
      output += "\\\"";
      break;
    case '\n':
      output += "\\n";
      break;
    case '\r':
      output += "\\r";
      break;
    case '\t':
      output += "\\t";
      break;
    default:
      output.push_back(character);
      break;
    }
  }
  return output;
}

std::string report_path(std::string_view path_text) {
  const std::filesystem::path path{std::string(path_text)};
  if (path.is_absolute()) {
    return path.filename().string();
  }
  return path.lexically_normal().string();
}

bool next_value(int* index, int argc, char** argv, std::string* output) {
  if (*index + 1 >= argc) {
    std::cerr << argv[*index] << " requires a value\n";
    return false;
  }
  *output = argv[++(*index)];
  return true;
}

std::optional<Args> parse_args(int argc, char** argv) {
  Args args;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    if (arg == "--move-teacher") {
      if (!next_value(&index, argc, argv, &args.move_teacher_path)) {
        return std::nullopt;
      }
    } else if (arg == "--weights") {
      if (!next_value(&index, argc, argv, &args.weights_path)) {
        return std::nullopt;
      }
    } else if (arg == "--manifest") {
      if (!next_value(&index, argc, argv, &args.manifest_path)) {
        return std::nullopt;
      }
    } else if (arg == "--pattern-set") {
      if (!next_value(&index, argc, argv, &args.pattern_set)) {
        return std::nullopt;
      }
    } else if (arg == "--report-out") {
      if (!next_value(&index, argc, argv, &args.report_out_path)) {
        return std::nullopt;
      }
    } else if (arg == "--summary-out") {
      if (!next_value(&index, argc, argv, &args.summary_out_path)) {
        return std::nullopt;
      }
    } else if (arg == "--tie-margin") {
      std::string value;
      if (!next_value(&index, argc, argv, &value)) {
        return std::nullopt;
      }
      const std::optional<double> tie_margin = parse_double(value);
      if (!tie_margin.has_value() || *tie_margin < 0.0) {
        std::cerr << "--tie-margin must be a non-negative finite number\n";
        return std::nullopt;
      }
      args.tie_margin = *tie_margin;
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      return std::nullopt;
    }
  }
  if (args.move_teacher_path.empty() || args.weights_path.empty() || args.manifest_path.empty() ||
      args.pattern_set.empty() || args.report_out_path.empty() || args.summary_out_path.empty()) {
    std::cerr << "usage: vibe-othello-evaluate-move-teacher-ranking --move-teacher PATH "
                 "--weights PATH --manifest PATH --pattern-set NAME --report-out PATH "
                 "--summary-out PATH\n";
    return std::nullopt;
  }
  return args;
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

std::array<std::uint8_t, eval::PatternWeights::kDiscCountEntries> phase_by_disc_count_13() {
  std::array<std::uint8_t, eval::PatternWeights::kDiscCountEntries> phases{};
  for (std::uint8_t disc_count = 0; disc_count < phases.size(); ++disc_count) {
    const int normalized_count = disc_count < 4 ? 0 : static_cast<int>(disc_count) - 4;
    const int phase_id = std::min(12, (normalized_count * 13) / 60);
    phases[disc_count] = static_cast<std::uint8_t>(phase_id);
  }
  return phases;
}

std::optional<LoadedArtifact> load_artifact(const Args& args) {
  const std::optional<pattern::PatternSetOption> selected =
      pattern::select_pattern_set(args.pattern_set, pattern::IndexMode::raw);
  if (!selected.has_value() || selected->pattern_set == nullptr) {
    std::cerr << "--pattern-set must be " << pattern::pattern_set_option_names() << '\n';
    return std::nullopt;
  }

  const std::optional<std::string> manifest_text = read_text_file(args.manifest_path, "manifest");
  if (!manifest_text.has_value()) {
    return std::nullopt;
  }
  const std::optional<std::string> manifest_pattern_set =
      json_string_field(*manifest_text, "pattern_set_id");
  if (!manifest_pattern_set.has_value()) {
    std::cerr << "manifest is missing string field pattern_set_id: " << args.manifest_path << '\n';
    return std::nullopt;
  }
  if (*manifest_pattern_set != selected->pattern_set->id) {
    std::cerr << "manifest pattern_set_id mismatch: expected " << selected->pattern_set->id
              << ", got " << *manifest_pattern_set << '\n';
    return std::nullopt;
  }
  const std::optional<std::string> manifest_checksum =
      json_string_field(*manifest_text, "weights_checksum");
  if (!manifest_checksum.has_value()) {
    std::cerr << "manifest is missing string field weights_checksum: " << args.manifest_path
              << '\n';
    return std::nullopt;
  }

  const std::optional<std::vector<std::uint8_t>> artifact =
      read_binary_file(args.weights_path, "artifact weights");
  if (!artifact.has_value()) {
    return std::nullopt;
  }
  if (artifact->size() < sizeof(std::uint32_t)) {
    std::cerr << "artifact is too small: " << args.weights_path << '\n';
    return std::nullopt;
  }
  const std::string artifact_checksum = crc32_checksum_string(
      std::span<const std::uint8_t>(*artifact).first(artifact->size() - sizeof(std::uint32_t)));
  if (artifact_checksum != *manifest_checksum) {
    std::cerr << "artifact checksum mismatch: manifest " << *manifest_checksum << ", actual "
              << artifact_checksum << '\n';
    return std::nullopt;
  }

  const eval::PatternManifest manifest{
      .format_version = eval::kPatternWeightFormatVersion,
      .bit_order = eval::PatternBitOrder::a1_lsb,
      .score_unit = eval::PatternScoreUnit::disc_diff,
      .score_scale = 1,
      .phase_count = 13,
      .pattern_set_id = selected->pattern_set->id,
      .patterns = selected->pattern_set->patterns,
  };
  const eval::PatternWeightsLoadResult loaded = eval::load_pattern_weights(manifest, *artifact);
  if (!loaded.ok()) {
    std::cerr << "runtime loader rejected artifact\n";
    return std::nullopt;
  }
  std::optional<eval::PatternWeights> weights =
      eval::make_pattern_weights(*loaded.weights, phase_by_disc_count_13());
  if (!weights.has_value()) {
    std::cerr << "loaded artifact could not be converted to runtime weights\n";
    return std::nullopt;
  }
  try {
    return LoadedArtifact{
        .pattern_set_id = selected->pattern_set->id,
        .artifact_checksum = artifact_checksum,
        .evaluator = eval::PatternEvaluator{std::move(*weights), std::move(selected->feature_set)},
    };
  } catch (const std::exception& error) {
    std::cerr << "pattern evaluator rejected artifact: " << error.what() << '\n';
    return std::nullopt;
  }
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

bool parse_move_teacher_row(std::string_view line, bool schema_v2, MoveTeacherRow* row,
                            std::string* error) {
  const std::vector<std::string_view> fields = split_tabs(trim_trailing_cr(line));
  if (fields.size() != (schema_v2 ? 23u : 19u)) {
    *error = schema_v2 ? "expected 23 TSV fields for move-teacher schema v2"
                       : "expected 19 TSV fields for move-teacher schema v1";
    return false;
  }
  const std::optional<int> root_phase = parse_int(fields[3]);
  const std::optional<int> root_empty = parse_int(fields[4]);
  const std::optional<int> child_empty = parse_int(fields[8]);
  const std::optional<int> child_phase = parse_int(fields[9]);
  const std::optional<int> root_score = parse_int(fields[10]);
  const std::optional<int> child_score = parse_int(fields[11]);
  const std::optional<int> best_tie = parse_int(fields[13]);
  const std::optional<int> move_rank = parse_int(fields[14]);
  const std::optional<int> best_margin = parse_int(fields[15]);
  if (!root_phase.has_value() || !root_empty.has_value() || !child_empty.has_value() ||
      !child_phase.has_value() || !root_score.has_value() || !child_score.has_value() ||
      !best_tie.has_value() || !move_rank.has_value() || !best_margin.has_value()) {
    *error = "numeric fields must be integers";
    return false;
  }
  if (fields[12] != "0" && fields[12] != "1") {
    *error = "is_best_move must be 0 or 1";
    return false;
  }
  if (*root_score != -*child_score) {
    *error = "root_move_score_side_to_move must equal -child_label_score_side_to_move";
    return false;
  }
  if (fields[0].empty() || fields[5].empty() || fields[6].empty() || fields[7].empty()) {
    *error = "root_board_id, move, child_board_id, and child_board_a1_to_h8 must be non-empty";
    return false;
  }
  if (!position_from_relative_board(fields[7]).has_value()) {
    *error = "child_board_a1_to_h8 could not be converted to Position";
    return false;
  }

  row->root_board_id = std::string(fields[0]);
  row->root_record_id = std::string(fields[1]);
  row->root_split = std::string(fields[2]);
  row->root_phase = *root_phase;
  row->root_empty_count = *root_empty;
  row->move = std::string(fields[5]);
  row->child_board_id = std::string(fields[6]);
  row->child_board = std::string(fields[7]);
  row->child_empty_count = *child_empty;
  row->child_phase = *child_phase;
  row->root_move_score_side_to_move = *root_score;
  row->child_label_score_side_to_move = *child_score;
  row->is_best_move = fields[12] == "1";
  row->best_move_tie_count = *best_tie;
  row->move_rank = *move_rank;
  row->best_score_margin = *best_margin;
  if (schema_v2) {
    if (fields[16].empty() || fields[17].empty() || fields[18].empty() || fields[19].empty() ||
        fields[22].empty()) {
      *error = "schema v2 teacher identity fields must be non-empty";
      return false;
    }
    row->teacher_kind = std::string(fields[16]);
    row->teacher_source = std::string(fields[17]);
    row->teacher_artifact_id = std::string(fields[18]);
    row->teacher_artifact_checksum = std::string(fields[19]);
    row->teacher_depth = std::string(fields[20]);
    row->teacher_nodes = std::string(fields[21]);
    row->teacher_search_config_id = std::string(fields[22]);
  } else {
    row->teacher_kind = "exact";
    row->teacher_source = std::string(fields[16]);
    row->teacher_depth = std::string(fields[17]);
    row->teacher_nodes = std::string(fields[18]);
  }
  return true;
}

std::optional<std::map<std::string, std::vector<MoveTeacherRow>>>
load_move_teacher(const std::string& path, EvaluationReport* report) {
  std::ifstream input(path);
  if (!input) {
    std::cerr << "cannot read move-teacher TSV: " << path << '\n';
    return std::nullopt;
  }
  std::string line;
  if (!std::getline(input, line)) {
    std::cerr << "move-teacher TSV is empty\n";
    return std::nullopt;
  }
  const std::string_view header = trim_trailing_cr(line);
  const bool schema_v2 = header == kMoveTeacherHeaderV2;
  if (!schema_v2 && header != kMoveTeacherHeaderV1) {
    std::cerr << "move-teacher TSV header mismatch\n";
    return std::nullopt;
  }

  std::map<std::string, std::vector<MoveTeacherRow>> roots;
  int line_number = 1;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty()) {
      continue;
    }
    MoveTeacherRow row;
    std::string error;
    if (!parse_move_teacher_row(line, schema_v2, &row, &error)) {
      std::cerr << "line " << line_number << ": " << error << '\n';
      return std::nullopt;
    }
    mix_checksum(line, report);
    roots[row.root_board_id].push_back(std::move(row));
  }
  if (roots.empty()) {
    std::cerr << "move-teacher TSV has no rows\n";
    return std::nullopt;
  }
  for (auto& [root_board_id, rows] : roots) {
    (void)root_board_id;
    std::sort(rows.begin(), rows.end(),
              [](const MoveTeacherRow& left, const MoveTeacherRow& right) {
                return left.move < right.move;
              });
  }
  return roots;
}

void update_score_range(int score, ScoreRange* range) {
  if (!range->has_value) {
    range->min = score;
    range->max = score;
    range->has_value = true;
    return;
  }
  range->min = std::min(range->min, score);
  range->max = std::max(range->max, score);
}

int sign(int value) noexcept {
  if (value > 0) {
    return 1;
  }
  if (value < 0) {
    return -1;
  }
  return 0;
}

double mean_or_zero(const std::vector<int>& values) {
  if (values.empty()) {
    return 0.0;
  }
  return static_cast<double>(std::accumulate(values.begin(), values.end(), 0LL)) /
         static_cast<double>(values.size());
}

double median_or_zero(std::vector<int> values) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const std::size_t middle = values.size() / 2;
  if (values.size() % 2 == 0) {
    return (static_cast<double>(values[middle - 1]) + static_cast<double>(values[middle])) / 2.0;
  }
  return static_cast<double>(values[middle]);
}

void add_metrics(const MetricsAccumulator& root, MetricsAccumulator* total) {
  total->root_count += root.root_count;
  total->legal_move_count += root.legal_move_count;
  total->top1_correct += root.top1_correct;
  total->top1_tie_aware_correct += root.top1_tie_aware_correct;
  total->best_in_top2 += root.best_in_top2;
  total->pairwise_correct += root.pairwise_correct;
  total->pairwise_count += root.pairwise_count;
  total->roots_with_all_moves_same_predicted_score +=
      root.roots_with_all_moves_same_predicted_score;
  total->regrets.insert(total->regrets.end(), root.regrets.begin(), root.regrets.end());
  total->exact_best_predicted_ranks.insert(total->exact_best_predicted_ranks.end(),
                                           root.exact_best_predicted_ranks.begin(),
                                           root.exact_best_predicted_ranks.end());
  total->predicted_best_exact_margins.insert(total->predicted_best_exact_margins.end(),
                                             root.predicted_best_exact_margins.begin(),
                                             root.predicted_best_exact_margins.end());
  if (root.predicted_score_range.has_value) {
    update_score_range(root.predicted_score_range.min, &total->predicted_score_range);
    update_score_range(root.predicted_score_range.max, &total->predicted_score_range);
  }
  total->child_value_absolute_error += root.child_value_absolute_error;
  total->child_value_squared_error += root.child_value_squared_error;
  total->child_value_count += root.child_value_count;
}

MetricsAccumulator evaluate_root(const std::vector<MoveTeacherRow>& rows,
                                 const eval::PatternEvaluator& evaluator, double tie_margin) {
  struct Prediction {
    int index = 0;
    int predicted_root_score = 0;
  };

  std::vector<Prediction> predictions;
  predictions.reserve(rows.size());
  for (std::size_t index = 0; index < rows.size(); ++index) {
    const std::optional<board_core::Position> child =
        position_from_relative_board(rows[index].child_board);
    const int child_score = evaluator.evaluate(*child);
    const int predicted_root_score = -child_score;
    predictions.push_back(Prediction{
        .index = static_cast<int>(index),
        .predicted_root_score = predicted_root_score,
    });
  }

  std::sort(predictions.begin(), predictions.end(),
            [&](const Prediction& left, const Prediction& right) {
              if (left.predicted_root_score != right.predicted_root_score) {
                return left.predicted_root_score > right.predicted_root_score;
              }
              return rows[static_cast<std::size_t>(left.index)].move <
                     rows[static_cast<std::size_t>(right.index)].move;
            });

  const int exact_best_score =
      std::max_element(rows.begin(), rows.end(),
                       [](const MoveTeacherRow& left, const MoveTeacherRow& right) {
                         return left.root_move_score_side_to_move <
                                right.root_move_score_side_to_move;
                       })
          ->root_move_score_side_to_move;
  const int predicted_best_index = predictions.front().index;
  const int predicted_best_exact_score =
      rows[static_cast<std::size_t>(predicted_best_index)].root_move_score_side_to_move;
  const int best_predicted_score = predictions.front().predicted_root_score;
  const bool all_same_predicted =
      std::all_of(predictions.begin(), predictions.end(), [&](const Prediction& prediction) {
        return prediction.predicted_root_score == best_predicted_score;
      });

  bool predicted_top_tie_has_exact_best = false;
  for (const Prediction& prediction : predictions) {
    if (prediction.predicted_root_score != best_predicted_score) {
      break;
    }
    if (exact_best_score -
            rows[static_cast<std::size_t>(prediction.index)].root_move_score_side_to_move <=
        tie_margin) {
      predicted_top_tie_has_exact_best = true;
      break;
    }
  }

  bool top2_has_exact_best = false;
  for (std::size_t rank = 0; rank < std::min<std::size_t>(2, predictions.size()); ++rank) {
    if (exact_best_score -
            rows[static_cast<std::size_t>(predictions[rank].index)].root_move_score_side_to_move <=
        tie_margin) {
      top2_has_exact_best = true;
      break;
    }
  }

  int best_exact_rank = static_cast<int>(predictions.size());
  for (std::size_t rank = 0; rank < predictions.size(); ++rank) {
    if (exact_best_score -
            rows[static_cast<std::size_t>(predictions[rank].index)].root_move_score_side_to_move <=
        tie_margin) {
      best_exact_rank = static_cast<int>(rank + 1);
      break;
    }
  }

  MetricsAccumulator result;
  result.root_count = 1;
  result.legal_move_count = static_cast<int>(rows.size());
  result.top1_correct = exact_best_score - rows[static_cast<std::size_t>(predicted_best_index)]
                                               .root_move_score_side_to_move <=
                                tie_margin
                            ? 1.0
                            : 0.0;
  result.top1_tie_aware_correct = predicted_top_tie_has_exact_best ? 1.0 : 0.0;
  result.best_in_top2 = top2_has_exact_best ? 1.0 : 0.0;
  result.roots_with_all_moves_same_predicted_score = all_same_predicted ? 1 : 0;
  result.regrets.push_back(exact_best_score - predicted_best_exact_score);
  result.exact_best_predicted_ranks.push_back(best_exact_rank);
  result.predicted_best_exact_margins.push_back(predicted_best_exact_score - exact_best_score);
  for (const Prediction& prediction : predictions) {
    update_score_range(prediction.predicted_root_score, &result.predicted_score_range);
    const MoveTeacherRow& row = rows[static_cast<std::size_t>(prediction.index)];
    const int child_score = -prediction.predicted_root_score;
    const int value_error = child_score - row.child_label_score_side_to_move;
    result.child_value_absolute_error += std::abs(value_error);
    result.child_value_squared_error += static_cast<double>(value_error) * value_error;
    ++result.child_value_count;
  }

  for (std::size_t left = 0; left < rows.size(); ++left) {
    for (std::size_t right = left + 1; right < rows.size(); ++right) {
      const int exact_diff =
          rows[left].root_move_score_side_to_move - rows[right].root_move_score_side_to_move;
      if (std::abs(exact_diff) <= tie_margin) {
        continue;
      }
      const int predicted_left =
          std::find_if(predictions.begin(), predictions.end(), [&](const Prediction& prediction) {
            return prediction.index == static_cast<int>(left);
          })->predicted_root_score;
      const int predicted_right =
          std::find_if(predictions.begin(), predictions.end(), [&](const Prediction& prediction) {
            return prediction.index == static_cast<int>(right);
          })->predicted_root_score;
      const int predicted_diff = predicted_left - predicted_right;
      ++result.pairwise_count;
      if (predicted_diff == 0) {
        result.pairwise_correct += 0.5;
      } else if (sign(exact_diff) == sign(predicted_diff)) {
        result.pairwise_correct += 1.0;
      }
    }
  }
  return result;
}

void evaluate_all(const std::map<std::string, std::vector<MoveTeacherRow>>& roots,
                  const LoadedArtifact& artifact, EvaluationReport* report) {
  for (const auto& [root_board_id, rows] : roots) {
    (void)root_board_id;
    const MetricsAccumulator root_metrics =
        evaluate_root(rows, artifact.evaluator, report->tie_margin);
    add_metrics(root_metrics, &report->overall);
    if (root_metrics.predicted_score_range.has_value) {
      update_score_range(root_metrics.predicted_score_range.min, &report->static_score_range);
      update_score_range(root_metrics.predicted_score_range.max, &report->static_score_range);
    }
    const MoveTeacherRow& first = rows.front();
    add_metrics(root_metrics, &report->by_empty_count[std::to_string(first.root_empty_count)]);
    add_metrics(root_metrics, &report->by_phase[std::to_string(first.root_phase)]);
    add_metrics(root_metrics, &report->by_split[first.root_split]);
  }
}

void write_metrics_object(std::ofstream& output, const MetricsAccumulator& metrics) {
  const double root_count = static_cast<double>(metrics.root_count);
  output << "{";
  output << "\"root_count\": " << metrics.root_count;
  output << ", \"legal_move_count\": " << metrics.legal_move_count;
  output << ", \"top1_accuracy\": " << std::fixed << std::setprecision(6)
         << (metrics.root_count == 0 ? 0.0 : metrics.top1_correct / root_count);
  output << ", \"top1_tie_aware_accuracy\": " << std::fixed << std::setprecision(6)
         << (metrics.root_count == 0 ? 0.0 : metrics.top1_tie_aware_correct / root_count);
  output << ", \"best_move_in_top2_rate\": " << std::fixed << std::setprecision(6)
         << (metrics.root_count == 0 ? 0.0 : metrics.best_in_top2 / root_count);
  output << ", \"pairwise_accuracy\": " << std::fixed << std::setprecision(6)
         << (metrics.pairwise_count == 0
                 ? 0.0
                 : metrics.pairwise_correct / static_cast<double>(metrics.pairwise_count));
  output << ", \"pairwise_count\": " << metrics.pairwise_count;
  output << ", \"mean_teacher_regret\": " << std::fixed << std::setprecision(6)
         << mean_or_zero(metrics.regrets);
  output << ", \"median_teacher_regret\": " << std::fixed << std::setprecision(6)
         << median_or_zero(metrics.regrets);
  output << ", \"exact_best_predicted_score_rank_mean\": " << std::fixed << std::setprecision(6)
         << mean_or_zero(metrics.exact_best_predicted_ranks);
  output << ", \"exact_best_predicted_score_rank_median\": " << std::fixed << std::setprecision(6)
         << median_or_zero(metrics.exact_best_predicted_ranks);
  output << ", \"predicted_best_exact_margin_mean\": " << std::fixed << std::setprecision(6)
         << mean_or_zero(metrics.predicted_best_exact_margins);
  output << ", \"roots_with_all_moves_same_predicted_score\": "
         << metrics.roots_with_all_moves_same_predicted_score;
  output << ", \"predicted_score_range\": ";
  if (metrics.predicted_score_range.has_value) {
    output << "{\"min\": " << metrics.predicted_score_range.min
           << ", \"max\": " << metrics.predicted_score_range.max << "}";
  } else {
    output << "null";
  }
  output << ", \"value_MAE\": " << std::fixed << std::setprecision(6)
         << (metrics.child_value_count == 0
                 ? 0.0
                 : metrics.child_value_absolute_error / metrics.child_value_count);
  output << ", \"value_RMSE\": " << std::fixed << std::setprecision(6)
         << (metrics.child_value_count == 0
                 ? 0.0
                 : std::sqrt(metrics.child_value_squared_error / metrics.child_value_count));
  output << "}";
}

void write_metrics_map(std::ofstream& output,
                       const std::map<std::string, MetricsAccumulator>& values) {
  output << "{";
  bool first = true;
  for (const auto& [key, metrics] : values) {
    if (!first) {
      output << ", ";
    }
    first = false;
    output << "\"" << json_escape(key) << "\": ";
    write_metrics_object(output, metrics);
  }
  output << "}";
}

bool write_report(const std::string& path, const EvaluationReport& report) {
  std::ofstream output(path);
  if (!output) {
    std::cerr << "cannot write ranking report: " << path << '\n';
    return false;
  }
  output << "{\n";
  output << "  \"artifact_checksum\": \"" << json_escape(report.artifact_checksum) << "\",\n";
  output << "  \"checksum\": \"" << checksum_string(report.checksum) << "\",\n";
  output << "  \"manifest_path\": \"" << json_escape(report.manifest_path) << "\",\n";
  output << "  \"move_teacher_path\": \"" << json_escape(report.move_teacher_path) << "\",\n";
  output << "  \"notes\": [\n";
  output << "    \"artifact child-position scores are converted to root move scores with "
            "-eval(child)\",\n";
  output << "    \"top1_accuracy uses deterministic predicted tie breaking by move name\",\n";
  output << "    \"top1_tie_aware_accuracy accepts any exact-best move in the predicted top-score "
            "tie\",\n";
  output << "    \"predicted_best_exact_margin_mean is exact(predicted best) - exact(best), so "
            "non-positive is expected\",\n";
  output << "    \"local-only ranking diagnostic\",\n";
  output << "    \"not an Elo result\",\n";
  output << "    \"not self-play\",\n";
  output << "    \"not a production strength claim\"\n";
  output << "  ],\n";
  output << "  \"pattern_set\": \"" << json_escape(report.pattern_set) << "\",\n";
  output << "  \"tie_margin\": " << std::fixed << std::setprecision(6) << report.tie_margin
         << ",\n";
  output << "  \"results_by_empty_count\": ";
  write_metrics_map(output, report.by_empty_count);
  output << ",\n";
  output << "  \"results_by_phase\": ";
  write_metrics_map(output, report.by_phase);
  output << ",\n";
  output << "  \"results_by_split\": ";
  write_metrics_map(output, report.by_split);
  output << ",\n";
  output << "  \"root_count\": " << report.overall.root_count << ",\n";
  output << "  \"legal_move_count\": " << report.overall.legal_move_count << ",\n";
  output << "  \"top1_accuracy\": " << std::fixed << std::setprecision(6)
         << (report.overall.root_count == 0
                 ? 0.0
                 : report.overall.top1_correct / static_cast<double>(report.overall.root_count))
         << ",\n";
  output << "  \"top1_tie_aware_accuracy\": " << std::fixed << std::setprecision(6)
         << (report.overall.root_count == 0 ? 0.0
                                            : report.overall.top1_tie_aware_correct /
                                                  static_cast<double>(report.overall.root_count))
         << ",\n";
  output << "  \"best_move_in_top2_rate\": " << std::fixed << std::setprecision(6)
         << (report.overall.root_count == 0
                 ? 0.0
                 : report.overall.best_in_top2 / static_cast<double>(report.overall.root_count))
         << ",\n";
  output << "  \"pairwise_accuracy\": " << std::fixed << std::setprecision(6)
         << (report.overall.pairwise_count == 0
                 ? 0.0
                 : report.overall.pairwise_correct /
                       static_cast<double>(report.overall.pairwise_count))
         << ",\n";
  output << "  \"pairwise_count\": " << report.overall.pairwise_count << ",\n";
  output << "  \"mean_teacher_regret\": " << std::fixed << std::setprecision(6)
         << mean_or_zero(report.overall.regrets) << ",\n";
  output << "  \"median_teacher_regret\": " << std::fixed << std::setprecision(6)
         << median_or_zero(report.overall.regrets) << ",\n";
  output << "  \"exact_best_predicted_score_rank_mean\": " << std::fixed << std::setprecision(6)
         << mean_or_zero(report.overall.exact_best_predicted_ranks) << ",\n";
  output << "  \"exact_best_predicted_score_rank_median\": " << std::fixed << std::setprecision(6)
         << median_or_zero(report.overall.exact_best_predicted_ranks) << ",\n";
  output << "  \"predicted_best_exact_margin_mean\": " << std::fixed << std::setprecision(6)
         << mean_or_zero(report.overall.predicted_best_exact_margins) << ",\n";
  output << "  \"roots_with_all_moves_same_predicted_score\": "
         << report.overall.roots_with_all_moves_same_predicted_score << ",\n";
  output << "  \"predicted_score_range\": ";
  if (report.overall.predicted_score_range.has_value) {
    output << "{\"min\": " << report.overall.predicted_score_range.min
           << ", \"max\": " << report.overall.predicted_score_range.max << "},\n";
  } else {
    output << "null,\n";
  }
  output << "  \"value_MAE\": " << std::fixed << std::setprecision(6)
         << (report.overall.child_value_count == 0
                 ? 0.0
                 : report.overall.child_value_absolute_error / report.overall.child_value_count)
         << ",\n";
  output << "  \"value_RMSE\": " << std::fixed << std::setprecision(6)
         << (report.overall.child_value_count == 0
                 ? 0.0
                 : std::sqrt(report.overall.child_value_squared_error /
                             report.overall.child_value_count))
         << ",\n";
  output << "  \"static_score_range\": ";
  if (report.static_score_range.has_value) {
    output << "{\"min\": " << report.static_score_range.min
           << ", \"max\": " << report.static_score_range.max << "},\n";
  } else {
    output << "null,\n";
  }
  output << "  \"weights_path\": \"" << json_escape(report.weights_path) << "\"\n";
  output << "}\n";
  return true;
}

bool write_summary(const std::string& path, const EvaluationReport& report) {
  std::ofstream output(path);
  if (!output) {
    std::cerr << "cannot write ranking summary: " << path << '\n';
    return false;
  }
  const double roots = static_cast<double>(report.overall.root_count);
  output << "# Move-Teacher Ranking Summary\n\n";
  output << "Local-only ranking diagnostic. Not Elo, not self-play, not a production strength "
            "claim.\n\n";
  output << "- pattern set: " << report.pattern_set << '\n';
  output << "- roots: " << report.overall.root_count << '\n';
  output << "- legal moves: " << report.overall.legal_move_count << '\n';
  output << "- top1 accuracy: " << std::fixed << std::setprecision(6)
         << (report.overall.root_count == 0 ? 0.0 : report.overall.top1_correct / roots) << '\n';
  output << "- tie-aware top1 accuracy: " << std::fixed << std::setprecision(6)
         << (report.overall.root_count == 0 ? 0.0 : report.overall.top1_tie_aware_correct / roots)
         << '\n';
  output << "- top2 contains exact best: " << std::fixed << std::setprecision(6)
         << (report.overall.root_count == 0 ? 0.0 : report.overall.best_in_top2 / roots) << '\n';
  output << "- pairwise accuracy: " << std::fixed << std::setprecision(6)
         << (report.overall.pairwise_count == 0
                 ? 0.0
                 : report.overall.pairwise_correct /
                       static_cast<double>(report.overall.pairwise_count))
         << '\n';
  output << "- mean teacher regret: " << std::fixed << std::setprecision(6)
         << mean_or_zero(report.overall.regrets) << '\n';
  output << "- median teacher regret: " << std::fixed << std::setprecision(6)
         << median_or_zero(report.overall.regrets) << '\n';
  output << "- all-same predicted roots: "
         << report.overall.roots_with_all_moves_same_predicted_score << '\n';
  return true;
}

} // namespace

int main(int argc, char** argv) {
  const std::optional<Args> args = parse_args(argc, argv);
  if (!args.has_value()) {
    return 2;
  }

  std::optional<LoadedArtifact> artifact = load_artifact(*args);
  if (!artifact.has_value()) {
    return 1;
  }

  EvaluationReport report{
      .move_teacher_path = report_path(args->move_teacher_path),
      .weights_path = report_path(args->weights_path),
      .manifest_path = report_path(args->manifest_path),
      .pattern_set = args->pattern_set,
      .artifact_checksum = artifact->artifact_checksum,
      .tie_margin = args->tie_margin,
  };
  mix_checksum(artifact->artifact_checksum, &report);
  mix_checksum("tie_margin=" + canonical_double(args->tie_margin), &report);

  const std::optional<std::map<std::string, std::vector<MoveTeacherRow>>> roots =
      load_move_teacher(args->move_teacher_path, &report);
  if (!roots.has_value()) {
    return 1;
  }

  evaluate_all(*roots, *artifact, &report);
  if (!write_report(args->report_out_path, report)) {
    return 1;
  }
  if (!write_summary(args->summary_out_path, report)) {
    return 1;
  }

  std::cout << "report=" << args->report_out_path << '\n';
  std::cout << "summary=" << args->summary_out_path << '\n';
  std::cout << "checksum=" << checksum_string(report.checksum) << '\n';
  return 0;
}
