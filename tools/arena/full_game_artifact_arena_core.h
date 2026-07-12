#ifndef VIBE_OTHELLO_TOOLS_FULL_GAME_ARTIFACT_ARENA_CORE_H_
#define VIBE_OTHELLO_TOOLS_FULL_GAME_ARTIFACT_ARENA_CORE_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vibe_othello::tools::full_game_arena {

enum class EngineRole : std::uint8_t {
  candidate,
  baseline,
};

struct SearchTelemetry {
  EngineRole role = EngineRole::candidate;
  std::string side_to_move;
  int occupied_count = 0;
  int phase = 0;
  int completed_depth = 0;
  std::uint64_t elapsed_ms = 0;
  std::uint64_t nodes = 0;
  std::uint64_t eval_calls = 0;
  std::uint64_t leaf_nodes = 0;
  std::uint64_t terminal_nodes = 0;
  std::uint64_t pass_nodes = 0;
  std::uint64_t tt_probes = 0;
  std::uint64_t tt_hits = 0;
  std::uint64_t tt_cutoffs = 0;
  std::uint64_t tt_stores = 0;
  std::uint64_t tt_overwrites = 0;
  std::uint64_t tt_collisions = 0;
  std::uint64_t tt_rejected_stores = 0;
  std::uint64_t pvs_researches = 0;
  std::uint64_t aspiration_fail_lows = 0;
  std::uint64_t aspiration_fail_highs = 0;
  std::uint64_t iid_searches = 0;
  std::uint64_t endgame_nodes = 0;
  std::uint64_t selective_cuts = 0;
  bool exact = false;
  bool stopped = false;
  bool exact_handoff_attempted = false;
  bool time_budget_applies = false;
  std::uint64_t time_budget_ms = 0;
};

struct TelemetrySummary {
  std::uint64_t search_calls = 0;
  std::uint64_t elapsed_ms = 0;
  std::uint64_t nodes = 0;
  std::uint64_t eval_calls = 0;
  std::uint64_t leaf_nodes = 0;
  std::uint64_t terminal_nodes = 0;
  std::uint64_t pass_nodes = 0;
  std::uint64_t tt_probes = 0;
  std::uint64_t tt_hits = 0;
  std::uint64_t tt_cutoffs = 0;
  std::uint64_t tt_stores = 0;
  std::uint64_t tt_overwrites = 0;
  std::uint64_t tt_collisions = 0;
  std::uint64_t tt_rejected_stores = 0;
  std::uint64_t pvs_researches = 0;
  std::uint64_t aspiration_fail_lows = 0;
  std::uint64_t aspiration_fail_highs = 0;
  std::uint64_t iid_searches = 0;
  std::uint64_t endgame_nodes = 0;
  std::uint64_t selective_cuts = 0;
  std::uint64_t stopped_searches = 0;
  std::uint64_t exact_handoff_attempts = 0;
  std::uint64_t exact_searches = 0;
  std::vector<std::uint64_t> completed_depths;
  std::vector<std::uint64_t> time_overshoot_ms;
};

struct PairGameResult {
  std::string opening_key;
  std::string side_assignment;
  double candidate_score = 0.0;
  int candidate_disc_diff = 0;
  bool failed = false;
  bool illegal = false;
};

struct PairObservation {
  std::string opening_key;
  double score = 0.0;
  int disc_diff_sum = 0;
  int games = 0;
  bool complete_color_swap = false;
  bool failed_or_illegal = false;
};

struct BootstrapInterval {
  double point_estimate = 0.0;
  double lower_95 = 0.0;
  double upper_95 = 0.0;
  std::size_t opening_pair_count = 0;
  std::size_t game_count = 0;
  std::uint64_t seed = 0;
  std::uint32_t samples = 0;
};

struct SanitySummary {
  bool paired_color_swap_complete = false;
  bool same_artifact_applicable = false;
  bool same_artifact_neutral = false;
  bool argument_order_complementary = false;
  int paired_disc_diff_sum = 0;
  std::size_t incomplete_pairs = 0;
};

[[nodiscard]] const char* engine_role_name(EngineRole role) noexcept;
[[nodiscard]] TelemetrySummary summarize_telemetry(std::span<const SearchTelemetry> records);
[[nodiscard]] double nearest_rank_percentile(std::span<const std::uint64_t> values,
                                             double percentile);
[[nodiscard]] std::vector<PairObservation>
make_pair_observations(std::span<const PairGameResult> games);
[[nodiscard]] BootstrapInterval paired_cluster_bootstrap(std::span<const PairObservation> pairs,
                                                         std::uint64_t seed, std::uint32_t samples);
[[nodiscard]] SanitySummary summarize_sanity(std::span<const PairObservation> pairs,
                                             bool same_artifact);
[[nodiscard]] bool argument_order_is_complementary(const BootstrapInterval& forward,
                                                   int forward_disc_diff_sum,
                                                   const BootstrapInterval& reverse,
                                                   int reverse_disc_diff_sum) noexcept;

} // namespace vibe_othello::tools::full_game_arena

#endif // VIBE_OTHELLO_TOOLS_FULL_GAME_ARTIFACT_ARENA_CORE_H_
