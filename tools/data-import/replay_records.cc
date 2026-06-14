#include "replay_records.h"

#include "vibe_othello/board_core/board.h"

#include <bit>
#include <charconv>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace vibe_othello::tools::data_import {
namespace {

using board_core::Move;
using board_core::MoveDelta;
using board_core::MoveKind;
using board_core::Position;
using board_core::Square;

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
  return board_core::square_from_file_rank(file - 'a', rank - '1');
}

std::optional<Move> parse_move_token(std::string_view text) noexcept {
  if (text == "pass") {
    return board_core::make_pass();
  }
  const std::optional<Square> square = parse_square(text);
  if (!square.has_value()) {
    return std::nullopt;
  }
  return board_core::make_move(*square);
}

int final_disc_diff(Position position) noexcept {
  const int black = std::popcount(board_core::black_discs(position));
  const int white = std::popcount(board_core::white_discs(position));
  return black - white;
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

  std::string parse_error;
  const std::optional<int> expected_final_disc_diff = parse_int(fields[3]);
  if (!expected_final_disc_diff.has_value()) {
    parse_error = "expected_final_disc_diff is invalid";
  }

  return Record{
      .line_number = line_number,
      .id = std::string{fields[0]},
      .expect_accept = expect_accept,
      .moves = std::string{fields[2]},
      .expected_final_disc_diff = expected_final_disc_diff,
      .notes = std::string{fields[4]},
      .parse_error = std::move(parse_error),
  };
}

} // namespace

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
      std::cerr << error << '\n';
      return false;
    }
    records->push_back(std::move(*record));
  }

  if (!saw_header) {
    std::cerr << "records file is empty\n";
    return false;
  }
  return true;
}

ReplayResult replay_record(const Record& record, bool collect_positions) {
  if (!record.parse_error.empty()) {
    return ReplayResult{
        .accepted = false,
        .error = record.parse_error,
    };
  }

  Position position = board_core::initial_position();
  std::vector<Position> positions;
  for (std::string_view token : split_words(record.moves)) {
    const std::optional<Move> move = parse_move_token(token);
    if (!move.has_value()) {
      return ReplayResult{
          .accepted = false,
          .error = "invalid move coordinate",
      };
    }

    if (move->kind == MoveKind::pass && board_core::has_legal_move(position)) {
      return ReplayResult{
          .accepted = false,
          .error = "pass is illegal while legal moves exist",
      };
    }

    MoveDelta delta{};
    const bool ok = move->kind == MoveKind::pass ? board_core::apply_pass(&position, &delta)
                                                 : board_core::apply_move(&position, *move, &delta);
    if (!ok) {
      return ReplayResult{
          .accepted = false,
          .error = "illegal move sequence",
      };
    }
    if (collect_positions) {
      positions.push_back(position);
    }
  }

  if (record.expect_accept && record.expected_final_disc_diff.has_value() &&
      final_disc_diff(position) != *record.expected_final_disc_diff) {
    return ReplayResult{
        .accepted = false,
        .error = "final disc difference mismatch",
        .positions = std::move(positions),
    };
  }

  return ReplayResult{
      .accepted = true,
      .positions = std::move(positions),
  };
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

} // namespace vibe_othello::tools::data_import
