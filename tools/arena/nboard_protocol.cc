#include "nboard_protocol.h"

#include "arena_core.h"
#include "vibe_othello/board_core/board.h"

#include <cctype>
#include <string>
#include <string_view>

namespace vibe_othello::tools::arena {
namespace {

constexpr std::string_view kInitialBoard =
    "---------------------------O*------*O--------------------------- *";

std::string_view trim(std::string_view text) noexcept {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.remove_prefix(1);
  }
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.remove_suffix(1);
  }
  return text;
}

} // namespace

std::optional<board_core::Move> parse_nboard_move_response(std::string_view line) {
  line = trim(line);
  if (!line.starts_with("===")) {
    return std::nullopt;
  }
  line.remove_prefix(3);
  line = trim(line);
  if (line.empty()) {
    return std::nullopt;
  }

  const std::size_t token_end = line.find_first_of("/ \t");
  std::string token{line.substr(0, token_end)};
  for (char& character : token) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  if (token == "pa" || token == "pass") {
    return board_core::make_pass();
  }
  return parse_move_token(token);
}

std::optional<std::string> format_nboard_ggf(std::span<const board_core::Move> moves,
                                             std::string* error) {
  board_core::Position position = board_core::initial_position();
  std::string ggf = "(;GM[Othello]PC[Vibe Othello Arena]RE[?]TY[8]BO[8 ";
  ggf += kInitialBoard;
  ggf.push_back(']');
  for (const board_core::Move move : moves) {
    ggf += position.side_to_move == board_core::Color::black ? "B[" : "W[";
    ggf += format_nboard_move(move);
    ggf.push_back(']');

    board_core::MoveDelta delta{};
    const bool applied = move.kind == board_core::MoveKind::pass
                             ? board_core::apply_pass(&position, &delta)
                             : board_core::apply_move(&position, move, &delta);
    if (!applied) {
      *error = "cannot format illegal NBoard game move: " + format_move(move);
      return std::nullopt;
    }
  }
  ggf += ";)";
  return ggf;
}

std::string format_nboard_move(board_core::Move move) {
  if (move.kind == board_core::MoveKind::pass) {
    return "PA";
  }
  std::string text = format_move(move);
  for (char& character : text) {
    character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
  }
  return text;
}

} // namespace vibe_othello::tools::arena
