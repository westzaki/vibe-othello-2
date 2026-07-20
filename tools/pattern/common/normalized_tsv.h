#ifndef VIBE_OTHELLO_TOOLS_PATTERN_COMMON_NORMALIZED_TSV_H_
#define VIBE_OTHELLO_TOOLS_PATTERN_COMMON_NORMALIZED_TSV_H_

#include "vibe_othello/board_core/position.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace vibe_othello::tools::pattern {

inline constexpr std::string_view kNormalizedHeaderV1 =
    "record_id\tposition_id\tsource_dataset_id\tsplit\tboard_a1_to_h8\tlabel_kind\tlabel_"
    "unit\tlabel_perspective\tlabel_score_side_to_move\toccupied_count\tphase\tplayer_disc_"
    "count\topponent_disc_count\tempty_count";

inline constexpr std::string_view kNormalizedHeaderV2 =
    "record_id\tposition_id\tgame_group_id\tboard_id\tsource_occurrence_id\tsource_dataset_id\t"
    "split\tboard_a1_to_h8\tlabel_kind\tlabel_unit\tlabel_perspective\tlabel_score_side_to_"
    "move\toccupied_count\tphase\tplayer_disc_count\topponent_disc_count\tempty_count";

[[nodiscard]] std::string_view trim_trailing_cr(std::string_view text) noexcept;
[[nodiscard]] std::vector<std::string_view> split_tabs(std::string_view text);
[[nodiscard]] std::optional<int> parse_int(std::string_view text) noexcept;
[[nodiscard]] std::optional<std::uint64_t> parse_u64(std::string_view text) noexcept;
[[nodiscard]] std::optional<board_core::Position>
position_from_a1_to_h8_board(std::string_view board) noexcept;

} // namespace vibe_othello::tools::pattern

#endif // VIBE_OTHELLO_TOOLS_PATTERN_COMMON_NORMALIZED_TSV_H_
