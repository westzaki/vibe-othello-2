#include "arena_core.h"

#include "vibe_othello/board_core/coordinates.h"

#include <bit>
#include <charconv>
#include <cstddef>

namespace vibe_othello::tools::arena {
namespace {

std::string_view trim(std::string_view text) noexcept {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
    text.remove_prefix(1);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
    text.remove_suffix(1);
  }
  return text;
}

std::vector<std::string_view> split_words(std::string_view text) {
  std::vector<std::string_view> words;
  std::size_t pos = 0;
  while (pos < text.size()) {
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) {
      ++pos;
    }
    const std::size_t begin = pos;
    while (pos < text.size() && text[pos] != ' ' && text[pos] != '\t') {
      ++pos;
    }
    if (begin != pos) {
      words.push_back(text.substr(begin, pos - begin));
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

std::optional<std::vector<board_core::Move>> parse_move_sequence(std::string_view text) {
  std::vector<board_core::Move> moves;
  for (std::string_view word : split_words(text)) {
    const std::optional<board_core::Move> move = parse_move_token(word);
    if (!move.has_value()) {
      return std::nullopt;
    }
    moves.push_back(*move);
  }
  return moves;
}

} // namespace

std::optional<board_core::Square> parse_square(std::string_view text) noexcept {
  if (text.size() != 2) {
    return std::nullopt;
  }
  const char file_char = text[0];
  const char rank_char = text[1];
  if (file_char < 'a' || file_char > 'h' || rank_char < '1' || rank_char > '8') {
    return std::nullopt;
  }
  return board_core::square_from_file_rank(file_char - 'a', rank_char - '1');
}

std::optional<board_core::Move> parse_move_token(std::string_view text) noexcept {
  if (text == "pass") {
    return board_core::make_pass();
  }
  const std::optional<board_core::Square> square = parse_square(text);
  if (!square.has_value()) {
    return std::nullopt;
  }
  return board_core::make_move(*square);
}

std::string format_move(board_core::Move move) {
  if (move.kind == board_core::MoveKind::pass) {
    return "pass";
  }
  const int file = board_core::file_of(move.square);
  const int rank = board_core::rank_of(move.square);
  std::string text;
  text.push_back(static_cast<char>('a' + file));
  text.push_back(static_cast<char>('1' + rank));
  return text;
}

std::string format_moves(std::span<const board_core::Move> moves) {
  std::string text;
  for (board_core::Move move : moves) {
    if (!text.empty()) {
      text.push_back(' ');
    }
    text += format_move(move);
  }
  return text;
}

std::optional<BestMoveResponse> parse_bestmove_response(std::string_view line) {
  line = trim(line);
  const std::vector<std::string_view> words = split_words(line);
  if (words.size() != 6 || words[0] != "bestmove" || words[2] != "score" || words[4] != "depth") {
    return std::nullopt;
  }

  std::optional<board_core::Move> move;
  if (words[1] != "none") {
    move = parse_move_token(words[1]);
    if (!move.has_value()) {
      return std::nullopt;
    }
  }
  const std::optional<int> score = parse_int(words[3]);
  const std::optional<int> depth = parse_int(words[5]);
  if (!score.has_value() || !depth.has_value() || *depth < 0) {
    return std::nullopt;
  }
  return BestMoveResponse{
      .move = move,
      .score = static_cast<search::Score>(*score),
      .depth = static_cast<search::Depth>(*depth),
  };
}

bool replay_moves(std::span<const board_core::Move> moves, board_core::Position* position,
                  std::string* error) {
  *position = board_core::initial_position();
  for (board_core::Move move : moves) {
    if (move.kind == board_core::MoveKind::pass && board_core::has_legal_move(*position)) {
      *error = "pass is illegal while legal moves exist";
      return false;
    }

    board_core::MoveDelta delta{};
    const bool ok = move.kind == board_core::MoveKind::pass
                        ? board_core::apply_pass(position, &delta)
                        : board_core::apply_move(position, move, &delta);
    if (!ok) {
      *error = "illegal opening move: " + format_move(move);
      return false;
    }
  }
  return true;
}

std::optional<std::vector<Opening>> parse_openings_file(std::string_view content,
                                                        std::string* error) {
  std::vector<Opening> openings;
  std::size_t line_number = 1;
  std::size_t offset = 0;
  while (offset <= content.size()) {
    const std::size_t next = content.find('\n', offset);
    std::string_view line = next == std::string_view::npos ? content.substr(offset)
                                                           : content.substr(offset, next - offset);
    offset = next == std::string_view::npos ? content.size() + 1 : next + 1;

    if (const std::size_t comment = line.find('#'); comment != std::string_view::npos) {
      line = line.substr(0, comment);
    }
    line = trim(line);
    if (line.empty()) {
      ++line_number;
      continue;
    }

    std::string id;
    std::string_view sequence_text = line;
    if (const std::size_t colon = line.find(':'); colon != std::string_view::npos) {
      id = std::string{trim(line.substr(0, colon))};
      sequence_text = trim(line.substr(colon + 1));
      if (id.empty()) {
        *error = "line " + std::to_string(line_number) + ": opening id is empty";
        return std::nullopt;
      }
      if (sequence_text.empty() && id != "start") {
        const std::optional<std::vector<board_core::Move>> shorthand = parse_move_sequence(id);
        if (shorthand.has_value()) {
          sequence_text = id;
        }
      }
    } else {
      id = std::string{line};
    }

    const std::optional<std::vector<board_core::Move>> moves = parse_move_sequence(sequence_text);
    if (!moves.has_value()) {
      *error = "line " + std::to_string(line_number) + ": invalid move token";
      return std::nullopt;
    }

    board_core::Position position{};
    std::string replay_error;
    if (!replay_moves(*moves, &position, &replay_error)) {
      *error = "line " + std::to_string(line_number) + ": " + replay_error;
      return std::nullopt;
    }

    if (id == "start") {
      id = "start";
    } else if (id.empty()) {
      id = format_moves(*moves);
    }
    openings.push_back(Opening{.id = id, .moves = *moves});
    ++line_number;
  }

  if (openings.empty()) {
    *error = "opening file did not contain any openings";
    return std::nullopt;
  }
  return openings;
}

Summary summarize(std::span<const GameRecord> games) noexcept {
  Summary summary;
  int disc_diff_sum = 0;
  for (const GameRecord& game : games) {
    ++summary.games;
    disc_diff_sum += game.candidate_disc_diff;
    if (game.candidate_result == "win") {
      ++summary.candidate_wins;
      summary.candidate_score += 1.0;
    } else if (game.candidate_result == "draw") {
      ++summary.candidate_draws;
      summary.candidate_score += 0.5;
    } else {
      ++summary.candidate_losses;
    }
    if (game.reason != "terminal") {
      ++summary.invalid_games;
    }
  }
  if (summary.games > 0) {
    summary.candidate_win_rate =
        static_cast<double>(summary.candidate_wins) / static_cast<double>(summary.games);
    summary.candidate_avg_disc_diff =
        static_cast<double>(disc_diff_sum) / static_cast<double>(summary.games);
  }
  return summary;
}

std::string json_escape(std::string_view text) {
  std::string escaped;
  for (const char ch : text) {
    switch (ch) {
    case '\\':
      escaped += "\\\\";
      break;
    case '"':
      escaped += "\\\"";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      escaped.push_back(ch);
      break;
    }
  }
  return escaped;
}

} // namespace vibe_othello::tools::arena
