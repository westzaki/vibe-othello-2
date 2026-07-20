#include "normalized_tsv.h"

#include "vibe_othello/board_core/board.h"

#include <charconv>
#include <system_error>

namespace vibe_othello::tools::pattern {

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

std::optional<board_core::Position> position_from_a1_to_h8_board(std::string_view board) noexcept {
  if (board.size() != board_core::kSquareCount) {
    return std::nullopt;
  }

  board_core::Bitboard player = 0;
  board_core::Bitboard opponent = 0;
  for (std::size_t index = 0; index < board.size(); ++index) {
    const board_core::Square square = board_core::square_from_index(static_cast<int>(index));
    const board_core::Bitboard square_bit = board_core::bit(square);
    if (board[index] == 'X') {
      player |= square_bit;
    } else if (board[index] == 'O') {
      opponent |= square_bit;
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

} // namespace vibe_othello::tools::pattern
