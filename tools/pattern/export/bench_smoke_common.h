#ifndef VIBE_OTHELLO_TOOLS_PATTERN_EXPORT_BENCH_SMOKE_COMMON_H_
#define VIBE_OTHELLO_TOOLS_PATTERN_EXPORT_BENCH_SMOKE_COMMON_H_

#include "vibe_othello/board_core/position.h"
#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/pattern_weights.h"
#include "vibe_othello/search/score.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vibe_othello::tools::pattern::bench_smoke {

struct Args {
  std::string positions_tsv_path;
  std::string v0a_weights_path;
  std::string v0b_weights_path;
  std::string v0a_artifact_checksum;
  std::string v0b_artifact_checksum;
  std::string report_out_path;
  std::string pattern_set = "tiny";
};

struct PositionFixture {
  std::string position_id;
  board_core::Position position;
  int disc_count = 0;
};

enum class PositionFilter {
  all,
  searchable_depth_one,
};

[[nodiscard]] std::optional<Args> parse_args(int argc, char** argv,
                                             std::string_view executable_name);
[[nodiscard]] std::string json_escape(std::string_view text);
[[nodiscard]] std::string checksum_for(std::string_view text);
[[nodiscard]] int phase_for_disc_count(int disc_count) noexcept;
[[nodiscard]] std::optional<evaluation::PatternWeights>
load_weights(const std::string& path, const evaluation::PatternSet& pattern_set);
[[nodiscard]] std::optional<std::vector<PositionFixture>> load_positions(const std::string& path,
                                                                         PositionFilter filter);
[[nodiscard]] bool score_in_search_range(search::Score score) noexcept;
[[nodiscard]] std::string_view smoke_source_for(const evaluation::PatternSet& pattern_set,
                                                bool search_smoke) noexcept;
[[nodiscard]] bool write_report(const std::string& path, std::string_view report_body,
                                std::string_view checksum, std::string_view report_label);

} // namespace vibe_othello::tools::pattern::bench_smoke

#endif // VIBE_OTHELLO_TOOLS_PATTERN_EXPORT_BENCH_SMOKE_COMMON_H_
