#include "search/endgame_positions.h"

#include "vibe_othello/board_core/serialization.h"

#include <array>
#include <bit>
#include <cassert>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <string>
#include <system_error>

namespace vibe_othello::search::test_support {
namespace {

void require_invariant(bool condition) noexcept {
  assert(condition);
  if (!condition) {
    std::abort();
  }
}

std::vector<std::string_view> split_tabs(std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t begin = 0;
  while (begin <= line.size()) {
    const std::size_t tab = line.find('\t', begin);
    if (tab == std::string_view::npos) {
      fields.push_back(line.substr(begin));
      break;
    }
    fields.push_back(line.substr(begin, tab - begin));
    begin = tab + 1;
  }
  return fields;
}

std::optional<int> parse_int(std::string_view text) noexcept {
  int value = 0;
  const char* begin = text.data();
  const char* end = text.data() + text.size();
  const std::from_chars_result result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end) {
    return std::nullopt;
  }
  return value;
}

board_core::Move select_legal_move(board_core::Position position, std::size_t choice) {
  const board_core::Bitboard legal = board_core::legal_moves(position);
  if (legal == 0) {
    return board_core::make_pass();
  }

  std::array<board_core::Move, board_core::kSquareCount> moves{};
  std::size_t move_count = 0;
  for (int square_index = 0; square_index < board_core::kSquareCount; ++square_index) {
    const board_core::Square move_square = board_core::square_from_index(square_index);
    if ((legal & board_core::bit(move_square)) != 0) {
      moves[move_count] = board_core::make_move(move_square);
      ++move_count;
    }
  }

  require_invariant(move_count > 0);
  return moves[choice % move_count];
}

} // namespace

std::uint8_t endgame_empty_count(board_core::Position position) noexcept {
  return static_cast<std::uint8_t>(std::popcount(~board_core::occupied(position)));
}

std::vector<EndgamePositionCase> load_endgame_position_corpus(std::string_view path) {
  std::ifstream input{std::string(path)};
  require_invariant(input.is_open());

  std::vector<EndgamePositionCase> cases;
  std::string line;
  bool saw_header = false;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }

    const std::vector<std::string_view> fields = split_tabs(line);
    if (!saw_header) {
      require_invariant(fields.size() == 5);
      require_invariant(fields[0] == "id");
      require_invariant(fields[1] == "category");
      require_invariant(fields[2] == "position");
      require_invariant(fields[3] == "expected_empties");
      require_invariant(fields[4] == "notes");
      saw_header = true;
      continue;
    }

    require_invariant(fields.size() == 5);
    const std::optional<board_core::Position> position = board_core::parse_position(fields[2]);
    require_invariant(position.has_value());
    const std::optional<int> expected_empties = parse_int(fields[3]);
    require_invariant(expected_empties.has_value());
    require_invariant(*expected_empties >= 0);
    require_invariant(*expected_empties <= board_core::kSquareCount);
    cases.push_back(EndgamePositionCase{
        .id = std::string{fields[0]},
        .category = std::string{fields[1]},
        .position = *position,
        .expected_empties = static_cast<std::uint8_t>(*expected_empties),
        .notes = std::string{fields[4]},
    });
  }

  require_invariant(saw_header);
  require_invariant(!cases.empty());
  return cases;
}

std::optional<EndgamePositionCase>
find_endgame_position_case(const std::vector<EndgamePositionCase>& cases, std::string_view id) {
  for (const EndgamePositionCase& position_case : cases) {
    if (position_case.id == id) {
      return position_case;
    }
  }
  return std::nullopt;
}

board_core::Position generated_endgame_position(std::uint8_t target_empties) {
  static constexpr std::array<std::size_t, 64> kChoices{
      12, 11, 6, 6, 7,  11, 13, 13, 0, 15, 9, 12, 5,  7,  13, 15, 15, 13, 12, 8, 14, 5,
      3,  4,  2, 1, 12, 14, 5,  14, 4, 0,  9, 11, 13, 15, 12, 13, 2,  7,  5,  5, 7,  6,
      3,  8,  1, 9, 4,  2,  10, 6,  0, 11, 5, 13, 7,  3,  12, 1,  8,  4,  14, 2,
  };

  board_core::Position position = board_core::initial_position();
  std::size_t ply = 0;
  while (endgame_empty_count(position) > target_empties) {
    require_invariant(!board_core::is_terminal(position));
    const board_core::Move move = select_legal_move(position, kChoices[ply % kChoices.size()]);
    board_core::MoveDelta delta{};
    require_invariant(board_core::apply_move(&position, move, &delta));
    if (move.kind == board_core::MoveKind::normal) {
      ++ply;
    }
  }

  require_invariant(endgame_empty_count(position) == target_empties);
  return position;
}

} // namespace vibe_othello::search::test_support
