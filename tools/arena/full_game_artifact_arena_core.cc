#include "full_game_artifact_arena_core.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>

namespace vibe_othello::tools::full_game_arena {
namespace {

class SplitMix64 {
public:
  explicit SplitMix64(std::uint64_t seed) : state_(seed) {}

  [[nodiscard]] std::uint64_t next() noexcept {
    state_ += 0x9e3779b97f4a7c15ULL;
    std::uint64_t value = state_;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
  }

private:
  std::uint64_t state_;
};

std::size_t bounded_index(SplitMix64* rng, std::size_t upper_bound) noexcept {
  const std::uint64_t bound = static_cast<std::uint64_t>(upper_bound);
  const std::uint64_t threshold = static_cast<std::uint64_t>(-bound) % bound;
  for (;;) {
    const std::uint64_t value = rng->next();
    if (value >= threshold) {
      return static_cast<std::size_t>(value % bound);
    }
  }
}

void add(TelemetrySummary* summary, const SearchTelemetry& record) {
  ++summary->search_calls;
  summary->elapsed_ns += record.elapsed_ns;
  summary->engine_elapsed_ms += record.engine_elapsed_ms;
  summary->timer_accounting_delta_ns += record.timer_accounting_delta_ns;
  summary->nodes += record.nodes;
  summary->eval_calls += record.eval_calls;
  summary->leaf_nodes += record.leaf_nodes;
  summary->terminal_nodes += record.terminal_nodes;
  summary->pass_nodes += record.pass_nodes;
  summary->tt_probes += record.tt_probes;
  summary->tt_hits += record.tt_hits;
  summary->tt_cutoffs += record.tt_cutoffs;
  summary->tt_stores += record.tt_stores;
  summary->tt_replacements += record.tt_replacements;
  summary->tt_bucket_conflicts += record.tt_bucket_conflicts;
  summary->tt_same_key_updates += record.tt_same_key_updates;
  summary->tt_probe_slots += record.tt_probe_slots;
  summary->tt_generation_age_hits += record.tt_generation_age_hits;
  summary->tt_rejected_stores += record.tt_rejected_stores;
  summary->pvs_researches += record.pvs_researches;
  summary->aspiration_fail_lows += record.aspiration_fail_lows;
  summary->aspiration_fail_highs += record.aspiration_fail_highs;
  summary->iid_searches += record.iid_searches;
  summary->endgame_nodes += record.endgame_nodes;
  summary->selective_cuts += record.selective_cuts;
  summary->stopped_searches += record.stopped ? 1U : 0U;
  summary->exact_handoff_uses += record.exact_handoff_used ? 1U : 0U;
  summary->exact_root_searches += record.exact_root_search ? 1U : 0U;
  summary->exact_searches += record.exact ? 1U : 0U;
  summary->completed_depths.push_back(static_cast<std::uint64_t>(record.completed_depth));
  if (record.time_budget_applies) {
    summary->time_overshoot_ns.push_back(
        record.elapsed_ns > record.time_budget_ns ? record.elapsed_ns - record.time_budget_ns : 0);
  }
}

} // namespace

const char* engine_role_name(EngineRole role) noexcept {
  return role == EngineRole::candidate ? "candidate" : "baseline";
}

TelemetrySummary summarize_telemetry(std::span<const SearchTelemetry> records) {
  TelemetrySummary summary;
  for (const SearchTelemetry& record : records) {
    add(&summary, record);
  }
  return summary;
}

std::optional<double> events_per_second(std::uint64_t events, std::uint64_t elapsed_ns) noexcept {
  if (elapsed_ns == 0) {
    return std::nullopt;
  }
  return static_cast<double>(events) * 1'000'000'000.0 / static_cast<double>(elapsed_ns);
}

double nearest_rank_percentile(std::span<const std::uint64_t> values, double percentile) {
  if (values.empty()) {
    return 0.0;
  }
  std::vector<std::uint64_t> sorted{values.begin(), values.end()};
  std::sort(sorted.begin(), sorted.end());
  const double clamped = std::clamp(percentile, 0.0, 1.0);
  const std::size_t rank = static_cast<std::size_t>(std::ceil(clamped * sorted.size()));
  return static_cast<double>(sorted[rank == 0 ? 0 : rank - 1]);
}

std::vector<PairObservation> make_pair_observations(std::span<const PairGameResult> games) {
  struct PairAccumulator {
    PairObservation observation;
    bool candidate_black = false;
    bool candidate_white = false;
  };
  std::map<std::string, PairAccumulator> grouped;
  for (const PairGameResult& game : games) {
    PairAccumulator& pair = grouped[game.opening_key];
    pair.observation.opening_key = game.opening_key;
    pair.observation.score += game.candidate_score;
    pair.observation.disc_diff_sum += game.candidate_disc_diff;
    ++pair.observation.games;
    pair.observation.failed_or_illegal |= game.failed || game.illegal;
    pair.candidate_black |= game.side_assignment == "candidate_black";
    pair.candidate_white |= game.side_assignment == "candidate_white";
  }
  std::vector<PairObservation> pairs;
  pairs.reserve(grouped.size());
  for (auto& [_, accumulator] : grouped) {
    accumulator.observation.complete_color_swap = accumulator.observation.games == 2 &&
                                                  accumulator.candidate_black &&
                                                  accumulator.candidate_white;
    if (accumulator.observation.games != 0) {
      accumulator.observation.score /= static_cast<double>(accumulator.observation.games);
    }
    pairs.push_back(std::move(accumulator.observation));
  }
  return pairs;
}

BootstrapInterval paired_cluster_bootstrap(std::span<const PairObservation> pairs,
                                           std::uint64_t seed, std::uint32_t samples) {
  BootstrapInterval interval{
      .opening_pair_count = pairs.size(),
      .game_count = pairs.size() * 2U,
      .seed = seed,
      .samples = samples,
  };
  if (pairs.empty()) {
    return interval;
  }
  for (const PairObservation& pair : pairs) {
    interval.point_estimate += pair.score;
  }
  interval.point_estimate /= static_cast<double>(pairs.size());
  if (samples == 0) {
    interval.lower_95 = interval.point_estimate;
    interval.upper_95 = interval.point_estimate;
    return interval;
  }

  SplitMix64 rng{seed};
  std::vector<double> estimates;
  estimates.reserve(samples);
  for (std::uint32_t sample = 0; sample < samples; ++sample) {
    double total = 0.0;
    for (std::size_t draw = 0; draw < pairs.size(); ++draw) {
      total += pairs[bounded_index(&rng, pairs.size())].score;
    }
    estimates.push_back(total / static_cast<double>(pairs.size()));
  }
  std::sort(estimates.begin(), estimates.end());
  const auto percentile = [&estimates](double probability) {
    const std::size_t rank = static_cast<std::size_t>(std::ceil(probability * estimates.size()));
    return estimates[rank == 0 ? 0 : rank - 1];
  };
  interval.lower_95 = percentile(0.025);
  interval.upper_95 = percentile(0.975);
  return interval;
}

SanitySummary summarize_sanity(std::span<const PairObservation> pairs, bool same_artifact) {
  SanitySummary sanity{.same_artifact_applicable = same_artifact};
  sanity.paired_color_swap_complete = !pairs.empty();
  sanity.same_artifact_neutral = same_artifact && !pairs.empty();
  for (const PairObservation& pair : pairs) {
    sanity.paired_color_swap_complete &= pair.complete_color_swap;
    sanity.incomplete_pairs += pair.complete_color_swap ? 0U : 1U;
    sanity.paired_disc_diff_sum += pair.disc_diff_sum;
    sanity.same_artifact_neutral &= pair.complete_color_swap && !pair.failed_or_illegal &&
                                    std::abs(pair.score - 0.5) <= 0.0000001 &&
                                    pair.disc_diff_sum == 0;
  }
  return sanity;
}

bool argument_order_is_complementary(const BootstrapInterval& forward, int forward_disc_diff_sum,
                                     const BootstrapInterval& reverse,
                                     int reverse_disc_diff_sum) noexcept {
  return forward.opening_pair_count == reverse.opening_pair_count &&
         forward.game_count == reverse.game_count &&
         std::abs((forward.point_estimate + reverse.point_estimate) - 1.0) <= 0.0000001 &&
         forward_disc_diff_sum == -reverse_disc_diff_sum;
}

StrengthGateSummary evaluate_strength_gate(bool pure_limit_mode, std::size_t failed_games,
                                           std::size_t illegal_games, std::size_t incomplete_pairs,
                                           std::size_t opening_pairs,
                                           std::size_t minimum_opening_pairs,
                                           bool candidate_telemetry_present,
                                           bool baseline_telemetry_present) {
  StrengthGateSummary gate;
  if (!pure_limit_mode) {
    gate.reasons.emplace_back("limit_mode_not_pure");
  }
  if (failed_games != 0) {
    gate.reasons.emplace_back("failed_games_nonzero");
  }
  if (illegal_games != 0) {
    gate.reasons.emplace_back("illegal_games_nonzero");
  }
  if (incomplete_pairs != 0) {
    gate.reasons.emplace_back("incomplete_pairs_nonzero");
  }
  if (opening_pairs < minimum_opening_pairs) {
    gate.reasons.emplace_back("insufficient_opening_pairs");
  }
  if (!candidate_telemetry_present) {
    gate.reasons.emplace_back("candidate_telemetry_missing");
  }
  if (!baseline_telemetry_present) {
    gate.reasons.emplace_back("baseline_telemetry_missing");
  }
  gate.eligible = gate.reasons.empty();
  return gate;
}

} // namespace vibe_othello::tools::full_game_arena
