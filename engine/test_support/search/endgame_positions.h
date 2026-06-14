#ifndef VIBE_OTHELLO_TEST_SUPPORT_SEARCH_ENDGAME_POSITIONS_H_
#define VIBE_OTHELLO_TEST_SUPPORT_SEARCH_ENDGAME_POSITIONS_H_

#include "vibe_othello/board_core/board.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vibe_othello::search::test_support {

struct EndgamePositionCase {
  std::string id;
  std::string category;
  board_core::Position position;
  std::uint8_t expected_empties = 0;
  std::string notes;
};

std::uint8_t endgame_empty_count(board_core::Position position) noexcept;

std::vector<EndgamePositionCase> load_endgame_position_corpus(std::string_view path);

std::optional<EndgamePositionCase>
find_endgame_position_case(const std::vector<EndgamePositionCase>& cases, std::string_view id);

board_core::Position generated_endgame_position(std::uint8_t target_empties);

} // namespace vibe_othello::search::test_support

#endif // VIBE_OTHELLO_TEST_SUPPORT_SEARCH_ENDGAME_POSITIONS_H_
