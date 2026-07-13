#include "../../src/search/search_internal.h"
#include "search/endgame_positions.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/search/search.h"

#include <array>
#include <atomic>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

namespace vibe_othello::search::internal {
namespace {

class DiscDifferenceEvaluator final : public Evaluator {
public:
  Score evaluate(const board_core::Position& position) const noexcept override {
    return static_cast<Score>(std::popcount(position.player)) -
           static_cast<Score>(std::popcount(position.opponent));
  }
};

class StopRequestingEvaluator final : public Evaluator {
public:
  explicit StopRequestingEvaluator(std::atomic_bool* stop) noexcept : stop_(stop) {}

  Score evaluate(const board_core::Position& position) const noexcept override {
    stop_->store(true, std::memory_order_release);
    return static_cast<Score>(std::popcount(position.player)) -
           static_cast<Score>(std::popcount(position.opponent));
  }

private:
  std::atomic_bool* stop_;
};

class SlowEvaluator final : public Evaluator {
public:
  Score evaluate(const board_core::Position& position) const noexcept override {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
    return static_cast<Score>(std::popcount(position.player)) -
           static_cast<Score>(std::popcount(position.opponent));
  }
};

constexpr std::string_view kTestChecksum =
    "0000000000000000000000000000000000000000000000000000000000000000";

ProbCutCalibrationEntryV1 entry(std::uint8_t phase, Depth deep_depth, Depth shallow_depth,
                                double intercept = 100.0, double sigma = 1.0) {
  return ProbCutCalibrationEntryV1{
      .phase = phase,
      .deep_depth = deep_depth,
      .shallow_depth = shallow_depth,
      .regression_slope = 1.0,
      .intercept = intercept,
      .residual_sigma = sigma,
      .confidence_multiplier = 1.0,
      .minimum_shallow_score = -200,
      .maximum_shallow_score = 200,
      .minimum_beta = -200,
      .maximum_beta = 200,
  };
}

template <std::size_t Size>
ProbCutCalibrationProfileV1 profile_for(const std::array<ProbCutCalibrationEntryV1, Size>& entries,
                                        std::string_view id = "synthetic-probcut-v1") {
  return ProbCutCalibrationProfileV1{
      .profile_id = id,
      .source_calibration_report_checksum_sha256 = kTestChecksum,
      .evaluator_family = "synthetic-disc-difference",
      .artifact_family = "none",
      .node_class = ProbCutNodeClassV1::non_pv_scout_beta_only,
      .entries = entries,
  };
}

SearchOptions options_for(const ProbCutCalibrationProfileV1* profile, Depth minimum_depth,
                          Depth reduction, bool shadow_verify = false) {
  SearchOptions options{};
  options.probcut_options = ProbCutOptionsV1{
      .use_probcut = true,
      .minimum_depth = minimum_depth,
      .shallow_depth_reduction = reduction,
      .confidence_multiplier = 1.0,
      .minimum_margin = 0,
      .maximum_margin = 10,
      .non_pv_only = true,
      .beta_only = true,
      .disable_near_exact = true,
      .shadow_verify = shadow_verify,
      .calibration_profile_id = profile->profile_id,
      .calibration_profile = profile,
  };
  return options;
}

struct DirectRun {
  SearchNodeResult result;
  SearchStats stats;
};

DirectRun run_null_window(board_core::Position position, Score beta, Depth depth,
                          SearchOptions options, SearchLimits limits = {}) {
  DiscDifferenceEvaluator evaluator;
  SearchLimitState limit_state = initialize_limit_state(limits);
  SearchContext context{
      .position_state = make_search_position(position),
      .evaluator = evaluator,
      .limits = limits,
      .options = normalize_search_options(options),
      .limit_state = &limit_state,
  };
  SearchNodeResult result = null_window_search(&context, beta, depth, Ply{0});
  return DirectRun{
      .result = result,
      .stats = context.stats,
  };
}

void require_same_official_result(const SearchResult& actual, const SearchResult& expected) {
  REQUIRE(actual.best_move == expected.best_move);
  REQUIRE(actual.score == expected.score);
  REQUIRE(actual.score_kind == expected.score_kind);
  REQUIRE(actual.bound == expected.bound);
  REQUIRE(actual.completed_depth == expected.completed_depth);
  REQUIRE(actual.nodes == expected.nodes);
  REQUIRE(actual.stats == expected.stats);
  REQUIRE(actual.shadow_calibration == expected.shadow_calibration);
  REQUIRE(actual.pv == expected.pv);
  REQUIRE(actual.root_moves == expected.root_moves);
  REQUIRE(actual.exact == expected.exact);
  REQUIRE(actual.stopped == expected.stopped);
}

std::array<ProbCutCalibrationEntryV1, 13> all_phase_entries(Depth deep_depth, Depth shallow_depth,
                                                            double intercept = 100.0) {
  std::array<ProbCutCalibrationEntryV1, 13> entries{};
  for (std::uint8_t phase = 0; phase < entries.size(); ++phase) {
    entries[phase] = entry(phase, deep_depth, shallow_depth, intercept);
  }
  return entries;
}

TEST_CASE("disabled ProbCut has bit-exact parity", "[search][probcut]") {
  const std::array entries{entry(0, Depth{3}, Depth{1})};
  const ProbCutCalibrationProfileV1 profile = profile_for(entries);
  SearchOptions disabled = options_for(&profile, Depth{3}, Depth{2});
  disabled.midgame.use_pvs = true;
  disabled.probcut_options.use_probcut = false;
  SearchOptions baseline = disabled;
  baseline.probcut_options = {};
  DiscDifferenceEvaluator actual_evaluator;
  DiscDifferenceEvaluator expected_evaluator;

  SearchSession actual_session;
  SearchSession expected_session;
  const SearchResult actual = search_fixed_depth(actual_session, board_core::initial_position(),
                                                 actual_evaluator, Depth{5}, disabled);
  const SearchResult expected = search_fixed_depth(expected_session, board_core::initial_position(),
                                                   expected_evaluator, Depth{5}, baseline);

  require_same_official_result(actual, expected);
}

TEST_CASE("ProbCut is disabled at PV nodes and by the legacy kernel", "[search][probcut]") {
  const std::array entries{entry(0, Depth{4}, Depth{2})};
  const ProbCutCalibrationProfileV1 profile = profile_for(entries);
  SearchOptions options = options_for(&profile, Depth{4}, Depth{2});
  DiscDifferenceEvaluator evaluator;
  SearchContext context{
      .position_state = make_search_position(board_core::initial_position()),
      .evaluator = evaluator,
      .options = normalize_search_options(options),
  };

  const SearchNodeResult pv_result = alphabeta(&context, kScoreLoss, kScoreWin, Depth{4}, Ply{0});
  REQUIRE(pv_result.is_complete());
  REQUIRE(context.stats.probcut_attempts == 0);

  options.experimental.use_legacy_search_kernel = true;
  REQUIRE_FALSE(normalize_search_options(options).probcut.use_probcut);
}

TEST_CASE("ProbCut rejects pass and near-exact nodes", "[search][probcut]") {
  constexpr board_core::Position pass_position{
      .player = board_core::bit(board_core::square_from_file_rank(1, 0)),
      .opponent = board_core::bit(board_core::square_from_file_rank(0, 0)),
      .side_to_move = board_core::Color::black,
  };
  const auto entries = all_phase_entries(Depth{4}, Depth{2});
  const ProbCutCalibrationProfileV1 profile = profile_for(entries);
  const SearchOptions options = options_for(&profile, Depth{4}, Depth{2});

  const DirectRun pass = run_null_window(pass_position, Score{0}, Depth{4}, options);
  REQUIRE(pass.result.is_complete());
  REQUIRE(pass.stats.probcut_attempts == 0);
  REQUIRE(pass.stats.probcut_rejected_pass == 1);

  SearchOptions exact = options;
  exact.endgame.exact_endgame = true;
  exact.endgame.endgame_exact_empties = 12;
  const DirectRun near_exact =
      run_null_window(test_support::generated_endgame_position(12), Score{0}, Depth{4}, exact);
  REQUIRE(near_exact.result.is_complete());
  REQUIRE(near_exact.stats.probcut_attempts == 0);
  REQUIRE(near_exact.stats.probcut_rejected_near_exact == 1);
}

TEST_CASE("ProbCut never runs at a terminal exact-disc-difference node", "[search][probcut]") {
  constexpr board_core::Bitboard player = (board_core::Bitboard{1} << 40) - 1;
  constexpr board_core::Position terminal{
      .player = player,
      .opponent = ~player,
      .side_to_move = board_core::Color::black,
  };
  const auto entries = all_phase_entries(Depth{4}, Depth{2});
  const ProbCutCalibrationProfileV1 profile = profile_for(entries);

  const DirectRun result =
      run_null_window(terminal, Score{0}, Depth{4}, options_for(&profile, Depth{4}, Depth{2}));
  REQUIRE(result.result.is_complete());
  REQUIRE(result.result.value().score == 16);
  REQUIRE(result.stats.terminal_nodes == 1);
  REQUIRE(result.stats.probcut_attempts == 0);
  REQUIRE(result.stats.probcut_beta_cutoffs == 0);
}

TEST_CASE("ProbCut does not extrapolate unsupported phase or depth pairs", "[search][probcut]") {
  const std::array wrong_phase_entries{entry(1, Depth{4}, Depth{2})};
  const ProbCutCalibrationProfileV1 wrong_phase_profile = profile_for(wrong_phase_entries);
  const DirectRun wrong_phase =
      run_null_window(board_core::initial_position(), Score{0}, Depth{4},
                      options_for(&wrong_phase_profile, Depth{4}, Depth{2}));
  REQUIRE(wrong_phase.stats.probcut_attempts == 0);
  REQUIRE(wrong_phase.stats.probcut_rejected_by_phase == 1);

  const std::array wrong_depth_entries{entry(0, Depth{5}, Depth{3})};
  const ProbCutCalibrationProfileV1 wrong_depth_profile = profile_for(wrong_depth_entries);
  const DirectRun wrong_depth =
      run_null_window(board_core::initial_position(), Score{0}, Depth{4},
                      options_for(&wrong_depth_profile, Depth{4}, Depth{2}));
  REQUIRE(wrong_depth.stats.probcut_attempts == 0);
  REQUIRE(wrong_depth.stats.probcut_rejected_by_depth == 1);
}

TEST_CASE("unsupported ProbCut calibration preserves the root result", "[search][probcut][root]") {
  const std::array entries{entry(12, Depth{3}, Depth{1})};
  const ProbCutCalibrationProfileV1 profile = profile_for(entries);
  SearchOptions unsupported = options_for(&profile, Depth{3}, Depth{2});
  unsupported.midgame.use_pvs = true;
  SearchOptions baseline = unsupported;
  baseline.probcut_options = {};
  DiscDifferenceEvaluator actual_evaluator;
  DiscDifferenceEvaluator expected_evaluator;
  SearchSession actual_session;
  SearchSession expected_session;

  const SearchResult actual = search_fixed_depth(actual_session, board_core::initial_position(),
                                                 actual_evaluator, Depth{4}, unsupported);
  const SearchResult expected = search_fixed_depth(expected_session, board_core::initial_position(),
                                                   expected_evaluator, Depth{4}, baseline);
  REQUIRE(actual.stats.probcut_attempts == 0);
  REQUIRE(actual.stats.probcut_rejected_by_phase > 0);
  REQUIRE(actual.best_move == expected.best_move);
  REQUIRE(actual.score == expected.score);
  REQUIRE(actual.bound == expected.bound);
  REQUIRE(actual.completed_depth == expected.completed_depth);
  REQUIRE(actual.nodes == expected.nodes);
  REQUIRE(actual.pv == expected.pv);
  REQUIRE(actual.root_moves == expected.root_moves);
}

TEST_CASE("ProbCut rejects insufficient confidence and sentinel-adjacent windows",
          "[search][probcut]") {
  const std::array uncertain_entries{entry(0, Depth{4}, Depth{2}, 100.0, 100.0)};
  const ProbCutCalibrationProfileV1 uncertain_profile = profile_for(uncertain_entries);
  const DirectRun uncertain = run_null_window(board_core::initial_position(), Score{0}, Depth{4},
                                              options_for(&uncertain_profile, Depth{4}, Depth{2}));
  REQUIRE(uncertain.stats.probcut_attempts == 1);
  REQUIRE(uncertain.stats.probcut_successes == 0);
  REQUIRE(uncertain.stats.probcut_rejected_confidence == 1);

  const std::array entries{entry(0, Depth{4}, Depth{2})};
  const ProbCutCalibrationProfileV1 profile = profile_for(entries);
  const DirectRun sentinel = run_null_window(board_core::initial_position(), kScoreWin - 1,
                                             Depth{4}, options_for(&profile, Depth{4}, Depth{2}));
  REQUIRE(sentinel.stats.probcut_attempts == 0);
  REQUIRE(sentinel.stats.probcut_rejected_confidence == 1);
}

TEST_CASE("successful beta ProbCut returns and stores only a lower bound",
          "[search][probcut][tt]") {
  const std::array entries{entry(0, Depth{4}, Depth{2})};
  const ProbCutCalibrationProfileV1 profile = profile_for(entries);
  SearchOptions options = options_for(&profile, Depth{4}, Depth{2});
  options.midgame.use_midgame_tt = true;
  DiscDifferenceEvaluator evaluator;
  TranspositionTable tt;
  SearchContext context{
      .position_state = make_search_position(board_core::initial_position()),
      .evaluator = evaluator,
      .options = normalize_search_options(options),
      .transposition_table = &tt,
  };

  const SearchNodeResult result = null_window_search(&context, Score{0}, Depth{4}, Ply{0});
  REQUIRE(result.is_complete());
  REQUIRE(result.value().score == 0);
  REQUIRE(result.value().pv.size == 0);
  REQUIRE(context.stats.probcut_attempts == 1);
  REQUIRE(context.stats.probcut_shallow_nodes > 0);
  REQUIRE(context.stats.probcut_successes == 1);
  REQUIRE(context.stats.probcut_beta_cutoffs == 1);
  REQUIRE(context.stats.selective_cuts == 1);
  REQUIRE(result.is_selective());

  SearchStats probe_stats{};
  const std::optional<TTEntry> stored =
      tt.probe(context.position_state.key, TTEntryKind::midgame, &probe_stats);
  REQUIRE(stored.has_value());
  REQUIRE(stored->depth == 4);
  REQUIRE(stored->score == 0);
  REQUIRE(stored->bound == BoundType::lower);
  REQUIRE(stored->selective);
  REQUIRE_FALSE(stored->has_best_move);

  SearchContext reused_context{
      .position_state = make_search_position(board_core::initial_position()),
      .evaluator = evaluator,
      .options = normalize_search_options(options),
      .transposition_table = &tt,
  };
  const SearchNodeResult reused = null_window_search(&reused_context, Score{0}, Depth{4}, Ply{0});
  REQUIRE(reused.is_complete());
  REQUIRE(reused_context.stats.tt_cutoffs == 1);
  REQUIRE(reused_context.stats.probcut_attempts == 0);
  REQUIRE(reused_context.stats.selective_cuts == 1);
  REQUIRE(reused.is_selective());
}

TEST_CASE("shadow verification detects a false cut without changing the deep result",
          "[search][probcut][shadow]") {
  const std::array entries{entry(0, Depth{4}, Depth{2}, 100.0)};
  const ProbCutCalibrationProfileV1 profile = profile_for(entries);
  SearchOptions shadow = options_for(&profile, Depth{4}, Depth{2}, true);
  SearchOptions baseline = shadow;
  baseline.probcut_options = {};

  const DirectRun actual =
      run_null_window(board_core::initial_position(), Score{50}, Depth{4}, shadow);
  const DirectRun expected =
      run_null_window(board_core::initial_position(), Score{50}, Depth{4}, baseline);
  REQUIRE(actual.result.is_complete());
  REQUIRE(expected.result.is_complete());
  REQUIRE(actual.result.value().score == expected.result.value().score);
  REQUIRE(actual.result.value().pv == expected.result.value().pv);
  REQUIRE(actual.stats.probcut_successes == 1);
  REQUIRE(actual.stats.probcut_beta_cutoffs == 0);
  REQUIRE(actual.stats.selective_cuts == 0);
  REQUIRE(actual.stats.probcut_shadow_candidates == 1);
  REQUIRE(actual.stats.probcut_shadow_verifications == 1);
  REQUIRE(actual.stats.probcut_shadow_false_cuts == 1);
  REQUIRE(actual.stats.nodes == expected.stats.nodes + actual.stats.probcut_shallow_nodes);
}

TEST_CASE("ProbCut shallow search propagates the official node limit", "[search][probcut]") {
  const std::array entries{entry(0, Depth{4}, Depth{2})};
  const ProbCutCalibrationProfileV1 profile = profile_for(entries);
  const DirectRun stopped = run_null_window(
      board_core::initial_position(), Score{0}, Depth{4}, options_for(&profile, Depth{4}, Depth{2}),
      SearchLimits{.max_depth = Depth{4}, .max_nodes = NodeCount{2}});

  REQUIRE(stopped.result.is_stopped());
  REQUIRE(stopped.stats.nodes == 2);
  REQUIRE(stopped.stats.probcut_attempts == 1);
  REQUIRE(stopped.stats.probcut_shallow_nodes == 1);
  REQUIRE(stopped.stats.probcut_beta_cutoffs == 0);
}

TEST_CASE("ProbCut shallow search propagates an external stop request", "[search][probcut]") {
  const std::array entries{entry(0, Depth{4}, Depth{2})};
  const ProbCutCalibrationProfileV1 profile = profile_for(entries);
  const SearchOptions options = options_for(&profile, Depth{4}, Depth{2});
  std::atomic_bool stop{false};
  StopRequestingEvaluator evaluator{&stop};
  const SearchLimits limits{
      .max_depth = Depth{4},
      .stop_requested = &stop,
  };
  SearchLimitState limit_state = initialize_limit_state(limits);
  SearchContext context{
      .position_state = make_search_position(board_core::initial_position()),
      .evaluator = evaluator,
      .limits = limits,
      .options = normalize_search_options(options),
      .limit_state = &limit_state,
  };

  const SearchNodeResult result = null_window_search(&context, Score{0}, Depth{4}, Ply{0});
  REQUIRE(result.is_stopped());
  REQUIRE(stop.load(std::memory_order_acquire));
  REQUIRE(context.stats.probcut_attempts == 1);
  REQUIRE(context.stats.probcut_shallow_nodes > 0);
  REQUIRE(context.stats.probcut_beta_cutoffs == 0);
}

TEST_CASE("ProbCut shallow search shares the official time limit", "[search][probcut]") {
  const std::array entries{entry(0, Depth{4}, Depth{2})};
  const ProbCutCalibrationProfileV1 profile = profile_for(entries);
  SlowEvaluator evaluator;
  const SearchLimits limits{
      .max_depth = Depth{4},
      .max_time = std::chrono::milliseconds{1},
  };
  SearchLimitState limit_state = initialize_limit_state(limits);
  SearchContext context{
      .position_state = make_search_position(board_core::initial_position()),
      .evaluator = evaluator,
      .limits = limits,
      .options = normalize_search_options(options_for(&profile, Depth{4}, Depth{2})),
      .limit_state = &limit_state,
  };

  const SearchNodeResult result = null_window_search(&context, Score{0}, Depth{4}, Ply{0});
  REQUIRE(result.is_stopped());
  REQUIRE(context.stats.probcut_attempts == 1);
  REQUIRE(context.stats.probcut_shallow_nodes > 0);
  REQUIRE(context.stats.probcut_beta_cutoffs == 0);
}

TEST_CASE("real ProbCut keeps root reporting heuristic bounded and PV-safe",
          "[search][probcut][root][pvs]") {
  const auto entries = all_phase_entries(Depth{3}, Depth{1});
  const ProbCutCalibrationProfileV1 profile = profile_for(entries);
  SearchOptions options = options_for(&profile, Depth{3}, Depth{2});
  options.midgame.use_pvs = true;
  options.reporting.multi_pv = 1;
  DiscDifferenceEvaluator evaluator;
  SearchSession session;

  const SearchResult result =
      search_fixed_depth(session, board_core::initial_position(), evaluator, Depth{4}, options);
  REQUIRE(result.stats.probcut_beta_cutoffs > 0);
  REQUIRE(result.score_kind == ScoreKind::heuristic);
  REQUIRE_FALSE(result.exact);
  bool saw_selective_root_move = false;
  for (const RootMoveInfo& root_move : result.root_moves) {
    if (!root_move.selective) {
      continue;
    }
    saw_selective_root_move = true;
    REQUIRE(root_move.bound == BoundType::upper);
    REQUIRE(root_move.score_kind == ScoreKind::heuristic);
    REQUIRE_FALSE(root_move.exact);
    REQUIRE(root_move.pv.size == 1);
    REQUIRE(root_move.pv.moves[0] == root_move.move);
  }
  REQUIRE(saw_selective_root_move);
}

TEST_CASE("ProbCut descendant prevents an exact TT store at its ancestor",
          "[search][probcut][tt][pvs]") {
  const auto entries = all_phase_entries(Depth{3}, Depth{1});
  const ProbCutCalibrationProfileV1 profile = profile_for(entries);
  SearchOptions options = options_for(&profile, Depth{3}, Depth{2});
  options.midgame.use_pvs = true;
  options.midgame.use_midgame_tt = true;
  DiscDifferenceEvaluator evaluator;
  TranspositionTable tt;
  SearchContext context{
      .position_state = make_search_position(board_core::initial_position()),
      .evaluator = evaluator,
      .options = normalize_search_options(options),
      .transposition_table = &tt,
  };
  const board_core::PositionHash root_key = context.position_state.key;

  const SearchNodeResult result =
      full_window_search(&context, kScoreLoss, kScoreWin, Depth{4}, Ply{0});
  REQUIRE(result.is_complete());
  REQUIRE(context.stats.probcut_beta_cutoffs > 0);
  REQUIRE(result.is_selective());
  SearchStats probe_stats{};
  const std::optional<TTEntry> root_entry = tt.probe(root_key, TTEntryKind::midgame, &probe_stats);
  REQUIRE_FALSE(root_entry.has_value());
}

TEST_CASE("selective TT suppression does not leak into an unrelated subtree",
          "[search][probcut][tt]") {
  const std::array entries{entry(0, Depth{4}, Depth{2})};
  const ProbCutCalibrationProfileV1 profile = profile_for(entries);
  SearchOptions options = options_for(&profile, Depth{4}, Depth{2});
  options.midgame.use_midgame_tt = true;
  DiscDifferenceEvaluator evaluator;
  TranspositionTable tt;
  SearchContext context{
      .position_state = make_search_position(board_core::initial_position()),
      .evaluator = evaluator,
      .options = normalize_search_options(options),
      .transposition_table = &tt,
  };

  const SearchNodeResult selective = null_window_search(&context, Score{0}, Depth{4}, Ply{0});
  REQUIRE(selective.is_selective());

  board_core::Position sibling = board_core::initial_position();
  board_core::MoveDelta delta{};
  REQUIRE(board_core::apply_move(
      &sibling, board_core::make_move(board_core::square_from_file_rank(3, 2)), &delta));
  context.position_state = make_search_position(sibling);
  context.options.probcut = {};
  context.options.probcut_profile_semantic_fingerprint = 0;
  context.stack = {};
  const board_core::PositionHash sibling_key = context.position_state.key;

  const SearchNodeResult exact =
      full_window_search(&context, kScoreLoss, kScoreWin, Depth{2}, Ply{0});
  REQUIRE(exact.is_complete());
  REQUIRE_FALSE(exact.is_selective());
  SearchStats probe_stats{};
  const std::optional<TTEntry> stored = tt.probe(sibling_key, TTEntryKind::midgame, &probe_stats);
  REQUIRE(stored.has_value());
  REQUIRE(stored->bound == BoundType::exact);
  REQUIRE_FALSE(stored->selective);
}

TEST_CASE("ProbCut shadow combines with aspiration IID and PVS and preserves the root result",
          "[search][probcut][aspiration][iid][pvs]") {
  const auto entries = all_phase_entries(Depth{3}, Depth{1});
  const ProbCutCalibrationProfileV1 profile = profile_for(entries);
  SearchOptions shadow = options_for(&profile, Depth{3}, Depth{2}, true);
  shadow.midgame.use_pvs = true;
  shadow.midgame.use_aspiration = true;
  shadow.midgame.use_iid = true;
  SearchOptions baseline = shadow;
  baseline.probcut_options = {};
  DiscDifferenceEvaluator actual_evaluator;
  DiscDifferenceEvaluator expected_evaluator;

  const SearchResult actual = search_iterative(board_core::initial_position(), actual_evaluator,
                                               SearchLimits{.max_depth = Depth{4}}, shadow);
  const SearchResult expected = search_iterative(board_core::initial_position(), expected_evaluator,
                                                 SearchLimits{.max_depth = Depth{4}}, baseline);
  REQUIRE(actual.best_move == expected.best_move);
  REQUIRE(actual.score == expected.score);
  REQUIRE(actual.bound == expected.bound);
  REQUIRE(actual.completed_depth == expected.completed_depth);
  REQUIRE(actual.pv == expected.pv);
  REQUIRE(actual.root_moves == expected.root_moves);
  REQUIRE(actual.stats.probcut_attempts > 0);
  REQUIRE(actual.stats.probcut_shadow_verifications > 0);
  REQUIRE(actual.stats.probcut_beta_cutoffs == 0);
  REQUIRE(actual.stats.iid_searches == expected.stats.iid_searches);
}

TEST_CASE("SearchSession clears TT when ProbCut profile semantics change",
          "[search][probcut][session][tt]") {
  const auto first_entries = all_phase_entries(Depth{3}, Depth{1}, 100.0);
  const auto changed_entries = all_phase_entries(Depth{3}, Depth{1}, 101.0);
  const ProbCutCalibrationProfileV1 first_profile =
      profile_for(first_entries, "synthetic-probcut-session-v1");
  const ProbCutCalibrationProfileV1 changed_profile =
      profile_for(changed_entries, "synthetic-probcut-session-v1");
  SearchOptions first = options_for(&first_profile, Depth{3}, Depth{2}, true);
  first.midgame.use_pvs = true;
  first.midgame.use_midgame_tt = true;
  first.ordering.use_tt_best_move_ordering = true;
  SearchOptions changed = options_for(&changed_profile, Depth{3}, Depth{2}, true);
  changed.midgame = first.midgame;
  changed.ordering = first.ordering;
  SearchSession session;
  DiscDifferenceEvaluator evaluator;

  (void)search_fixed_depth(session, board_core::initial_position(), evaluator, Depth{4}, first);
  const SearchResult reused =
      search_fixed_depth(session, board_core::initial_position(), evaluator, Depth{4}, first);
  REQUIRE(reused.stats.tt_generation_age_hits > 0);

  const SearchResult after_change =
      search_fixed_depth(session, board_core::initial_position(), evaluator, Depth{4}, changed);
  REQUIRE(after_change.stats.tt_generation_age_hits == 0);
}

} // namespace
} // namespace vibe_othello::search::internal
