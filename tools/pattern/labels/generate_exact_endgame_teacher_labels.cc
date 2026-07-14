#include "vibe_othello/board_core/board.h"
#include "vibe_othello/search/search.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kNormalizedHeaderV1 =
    "record_id\tposition_id\tsource_dataset_id\tsplit\tboard_a1_to_h8\tlabel_kind\tlabel_"
    "unit\tlabel_perspective\tlabel_score_side_to_move\toccupied_count\tphase\tplayer_disc_"
    "count\topponent_disc_count\tempty_count";
constexpr std::string_view kNormalizedHeaderV2 =
    "record_id\tposition_id\tgame_group_id\tboard_id\tsource_occurrence_id\tsource_dataset_id\t"
    "split\tboard_a1_to_h8\tlabel_kind\tlabel_unit\tlabel_perspective\tlabel_score_side_to_"
    "move\toccupied_count\tphase\tplayer_disc_count\topponent_disc_count\tempty_count";
constexpr std::string_view kTeacherHeader =
    "board_id\tlabel_kind\tlabel_unit\tlabel_perspective\tlabel_score_side_to_move\tteacher_"
    "source\tteacher_depth\tteacher_nodes";

struct Args {
  std::string normalized_tsv_path;
  std::string teacher_labels_out_path;
  std::string report_out_path;
  int max_empty = 12;
  std::optional<std::size_t> max_positions;
  std::uint64_t seed = 0;
  std::size_t progress_every = 0;
};

struct Candidate {
  std::string board_id;
  std::string board;
  int empty_count = 0;
  std::uint64_t sample_key = 0;
};

struct LabelRow {
  std::string board_id;
  int score = 0;
  int depth = 0;
  vibe_othello::search::NodeCount nodes = 0;
};

struct Report {
  int schema_version = 1;
  std::string normalized_input_path;
  std::string teacher_labels_out;
  int max_empty = 0;
  std::optional<std::size_t> max_positions;
  std::uint64_t seed = 0;
  std::size_t input_rows = 0;
  std::size_t eligible_rows = 0;
  std::size_t selected_rows = 0;
  std::size_t unique_boards_seen = 0;
  std::size_t unique_boards_solved = 0;
  std::size_t skipped_too_many_empty = 0;
  std::size_t duplicate_board_rows = 0;
  std::size_t solve_failures = 0;
  std::optional<int> teacher_depth_min;
  std::optional<int> teacher_depth_max;
  std::uint64_t teacher_nodes_sum = 0;
  std::optional<int> label_score_min;
  std::optional<int> label_score_max;
  std::int64_t label_score_sum = 0;
  std::uint64_t checksum = 14695981039346656037ull;
  double wall_time_sec = 0.0;
  double positions_per_sec = 0.0;
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

std::uint64_t fnv1a64_update(std::uint64_t hash, std::string_view text) noexcept {
  for (const char character : text) {
    hash ^= static_cast<unsigned char>(character);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::uint64_t fnv1a64(std::string_view text) noexcept {
  return fnv1a64_update(14695981039346656037ull, text);
}

std::uint64_t sample_key(std::string_view board_id, std::uint64_t seed) noexcept {
  std::uint64_t hash = 14695981039346656037ull;
  hash = fnv1a64_update(hash, std::to_string(seed));
  hash = fnv1a64_update(hash, "\t");
  hash = fnv1a64_update(hash, board_id);
  return hash;
}

void mix_checksum(std::string_view text, Report* report) noexcept {
  report->checksum = fnv1a64_update(report->checksum, text);
  report->checksum ^= static_cast<unsigned char>('\n');
  report->checksum *= 1099511628211ull;
}

std::string checksum_string(std::uint64_t checksum) {
  std::ostringstream output;
  output << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << checksum;
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

bool parse_required_value(int* index, int argc, char** argv, std::string* value) {
  if (*index + 1 >= argc) {
    std::cerr << argv[*index] << " requires a value\n";
    return false;
  }
  *value = argv[++(*index)];
  return true;
}

std::optional<Args> parse_args(int argc, char** argv) {
  Args args;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    if (arg == "--normalized-tsv") {
      if (!parse_required_value(&index, argc, argv, &args.normalized_tsv_path)) {
        return std::nullopt;
      }
    } else if (arg == "--teacher-labels-out") {
      if (!parse_required_value(&index, argc, argv, &args.teacher_labels_out_path)) {
        return std::nullopt;
      }
    } else if (arg == "--report-out") {
      if (!parse_required_value(&index, argc, argv, &args.report_out_path)) {
        return std::nullopt;
      }
    } else if (arg == "--max-empty") {
      std::string value;
      if (!parse_required_value(&index, argc, argv, &value)) {
        return std::nullopt;
      }
      const std::optional<int> parsed = parse_int(value);
      if (!parsed.has_value() || *parsed < 0 || *parsed > 64) {
        std::cerr << "--max-empty must be an integer in [0, 64]\n";
        return std::nullopt;
      }
      args.max_empty = *parsed;
    } else if (arg == "--max-positions") {
      std::string value;
      if (!parse_required_value(&index, argc, argv, &value)) {
        return std::nullopt;
      }
      const std::optional<std::uint64_t> parsed = parse_u64(value);
      if (!parsed.has_value() || *parsed == 0 ||
          *parsed > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        std::cerr << "--max-positions must be a positive integer\n";
        return std::nullopt;
      }
      args.max_positions = static_cast<std::size_t>(*parsed);
    } else if (arg == "--seed") {
      std::string value;
      if (!parse_required_value(&index, argc, argv, &value)) {
        return std::nullopt;
      }
      const std::optional<std::uint64_t> parsed = parse_u64(value);
      if (!parsed.has_value()) {
        std::cerr << "--seed must be a non-negative integer\n";
        return std::nullopt;
      }
      args.seed = *parsed;
    } else if (arg == "--progress-every") {
      std::string value;
      if (!parse_required_value(&index, argc, argv, &value)) {
        return std::nullopt;
      }
      const std::optional<std::uint64_t> parsed = parse_u64(value);
      if (!parsed.has_value() ||
          *parsed > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        std::cerr << "--progress-every must be a non-negative integer\n";
        return std::nullopt;
      }
      args.progress_every = static_cast<std::size_t>(*parsed);
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      return std::nullopt;
    }
  }
  if (args.normalized_tsv_path.empty() || args.teacher_labels_out_path.empty() ||
      args.report_out_path.empty()) {
    std::cerr << "usage: vibe-othello-generate-exact-endgame-teacher-labels "
                 "--normalized-tsv PATH --teacher-labels-out PATH --report-out PATH "
                 "[--max-empty N] [--max-positions N] [--seed N] [--progress-every N]\n";
    return std::nullopt;
  }
  return args;
}

bool validate_board_counts(std::string_view board, int occupied_count, int player_disc_count,
                           int opponent_disc_count, int empty_count, std::string* error) {
  if (board.size() != vibe_othello::board_core::kSquareCount) {
    *error = "board_a1_to_h8 must be exactly 64 characters";
    return false;
  }
  int actual_player = 0;
  int actual_opponent = 0;
  int actual_empty = 0;
  for (const char value : board) {
    if (value == 'X') {
      ++actual_player;
    } else if (value == 'O') {
      ++actual_opponent;
    } else if (value == '-') {
      ++actual_empty;
    } else {
      *error = "board_a1_to_h8 contains invalid character";
      return false;
    }
  }
  if (actual_player != player_disc_count || actual_opponent != opponent_disc_count ||
      actual_empty != empty_count || actual_player + actual_opponent != occupied_count ||
      occupied_count + empty_count != vibe_othello::board_core::kSquareCount) {
    *error = "board counts do not match count columns";
    return false;
  }
  return true;
}

std::optional<vibe_othello::board_core::Position>
position_from_a1_to_h8_board(std::string_view board) noexcept {
  namespace board_core = vibe_othello::board_core;

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

bool parse_normalized_row(std::string_view line, int line_number, Candidate* candidate,
                          std::string* error) {
  const std::vector<std::string_view> fields = split_tabs(trim_trailing_cr(line));
  if (fields.size() != 17) {
    *error = "expected 17 TSV fields for normalized schema v2";
    return false;
  }
  if (fields[3].empty()) {
    *error = "board_id must be non-empty";
    return false;
  }
  if (fields[7].empty()) {
    *error = "board_a1_to_h8 must be non-empty";
    return false;
  }
  const std::optional<int> label = parse_int(fields[11]);
  const std::optional<int> occupied = parse_int(fields[12]);
  const std::optional<int> phase = parse_int(fields[13]);
  const std::optional<int> player_count = parse_int(fields[14]);
  const std::optional<int> opponent_count = parse_int(fields[15]);
  const std::optional<int> empty_count = parse_int(fields[16]);
  if (!label.has_value() || !occupied.has_value() || !phase.has_value() ||
      !player_count.has_value() || !opponent_count.has_value() || !empty_count.has_value()) {
    *error = "numeric fields must be integers";
    return false;
  }
  if (*label < -64 || *label > 64) {
    *error = "label_score_side_to_move must be in [-64, 64]";
    return false;
  }
  if (*phase < 0 || *phase > 12) {
    *error = "phase must be in [0, 12]";
    return false;
  }
  if (!validate_board_counts(fields[7], *occupied, *player_count, *opponent_count, *empty_count,
                             error)) {
    return false;
  }
  candidate->board_id = std::string(fields[3]);
  candidate->board = std::string(fields[7]);
  candidate->empty_count = *empty_count;
  (void)line_number;
  return true;
}

bool load_candidates(const Args& args, std::vector<Candidate>* candidates, Report* report) {
  std::ifstream input(args.normalized_tsv_path);
  if (!input) {
    std::cerr << "cannot read normalized TSV: " << args.normalized_tsv_path << '\n';
    return false;
  }

  std::string line;
  if (!std::getline(input, line)) {
    std::cerr << "normalized TSV is empty\n";
    return false;
  }
  const std::string_view header = trim_trailing_cr(line);
  if (header == kNormalizedHeaderV1) {
    std::cerr << "normalized TSV must use schema v2; schema v1 is not supported\n";
    return false;
  }
  if (header != kNormalizedHeaderV2) {
    std::cerr << "normalized TSV must use schema v2 header\n";
    return false;
  }

  std::set<std::string> seen_boards;
  std::map<std::string, Candidate> unique_eligible;
  int line_number = 1;
  while (std::getline(input, line)) {
    ++line_number;
    ++report->input_rows;
    Candidate candidate;
    std::string error;
    if (!parse_normalized_row(line, line_number, &candidate, &error)) {
      std::cerr << "line " << line_number << ": " << error << '\n';
      return false;
    }

    const bool duplicate_seen = !seen_boards.insert(candidate.board_id).second;
    if (duplicate_seen) {
      ++report->duplicate_board_rows;
    }
    if (candidate.empty_count > args.max_empty) {
      ++report->skipped_too_many_empty;
      continue;
    }
    ++report->eligible_rows;
    if (unique_eligible.contains(candidate.board_id)) {
      continue;
    }
    candidate.sample_key = sample_key(candidate.board_id, args.seed);
    unique_eligible.emplace(candidate.board_id, std::move(candidate));
  }

  report->unique_boards_seen = seen_boards.size();
  candidates->reserve(unique_eligible.size());
  for (auto& [board_id, candidate] : unique_eligible) {
    (void)board_id;
    candidates->push_back(std::move(candidate));
  }
  return true;
}

std::vector<Candidate> select_candidates(std::vector<Candidate> candidates,
                                         std::optional<std::size_t> max_positions) {
  if (max_positions.has_value() && candidates.size() > *max_positions) {
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& left, const Candidate& right) {
                if (left.sample_key != right.sample_key) {
                  return left.sample_key < right.sample_key;
                }
                return left.board_id < right.board_id;
              });
    candidates.resize(*max_positions);
  }
  std::sort(
      candidates.begin(), candidates.end(),
      [](const Candidate& left, const Candidate& right) { return left.board_id < right.board_id; });
  return candidates;
}

std::optional<LabelRow> solve_candidate(const Candidate& candidate, std::string* error) {
  const std::optional<vibe_othello::board_core::Position> position =
      position_from_a1_to_h8_board(candidate.board);
  if (!position.has_value()) {
    *error = "board_a1_to_h8 could not be converted to Position";
    return std::nullopt;
  }

  vibe_othello::search::SearchOptions options;
  options.endgame.use_endgame_tt = true;
  const vibe_othello::search::SearchResult result = vibe_othello::search::solve_exact_endgame(
      *position, vibe_othello::search::SearchLimits{}, options);
  if (result.stopped || !result.exact ||
      result.score_kind != vibe_othello::search::ScoreKind::exact_disc_diff) {
    *error = "exact endgame solve did not complete";
    return std::nullopt;
  }
  if (result.score < -64 || result.score > 64) {
    *error = "exact endgame score outside [-64, 64]";
    return std::nullopt;
  }
  return LabelRow{
      .board_id = candidate.board_id,
      .score = result.score,
      .depth = candidate.empty_count,
      .nodes = result.stats.endgame_nodes != 0 ? result.stats.endgame_nodes : result.nodes,
  };
}

void update_report_for_label(const LabelRow& row, Report* report) {
  report->teacher_depth_min = report->teacher_depth_min.has_value()
                                  ? std::min(*report->teacher_depth_min, row.depth)
                                  : row.depth;
  report->teacher_depth_max = report->teacher_depth_max.has_value()
                                  ? std::max(*report->teacher_depth_max, row.depth)
                                  : row.depth;
  report->teacher_nodes_sum += row.nodes;
  report->label_score_min = report->label_score_min.has_value()
                                ? std::min(*report->label_score_min, row.score)
                                : row.score;
  report->label_score_max = report->label_score_max.has_value()
                                ? std::max(*report->label_score_max, row.score)
                                : row.score;
  report->label_score_sum += row.score;
}

std::string teacher_line(const LabelRow& row) {
  std::ostringstream output;
  output << row.board_id << "\tteacher_exact_final_disc_diff\tdisc\tside_to_move\t" << row.score
         << "\texact-endgame-v1\t" << row.depth << '\t' << row.nodes;
  return output.str();
}

bool write_teacher_labels(const std::string& path, const std::vector<LabelRow>& rows,
                          Report* report) {
  std::ofstream output(path);
  if (!output) {
    std::cerr << "cannot write teacher label TSV: " << path << '\n';
    return false;
  }
  output << kTeacherHeader << '\n';
  for (const LabelRow& row : rows) {
    const std::string line = teacher_line(row);
    output << line << '\n';
    mix_checksum(line, report);
  }
  return true;
}

void write_optional_int(std::ofstream& output, std::string_view key, std::optional<int> value) {
  output << "  \"" << key << "\": ";
  if (value.has_value()) {
    output << *value;
  } else {
    output << "null";
  }
  output << ",\n";
}

bool write_report(const std::string& path, const Report& report) {
  std::ofstream output(path);
  if (!output) {
    std::cerr << "cannot write report JSON: " << path << '\n';
    return false;
  }
  output << "{\n";
  output << "  \"checksum\": \"" << checksum_string(report.checksum) << "\",\n";
  output << "  \"duplicate_board_rows\": " << report.duplicate_board_rows << ",\n";
  output << "  \"eligible_rows\": " << report.eligible_rows << ",\n";
  output << "  \"input_rows\": " << report.input_rows << ",\n";
  write_optional_int(output, "label_score_max", report.label_score_max);
  write_optional_int(output, "label_score_min", report.label_score_min);
  output << "  \"label_score_sum\": " << report.label_score_sum << ",\n";
  output << "  \"max_empty\": " << report.max_empty << ",\n";
  output << "  \"max_positions\": ";
  if (report.max_positions.has_value()) {
    output << *report.max_positions;
  } else {
    output << "null";
  }
  output << ",\n";
  output << "  \"normalized_input_path\": \"" << json_escape(report.normalized_input_path)
         << "\",\n";
  output << "  \"notes\": [\n";
  output << "    \"local-only exact teacher label generation\",\n";
  output << "    \"low-empty/endgame-only coverage\",\n";
  output << "    \"not a strength claim\",\n";
  output << "    \"not an Elo result\",\n";
  output << "    \"not match bench\",\n";
  output << "    \"not self-play\",\n";
  output << "    \"not a production artifact\",\n";
  output << "    \"generated labels must not be committed\",\n";
  output << "    \"teacher label TSV rows are emitted in sorted board_id order\"\n";
  output << "  ],\n";
  output << "  \"positions_per_sec\": " << std::fixed << std::setprecision(6)
         << report.positions_per_sec << ",\n";
  output << "  \"schema_version\": " << report.schema_version << ",\n";
  output << "  \"seed\": " << report.seed << ",\n";
  output << "  \"selected_rows\": " << report.selected_rows << ",\n";
  output << "  \"skipped_too_many_empty\": " << report.skipped_too_many_empty << ",\n";
  output << "  \"solve_failures\": " << report.solve_failures << ",\n";
  write_optional_int(output, "teacher_depth_max", report.teacher_depth_max);
  write_optional_int(output, "teacher_depth_min", report.teacher_depth_min);
  output << "  \"teacher_labels_out\": \"" << json_escape(report.teacher_labels_out) << "\",\n";
  output << "  \"teacher_nodes_sum\": " << report.teacher_nodes_sum << ",\n";
  output << "  \"unique_boards_seen\": " << report.unique_boards_seen << ",\n";
  output << "  \"unique_boards_solved\": " << report.unique_boards_solved << ",\n";
  output << "  \"wall_time_sec\": " << std::fixed << std::setprecision(6) << report.wall_time_sec
         << "\n";
  output << "}\n";
  return true;
}

} // namespace

int main(int argc, char** argv) {
  const auto started = std::chrono::steady_clock::now();
  const std::optional<Args> args = parse_args(argc, argv);
  if (!args.has_value()) {
    return 2;
  }

  Report report{
      .normalized_input_path = report_path(args->normalized_tsv_path),
      .teacher_labels_out = report_path(args->teacher_labels_out_path),
      .max_empty = args->max_empty,
      .max_positions = args->max_positions,
      .seed = args->seed,
  };

  std::vector<Candidate> candidates;
  if (!load_candidates(*args, &candidates, &report)) {
    return 1;
  }
  if (report.eligible_rows == 0 || candidates.empty()) {
    std::cerr << "no eligible normalized schema v2 rows with empty_count <= " << args->max_empty
              << '\n';
    return 1;
  }

  std::vector<Candidate> selected = select_candidates(std::move(candidates), args->max_positions);
  report.selected_rows = selected.size();
  std::vector<LabelRow> labels;
  labels.reserve(selected.size());
  for (std::size_t index = 0; index < selected.size(); ++index) {
    const Candidate& candidate = selected[index];
    std::string error;
    const std::optional<LabelRow> label = solve_candidate(candidate, &error);
    if (!label.has_value()) {
      ++report.solve_failures;
      std::cerr << candidate.board_id << ": " << error << '\n';
      continue;
    }
    labels.push_back(*label);
    update_report_for_label(*label, &report);
    if (args->progress_every != 0 && labels.size() % args->progress_every == 0) {
      std::cerr << "progress solved=" << labels.size() << " selected=" << selected.size() << '\n';
    }
  }
  report.unique_boards_solved = labels.size();

  const auto finished = std::chrono::steady_clock::now();
  report.wall_time_sec = std::chrono::duration<double>(finished - started).count();
  if (report.wall_time_sec > 0.0) {
    report.positions_per_sec =
        static_cast<double>(report.unique_boards_solved) / report.wall_time_sec;
  }

  if (labels.empty()) {
    std::cerr << "no teacher labels solved\n";
    return 1;
  }
  if (report.solve_failures != 0) {
    std::cerr << "exact teacher label generation had solve failures: " << report.solve_failures
              << '\n';
    return 1;
  }
  if (!write_teacher_labels(args->teacher_labels_out_path, labels, &report)) {
    return 1;
  }
  if (!write_report(args->report_out_path, report)) {
    return 1;
  }

  std::cout << "teacher_labels=" << args->teacher_labels_out_path << '\n';
  std::cout << "report=" << args->report_out_path << '\n';
  std::cout << "checksum=" << checksum_string(report.checksum) << '\n';
  return 0;
}
