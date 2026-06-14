#include "vibe_othello/board_core/board.h"

#include <bit>
#include <charconv>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using vibe_othello::board_core::Move;
using vibe_othello::board_core::MoveDelta;
using vibe_othello::board_core::MoveKind;
using vibe_othello::board_core::Position;
using vibe_othello::board_core::Square;

struct Args {
  std::string records_path;
  std::string manifest_path;
};

struct Record {
  std::string id;
  bool expect_accept = false;
  std::string moves;
  int expected_final_disc_diff = 0;
  std::string notes;
};

struct Summary {
  int total_records = 0;
  int accepted_records = 0;
  int rejected_records = 0;
  int expectation_failures = 0;
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

std::vector<std::string_view> split_words(std::string_view text) {
  std::vector<std::string_view> words;
  std::size_t offset = 0;
  while (offset < text.size()) {
    while (offset < text.size() && (text[offset] == ' ' || text[offset] == '\t')) {
      ++offset;
    }
    const std::size_t begin = offset;
    while (offset < text.size() && text[offset] != ' ' && text[offset] != '\t') {
      ++offset;
    }
    if (begin != offset) {
      words.push_back(text.substr(begin, offset - begin));
    }
  }
  return words;
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

std::optional<Square> parse_square(std::string_view text) noexcept {
  if (text.size() != 2) {
    return std::nullopt;
  }
  const char file = text[0];
  const char rank = text[1];
  if (file < 'a' || file > 'h' || rank < '1' || rank > '8') {
    return std::nullopt;
  }
  return vibe_othello::board_core::square_from_file_rank(file - 'a', rank - '1');
}

std::optional<Move> parse_move_token(std::string_view text) noexcept {
  if (text == "pass") {
    return vibe_othello::board_core::make_pass();
  }
  const std::optional<Square> square = parse_square(text);
  if (!square.has_value()) {
    return std::nullopt;
  }
  return vibe_othello::board_core::make_move(*square);
}

int final_disc_diff(Position position) noexcept {
  const int black = std::popcount(vibe_othello::board_core::black_discs(position));
  const int white = std::popcount(vibe_othello::board_core::white_discs(position));
  return black - white;
}

bool replay_moves(const Record& record, std::string* error) {
  Position position = vibe_othello::board_core::initial_position();
  for (std::string_view token : split_words(record.moves)) {
    const std::optional<Move> move = parse_move_token(token);
    if (!move.has_value()) {
      *error = "invalid move coordinate";
      return false;
    }

    if (move->kind == MoveKind::pass && vibe_othello::board_core::has_legal_move(position)) {
      *error = "pass is illegal while legal moves exist";
      return false;
    }

    MoveDelta delta{};
    const bool ok = move->kind == MoveKind::pass
                        ? vibe_othello::board_core::apply_pass(&position, &delta)
                        : vibe_othello::board_core::apply_move(&position, *move, &delta);
    if (!ok) {
      *error = "illegal move sequence";
      return false;
    }
  }

  if (record.expect_accept && final_disc_diff(position) != record.expected_final_disc_diff) {
    *error = "final disc difference mismatch";
    return false;
  }

  return true;
}

std::optional<Record> parse_record(std::string_view line, int line_number, std::string* error) {
  const std::vector<std::string_view> fields = split_tabs(trim_trailing_cr(line));
  if (fields.size() != 5) {
    *error = "line " + std::to_string(line_number) + ": expected 5 TSV fields";
    return std::nullopt;
  }

  if (fields[0].empty()) {
    *error = "line " + std::to_string(line_number) + ": id is empty";
    return std::nullopt;
  }

  bool expect_accept = false;
  if (fields[1] == "accept") {
    expect_accept = true;
  } else if (fields[1] == "reject") {
    expect_accept = false;
  } else {
    *error = "line " + std::to_string(line_number) + ": expected_status must be accept or reject";
    return std::nullopt;
  }

  const std::optional<int> expected_final_disc_diff = parse_int(fields[3]);
  if (!expected_final_disc_diff.has_value()) {
    *error = "line " + std::to_string(line_number) + ": expected_final_disc_diff is invalid";
    return std::nullopt;
  }

  return Record{
      .id = std::string{fields[0]},
      .expect_accept = expect_accept,
      .moves = std::string{fields[2]},
      .expected_final_disc_diff = *expected_final_disc_diff,
      .notes = std::string{fields[4]},
  };
}

bool load_records(const std::string& path, std::vector<Record>* records, Summary* summary) {
  std::ifstream input(path);
  if (!input) {
    std::cerr << "cannot read records: " << path << '\n';
    return false;
  }

  std::string line;
  int line_number = 0;
  bool saw_header = false;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty()) {
      continue;
    }

    if (!saw_header) {
      saw_header = true;
      if (trim_trailing_cr(line) != "id\texpected_status\tmoves\texpected_final_disc_diff\tnotes") {
        std::cerr << "line " << line_number << ": unexpected TSV header\n";
        return false;
      }
      continue;
    }

    ++summary->total_records;
    std::string error;
    std::optional<Record> record = parse_record(line, line_number, &error);
    if (!record.has_value()) {
      ++summary->rejected_records;
      continue;
    }
    records->push_back(*record);
  }

  if (!saw_header) {
    std::cerr << "records file is empty\n";
    return false;
  }
  return true;
}

bool manifest_is_readable(const std::string& path) {
  if (path.empty()) {
    return true;
  }
  std::ifstream input(path);
  if (!input) {
    std::cerr << "cannot read manifest: " << path << '\n';
    return false;
  }
  return true;
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
    } else if (arg == "--manifest") {
      if (index + 1 >= argc) {
        std::cerr << "--manifest requires a value\n";
        return std::nullopt;
      }
      args.manifest_path = argv[++index];
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      return std::nullopt;
    }
  }

  if (args.records_path.empty()) {
    std::cerr << "usage: vibe-othello-data-import-replay-smoke --records PATH [--manifest PATH]\n";
    return std::nullopt;
  }
  return args;
}

} // namespace

int main(int argc, char** argv) {
  const std::optional<Args> args = parse_args(argc, argv);
  if (!args.has_value()) {
    return 2;
  }
  if (!manifest_is_readable(args->manifest_path)) {
    return 1;
  }

  Summary summary;
  std::vector<Record> records;
  if (!load_records(args->records_path, &records, &summary)) {
    return 1;
  }

  for (const Record& record : records) {
    std::string error;
    const bool accepted = replay_moves(record, &error);
    if (accepted) {
      ++summary.accepted_records;
    } else {
      ++summary.rejected_records;
    }

    if (accepted != record.expect_accept) {
      ++summary.expectation_failures;
      std::cerr << record.id << ": expected " << (record.expect_accept ? "accept" : "reject")
                << " but got " << (accepted ? "accept" : "reject");
      if (!error.empty()) {
        std::cerr << ": " << error;
      }
      std::cerr << '\n';
    }
  }

  std::cout << "summary total_records=" << summary.total_records
            << " accepted_records=" << summary.accepted_records
            << " rejected_records=" << summary.rejected_records << '\n';

  return summary.expectation_failures == 0 ? 0 : 1;
}
