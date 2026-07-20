#include "sequence_replay.h"

#include "vibe_othello/board_core/board.h"

#include <bit>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vibe_othello::tools::data_import {
namespace {

using board_core::Bitboard;
using board_core::Color;
using board_core::Move;
using board_core::MoveDelta;
using board_core::Position;
using board_core::Square;

std::string lower_ascii(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (const unsigned char value : text) {
    result.push_back(static_cast<char>(std::tolower(value)));
  }
  return result;
}

std::vector<std::string> split_words_lower(std::string_view text) {
  std::vector<std::string> words;
  std::size_t offset = 0;
  while (offset < text.size()) {
    while (offset < text.size() && std::isspace(static_cast<unsigned char>(text[offset])) != 0) {
      ++offset;
    }
    const std::size_t begin = offset;
    while (offset < text.size() && std::isspace(static_cast<unsigned char>(text[offset])) == 0) {
      ++offset;
    }
    if (begin != offset) {
      words.push_back(lower_ascii(text.substr(begin, offset - begin)));
    }
  }
  return words;
}

bool is_coordinate(std::string_view token) noexcept {
  return token.size() == 2 && token[0] >= 'a' && token[0] <= 'h' && token[1] >= '1' &&
         token[1] <= '8';
}

bool is_compact_coordinates(std::string_view token) noexcept {
  if (token.empty() || token.size() % 2 != 0) {
    return false;
  }
  for (std::size_t index = 0; index < token.size(); index += 2) {
    if (!is_coordinate(token.substr(index, 2))) {
      return false;
    }
  }
  return true;
}

std::vector<std::string> parse_transcript_tokens(std::string_view text) {
  std::vector<std::string> tokens = split_words_lower(text);
  if (tokens.size() == 1 && is_compact_coordinates(tokens[0])) {
    std::vector<std::string> compact_tokens;
    compact_tokens.reserve(tokens[0].size() / 2);
    for (std::size_t index = 0; index < tokens[0].size(); index += 2) {
      compact_tokens.push_back(tokens[0].substr(index, 2));
    }
    return compact_tokens;
  }
  return tokens;
}

std::optional<Square> parse_square(std::string_view token) noexcept {
  if (!is_coordinate(token)) {
    return std::nullopt;
  }
  return board_core::square_from_file_rank(token[0] - 'a', token[1] - '1');
}

std::string coordinate_for_square(Square square) {
  std::string result;
  result.push_back(static_cast<char>('a' + board_core::file_of(square)));
  result.push_back(static_cast<char>('1' + board_core::rank_of(square)));
  return result;
}

std::string join_canonical_moves(const std::vector<std::string>& moves) {
  std::string result;
  for (const std::string& move : moves) {
    if (!result.empty()) {
      result.push_back(' ');
    }
    result.append(move);
  }
  return result;
}

int black_final_disc_diff(Position position) noexcept {
  const int black = std::popcount(board_core::black_discs(position));
  const int white = std::popcount(board_core::white_discs(position));
  return black - white;
}

std::string relative_board_a1_to_h8(Position position) {
  std::string result;
  result.reserve(board_core::kSquareCount);
  for (int index = 0; index < board_core::kSquareCount; ++index) {
    const Bitboard square = board_core::bit(board_core::square_from_index(index));
    if ((position.player & square) != 0) {
      result.push_back('X');
    } else if ((position.opponent & square) != 0) {
      result.push_back('O');
    } else {
      result.push_back('-');
    }
  }
  return result;
}

SequenceReplaySnapshot snapshot_for(Position position, int ply, int final_disc_diff) {
  const std::string board = relative_board_a1_to_h8(position);
  return SequenceReplaySnapshot{
      .ply = ply,
      .board_a1_to_h8 = board,
      .label_score_side_to_move =
          position.side_to_move == Color::black ? final_disc_diff : -final_disc_diff,
      .player_disc_count = std::popcount(position.player),
      .opponent_disc_count = std::popcount(position.opponent),
      .empty_count = std::popcount(~board_core::occupied(position)),
  };
}

SequenceReplayResult reject(std::string error) {
  return SequenceReplayResult{
      .accepted = false,
      .error = std::move(error),
  };
}

} // namespace

namespace {

SequenceReplayResult replay_sequence(std::string_view text, bool require_terminal) {
  const std::vector<std::string> tokens = parse_transcript_tokens(text);
  if (tokens.empty()) {
    return reject("transcript is empty");
  }

  Position position = board_core::initial_position();
  std::vector<Position> positions;
  std::vector<std::string> canonical_moves;
  int move_ply = 0;
  int pass_count = 0;

  for (std::size_t token_index = 0; token_index < tokens.size(); ++token_index) {
    const std::string& token = tokens[token_index];
    const int displayed_token_index = static_cast<int>(token_index + 1);

    if (token == "pass" || token == "p") {
      if (board_core::has_legal_move(position)) {
        return reject("token " + std::to_string(displayed_token_index) +
                      ": pass is illegal with legal moves");
      }
      MoveDelta pass_delta{};
      if (!board_core::apply_pass(&position, &pass_delta)) {
        return reject("token " + std::to_string(displayed_token_index) +
                      ": pass after terminal position");
      }
      canonical_moves.push_back("pass");
      ++pass_count;
      continue;
    }

    const std::optional<Square> square = parse_square(token);
    if (!square.has_value()) {
      return reject("token " + std::to_string(displayed_token_index) + ": invalid move token '" +
                    token + "'");
    }

    const Move move = board_core::make_move(*square);
    MoveDelta move_delta{};
    if (!board_core::apply_move(&position, move, &move_delta)) {
      if (board_core::has_legal_move(position)) {
        return reject("token " + std::to_string(displayed_token_index) + ": illegal move '" +
                      token + "'");
      }

      Position after_pass = position;
      MoveDelta pass_delta{};
      if (!board_core::apply_pass(&after_pass, &pass_delta) ||
          !board_core::apply_move(&after_pass, move, &move_delta)) {
        return reject("token " + std::to_string(displayed_token_index) + ": illegal move '" +
                      token + "'");
      }
      position = after_pass;
      canonical_moves.push_back("pass");
      ++pass_count;
    }

    canonical_moves.push_back(coordinate_for_square(*square));
    ++move_ply;
    positions.push_back(position);
  }

  while (!board_core::is_terminal(position) && !board_core::has_legal_move(position)) {
    MoveDelta pass_delta{};
    if (!board_core::apply_pass(&position, &pass_delta)) {
      break;
    }
    ++pass_count;
  }

  const bool terminal = board_core::is_terminal(position);
  if (require_terminal && !terminal) {
    return reject("transcript ended before terminal position");
  }

  const int final_disc_diff = black_final_disc_diff(position);
  std::vector<SequenceReplaySnapshot> snapshots;
  snapshots.reserve(positions.size());
  int ply = 0;
  for (const Position snapshot_position : positions) {
    ++ply;
    snapshots.push_back(snapshot_for(snapshot_position, ply, final_disc_diff));
  }

  return SequenceReplayResult{
      .accepted = true,
      .pass_count = pass_count,
      .terminal = terminal,
      .canonical_moves = join_canonical_moves(canonical_moves),
      .snapshots = std::move(snapshots),
  };
}

} // namespace

SequenceReplayResult replay_egaroucid_sequence(std::string_view text) {
  return replay_sequence(text, true);
}

SequenceReplayResult replay_wthor_sequence(std::string_view text) {
  return replay_sequence(text, false);
}

} // namespace vibe_othello::tools::data_import
