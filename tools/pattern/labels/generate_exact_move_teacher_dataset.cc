#include "vibe_othello/board_core/board.h"
#include "vibe_othello/search/search.h"

#include <algorithm>
#include <bit>
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

namespace board_core = vibe_othello::board_core;
namespace search = vibe_othello::search;

constexpr std::string_view kNormalizedHeaderV1 =
    "record_id\tposition_id\tsource_dataset_id\tsplit\tboard_a1_to_h8\tlabel_kind\tlabel_"
    "unit\tlabel_perspective\tlabel_score_side_to_move\toccupied_count\tphase\tplayer_disc_"
    "count\topponent_disc_count\tempty_count";
constexpr std::string_view kNormalizedHeaderV2 =
    "record_id\tposition_id\tgame_group_id\tboard_id\tsource_occurrence_id\tsource_dataset_id\t"
    "split\tboard_a1_to_h8\tlabel_kind\tlabel_unit\tlabel_perspective\tlabel_score_side_to_"
    "move\toccupied_count\tphase\tplayer_disc_count\topponent_disc_count\tempty_count";
constexpr std::string_view kMoveTeacherHeader =
    "root_board_id\troot_record_id\troot_split\troot_phase\troot_empty_count\tmove\tchild_"
    "board_id\tchild_board_a1_to_h8\tchild_empty_count\tchild_phase\troot_move_score_side_to_"
    "move\tchild_label_score_side_to_move\tis_best_move\tbest_move_tie_count\tmove_rank\tbest_"
    "score_margin\tteacher_source\tteacher_depth\tteacher_nodes";
constexpr std::string_view kMoveTeacherSource = "exact-move-teacher-v1";
constexpr std::string_view kChildLabelKind = "teacher_exact_move_child_final_disc_diff";

struct Args {
  std::string normalized_tsv_path;
  std::string move_teacher_out_path;
  std::string child_normalized_out_path;
  std::string report_out_path;
  int max_empty = 12;
  std::optional<std::size_t> max_roots;
  std::uint64_t seed = 0;
  std::size_t progress_every = 0;
};

struct RootRow {
  std::string record_id;
  std::string position_id;
  std::string game_group_id;
  std::string board_id;
  std::string source_occurrence_id;
  std::string source_dataset_id;
  std::string split;
  std::string board;
  int label_score_side_to_move = 0;
  int occupied_count = 0;
  int phase = 0;
  int player_disc_count = 0;
  int opponent_disc_count = 0;
  int empty_count = 0;
  std::uint64_t sample_key = 0;
};

struct MoveRow {
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
  int child_occupied_count = 0;
  int child_player_disc_count = 0;
  int child_opponent_disc_count = 0;
  int root_move_score_side_to_move = 0;
  int child_label_score_side_to_move = 0;
  bool is_best_move = false;
  int best_move_tie_count = 0;
  int move_rank = 0;
  int best_score_margin = 0;
  int teacher_depth = 0;
  search::NodeCount teacher_nodes = 0;
  std::string root_position_id;
  std::string root_game_group_id;
};

struct Report {
  int schema_version = 1;
  std::string normalized_input_path;
  std::string move_teacher_out;
  std::string child_normalized_out;
  int max_empty = 0;
  std::optional<std::size_t> max_roots;
  std::uint64_t seed = 0;
  std::size_t input_rows = 0;
  std::size_t eligible_rows = 0;
  std::size_t selected_roots = 0;
  std::size_t unique_roots_seen = 0;
  std::size_t unique_roots_selected = 0;
  std::size_t skipped_too_many_empty = 0;
  std::size_t duplicate_board_rows = 0;
  std::size_t terminal_roots_skipped = 0;
  std::size_t roots_with_normal_moves = 0;
  std::size_t roots_with_pass_move = 0;
  std::size_t move_rows = 0;
  std::size_t child_normalized_rows = 0;
  std::size_t solve_failures = 0;
  std::optional<int> root_move_score_min;
  std::optional<int> root_move_score_max;
  std::int64_t root_move_score_sum = 0;
  std::optional<int> child_label_score_min;
  std::optional<int> child_label_score_max;
  std::int64_t child_label_score_sum = 0;
  std::uint64_t teacher_nodes_sum = 0;
  std::uint64_t checksum = 14695981039346656037ull;
  double wall_time_sec = 0.0;
  double moves_per_sec = 0.0;
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
    } else if (arg == "--move-teacher-out") {
      if (!parse_required_value(&index, argc, argv, &args.move_teacher_out_path)) {
        return std::nullopt;
      }
    } else if (arg == "--child-normalized-out") {
      if (!parse_required_value(&index, argc, argv, &args.child_normalized_out_path)) {
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
    } else if (arg == "--max-roots") {
      std::string value;
      if (!parse_required_value(&index, argc, argv, &value)) {
        return std::nullopt;
      }
      const std::optional<std::uint64_t> parsed = parse_u64(value);
      if (!parsed.has_value() || *parsed == 0 ||
          *parsed > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        std::cerr << "--max-roots must be a positive integer\n";
        return std::nullopt;
      }
      args.max_roots = static_cast<std::size_t>(*parsed);
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
  if (args.normalized_tsv_path.empty() || args.move_teacher_out_path.empty() ||
      args.child_normalized_out_path.empty() || args.report_out_path.empty()) {
    std::cerr << "usage: vibe-othello-generate-exact-move-teacher-dataset "
                 "--normalized-tsv PATH --move-teacher-out PATH --child-normalized-out PATH "
                 "--report-out PATH [--max-empty N] [--max-roots N] [--seed N] "
                 "[--progress-every N]\n";
    return std::nullopt;
  }
  return args;
}

bool validate_board_counts(std::string_view board, int occupied_count, int player_disc_count,
                           int opponent_disc_count, int empty_count, std::string* error) {
  if (board.size() != board_core::kSquareCount) {
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
      occupied_count + empty_count != board_core::kSquareCount) {
    *error = "board counts do not match count columns";
    return false;
  }
  return true;
}

int phase_for_occupied_count(int occupied_count) noexcept {
  return std::min(12, ((occupied_count - 4) * 13) / 60);
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

std::string relative_board_from_position(board_core::Position position) {
  std::string board;
  board.resize(board_core::kSquareCount, '-');
  for (int index = 0; index < board_core::kSquareCount; ++index) {
    const board_core::Bitboard bit = board_core::bit(board_core::square_from_index(index));
    if ((position.player & bit) != 0) {
      board[static_cast<std::size_t>(index)] = 'X';
    } else if ((position.opponent & bit) != 0) {
      board[static_cast<std::size_t>(index)] = 'O';
    }
  }
  return board;
}

bool parse_normalized_row(std::string_view line, RootRow* row, std::string* error) {
  const std::vector<std::string_view> fields = split_tabs(trim_trailing_cr(line));
  if (fields.size() != 17) {
    *error = "expected 17 TSV fields for normalized schema v2";
    return false;
  }
  if (fields[0].empty() || fields[1].empty() || fields[2].empty() || fields[3].empty() ||
      fields[4].empty() || fields[5].empty()) {
    *error = "record_id, position_id, game_group_id, board_id, source_occurrence_id, and "
             "source_dataset_id must be non-empty";
    return false;
  }
  if (fields[6] != "train" && fields[6] != "validation" && fields[6] != "test") {
    *error = "split must be train, validation, or test";
    return false;
  }
  if (fields[10] != "side_to_move") {
    *error = "label_perspective must be side_to_move";
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
  if (*occupied < 4 || *occupied > 64 || *empty_count < 0 || *empty_count > 60) {
    *error = "occupied_count or empty_count is outside normalized schema v2 range";
    return false;
  }
  if (*phase < 0 || *phase > 12 || *phase != phase_for_occupied_count(*occupied)) {
    *error = "phase must be in [0, 12] and match occupied_count";
    return false;
  }
  if (!validate_board_counts(fields[7], *occupied, *player_count, *opponent_count, *empty_count,
                             error)) {
    return false;
  }

  row->record_id = std::string(fields[0]);
  row->position_id = std::string(fields[1]);
  row->game_group_id = std::string(fields[2]);
  row->board_id = std::string(fields[3]);
  row->source_occurrence_id = std::string(fields[4]);
  row->source_dataset_id = std::string(fields[5]);
  row->split = std::string(fields[6]);
  row->board = std::string(fields[7]);
  row->label_score_side_to_move = *label;
  row->occupied_count = *occupied;
  row->phase = *phase;
  row->player_disc_count = *player_count;
  row->opponent_disc_count = *opponent_count;
  row->empty_count = *empty_count;
  return true;
}

bool load_roots(const Args& args, std::vector<RootRow>* roots, Report* report) {
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
  std::map<std::string, RootRow> unique_eligible;
  int line_number = 1;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty()) {
      continue;
    }
    ++report->input_rows;
    RootRow row;
    std::string error;
    if (!parse_normalized_row(line, &row, &error)) {
      std::cerr << "line " << line_number << ": " << error << '\n';
      return false;
    }
    const bool duplicate_seen = !seen_boards.insert(row.board_id).second;
    if (duplicate_seen) {
      ++report->duplicate_board_rows;
    }
    if (row.empty_count > args.max_empty) {
      ++report->skipped_too_many_empty;
      continue;
    }
    ++report->eligible_rows;
    if (unique_eligible.contains(row.board_id)) {
      continue;
    }
    row.sample_key = sample_key(row.board_id, args.seed);
    unique_eligible.emplace(row.board_id, std::move(row));
  }

  report->unique_roots_seen = seen_boards.size();
  roots->reserve(unique_eligible.size());
  for (auto& [board_id, root] : unique_eligible) {
    (void)board_id;
    roots->push_back(std::move(root));
  }
  return true;
}

std::vector<RootRow> select_roots(std::vector<RootRow> roots,
                                  std::optional<std::size_t> max_roots) {
  if (max_roots.has_value() && roots.size() > *max_roots) {
    std::sort(roots.begin(), roots.end(), [](const RootRow& left, const RootRow& right) {
      if (left.sample_key != right.sample_key) {
        return left.sample_key < right.sample_key;
      }
      return left.board_id < right.board_id;
    });
    roots.resize(*max_roots);
  }
  std::sort(roots.begin(), roots.end(), [](const RootRow& left, const RootRow& right) {
    return left.board_id < right.board_id;
  });
  return roots;
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

std::vector<board_core::Move> legal_root_moves(board_core::Position position, bool* terminal_root) {
  *terminal_root = false;
  const board_core::Bitboard legal = board_core::legal_moves(position);
  std::vector<board_core::Move> moves;
  for (int index = 0; index < board_core::kSquareCount; ++index) {
    const board_core::Square square = board_core::square_from_index(index);
    if ((legal & board_core::bit(square)) != 0) {
      moves.push_back(board_core::make_move(square));
    }
  }
  if (!moves.empty()) {
    return moves;
  }
  if (board_core::is_terminal(position)) {
    *terminal_root = true;
    return moves;
  }
  board_core::MoveDelta delta{};
  if (board_core::make_move_delta(position, board_core::make_pass(), &delta)) {
    moves.push_back(board_core::make_pass());
  }
  return moves;
}

std::optional<MoveRow> solve_child_for_move(const RootRow& root, board_core::Position root_position,
                                            board_core::Move move, std::string* error) {
  board_core::MoveDelta delta{};
  const bool applied = move.kind == board_core::MoveKind::pass
                           ? board_core::apply_pass(&root_position, &delta)
                           : board_core::apply_move(&root_position, move, &delta);
  if (!applied) {
    *error = "move application failed";
    return std::nullopt;
  }

  search::SearchOptions options;
  options.use_endgame_tt = true;
  options.endgame.use_endgame_tt = true;
  const search::SearchResult result =
      search::solve_exact_endgame(root_position, search::SearchLimits{}, options);
  if (result.stopped || !result.exact || result.score_kind != search::ScoreKind::exact_disc_diff) {
    *error = "exact child solve did not complete";
    return std::nullopt;
  }
  if (result.score < -64 || result.score > 64) {
    *error = "exact child score outside [-64, 64]";
    return std::nullopt;
  }

  const std::string move_text = move_to_string(move);
  const std::string child_board = relative_board_from_position(root_position);
  const int child_player_count = std::popcount(root_position.player);
  const int child_opponent_count = std::popcount(root_position.opponent);
  const int child_occupied_count = child_player_count + child_opponent_count;
  const int child_empty_count = board_core::kSquareCount - child_occupied_count;

  return MoveRow{
      .root_board_id = root.board_id,
      .root_record_id = root.record_id,
      .root_split = root.split,
      .root_phase = root.phase,
      .root_empty_count = root.empty_count,
      .move = move_text,
      .child_board_id = "move-teacher-v1:" + root.board_id + ":" + move_text,
      .child_board = child_board,
      .child_empty_count = child_empty_count,
      .child_phase = phase_for_occupied_count(child_occupied_count),
      .child_occupied_count = child_occupied_count,
      .child_player_disc_count = child_player_count,
      .child_opponent_disc_count = child_opponent_count,
      .root_move_score_side_to_move = -result.score,
      .child_label_score_side_to_move = result.score,
      .teacher_depth = child_empty_count,
      .teacher_nodes = result.stats.endgame_nodes != 0 ? result.stats.endgame_nodes : result.nodes,
      .root_position_id = root.position_id,
      .root_game_group_id = root.game_group_id,
  };
}

void rank_root_moves(std::vector<MoveRow>* rows) {
  if (rows->empty()) {
    return;
  }
  const int best_score =
      std::max_element(rows->begin(), rows->end(), [](const MoveRow& left, const MoveRow& right) {
        return left.root_move_score_side_to_move < right.root_move_score_side_to_move;
      })->root_move_score_side_to_move;
  int best_tie_count = 0;
  for (const MoveRow& row : *rows) {
    if (row.root_move_score_side_to_move == best_score) {
      ++best_tie_count;
    }
  }

  std::vector<std::size_t> order(rows->size());
  for (std::size_t index = 0; index < order.size(); ++index) {
    order[index] = index;
  }
  std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
    const MoveRow& lhs = (*rows)[left];
    const MoveRow& rhs = (*rows)[right];
    if (lhs.root_move_score_side_to_move != rhs.root_move_score_side_to_move) {
      return lhs.root_move_score_side_to_move > rhs.root_move_score_side_to_move;
    }
    return lhs.move < rhs.move;
  });
  for (std::size_t rank = 0; rank < order.size(); ++rank) {
    MoveRow& row = (*rows)[order[rank]];
    row.is_best_move = row.root_move_score_side_to_move == best_score;
    row.best_move_tie_count = best_tie_count;
    row.move_rank = static_cast<int>(rank + 1);
    row.best_score_margin = best_score - row.root_move_score_side_to_move;
  }
  std::sort(rows->begin(), rows->end(),
            [](const MoveRow& left, const MoveRow& right) { return left.move < right.move; });
}

void update_report_for_move(const MoveRow& row, Report* report) {
  report->root_move_score_min =
      report->root_move_score_min.has_value()
          ? std::min(*report->root_move_score_min, row.root_move_score_side_to_move)
          : row.root_move_score_side_to_move;
  report->root_move_score_max =
      report->root_move_score_max.has_value()
          ? std::max(*report->root_move_score_max, row.root_move_score_side_to_move)
          : row.root_move_score_side_to_move;
  report->root_move_score_sum += row.root_move_score_side_to_move;
  report->child_label_score_min =
      report->child_label_score_min.has_value()
          ? std::min(*report->child_label_score_min, row.child_label_score_side_to_move)
          : row.child_label_score_side_to_move;
  report->child_label_score_max =
      report->child_label_score_max.has_value()
          ? std::max(*report->child_label_score_max, row.child_label_score_side_to_move)
          : row.child_label_score_side_to_move;
  report->child_label_score_sum += row.child_label_score_side_to_move;
  report->teacher_nodes_sum += row.teacher_nodes;
}

std::string move_teacher_line(const MoveRow& row) {
  std::ostringstream output;
  output << row.root_board_id << '\t' << row.root_record_id << '\t' << row.root_split << '\t'
         << row.root_phase << '\t' << row.root_empty_count << '\t' << row.move << '\t'
         << row.child_board_id << '\t' << row.child_board << '\t' << row.child_empty_count << '\t'
         << row.child_phase << '\t' << row.root_move_score_side_to_move << '\t'
         << row.child_label_score_side_to_move << '\t' << (row.is_best_move ? '1' : '0') << '\t'
         << row.best_move_tie_count << '\t' << row.move_rank << '\t' << row.best_score_margin
         << '\t' << kMoveTeacherSource << '\t' << row.teacher_depth << '\t' << row.teacher_nodes;
  return output.str();
}

std::string child_normalized_line(const MoveRow& row) {
  std::ostringstream output;
  const std::string child_record_id = row.child_board_id;
  const std::string child_position_id = row.child_board_id;
  const std::string source_occurrence_id = row.child_board_id + ":source";
  output << child_record_id << '\t' << child_position_id << '\t' << row.root_game_group_id << '\t'
         << row.child_board_id << '\t' << source_occurrence_id << '\t' << kMoveTeacherSource << '\t'
         << row.root_split << '\t' << row.child_board << '\t' << kChildLabelKind << '\t'
         << "disc\tside_to_move\t" << row.child_label_score_side_to_move << '\t'
         << row.child_occupied_count << '\t' << row.child_phase << '\t'
         << row.child_player_disc_count << '\t' << row.child_opponent_disc_count << '\t'
         << row.child_empty_count;
  return output.str();
}

bool write_outputs(const Args& args, const std::vector<MoveRow>& rows, Report* report) {
  std::ofstream move_output(args.move_teacher_out_path);
  if (!move_output) {
    std::cerr << "cannot write move-teacher TSV: " << args.move_teacher_out_path << '\n';
    return false;
  }
  std::ofstream child_output(args.child_normalized_out_path);
  if (!child_output) {
    std::cerr << "cannot write child normalized TSV: " << args.child_normalized_out_path << '\n';
    return false;
  }
  move_output << kMoveTeacherHeader << '\n';
  child_output << kNormalizedHeaderV2 << '\n';
  for (const MoveRow& row : rows) {
    const std::string move_line = move_teacher_line(row);
    const std::string child_line = child_normalized_line(row);
    move_output << move_line << '\n';
    child_output << child_line << '\n';
    mix_checksum(move_line, report);
    mix_checksum(child_line, report);
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
  output << "  \"child_label_score_sum\": " << report.child_label_score_sum << ",\n";
  write_optional_int(output, "child_label_score_max", report.child_label_score_max);
  write_optional_int(output, "child_label_score_min", report.child_label_score_min);
  output << "  \"child_normalized_out\": \"" << json_escape(report.child_normalized_out) << "\",\n";
  output << "  \"child_normalized_rows\": " << report.child_normalized_rows << ",\n";
  output << "  \"duplicate_board_rows\": " << report.duplicate_board_rows << ",\n";
  output << "  \"eligible_rows\": " << report.eligible_rows << ",\n";
  output << "  \"input_rows\": " << report.input_rows << ",\n";
  output << "  \"max_empty\": " << report.max_empty << ",\n";
  output << "  \"max_roots\": ";
  if (report.max_roots.has_value()) {
    output << *report.max_roots;
  } else {
    output << "null";
  }
  output << ",\n";
  output << "  \"move_rows\": " << report.move_rows << ",\n";
  output << "  \"move_teacher_out\": \"" << json_escape(report.move_teacher_out) << "\",\n";
  output << "  \"moves_per_sec\": " << std::fixed << std::setprecision(6) << report.moves_per_sec
         << ",\n";
  output << "  \"normalized_input_path\": \"" << json_escape(report.normalized_input_path)
         << "\",\n";
  output << "  \"notes\": [\n";
  output << "    \"local-only exact move-teacher dataset generation\",\n";
  output << "    \"input and child boards use side-to-move-relative X/O convention\",\n";
  output << "    \"root_move_score_side_to_move is -child_label_score_side_to_move\",\n";
  output << "    \"teacher_depth is the child empty count solved exactly\",\n";
  output << "    \"terminal roots are skipped and counted\",\n";
  output << "    \"not a strength claim\",\n";
  output << "    \"not an Elo result\",\n";
  output << "    \"not self-play\",\n";
  output << "    \"not a production artifact\",\n";
  output << "    \"generated labels and artifacts must not be committed\"\n";
  output << "  ],\n";
  output << "  \"roots_with_normal_moves\": " << report.roots_with_normal_moves << ",\n";
  output << "  \"roots_with_pass_move\": " << report.roots_with_pass_move << ",\n";
  output << "  \"root_move_score_sum\": " << report.root_move_score_sum << ",\n";
  write_optional_int(output, "root_move_score_max", report.root_move_score_max);
  write_optional_int(output, "root_move_score_min", report.root_move_score_min);
  output << "  \"schema_version\": " << report.schema_version << ",\n";
  output << "  \"seed\": " << report.seed << ",\n";
  output << "  \"selected_roots\": " << report.selected_roots << ",\n";
  output << "  \"skipped_too_many_empty\": " << report.skipped_too_many_empty << ",\n";
  output << "  \"solve_failures\": " << report.solve_failures << ",\n";
  output << "  \"teacher_nodes_sum\": " << report.teacher_nodes_sum << ",\n";
  output << "  \"terminal_roots_skipped\": " << report.terminal_roots_skipped << ",\n";
  output << "  \"unique_roots_seen\": " << report.unique_roots_seen << ",\n";
  output << "  \"unique_roots_selected\": " << report.unique_roots_selected << ",\n";
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
      .move_teacher_out = report_path(args->move_teacher_out_path),
      .child_normalized_out = report_path(args->child_normalized_out_path),
      .max_empty = args->max_empty,
      .max_roots = args->max_roots,
      .seed = args->seed,
  };

  std::vector<RootRow> roots;
  if (!load_roots(*args, &roots, &report)) {
    return 1;
  }
  if (report.eligible_rows == 0 || roots.empty()) {
    std::cerr << "no eligible normalized schema v2 roots with empty_count <= " << args->max_empty
              << '\n';
    return 1;
  }

  std::vector<RootRow> selected = select_roots(std::move(roots), args->max_roots);
  report.selected_roots = selected.size();
  report.unique_roots_selected = selected.size();

  std::vector<MoveRow> all_rows;
  for (std::size_t root_index = 0; root_index < selected.size(); ++root_index) {
    const RootRow& root = selected[root_index];
    const std::optional<board_core::Position> position = position_from_relative_board(root.board);
    if (!position.has_value()) {
      std::cerr << root.board_id << ": board_a1_to_h8 could not be converted to Position\n";
      return 1;
    }
    bool terminal_root = false;
    const std::vector<board_core::Move> moves = legal_root_moves(*position, &terminal_root);
    if (terminal_root) {
      ++report.terminal_roots_skipped;
      continue;
    }
    if (moves.empty()) {
      ++report.solve_failures;
      std::cerr << root.board_id << ": no legal move and pass was not legal\n";
      continue;
    }
    if (moves.size() == 1 && moves.front().kind == board_core::MoveKind::pass) {
      ++report.roots_with_pass_move;
    } else {
      ++report.roots_with_normal_moves;
    }

    std::vector<MoveRow> root_rows;
    for (const board_core::Move move : moves) {
      std::string error;
      std::optional<MoveRow> row = solve_child_for_move(root, *position, move, &error);
      if (!row.has_value()) {
        ++report.solve_failures;
        std::cerr << root.board_id << " " << move_to_string(move) << ": " << error << '\n';
        continue;
      }
      update_report_for_move(*row, &report);
      root_rows.push_back(std::move(*row));
    }
    rank_root_moves(&root_rows);
    all_rows.insert(all_rows.end(), root_rows.begin(), root_rows.end());

    if (args->progress_every != 0 && (root_index + 1) % args->progress_every == 0) {
      std::cerr << "progress roots=" << (root_index + 1) << " selected=" << selected.size()
                << " moves=" << all_rows.size() << '\n';
    }
  }

  report.move_rows = all_rows.size();
  report.child_normalized_rows = all_rows.size();
  const auto finished = std::chrono::steady_clock::now();
  report.wall_time_sec = std::chrono::duration<double>(finished - started).count();
  if (report.wall_time_sec > 0.0) {
    report.moves_per_sec = static_cast<double>(report.move_rows) / report.wall_time_sec;
  }

  if (all_rows.empty()) {
    std::cerr << "no move-teacher rows generated\n";
    return 1;
  }
  if (report.solve_failures != 0) {
    std::cerr << "move-teacher generation had solve failures: " << report.solve_failures << '\n';
    return 1;
  }
  if (!write_outputs(*args, all_rows, &report)) {
    return 1;
  }
  if (!write_report(args->report_out_path, report)) {
    return 1;
  }

  std::cout << "move_teacher=" << args->move_teacher_out_path << '\n';
  std::cout << "child_normalized=" << args->child_normalized_out_path << '\n';
  std::cout << "report=" << args->report_out_path << '\n';
  std::cout << "checksum=" << checksum_string(report.checksum) << '\n';
  return 0;
}
