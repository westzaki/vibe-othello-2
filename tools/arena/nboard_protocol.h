#ifndef VIBE_OTHELLO_TOOLS_ARENA_NBOARD_PROTOCOL_H_
#define VIBE_OTHELLO_TOOLS_ARENA_NBOARD_PROTOCOL_H_

#include "vibe_othello/board_core/board.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace vibe_othello::tools::arena {

[[nodiscard]] std::optional<board_core::Move> parse_nboard_move_response(std::string_view line);
[[nodiscard]] std::optional<std::string> format_nboard_ggf(std::span<const board_core::Move> moves,
                                                           std::string* error);
[[nodiscard]] std::string format_nboard_move(board_core::Move move);

} // namespace vibe_othello::tools::arena

#endif // VIBE_OTHELLO_TOOLS_ARENA_NBOARD_PROTOCOL_H_
