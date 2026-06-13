#include "vibe_othello/board_core/serialization.h"

namespace vibe_othello::board_core {
namespace {

inline constexpr int kSerializedBoardLength = kSquareCount + (kBoardSize - 1);
inline constexpr int kSerializedPositionLength = kSerializedBoardLength + 2;

constexpr char side_to_move_char(Color color) noexcept {
  return color == Color::black ? 'b' : 'w';
}

std::optional<Color> parse_side_to_move(char value) noexcept {
  if (value == 'b') {
    return Color::black;
  }
  if (value == 'w') {
    return Color::white;
  }
  return std::nullopt;
}

bool parse_disc(char value, Square square, Bitboard* black, Bitboard* white) noexcept {
  if (value == '.') {
    return true;
  }
  if (value == 'B') {
    *black |= bit(square);
    return true;
  }
  if (value == 'W') {
    *white |= bit(square);
    return true;
  }
  return false;
}

} // namespace

std::string format_position(Position position) {
  std::string result;
  result.reserve(kSerializedPositionLength);

  const Bitboard black = black_discs(position);
  const Bitboard white = white_discs(position);

  for (int rank = kBoardSize - 1; rank >= 0; --rank) {
    if (rank != kBoardSize - 1) {
      result.push_back('/');
    }

    for (int file = 0; file < kBoardSize; ++file) {
      const Bitboard square = bit(square_from_file_rank(file, rank));
      if ((black & square) != 0) {
        result.push_back('B');
      } else if ((white & square) != 0) {
        result.push_back('W');
      } else {
        result.push_back('.');
      }
    }
  }

  result.push_back(' ');
  result.push_back(side_to_move_char(position.side_to_move));
  return result;
}

std::optional<Position> parse_position(std::string_view text) noexcept {
  if (text.size() != kSerializedPositionLength) {
    return std::nullopt;
  }

  Bitboard black = 0;
  Bitboard white = 0;
  int offset = 0;

  for (int rank = kBoardSize - 1; rank >= 0; --rank) {
    if (rank != kBoardSize - 1) {
      if (text[static_cast<std::size_t>(offset)] != '/') {
        return std::nullopt;
      }
      ++offset;
    }

    for (int file = 0; file < kBoardSize; ++file) {
      const Square square = square_from_file_rank(file, rank);
      if (!parse_disc(text[static_cast<std::size_t>(offset)], square, &black, &white)) {
        return std::nullopt;
      }
      ++offset;
    }
  }

  if (text[static_cast<std::size_t>(offset)] != ' ') {
    return std::nullopt;
  }
  ++offset;

  const std::optional<Color> side_to_move =
      parse_side_to_move(text[static_cast<std::size_t>(offset)]);
  if (!side_to_move.has_value()) {
    return std::nullopt;
  }

  Position position{
      .player = *side_to_move == Color::black ? black : white,
      .opponent = *side_to_move == Color::black ? white : black,
      .side_to_move = *side_to_move,
  };
  if (!is_valid(position)) {
    return std::nullopt;
  }

  return position;
}

} // namespace vibe_othello::board_core
