#include "index_mode.h"
#include "pattern_set_options.h"
#include "replay_records.h"
#include "schema_validation.h"
#include "smoke_fixture.h"
#include "vibe_othello/board_core/coordinates.h"
#include "vibe_othello/evaluation/pattern_feature_set.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

enum class SplitPolicy {
  record_hash,
  tiny_cycle,
};

enum class OutputFormat {
  expanded_tsv,
  compact_tsv,
};

struct Args {
  std::string records_path;
  std::string normalized_tsv_path;
  std::string manifest_path;
  std::string report_path;
  SplitPolicy split_policy = SplitPolicy::record_hash;
  vibe_othello::tools::pattern::IndexMode index_mode = vibe_othello::tools::pattern::IndexMode::raw;
  std::string pattern_set = "tiny";
  OutputFormat output_format = OutputFormat::expanded_tsv;
};

struct NormalizedRow {
  std::string record_id;
  std::string position_id;
  std::string game_group_id;
  std::string board_id;
  std::string source_occurrence_id;
  std::string source_dataset_id;
  std::string split;
  std::string board;
  std::string label_kind;
  std::string label_unit;
  std::string label_perspective;
  int label_score_side_to_move = 0;
  int occupied_count = 0;
  int phase = 0;
  int player_disc_count = 0;
  int opponent_disc_count = 0;
  int empty_count = 0;
};

struct ReportSummary {
  int schema_version = 1;
  int input_rows = 0;
  int accepted_rows = 0;
  int rejected_rows = 0;
  int example_rows = 0;
  int feature_occurrence_count = 0;
  int max_features_per_example = 0;
  std::uint64_t total_table_entries = 0;
  int repeated_position_count = 0;
  int exact_duplicate_record_count = 0;
  int cross_split_board_collision_count = 0;
  OutputFormat output_format = OutputFormat::expanded_tsv;
  std::string pattern_set_id;
  std::string pattern_contract_digest;
  vibe_othello::tools::pattern::IndexMode index_mode = vibe_othello::tools::pattern::IndexMode::raw;
  std::vector<std::map<std::string, std::string>> feature_families;
  std::set<std::string> source_dataset_ids;
  std::set<std::string> game_group_ids;
  std::set<std::string> board_ids;
  std::map<std::string, int> cross_split_board_collision_counts_by_pair;
  std::map<std::string, int> counts_by_split;
  std::map<int, int> counts_by_phase;
  std::map<std::string, int> counts_by_label_kind;
  std::optional<int> label_min;
  std::optional<int> label_max;
  std::int64_t label_sum = 0;
  std::uint64_t checksum = 14695981039346656037ull;
};

std::optional<SplitPolicy> parse_split_policy(std::string_view text) {
  if (text == "record-hash") {
    return SplitPolicy::record_hash;
  }
  if (text == "tiny-cycle") {
    return SplitPolicy::tiny_cycle;
  }
  return std::nullopt;
}

std::string_view split_policy_name(SplitPolicy policy) noexcept {
  switch (policy) {
  case SplitPolicy::record_hash:
    return "record-hash";
  case SplitPolicy::tiny_cycle:
    return "tiny-cycle";
  }
  return "unknown";
}

std::optional<OutputFormat> parse_output_format(std::string_view text) {
  if (text == "expanded-tsv") {
    return OutputFormat::expanded_tsv;
  }
  if (text == "compact-tsv") {
    return OutputFormat::compact_tsv;
  }
  return std::nullopt;
}

std::string_view output_format_name(OutputFormat format) noexcept {
  switch (format) {
  case OutputFormat::expanded_tsv:
    return "expanded-tsv";
  case OutputFormat::compact_tsv:
    return "compact-tsv";
  }
  return "unknown";
}

std::string_view index_mode_name(vibe_othello::tools::pattern::IndexMode mode) noexcept {
  switch (mode) {
  case vibe_othello::tools::pattern::IndexMode::raw:
    return "raw";
  case vibe_othello::tools::pattern::IndexMode::canonical:
    return "canonical";
  }
  return "unknown";
}

std::optional<Args> parse_args(int argc, char** argv) {
  Args args;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    if (arg == "--records") {
      if (index + 1 >= argc) {
        std::cerr << "--records requires a value\n";
        return std::nullopt;
      }
      args.records_path = argv[++index];
    } else if (arg == "--normalized-tsv") {
      if (index + 1 >= argc) {
        std::cerr << "--normalized-tsv requires a value\n";
        return std::nullopt;
      }
      args.normalized_tsv_path = argv[++index];
    } else if (arg == "--manifest") {
      if (index + 1 >= argc) {
        std::cerr << "--manifest requires a value\n";
        return std::nullopt;
      }
      args.manifest_path = argv[++index];
    } else if (arg == "--report") {
      if (index + 1 >= argc) {
        std::cerr << "--report requires a value\n";
        return std::nullopt;
      }
      args.report_path = argv[++index];
    } else if (arg == "--split-policy") {
      if (index + 1 >= argc) {
        std::cerr << "--split-policy requires a value\n";
        return std::nullopt;
      }
      const std::optional<SplitPolicy> policy = parse_split_policy(argv[++index]);
      if (!policy.has_value()) {
        std::cerr << "--split-policy must be record-hash or tiny-cycle\n";
        return std::nullopt;
      }
      args.split_policy = *policy;
    } else if (arg == "--index-mode") {
      if (index + 1 >= argc) {
        std::cerr << "--index-mode requires a value\n";
        return std::nullopt;
      }
      const std::optional<vibe_othello::tools::pattern::IndexMode> mode =
          vibe_othello::tools::pattern::parse_index_mode(argv[++index]);
      if (!mode.has_value()) {
        std::cerr << "--index-mode must be raw or canonical\n";
        return std::nullopt;
      }
      args.index_mode = *mode;
    } else if (arg == "--pattern-set") {
      if (index + 1 >= argc) {
        std::cerr << "--pattern-set requires a value\n";
        return std::nullopt;
      }
      args.pattern_set = argv[++index];
    } else if (arg == "--output-format") {
      if (index + 1 >= argc) {
        std::cerr << "--output-format requires a value\n";
        return std::nullopt;
      }
      const std::optional<OutputFormat> format = parse_output_format(argv[++index]);
      if (!format.has_value()) {
        std::cerr << "--output-format must be expanded-tsv or compact-tsv\n";
        return std::nullopt;
      }
      args.output_format = *format;
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      return std::nullopt;
    }
  }

  if (args.records_path.empty() == args.normalized_tsv_path.empty()) {
    std::cerr << "usage: vibe-othello-pattern-dataset-smoke "
                 "(--records PATH [--manifest PATH] [--split-policy record-hash|tiny-cycle] | "
                 "--normalized-tsv PATH --report PATH) [--index-mode raw|canonical] "
                 "[--pattern-set tiny|buro-lite] [--output-format expanded-tsv|compact-tsv]\n";
    return std::nullopt;
  }
  if (!args.normalized_tsv_path.empty() && args.report_path.empty()) {
    std::cerr << "--normalized-tsv requires --report\n";
    return std::nullopt;
  }
  return args;
}

std::uint64_t fnv1a64(std::string_view text) noexcept {
  std::uint64_t hash = 14695981039346656037ull;
  for (const char character : text) {
    hash ^= static_cast<unsigned char>(character);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string_view split_for_hash(std::string_view record_id) noexcept {
  switch (fnv1a64(record_id) % 10) {
  case 0:
    return "validation";
  case 1:
    return "test";
  default:
    return "train";
  }
}

std::string_view split_for_tiny_cycle(std::size_t accepted_ply_ordinal) noexcept {
  switch (accepted_ply_ordinal % 3) {
  case 0:
    return "train";
  case 1:
    return "validation";
  default:
    return "test";
  }
}

std::string_view split_for(SplitPolicy policy, std::string_view record_id,
                           std::size_t accepted_ply_ordinal) noexcept {
  switch (policy) {
  case SplitPolicy::record_hash:
    return split_for_hash(record_id);
  case SplitPolicy::tiny_cycle:
    return split_for_tiny_cycle(accepted_ply_ordinal);
  }
  return "train";
}

struct EmitSummary {
  int rows = 0;
  int example_rows = 0;
  int feature_occurrence_count = 0;
  int max_features_per_example = 0;
  int train_rows = 0;
  int validation_rows = 0;
  int test_rows = 0;
};

void count_split(std::string_view split, EmitSummary* summary) noexcept {
  if (split == "train") {
    ++summary->train_rows;
  } else if (split == "validation") {
    ++summary->validation_rows;
  } else if (split == "test") {
    ++summary->test_rows;
  }
}

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

void mix_checksum(std::string_view text, ReportSummary* summary) noexcept {
  for (const char character : text) {
    summary->checksum ^= static_cast<unsigned char>(character);
    summary->checksum *= 1099511628211ull;
  }
  summary->checksum ^= static_cast<unsigned char>('\n');
  summary->checksum *= 1099511628211ull;
}

std::string checksum_string(std::uint64_t checksum) {
  std::ostringstream output;
  output << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << checksum;
  return output.str();
}

std::string digest_string(std::uint64_t checksum) {
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::nouppercase << std::setfill('0') << std::setw(16)
         << checksum;
  return output.str();
}

std::string square_name(vibe_othello::board_core::Square square) {
  const int file = vibe_othello::board_core::file_of(square);
  const int rank = vibe_othello::board_core::rank_of(square);
  if (file < 0 || rank < 0) {
    return "invalid";
  }

  std::string name;
  name.push_back(static_cast<char>('a' + file));
  name.push_back(static_cast<char>('1' + rank));
  return name;
}

std::string_view
symmetry_policy_name(vibe_othello::evaluation::PatternSymmetryPolicy symmetry_policy) noexcept {
  switch (symmetry_policy) {
  case vibe_othello::evaluation::PatternSymmetryPolicy::none:
    return "none";
  case vibe_othello::evaluation::PatternSymmetryPolicy::reverse:
    return "reverse";
  case vibe_othello::evaluation::PatternSymmetryPolicy::square_d4:
    return "square-d4";
  }
  return "unknown";
}

std::vector<std::string> square_names(std::span<const vibe_othello::board_core::Square> squares) {
  std::vector<std::string> names;
  names.reserve(squares.size());
  for (const vibe_othello::board_core::Square square : squares) {
    names.push_back(square_name(square));
  }
  return names;
}

std::string join_strings(const std::vector<std::string>& values) {
  std::string joined;
  bool first = true;
  for (const std::string& value : values) {
    if (!first) {
      joined.push_back(',');
    }
    first = false;
    joined += value;
  }
  return joined;
}

void append_digest_line(std::string* input, std::string_view line) {
  input->append(line);
  input->push_back('\n');
}

std::uint64_t fnv1a64_digest(std::string_view text) noexcept {
  std::uint64_t hash = 14695981039346656037ull;
  for (const char character : text) {
    hash ^= static_cast<unsigned char>(character);
    hash *= 1099511628211ull;
  }
  return hash;
}

bool validate_split(std::string_view split) noexcept {
  return split == "train" || split == "validation" || split == "test";
}

bool validate_label_kind(int schema_version, std::string_view label_kind) noexcept {
  if (schema_version == 1) {
    return label_kind == "engine_disc_estimate";
  }
  return label_kind == "observed_final_disc_diff" ||
         label_kind == "teacher_exact_final_disc_diff" ||
         label_kind == "teacher_exact_move_child_final_disc_diff" ||
         label_kind == "teacher_search_final_disc_diff" ||
         label_kind == "teacher_static_eval_disc_diff";
}

bool validate_label_unit(std::string_view label_unit) noexcept {
  return label_unit == "final_disc_diff" || label_unit == "disc";
}

int egaroucid_phase_for_occupied_count(int occupied_count, int schema_version) noexcept {
  const int occupied_bucket_count = 60;
  return std::min(12, ((occupied_count - 4) * 13) / occupied_bucket_count);
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

std::string split_pair(std::string_view left, std::string_view right) {
  if (right < left) {
    std::swap(left, right);
  }
  return std::string(left) + "__" + std::string(right);
}

bool parse_normalized_row(std::string_view line, int line_number, int schema_version,
                          NormalizedRow* row, std::string* error) {
  static constexpr std::string_view kExpectedHeaderV1 =
      "record_id\tposition_id\tsource_dataset_id\tsplit\tboard_a1_to_h8\tlabel_kind\tlabel_"
      "unit\tlabel_perspective\tlabel_score_side_to_move\toccupied_count\tphase\tplayer_disc_"
      "count\topponent_disc_count\tempty_count";
  static constexpr std::string_view kExpectedHeaderV2 =
      "record_id\tposition_id\tgame_group_id\tboard_id\tsource_occurrence_id\tsource_dataset_id\t"
      "split\tboard_a1_to_h8\tlabel_kind\tlabel_unit\tlabel_perspective\tlabel_score_side_to_"
      "move\toccupied_count\tphase\tplayer_disc_count\topponent_disc_count\tempty_count";

  const std::vector<std::string_view> fields = split_tabs(trim_trailing_cr(line));
  const std::size_t expected_field_count = schema_version == 2 ? 17 : 14;
  if (fields.size() != expected_field_count) {
    *error = schema_version == 2 ? "expected 17 TSV fields" : "expected 14 TSV fields";
    return false;
  }
  if (line_number == 1 && ((schema_version == 1 && trim_trailing_cr(line) == kExpectedHeaderV1) ||
                           (schema_version == 2 && trim_trailing_cr(line) == kExpectedHeaderV2))) {
    *error = "header";
    return false;
  }

  row->record_id = std::string(fields[0]);
  row->position_id = std::string(fields[1]);
  std::size_t offset = 2;
  if (schema_version == 2) {
    row->game_group_id = std::string(fields[offset++]);
    row->board_id = std::string(fields[offset++]);
    row->source_occurrence_id = std::string(fields[offset++]);
  }
  row->source_dataset_id = std::string(fields[offset++]);
  row->split = std::string(fields[offset++]);
  row->board = std::string(fields[offset++]);
  row->label_kind = std::string(fields[offset++]);
  row->label_unit = std::string(fields[offset++]);
  row->label_perspective = std::string(fields[offset++]);

  if (row->record_id.empty() || row->position_id.empty() || row->source_dataset_id.empty()) {
    *error = "record_id, position_id, and source_dataset_id must be non-empty";
    return false;
  }
  if (schema_version == 2 &&
      (row->game_group_id.empty() || row->board_id.empty() || row->source_occurrence_id.empty())) {
    *error = "game_group_id, board_id, and source_occurrence_id must be non-empty";
    return false;
  }
  if (!validate_split(row->split)) {
    *error = "split must be train, validation, or test";
    return false;
  }
  if (row->board.size() != vibe_othello::board_core::kSquareCount) {
    *error = "board_a1_to_h8 must be exactly 64 characters";
    return false;
  }
  if (!std::all_of(row->board.begin(), row->board.end(),
                   [](char value) { return value == 'X' || value == 'O' || value == '-'; })) {
    *error = "board_a1_to_h8 contains invalid character";
    return false;
  }
  if (!validate_label_kind(schema_version, row->label_kind)) {
    *error = schema_version == 1
                 ? "v1 label_kind must be engine_disc_estimate"
                 : "v2 label_kind must be observed_final_disc_diff or a teacher label kind";
    return false;
  }
  if (!validate_label_unit(row->label_unit)) {
    *error = "label_unit must be final_disc_diff or disc";
    return false;
  }
  if (row->label_perspective != "side_to_move") {
    *error = "label_perspective must be side_to_move";
    return false;
  }

  const std::optional<int> label = parse_int(fields[offset++]);
  const std::optional<int> occupied = parse_int(fields[offset++]);
  const std::optional<int> phase = parse_int(fields[offset++]);
  const std::optional<int> player_count = parse_int(fields[offset++]);
  const std::optional<int> opponent_count = parse_int(fields[offset++]);
  const std::optional<int> empty_count = parse_int(fields[offset++]);
  if (!label.has_value() || !occupied.has_value() || !phase.has_value() ||
      !player_count.has_value() || !opponent_count.has_value() || !empty_count.has_value()) {
    *error = "numeric fields must be integers";
    return false;
  }
  if (*label < -64 || *label > 64) {
    *error = "label_score_side_to_move must be in [-64, 64]";
    return false;
  }
  const int max_occupied = schema_version == 2 ? 64 : 63;
  if (*occupied < 4 || *occupied > max_occupied) {
    *error = schema_version == 2 ? "occupied_count must be in [4, 64]"
                                 : "occupied_count must be in [4, 63]";
    return false;
  }
  if (*phase < 0 || *phase > 12) {
    *error = "phase must be in [0, 12]";
    return false;
  }

  row->label_score_side_to_move = *label;
  row->occupied_count = *occupied;
  row->phase = *phase;
  row->player_disc_count = *player_count;
  row->opponent_disc_count = *opponent_count;
  row->empty_count = *empty_count;

  const int actual_player_count =
      static_cast<int>(std::count(row->board.begin(), row->board.end(), 'X'));
  const int actual_opponent_count =
      static_cast<int>(std::count(row->board.begin(), row->board.end(), 'O'));
  const int actual_empty_count =
      static_cast<int>(std::count(row->board.begin(), row->board.end(), '-'));
  if (row->player_disc_count != actual_player_count ||
      row->opponent_disc_count != actual_opponent_count || row->empty_count != actual_empty_count ||
      row->occupied_count != actual_player_count + actual_opponent_count) {
    *error = "board counts do not match count columns";
    return false;
  }
  if (row->empty_count + row->occupied_count != vibe_othello::board_core::kSquareCount) {
    *error = "occupied_count plus empty_count must equal 64";
    return false;
  }
  if (row->phase != egaroucid_phase_for_occupied_count(row->occupied_count, schema_version)) {
    *error = "phase must match occupied_count";
    return false;
  }
  return true;
}

bool load_normalized_rows(const std::string& path, std::vector<NormalizedRow>* rows,
                          ReportSummary* report) {
  static constexpr std::string_view kExpectedHeaderV1 =
      "record_id\tposition_id\tsource_dataset_id\tsplit\tboard_a1_to_h8\tlabel_kind\tlabel_"
      "unit\tlabel_perspective\tlabel_score_side_to_move\toccupied_count\tphase\tplayer_disc_"
      "count\topponent_disc_count\tempty_count";
  static constexpr std::string_view kExpectedHeaderV2 =
      "record_id\tposition_id\tgame_group_id\tboard_id\tsource_occurrence_id\tsource_dataset_id\t"
      "split\tboard_a1_to_h8\tlabel_kind\tlabel_unit\tlabel_perspective\tlabel_score_side_to_"
      "move\toccupied_count\tphase\tplayer_disc_count\topponent_disc_count\tempty_count";

  std::ifstream input(path);
  if (!input) {
    std::cerr << "cannot read normalized TSV: " << path << '\n';
    return false;
  }

  std::map<std::string, std::string> split_by_position_id;
  std::map<std::string, std::set<std::string>> splits_by_board_id;
  std::map<std::string, int> rows_by_position_id;
  std::set<std::string> exact_duplicate_keys;
  bool ok = true;
  bool saw_header = false;
  std::string line;
  int line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (!saw_header) {
      saw_header = true;
      if (trim_trailing_cr(line) == kExpectedHeaderV1) {
        report->schema_version = 1;
      } else if (trim_trailing_cr(line) == kExpectedHeaderV2) {
        report->schema_version = 2;
      } else {
        std::cerr << "line " << line_number << ": unexpected normalized TSV header\n";
        return false;
      }
      continue;
    }
    if (line.empty()) {
      continue;
    }

    ++report->input_rows;
    NormalizedRow row;
    std::string error;
    if (!parse_normalized_row(line, line_number, report->schema_version, &row, &error)) {
      ++report->rejected_rows;
      ok = false;
      std::cerr << "line " << line_number << ": " << error << '\n';
      continue;
    }

    const auto [split_it, inserted] = split_by_position_id.emplace(row.position_id, row.split);
    if (!inserted && split_it->second != row.split) {
      ++report->rejected_rows;
      ok = false;
      std::cerr << "line " << line_number << ": same position_id appears in multiple splits\n";
      continue;
    }

    const std::string duplicate_key =
        row.position_id + "\t" + row.board + "\t" + std::to_string(row.label_score_side_to_move);
    if (!exact_duplicate_keys.insert(duplicate_key).second) {
      ++report->exact_duplicate_record_count;
    }
    if (++rows_by_position_id[row.position_id] == 2) {
      ++report->repeated_position_count;
    }

    report->source_dataset_ids.insert(row.source_dataset_id);
    if (report->schema_version == 2) {
      report->game_group_ids.insert(row.game_group_id);
      report->board_ids.insert(row.board_id);
      splits_by_board_id[row.board_id].insert(row.split);
    }
    ++report->counts_by_split[row.split];
    ++report->counts_by_phase[row.phase];
    ++report->counts_by_label_kind[row.label_kind];
    report->label_min = report->label_min.has_value()
                            ? std::min(*report->label_min, row.label_score_side_to_move)
                            : row.label_score_side_to_move;
    report->label_max = report->label_max.has_value()
                            ? std::max(*report->label_max, row.label_score_side_to_move)
                            : row.label_score_side_to_move;
    report->label_sum += row.label_score_side_to_move;
    ++report->accepted_rows;
    mix_checksum(line, report);
    rows->push_back(std::move(row));
  }

  if (!saw_header) {
    std::cerr << "normalized TSV is empty\n";
    return false;
  }
  if (report->accepted_rows == 0) {
    std::cerr << "normalized TSV has no accepted rows\n";
    return false;
  }
  if (report->schema_version == 2) {
    for (const auto& [board_id, splits] : splits_by_board_id) {
      if (splits.size() < 2) {
        continue;
      }
      ++report->cross_split_board_collision_count;
      for (auto left = splits.begin(); left != splits.end(); ++left) {
        for (auto right = std::next(left); right != splits.end(); ++right) {
          ++report->cross_split_board_collision_counts_by_pair[split_pair(*left, *right)];
        }
      }
    }
  }
  return ok;
}

std::string json_escape(std::string_view text) {
  std::string result;
  for (const char character : text) {
    if (character == '"' || character == '\\') {
      result.push_back('\\');
      result.push_back(character);
    } else if (character == '\n') {
      result += "\\n";
    } else {
      result.push_back(character);
    }
  }
  return result;
}

void write_string_array(std::ostream& output, const std::set<std::string>& values) {
  output << '[';
  bool first = true;
  for (const std::string& value : values) {
    if (!first) {
      output << ", ";
    }
    first = false;
    output << '"' << json_escape(value) << '"';
  }
  output << ']';
}

void write_feature_family_array(std::ostream& output,
                                const std::vector<std::map<std::string, std::string>>& families) {
  output << "[";
  for (std::size_t index = 0; index < families.size(); ++index) {
    if (index != 0) {
      output << ", ";
    }
    output << "{";
    bool first = true;
    for (const auto& [key, value] : families[index]) {
      if (!first) {
        output << ", ";
      }
      first = false;
      output << '"' << key << "\": " << value;
    }
    output << "}";
  }
  output << "]";
}

template <typename Key>
void write_count_map(std::ostream& output, const std::map<Key, int>& counts) {
  output << "{";
  bool first = true;
  for (const auto& [key, value] : counts) {
    if (!first) {
      output << ", ";
    }
    first = false;
    output << '"' << key << "\": " << value;
  }
  output << "}";
}

template <> void write_count_map<int>(std::ostream& output, const std::map<int, int>& counts) {
  output << "{";
  bool first = true;
  for (const auto& [key, value] : counts) {
    if (!first) {
      output << ", ";
    }
    first = false;
    output << '"' << key << "\": " << value;
  }
  output << "}";
}

bool write_report(const std::string& path, const ReportSummary& report) {
  std::ofstream output(path);
  if (!output) {
    std::cerr << "cannot write dataset report: " << path << '\n';
    return false;
  }

  const double label_mean = report.accepted_rows == 0
                                ? 0.0
                                : static_cast<double>(report.label_sum) / report.accepted_rows;
  const double average_features =
      report.example_rows == 0
          ? 0.0
          : static_cast<double>(report.feature_occurrence_count) / report.example_rows;
  output << "{\n";
  output << "  \"schema_version\": " << report.schema_version << ",\n";
  output << "  \"normalized_schema_version\": " << report.schema_version << ",\n";
  output << "  \"output_format\": \"" << output_format_name(report.output_format) << "\",\n";
  output << "  \"example_rows\": " << report.example_rows << ",\n";
  output << "  \"feature_occurrence_count\": " << report.feature_occurrence_count << ",\n";
  output << "  \"average_features_per_example\": " << std::fixed << std::setprecision(6)
         << average_features << ",\n";
  output << "  \"max_features_per_example\": " << report.max_features_per_example << ",\n";
  output << "  \"pattern_set_id\": \"" << json_escape(report.pattern_set_id) << "\",\n";
  output << "  \"pattern_contract_digest\": \"" << json_escape(report.pattern_contract_digest)
         << "\",\n";
  output << "  \"index_mode\": \"" << index_mode_name(report.index_mode) << "\",\n";
  output << "  \"feature_families\": ";
  write_feature_family_array(output, report.feature_families);
  output << ",\n";
  output << "  \"total_table_entries\": " << report.total_table_entries << ",\n";
  output << "  \"source_dataset_ids\": ";
  write_string_array(output, report.source_dataset_ids);
  output << ",\n";
  output << "  \"input_rows\": " << report.input_rows << ",\n";
  output << "  \"accepted_rows\": " << report.accepted_rows << ",\n";
  output << "  \"rejected_rows\": " << report.rejected_rows << ",\n";
  output << "  \"counts_by_split\": ";
  write_count_map(output, report.counts_by_split);
  output << ",\n";
  output << "  \"counts_by_phase\": ";
  write_count_map(output, report.counts_by_phase);
  output << ",\n";
  output << "  \"counts_by_label_kind\": ";
  write_count_map(output, report.counts_by_label_kind);
  output << ",\n";
  output << "  \"label_min\": " << report.label_min.value_or(0) << ",\n";
  output << "  \"label_max\": " << report.label_max.value_or(0) << ",\n";
  output << "  \"label_mean\": " << std::fixed << std::setprecision(6) << label_mean << ",\n";
  output << "  \"repeated_position_count\": " << report.repeated_position_count << ",\n";
  output << "  \"exact_duplicate_record_count\": " << report.exact_duplicate_record_count << ",\n";
  if (report.schema_version == 2) {
    output << "  \"game_group_count\": " << report.game_group_ids.size() << ",\n";
    output << "  \"unique_board_count\": " << report.board_ids.size() << ",\n";
    output << "  \"cross_split_board_collision_count\": "
           << report.cross_split_board_collision_count << ",\n";
    output << "  \"cross_split_board_collision_counts_by_pair\": ";
    write_count_map(output, report.cross_split_board_collision_counts_by_pair);
    output << ",\n";
  }
  output << "  \"checksum\": \"" << checksum_string(report.checksum) << "\",\n";
  output << "  \"split_policy\": \""
         << (report.schema_version == 2 ? "importer-preserved: dataset_id + game_group_id sha256"
                                        : "position-sha256")
         << "\",\n";
  output << "  \"duplicate_policy\": \"keep_all_input_order\"\n";
  output << "}\n";
  return true;
}

bool record_pattern_family_summary(const vibe_othello::evaluation::PatternFeatureSet& feature_set,
                                   const vibe_othello::evaluation::PatternSet& pattern_set,
                                   ReportSummary* report) {
  namespace eval = vibe_othello::evaluation;

  if (feature_set.tables.size() != pattern_set.patterns.size()) {
    return false;
  }

  report->feature_families.clear();
  report->total_table_entries = 0;
  for (std::size_t table_index = 0; table_index < feature_set.tables.size(); ++table_index) {
    const eval::PatternFeatureTable& table = feature_set.tables[table_index];
    const eval::PatternDefinition& definition = pattern_set.patterns[table_index];
    if (table.pattern_id != definition.id || table.pattern_length != definition.length) {
      return false;
    }
    const std::optional<std::uint32_t> table_size = eval::checked_pattern_size(definition.length);
    if (!table_size.has_value()) {
      return false;
    }
    report->total_table_entries += *table_size;
    report->feature_families.push_back({
        {"pattern_id", "\"" + json_escape(table.pattern_id) + "\""},
        {"pattern_length", std::to_string(table.pattern_length)},
        {"instance_count", std::to_string(table.instances.size())},
        {"table_size", std::to_string(*table_size)},
    });
  }
  return true;
}

std::optional<std::string>
pattern_contract_digest(const vibe_othello::evaluation::PatternFeatureSet& feature_set,
                        const vibe_othello::evaluation::PatternSet& pattern_set,
                        vibe_othello::tools::pattern::IndexMode index_mode) {
  namespace eval = vibe_othello::evaluation;

  if (feature_set.tables.size() != pattern_set.patterns.size()) {
    return std::nullopt;
  }

  std::string input;
  append_digest_line(&input, "pattern-contract-digest-v1");
  append_digest_line(&input, "pattern_set_id=" + pattern_set.id);
  append_digest_line(&input, "index_mode=" + std::string(index_mode_name(index_mode)));

  std::vector<std::string> ordered_pattern_ids;
  ordered_pattern_ids.reserve(pattern_set.patterns.size());
  for (const eval::PatternDefinition& definition : pattern_set.patterns) {
    ordered_pattern_ids.push_back(definition.id);
  }
  append_digest_line(&input, "ordered_pattern_ids=" + join_strings(ordered_pattern_ids));
  append_digest_line(&input, "pattern_count=" + std::to_string(pattern_set.patterns.size()));

  for (std::size_t pattern_index = 0; pattern_index < pattern_set.patterns.size();
       ++pattern_index) {
    const eval::PatternDefinition& definition = pattern_set.patterns[pattern_index];
    const eval::PatternFeatureTable& table = feature_set.tables[pattern_index];
    if (table.pattern_id != definition.id || table.pattern_length != definition.length) {
      return std::nullopt;
    }
    const std::optional<std::uint32_t> table_size = eval::checked_pattern_size(definition.length);
    if (!table_size.has_value()) {
      return std::nullopt;
    }

    const std::string prefix = "pattern[" + std::to_string(pattern_index) + "].";
    append_digest_line(&input, prefix + "id=" + definition.id);
    append_digest_line(&input, prefix + "length=" + std::to_string(definition.length));
    append_digest_line(&input, prefix + "symmetry_policy=" +
                                   std::string(symmetry_policy_name(definition.symmetry_policy)));
    append_digest_line(&input,
                       prefix + "squares=" + join_strings(square_names(definition.squares)));
    append_digest_line(&input,
                       prefix + "feature_instance_count=" + std::to_string(table.instances.size()));
    for (std::size_t instance = 0; instance < table.instances.size(); ++instance) {
      append_digest_line(&input,
                         prefix + "feature_instance[" + std::to_string(instance) +
                             "].squares=" + join_strings(square_names(table.instances[instance])));
    }
    append_digest_line(&input, prefix + "table_size=" + std::to_string(*table_size));
  }

  return digest_string(fnv1a64_digest(input));
}

struct FeatureOccurrence {
  std::string pattern_id;
  std::size_t instance = 0;
  std::uint32_t ternary_index = 0;
};

std::optional<std::vector<FeatureOccurrence>>
feature_occurrences_for_position(vibe_othello::board_core::Position position,
                                 const vibe_othello::evaluation::PatternFeatureSet& feature_set,
                                 const vibe_othello::evaluation::PatternSet& pattern_set,
                                 vibe_othello::tools::pattern::IndexMode index_mode) {
  namespace eval = vibe_othello::evaluation;
  namespace pattern = vibe_othello::tools::pattern;

  std::vector<FeatureOccurrence> occurrences;
  for (std::size_t table_index = 0; table_index < feature_set.tables.size(); ++table_index) {
    const eval::PatternFeatureTable& table = feature_set.tables[table_index];
    const eval::PatternDefinition& definition = pattern_set.patterns[table_index];
    for (std::size_t instance = 0; instance < table.instances.size(); ++instance) {
      const std::optional<std::uint32_t> index = pattern::index_for_mode(
          position, table.instances[instance], definition.symmetry_policy, index_mode);
      if (!index.has_value()) {
        std::cerr << "failed to encode pattern index: " << table.pattern_id << '\n';
        return std::nullopt;
      }
      occurrences.push_back(FeatureOccurrence{
          .pattern_id = table.pattern_id,
          .instance = instance,
          .ternary_index = *index,
      });
    }
  }
  return occurrences;
}

void record_example_summary(std::string_view split, int feature_count, EmitSummary* summary) {
  (void)split;
  ++summary->example_rows;
  summary->feature_occurrence_count += feature_count;
  summary->max_features_per_example = std::max(summary->max_features_per_example, feature_count);
}

void emit_expanded_rows(std::string_view record_id, int ply, std::string_view split, int label,
                        int phase, const std::vector<FeatureOccurrence>& features,
                        EmitSummary* emit_summary) {
  for (const FeatureOccurrence& feature : features) {
    std::cout << record_id << '\t' << ply << '\t' << split << '\t' << label << '\t' << phase << '\t'
              << feature.pattern_id << '\t' << feature.instance << '\t' << feature.ternary_index
              << '\n';
    ++emit_summary->rows;
    count_split(split, emit_summary);
  }
  record_example_summary(split, static_cast<int>(features.size()), emit_summary);
}

std::string compact_feature_string(const std::vector<FeatureOccurrence>& features) {
  std::ostringstream output;
  bool first = true;
  for (const FeatureOccurrence& feature : features) {
    if (!first) {
      output << ',';
    }
    first = false;
    output << feature.pattern_id << ':' << feature.instance << ':' << feature.ternary_index;
  }
  return output.str();
}

void emit_compact_row(std::string_view record_id, int ply, std::string_view split, int label,
                      int phase, const std::vector<FeatureOccurrence>& features,
                      EmitSummary* emit_summary) {
  std::cout << record_id << '\t' << ply << '\t' << split << '\t' << label << '\t' << phase << '\t'
            << compact_feature_string(features) << '\n';
  ++emit_summary->rows;
  count_split(split, emit_summary);
  record_example_summary(split, static_cast<int>(features.size()), emit_summary);
}

void emit_example(std::string_view record_id, int ply, std::string_view split, int label, int phase,
                  const std::vector<FeatureOccurrence>& features, OutputFormat output_format,
                  EmitSummary* emit_summary) {
  switch (output_format) {
  case OutputFormat::expanded_tsv:
    emit_expanded_rows(record_id, ply, split, label, phase, features, emit_summary);
    return;
  case OutputFormat::compact_tsv:
    emit_compact_row(record_id, ply, split, label, phase, features, emit_summary);
    return;
  }
}

void write_header(OutputFormat output_format) {
  switch (output_format) {
  case OutputFormat::expanded_tsv:
    std::cout
        << "record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_id\tinstance\tternary_"
           "index\n";
    return;
  case OutputFormat::compact_tsv:
    std::cout << "record_id\tply\tsplit\tlabel_final_disc_diff\tphase\tpattern_features\n";
    return;
  }
}

} // namespace

int main(int argc, char** argv) {
  namespace importer = vibe_othello::tools::data_import;
  namespace eval = vibe_othello::evaluation;
  namespace pattern = vibe_othello::tools::pattern;

  const std::optional<Args> args = parse_args(argc, argv);
  if (!args.has_value()) {
    return 2;
  }
  if (!args->manifest_path.empty() && !importer::manifest_is_readable(args->manifest_path)) {
    return 1;
  }

  const std::optional<pattern::PatternSetOption> selected_pattern_set =
      pattern::select_pattern_set(args->pattern_set, args->index_mode);
  if (!selected_pattern_set.has_value() || selected_pattern_set->pattern_set == nullptr) {
    std::cerr << "--pattern-set must be " << pattern::pattern_set_option_names() << '\n';
    return 2;
  }
  const eval::PatternSet& pattern_set = *selected_pattern_set->pattern_set;
  const eval::PatternFeatureSet& feature_set = selected_pattern_set->feature_set;
  const pattern::FeatureSetValidationResult validation =
      pattern::validate_feature_set(feature_set, pattern_set);
  if (!validation.valid) {
    std::cerr << validation.error << '\n';
    return 1;
  }

  if (!args->normalized_tsv_path.empty()) {
    ReportSummary report;
    report.output_format = args->output_format;
    report.pattern_set_id = pattern_set.id;
    report.index_mode = args->index_mode;
    if (!record_pattern_family_summary(feature_set, pattern_set, &report)) {
      std::cerr << "failed to summarize pattern family table sizes\n";
      return 1;
    }
    const std::optional<std::string> contract_digest =
        pattern_contract_digest(feature_set, pattern_set, args->index_mode);
    if (!contract_digest.has_value()) {
      std::cerr << "failed to compute pattern contract digest\n";
      return 1;
    }
    report.pattern_contract_digest = *contract_digest;
    std::vector<NormalizedRow> rows;
    const bool loaded = load_normalized_rows(args->normalized_tsv_path, &rows, &report);

    write_header(args->output_format);

    EmitSummary emit_summary;
    bool emitted = true;
    for (const NormalizedRow& row : rows) {
      const std::optional<vibe_othello::board_core::Position> position =
          position_from_a1_to_h8_board(row.board);
      if (!position.has_value()) {
        std::cerr << row.record_id << ": board_a1_to_h8 could not be converted to Position\n";
        emitted = false;
        continue;
      }
      const std::optional<std::vector<FeatureOccurrence>> features =
          feature_occurrences_for_position(*position, feature_set, pattern_set, args->index_mode);
      if (!features.has_value()) {
        emitted = false;
        continue;
      }
      if (features->empty()) {
        std::cerr << row.record_id << ": pattern feature list is empty\n";
        emitted = false;
        continue;
      }
      emit_example(row.record_id, row.occupied_count - 4, row.split, row.label_score_side_to_move,
                   row.phase, *features, args->output_format, &emit_summary);
    }
    report.example_rows = emit_summary.example_rows;
    report.feature_occurrence_count = emit_summary.feature_occurrence_count;
    report.max_features_per_example = emit_summary.max_features_per_example;

    if (!write_report(args->report_path, report)) {
      return 1;
    }
    std::cerr << "summary input_rows=" << report.input_rows
              << " accepted_rows=" << report.accepted_rows
              << " rejected_rows=" << report.rejected_rows << " emitted_rows=" << emit_summary.rows
              << " output_format=" << output_format_name(args->output_format)
              << " train_rows=" << emit_summary.train_rows
              << " validation_rows=" << emit_summary.validation_rows
              << " test_rows=" << emit_summary.test_rows << " split_policy="
              << (report.schema_version == 2 ? "importer-preserved:dataset_id+game_group_id-sha256"
                                             : "position-sha256")
              << " duplicate_policy=keep_all_input_order\n";
    if (emit_summary.rows == 0) {
      std::cerr << "no dataset rows emitted from normalized TSV\n";
      return 1;
    }
    return loaded && emitted ? 0 : 1;
  }

  importer::Summary summary;
  std::vector<importer::Record> records;
  if (!importer::load_records(args->records_path, &records, &summary)) {
    return 1;
  }

  write_header(args->output_format);

  EmitSummary emit_summary;
  std::size_t accepted_ply_ordinal = 0;
  for (const importer::Record& record : records) {
    const importer::ReplayResult result = importer::replay_record(record, true);
    if (result.accepted) {
      ++summary.accepted_records;
    } else {
      ++summary.rejected_records;
      std::cerr << record.id << ": " << result.error << '\n';
    }

    if (result.accepted != record.expect_accept) {
      ++summary.expectation_failures;
      std::cerr << record.id << ": expected " << (record.expect_accept ? "accept" : "reject")
                << " but got " << (result.accepted ? "accept" : "reject");
      if (!result.error.empty()) {
        std::cerr << ": " << result.error;
      }
      std::cerr << '\n';
      continue;
    }

    if (!record.expect_accept || !result.accepted) {
      continue;
    }
    if (!record.expected_final_disc_diff.has_value()) {
      ++summary.expectation_failures;
      std::cerr << record.id << ": accepted record is missing label_final_disc_diff\n";
      continue;
    }

    for (std::size_t ply = 0; ply < result.positions.size(); ++ply) {
      const std::string_view split = split_for(args->split_policy, record.id, accepted_ply_ordinal);
      ++accepted_ply_ordinal;

      const vibe_othello::board_core::Position position = result.positions[ply];
      const std::uint8_t phase = pattern::smoke::tiny_fixture_phase(position);
      const std::optional<std::vector<FeatureOccurrence>> features =
          feature_occurrences_for_position(position, feature_set, pattern_set, args->index_mode);
      if (!features.has_value()) {
        return 1;
      }
      if (features->empty()) {
        std::cerr << record.id << ": pattern feature list is empty\n";
        return 1;
      }
      emit_example(record.id, static_cast<int>(ply + 1), split, *record.expected_final_disc_diff,
                   static_cast<int>(phase), *features, args->output_format, &emit_summary);
    }
  }

  std::cerr << "summary total_records=" << summary.total_records
            << " accepted_records=" << summary.accepted_records
            << " rejected_records=" << summary.rejected_records
            << " emitted_rows=" << emit_summary.rows
            << " output_format=" << output_format_name(args->output_format)
            << " train_rows=" << emit_summary.train_rows
            << " validation_rows=" << emit_summary.validation_rows
            << " test_rows=" << emit_summary.test_rows
            << " split_policy=" << split_policy_name(args->split_policy)
            << " duplicate_policy=keep_all_input_order\n";

  if (emit_summary.rows == 0) {
    std::cerr << "no dataset rows emitted from expected-good records\n";
    return 1;
  }

  return summary.expectation_failures == 0 ? 0 : 1;
}
