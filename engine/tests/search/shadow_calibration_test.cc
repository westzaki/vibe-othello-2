#include "../../src/search/search_internal.h"
#include "search/endgame_positions.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/search/search.h"

#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <list>
#include <utility>
#include <vector>

namespace vibe_othello::search {
namespace {

class DiscDifferenceEvaluator final : public Evaluator {
public:
  Score evaluate(const board_core::Position& position) const noexcept override {
    return static_cast<Score>(std::popcount(position.player)) -
           static_cast<Score>(std::popcount(position.opponent));
  }
};

class Collector final : public ShadowCalibrationSink {
public:
  void record(const ShadowCalibrationSample& sample) noexcept override {
    samples.push_back(sample);
  }

  std::vector<ShadowCalibrationSample> samples;
};

constexpr std::string_view kProbCutTestChecksum =
    "0000000000000000000000000000000000000000000000000000000000000000";
constexpr std::array kProbCut3To1{ProbCutDepthPairV1{.deep_depth = 3, .shallow_depth = 1}};
constexpr std::array kProbCut4To2{ProbCutDepthPairV1{.deep_depth = 4, .shallow_depth = 2}};
constexpr std::array kShadowDepthPairs{
    ShadowCalibrationDepthPairV1{.deep_depth = 2, .shallow_depth = 1},
    ShadowCalibrationDepthPairV1{.deep_depth = 3, .shallow_depth = 2},
    ShadowCalibrationDepthPairV1{.deep_depth = 4, .shallow_depth = 3},
    ShadowCalibrationDepthPairV1{.deep_depth = 5, .shallow_depth = 4},
    ShadowCalibrationDepthPairV1{.deep_depth = 6, .shallow_depth = 5},
    ShadowCalibrationDepthPairV1{.deep_depth = 7, .shallow_depth = 6},
};

std::array<ProbCutCalibrationEntryV1, 13> probcut_entries(Depth deep_depth, Depth shallow_depth) {
  std::array<ProbCutCalibrationEntryV1, 13> entries{};
  for (std::uint8_t phase = 0; phase < entries.size(); ++phase) {
    entries[phase] = ProbCutCalibrationEntryV1{
        .phase = phase,
        .deep_depth = deep_depth,
        .shallow_depth = shallow_depth,
        .regression_slope = 1.0,
        .intercept = 100.0,
        .residual_sigma = 1.0,
        .confidence_multiplier = 1.0,
        .minimum_shallow_score = -200,
        .maximum_shallow_score = 200,
        .minimum_beta = -200,
        .maximum_beta = 200,
    };
  }
  return entries;
}

template <std::size_t Size>
ProbCutCalibrationProfileV1
probcut_profile(const std::array<ProbCutCalibrationEntryV1, Size>& entries) {
  static std::list<std::vector<ProbCutSchedulerEvidenceV1>> evidence_storage;
  std::vector<ProbCutSchedulerEvidenceV1>& evidence = evidence_storage.emplace_back();
  for (const ProbCutCalibrationEntryV1& entry : entries) {
    evidence.push_back(ProbCutSchedulerEvidenceV1{
        .pair_prefix_length = 1,
        .maximum_probes_per_node = 1,
        .phase = entry.phase,
        .search_mode = entry.search_mode,
        .minimum_empties = entry.minimum_empties,
        .maximum_empties = entry.maximum_empties,
        .deep_depth = entry.deep_depth,
        .exact_handoff_enabled = entry.exact_handoff_enabled,
        .exact_handoff_threshold = entry.exact_handoff_threshold,
        .minimum_exact_handoff_distance = entry.minimum_exact_handoff_distance,
        .maximum_exact_handoff_distance = entry.maximum_exact_handoff_distance,
        .holdout_node_count = 100,
        .false_cut_count = 0,
        .cut_candidate_count = 100,
        .false_cut_rate_upper_bound = 0.05,
    });
  }
  return ProbCutCalibrationProfileV1{
      .profile_id = "shadow-isolation-fixture-v1",
      .source_calibration_report_checksum_sha256 = kProbCutTestChecksum,
      .evaluator_family = "synthetic-disc-difference",
      .artifact_family = "none",
      .node_class = ProbCutNodeClassV1::non_pv_scout_beta_only,
      .validated_pair_order =
          entries.front().deep_depth == 3 ? std::span{kProbCut3To1} : std::span{kProbCut4To2},
      .validated_maximum_probes_per_node = 1,
      .joint_holdout_checksum_sha256 = kProbCutTestChecksum,
      .joint_false_cut_count = 0,
      .joint_cut_candidate_count = 100,
      .joint_false_cut_rate_upper_bound = 0.05,
      .scheduler_evidence = evidence,
      .entries = entries,
  };
}

ProbCutOptionsV1 probcut_options(const ProbCutCalibrationProfileV1* profile, bool shadow_verify) {
  return ProbCutOptionsV1{
      .use_probcut = true,
      .minimum_depth = 3,
      .shallow_depth_reduction = 2,
      .confidence_multiplier = 1.0,
      .minimum_margin = 0,
      .maximum_margin = 10,
      .non_pv_only = true,
      .beta_only = true,
      .disable_near_exact = true,
      .shadow_verify = shadow_verify,
      .evaluator_family = profile->evaluator_family,
      .artifact_family = profile->artifact_family,
      .calibration_profile_id = profile->profile_id,
      .calibration_profile = profile,
  };
}

SelectiveSearchOptionsV1 enabled_shadow(Collector* collector) {
  return SelectiveSearchOptionsV1{
      .enable_shadow_calibration = true,
      .sample_rate = kShadowCalibrationSampleRateScale,
      .max_samples_per_search = 64,
      .ordered_depth_pairs = kShadowDepthPairs,
      .include_pv_nodes = true,
      .include_pass_nodes = true,
      .include_near_exact_nodes = false,
      .sampling_seed = 42,
      .repo_sha = "0123456789abcdef",
      .search_config_id = "test-search-v1",
      .evaluator_id = "disc-difference-test",
      .artifact_id = "none",
      .sink = collector,
  };
}

SearchResult run_fixed(board_core::Position position, Depth depth, SearchOptions options) {
  SearchSession session;
  DiscDifferenceEvaluator evaluator;
  return search_fixed_depth(session, position, evaluator, depth, options);
}

void require_same_official_result(const SearchResult& actual, const SearchResult& expected) {
  REQUIRE(actual.best_move == expected.best_move);
  REQUIRE(actual.score == expected.score);
  REQUIRE(actual.score_kind == expected.score_kind);
  REQUIRE(actual.bound == expected.bound);
  REQUIRE(actual.completed_depth == expected.completed_depth);
  REQUIRE(actual.nodes == expected.nodes);
  REQUIRE(actual.stats == expected.stats);
  REQUIRE(actual.pv == expected.pv);
  REQUIRE(actual.root_moves == expected.root_moves);
  REQUIRE(actual.exact == expected.exact);
  REQUIRE(actual.stopped == expected.stopped);
}

TEST_CASE("disabled shadow calibration has bit-exact official parity",
          "[search][shadow_calibration]") {
  Collector collector;
  SearchOptions disabled{};
  disabled.midgame.use_pvs = true;
  disabled.selective = enabled_shadow(&collector);
  disabled.selective.enable_shadow_calibration = false;

  SearchOptions baseline = disabled;
  baseline.selective = {};
  const SearchResult actual = run_fixed(board_core::initial_position(), Depth{5}, disabled);
  const SearchResult expected = run_fixed(board_core::initial_position(), Depth{5}, baseline);

  require_same_official_result(actual, expected);
  REQUIRE(actual.shadow_calibration == ShadowCalibrationStats{});
  REQUIRE(collector.samples.empty());
}

TEST_CASE("shadow mode preserves official result nodes and PV", "[search][shadow_calibration]") {
  Collector collector;
  SearchOptions shadow{};
  shadow.midgame.use_pvs = true;
  shadow.midgame.use_iid = true;
  shadow.ordering.use_history = true;
  shadow.ordering.use_killers = true;
  shadow.selective = enabled_shadow(&collector);
  shadow.selective.max_samples_per_search = 12;

  SearchOptions baseline = shadow;
  baseline.selective = {};
  const SearchResult actual = run_fixed(board_core::initial_position(), Depth{5}, shadow);
  const SearchResult expected = run_fixed(board_core::initial_position(), Depth{5}, baseline);

  require_same_official_result(actual, expected);
  REQUIRE(actual.shadow_calibration.shadow_samples == collector.samples.size());
  REQUIRE(actual.shadow_calibration.shadow_samples == 12);
  REQUIRE(actual.shadow_calibration.shadow_shallow_nodes > 0);
  REQUIRE(actual.shadow_calibration.shadow_deep_verification_nodes > 0);
  REQUIRE(actual.stats.selective_cuts == 0);
}

TEST_CASE("shadow nodes do not consume the official iterative node limit",
          "[search][shadow_calibration]") {
  Collector collector;
  SearchOptions shadow{};
  shadow.midgame.use_pvs = true;
  shadow.selective = enabled_shadow(&collector);
  shadow.selective.max_samples_per_search = 8;
  SearchOptions baseline = shadow;
  baseline.selective = {};
  const SearchLimits limits{.max_depth = Depth{7}, .max_nodes = 1'000};
  SearchSession shadow_session;
  SearchSession baseline_session;
  DiscDifferenceEvaluator shadow_evaluator;
  DiscDifferenceEvaluator baseline_evaluator;

  const SearchResult actual = search_iterative(shadow_session, board_core::initial_position(),
                                               shadow_evaluator, limits, shadow);
  const SearchResult expected = search_iterative(baseline_session, board_core::initial_position(),
                                                 baseline_evaluator, limits, baseline);

  require_same_official_result(actual, expected);
  REQUIRE(actual.shadow_calibration.shadow_shallow_nodes > 0);
  REQUIRE(actual.shadow_calibration.shadow_samples == collector.samples.size());
}

TEST_CASE("shadow sampling is deterministic and metadata is complete",
          "[search][shadow_calibration]") {
  Collector first;
  Collector second;
  SearchOptions first_options{};
  first_options.midgame.use_pvs = true;
  first_options.selective = enabled_shadow(&first);
  first_options.selective.sample_rate = 500'000;
  SearchOptions second_options = first_options;
  second_options.selective.sink = &second;

  const SearchResult first_result =
      run_fixed(board_core::initial_position(), Depth{6}, first_options);
  const SearchResult second_result =
      run_fixed(board_core::initial_position(), Depth{6}, second_options);

  require_same_official_result(first_result, second_result);
  REQUIRE_FALSE(first.samples.empty());
  REQUIRE(first.samples == second.samples);
  for (const ShadowCalibrationSample& sample : first.samples) {
    REQUIRE(sample.schema_version == kShadowCalibrationSchemaVersion);
    REQUIRE(sample.repo_sha == "0123456789abcdef");
    REQUIRE(sample.search_config_id == "test-search-v1");
    REQUIRE(sample.evaluator_id == "disc-difference-test");
    REQUIRE(sample.artifact_id == "none");
    REQUIRE(sample.collection_config_id.size() == 16);
    REQUIRE(sample.canonical_position_hash != 0);
    REQUIRE(sample.occupied_count + sample.empties == board_core::kSquareCount);
    const int normalized_count =
        sample.occupied_count < 4 ? 0 : static_cast<int>(sample.occupied_count) - 4;
    REQUIRE(sample.phase == std::min(12, (normalized_count * 13) / 60));
    REQUIRE(sample.deep_depth >= 2);
    REQUIRE(sample.shallow_depth == sample.deep_depth - 1);
    REQUIRE(sample.collection_pair_count == kShadowDepthPairs.size());
    REQUIRE(sample.collection_pair_index < sample.collection_pair_count);
    REQUIRE(sample.same_deep_pair_index < sample.same_deep_pair_count);
    REQUIRE(sample.search_mode == SearchMode::move);
    REQUIRE_FALSE(sample.exact_handoff_enabled);
    REQUIRE(sample.exact_handoff_threshold == 0);
    REQUIRE(sample.exact_handoff_distance == 0);
    REQUIRE(sample.official_alpha < sample.official_beta);
    REQUIRE(sample.sampling_seed == 42);
    REQUIRE(sample.search_identity.size() == 16);
    REQUIRE(sample.pv_node == (sample.search_role == ShadowSearchRole::pv));
    const ShadowWindowResult expected_official_result =
        sample.official_deep_score <= sample.official_alpha
            ? ShadowWindowResult::fail_low
            : (sample.official_deep_score >= sample.official_beta ? ShadowWindowResult::fail_high
                                                                  : ShadowWindowResult::exact);
    REQUIRE(sample.actual_official_deep_result == expected_official_result);
    REQUIRE(sample.official_deep_bound == internal::classify_bound(sample.official_deep_score,
                                                                   sample.official_alpha,
                                                                   sample.official_beta));
    REQUIRE(sample.shallow_verification_bound ==
            internal::classify_bound(sample.shallow_verification_score, kScoreLoss, kScoreWin));
    REQUIRE(sample.deep_verification_bound ==
            internal::classify_bound(sample.deep_verification_score, kScoreLoss, kScoreWin));
    REQUIRE((sample.node_type == ShadowNodeType::pv || sample.node_type == ShadowNodeType::cut ||
             sample.node_type == ShadowNodeType::all));
    REQUIRE(sample.cut_node == (sample.node_type == ShadowNodeType::cut));
    REQUIRE(sample.all_node == (sample.node_type == ShadowNodeType::all));
  }
}

TEST_CASE("non-PV null-window samples have exact full-window verification pairs",
          "[search][shadow_calibration]") {
  Collector collector;
  SearchOptions options{};
  options.midgame.use_pvs = true;
  options.selective = enabled_shadow(&collector);
  options.selective.include_pv_nodes = false;
  options.selective.max_samples_per_search = 4;

  const SearchResult result = run_fixed(board_core::initial_position(), Depth{5}, options);

  REQUIRE_FALSE(collector.samples.empty());
  REQUIRE(result.shadow_calibration.shadow_samples == collector.samples.size());
  REQUIRE(result.shadow_calibration.shadow_deep_verification_nodes > 0);
  for (const ShadowCalibrationSample& sample : collector.samples) {
    REQUIRE_FALSE(sample.pv_node);
    REQUIRE(sample.search_role == ShadowSearchRole::non_pv_scout);
    REQUIRE(sample.official_beta == sample.official_alpha + 1);
    REQUIRE(sample.official_deep_bound != BoundType::exact);
    REQUIRE(sample.shallow_verification_bound == BoundType::exact);
    REQUIRE(sample.deep_verification_bound == BoundType::exact);
    REQUIRE((sample.node_type == ShadowNodeType::cut || sample.node_type == ShadowNodeType::all));
  }
}

TEST_CASE("collection policy changes collection and search identities",
          "[search][shadow_calibration]") {
  Collector collector;
  const SelectiveSearchOptionsV1 baseline_options = enabled_shadow(&collector);
  const auto baseline =
      internal::make_shadow_calibration_run(board_core::initial_position(), baseline_options);
  REQUIRE(baseline.has_value());

  std::vector<SelectiveSearchOptionsV1> variants;
  variants.push_back(baseline_options);
  variants.back().sample_rate -= 1;
  variants.push_back(baseline_options);
  variants.back().max_samples_per_search += 1;
  variants.push_back(baseline_options);
  variants.back().ordered_depth_pairs = variants.back().ordered_depth_pairs.first(3);
  variants.push_back(baseline_options);
  variants.back().include_pv_nodes = !variants.back().include_pv_nodes;
  variants.push_back(baseline_options);
  variants.back().include_pass_nodes = !variants.back().include_pass_nodes;
  variants.push_back(baseline_options);
  variants.back().include_near_exact_nodes = !variants.back().include_near_exact_nodes;

  for (const SelectiveSearchOptionsV1& variant : variants) {
    const auto run = internal::make_shadow_calibration_run(board_core::initial_position(), variant);
    REQUIRE(run.has_value());
    REQUIRE(run->collection_config_id != baseline->collection_config_id);
    REQUIRE(run->search_identity != baseline->search_identity);
  }
}

TEST_CASE("shadow sampling enforces its per-search cap", "[search][shadow_calibration]") {
  Collector collector;
  SearchOptions options{};
  options.selective = enabled_shadow(&collector);
  options.selective.max_samples_per_search = 3;

  const SearchResult result = run_fixed(board_core::initial_position(), Depth{5}, options);

  REQUIRE(collector.samples.size() == 3);
  REQUIRE(result.shadow_calibration.shadow_samples == 3);
  REQUIRE(result.shadow_calibration.shadow_candidates >= 3);
}

TEST_CASE("one sampled node records ordered shallow pairs with one deep verification",
          "[search][shadow_calibration][multi_probcut]") {
  constexpr std::array pairs{
      ShadowCalibrationDepthPairV1{.deep_depth = 3, .shallow_depth = 1},
      ShadowCalibrationDepthPairV1{.deep_depth = 3, .shallow_depth = 2},
  };
  Collector collector;
  SearchOptions options{};
  options.midgame.use_pvs = true;
  options.selective = enabled_shadow(&collector);
  options.selective.ordered_depth_pairs = pairs;
  options.selective.include_pv_nodes = true;
  options.selective.max_samples_per_search = 2;

  const SearchResult result = run_fixed(board_core::initial_position(), Depth{4}, options);

  REQUIRE(result.shadow_calibration.shadow_shallow_verification_searches == 2);
  REQUIRE(result.shadow_calibration.shadow_deep_verification_searches == 1);
  REQUIRE(collector.samples.size() == 2);
  REQUIRE(result.shadow_calibration.shadow_samples == 2);
  REQUIRE(collector.samples[0].canonical_position_hash ==
          collector.samples[1].canonical_position_hash);
  REQUIRE(collector.samples[0].deep_depth == 3);
  REQUIRE(collector.samples[1].deep_depth == 3);
  REQUIRE(collector.samples[0].shallow_depth == 1);
  REQUIRE(collector.samples[1].shallow_depth == 2);
  REQUIRE(collector.samples[0].same_deep_pair_index == 0);
  REQUIRE(collector.samples[1].same_deep_pair_index == 1);
  REQUIRE(collector.samples[0].deep_verification_score ==
          collector.samples[1].deep_verification_score);
}

TEST_CASE("PV nodes are excluded or included by policy", "[search][shadow_calibration]") {
  Collector excluded;
  SearchOptions excluded_options{};
  excluded_options.selective = enabled_shadow(&excluded);
  excluded_options.selective.include_pv_nodes = false;
  const SearchResult excluded_result =
      run_fixed(board_core::initial_position(), Depth{4}, excluded_options);

  Collector included;
  SearchOptions included_options = excluded_options;
  included_options.selective.include_pv_nodes = true;
  included_options.selective.sink = &included;
  const SearchResult included_result =
      run_fixed(board_core::initial_position(), Depth{4}, included_options);

  REQUIRE(excluded.samples.empty());
  REQUIRE(excluded_result.shadow_calibration.shadow_samples == 0);
  REQUIRE_FALSE(included.samples.empty());
  REQUIRE(included_result.shadow_calibration.shadow_samples == included.samples.size());
  for (const ShadowCalibrationSample& sample : included.samples) {
    REQUIRE(sample.pv_node);
    REQUIRE(sample.search_role == ShadowSearchRole::pv);
    REQUIRE(sample.node_type == ShadowNodeType::pv);
  }
}

TEST_CASE("ProbCut shallow and MPC verification are mutually isolated",
          "[search][shadow_calibration][probcut]") {
  const auto entries = probcut_entries(Depth{3}, Depth{1});
  const ProbCutCalibrationProfileV1 profile = probcut_profile(entries);

  Collector baseline_collector;
  SearchOptions baseline{};
  baseline.midgame.use_pvs = true;
  baseline.selective = enabled_shadow(&baseline_collector);
  baseline.selective.max_samples_per_search = 32;

  Collector combined_collector;
  SearchOptions combined = baseline;
  combined.selective.sink = &combined_collector;
  combined.probcut_options = probcut_options(&profile, true);

  const SearchResult expected = run_fixed(board_core::initial_position(), Depth{5}, baseline);
  const SearchResult actual = run_fixed(board_core::initial_position(), Depth{5}, combined);

  REQUIRE(actual.best_move == expected.best_move);
  REQUIRE(actual.score == expected.score);
  REQUIRE(actual.bound == expected.bound);
  REQUIRE(actual.pv == expected.pv);
  REQUIRE(actual.root_moves.size() == expected.root_moves.size());
  REQUIRE(actual.stats.probcut_attempts > 0);
  REQUIRE(actual.stats.probcut_successes > 0);
  REQUIRE(actual.stats.probcut_beta_cutoffs == 0);
  REQUIRE(actual.shadow_calibration.shadow_candidates ==
          expected.shadow_calibration.shadow_candidates);
  REQUIRE(combined_collector.samples == baseline_collector.samples);
  REQUIRE(actual.shadow_calibration.shadow_verification_probcut_attempts == 0);
  REQUIRE(actual.shadow_calibration.shadow_verification_probcut_beta_cutoffs == 0);
}

TEST_CASE("ProbCut shallow search never records MPC shadow samples",
          "[search][shadow_calibration][probcut]") {
  const std::array entries{probcut_entries(Depth{4}, Depth{2})[0]};
  const ProbCutCalibrationProfileV1 profile = probcut_profile(entries);
  Collector collector;
  SelectiveSearchOptionsV1 selective = enabled_shadow(&collector);
  std::optional<internal::ShadowCalibrationRun> shadow_run =
      internal::make_shadow_calibration_run(board_core::initial_position(), selective);
  REQUIRE(shadow_run.has_value());

  DiscDifferenceEvaluator evaluator;
  SearchOptions options{};
  options.midgame.use_pvs = true;
  options.selective = selective;
  options.probcut_options = probcut_options(&profile, false);
  options.probcut_options.minimum_depth = 4;
  internal::SearchContext context{
      .position_state = internal::make_search_position(board_core::initial_position()),
      .evaluator = evaluator,
      .options = internal::normalize_search_options(options),
      .shadow_calibration = &*shadow_run,
  };

  const internal::SearchNodeResult result =
      internal::null_window_search(&context, Score{0}, Depth{4}, Ply{1});
  REQUIRE(result.is_complete());
  REQUIRE(result.is_selective());
  REQUIRE(context.stats.probcut_beta_cutoffs == 1);
  REQUIRE(shadow_run->stats.shadow_candidates == 0);
  REQUIRE(collector.samples.empty());
}

std::pair<ShadowCalibrationStats, std::vector<ShadowCalibrationSample>>
run_pass_node_shadow(bool include_pass_nodes) {
  constexpr board_core::Position pass_position{
      .player = board_core::bit(board_core::square_from_file_rank(1, 0)),
      .opponent = board_core::bit(board_core::square_from_file_rank(0, 0)),
      .side_to_move = board_core::Color::black,
  };
  Collector collector;
  SelectiveSearchOptionsV1 selective = enabled_shadow(&collector);
  selective.include_pass_nodes = include_pass_nodes;
  std::optional<internal::ShadowCalibrationRun> run =
      internal::make_shadow_calibration_run(pass_position, selective);
  REQUIRE(run.has_value());
  DiscDifferenceEvaluator evaluator;
  SearchOptions options{};
  options.selective = selective;
  internal::SearchContext context{
      .position_state = internal::make_search_position(pass_position),
      .evaluator = evaluator,
      .limits = SearchLimits{.max_depth = Depth{3}},
      .options = internal::normalize_search_options(options),
      .shadow_calibration = &*run,
  };
  const internal::SearchNodeResult result =
      internal::alphabeta(&context, kScoreLoss, kScoreWin, Depth{3}, Ply{0});
  REQUIRE(result.is_complete());
  return {run->stats, std::move(collector.samples)};
}

TEST_CASE("pass nodes are excluded or included by policy", "[search][shadow_calibration]") {
  const auto [excluded_stats, excluded] = run_pass_node_shadow(false);
  const auto [included_stats, included] = run_pass_node_shadow(true);

  for (const ShadowCalibrationSample& sample : excluded) {
    REQUIRE_FALSE(sample.pass_state);
  }
  REQUIRE_FALSE(included.empty());
  REQUIRE(included_stats.shadow_samples > excluded_stats.shadow_samples);
  bool saw_pass = false;
  for (const ShadowCalibrationSample& sample : included) {
    saw_pass = saw_pass || sample.pass_state;
    REQUIRE_FALSE(sample.terminal_state);
  }
  REQUIRE(saw_pass);
}

TEST_CASE("near exact nodes are excluded unless explicitly included",
          "[search][shadow_calibration]") {
  const board_core::Position position = test_support::generated_endgame_position(12);
  Collector excluded;
  SearchOptions excluded_options{};
  excluded_options.endgame.exact_endgame = true;
  excluded_options.endgame.endgame_exact_empties = 12;
  excluded_options.selective = enabled_shadow(&excluded);
  excluded_options.selective.include_near_exact_nodes = false;
  const SearchResult excluded_result = run_fixed(position, Depth{4}, excluded_options);

  Collector included;
  SearchOptions included_options = excluded_options;
  included_options.selective.include_near_exact_nodes = true;
  included_options.selective.sink = &included;
  const SearchResult included_result = run_fixed(position, Depth{4}, included_options);

  require_same_official_result(included_result, excluded_result);
  REQUIRE(excluded.samples.empty());
  REQUIRE_FALSE(included.samples.empty());
  for (const ShadowCalibrationSample& sample : included.samples) {
    REQUIRE(sample.exact_handoff_eligible);
    REQUIRE(sample.exact_handoff_enabled);
    REQUIRE(sample.exact_handoff_threshold == 12);
    REQUIRE(sample.exact_handoff_distance == 0);
  }
}

} // namespace
} // namespace vibe_othello::search
