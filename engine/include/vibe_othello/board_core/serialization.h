#ifndef VIBE_OTHELLO_BOARD_CORE_SERIALIZATION_H_
#define VIBE_OTHELLO_BOARD_CORE_SERIALIZATION_H_

#include "vibe_othello/board_core/position.h"

#include <optional>
#include <string>
#include <string_view>

namespace vibe_othello::board_core {

std::string format_position(Position position);
std::optional<Position> parse_position(std::string_view text) noexcept;

} // namespace vibe_othello::board_core

#endif // VIBE_OTHELLO_BOARD_CORE_SERIALIZATION_H_
