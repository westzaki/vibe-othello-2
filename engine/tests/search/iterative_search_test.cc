#include "vibe_othello/board_core/board.h"
#include "vibe_othello/search/search.h"

#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <initializer_list>

namespace vibe_othello::search {
namespace {

constexpr board_core::Square square(int file, int rank) noexcept {
  return board_core::square_from_file_rank(file, rank);
}

class ConstantEvaluator final : public Evaluator {
public:
  explicit constexpr ConstantEvaluator(Score score) noexcept : score_(score) {}

  Score evaluate(const board_core::Position&) const noexcept override {
    ++calls;
    return score_;
  }

  mutable int calls = 0;

private:
  Score score_;
};

class DiscDifferenceEvaluator final : public Evaluator {
public:
  Score evaluate(const board_core::Position& position) const noexcept override {
    ++calls;
    return static_cast<Score>(std::popcount(position.player)) -
           static_cast<Score>(std::popcount(position.opponent));
  }

  mutable int calls = 0;
};

class CornerPreferenceEvaluator final : public Evaluator {
public:
  Score evaluate(const board_core::Position& position) const noexcept override {
    ++calls;

    Score score = 0;
    if ((board_core::black_discs(position) & board_core::bit(square(0, 0))) != 0) {
      score += 100;
    }
    if ((board_core::white_discs(position) & board_core::bit(square(0, 0))) != 0) {
      score -= 100;
    }

    return position.side_to_move == board_core::Color::black ? score : static_cast<Score>(-score);
  }

  mutable int calls = 0;
};

void require_replayable_pv(board_core::Position position, Line pv) {
  for (std::uint8_t index = 0; index < pv.size; ++index) {
    board_core::MoveDelta delta{};
    REQUIRE(board_core::apply_move(&position, pv.moves[index], &delta));
  }
}

void require_replayable_root_pvs(board_core::Position position, const SearchResult& result) {
  for (const RootMoveInfo& root_move : result.root_moves) {
    REQUIRE(root_move.pv.size > 0);
    REQUIRE(root_move.pv.moves[0] == root_move.move);
    require_replayable_pv(position, root_move.pv);
  }
}

void require_same_final_result(const SearchResult& actual, const SearchResult& expected) {
  REQUIRE(actual.best_move == expected.best_move);
  REQUIRE(actual.score == expected.score);
  REQUIRE(actual.bound == expected.bound);
  REQUIRE(actual.completed_depth == expected.completed_depth);
  REQUIRE(actual.pv == expected.pv);
  REQUIRE(actual.exact == expected.exact);
  REQUIRE(actual.stopped == expected.stopped);
}

void require_same_decision(const SearchResult& actual, const SearchResult& expected) {
  REQUIRE(actual.best_move == expected.best_move);
  REQUIRE(actual.score == expected.score);
  REQUIRE(actual.bound == expected.bound);
  REQUIRE(actual.completed_depth == expected.completed_depth);
  REQUIRE(actual.exact == expected.exact);
  REQUIRE(actual.stopped == expected.stopped);
}

void require_basic_stats_invariants(const SearchResult& result) {
  REQUIRE(result.nodes == result.stats.nodes);
  REQUIRE(result.stats.root_moves_searched >= result.root_moves.size());
  REQUIRE(result.stats.leaf_nodes <= result.stats.nodes);
  REQUIRE(result.stats.eval_calls <= result.stats.leaf_nodes);
  REQUIRE(result.stats.terminal_nodes <= result.stats.nodes);
  REQUIRE(result.stats.pass_nodes <= result.stats.nodes);
  REQUIRE(result.stats.beta_cutoffs <= result.stats.nodes);
  REQUIRE(result.stats.alpha_updates <= result.stats.nodes);
  REQUIRE(result.stats.tt_hits <= result.stats.tt_probes);
  REQUIRE(result.stats.tt_stores <= result.stats.nodes);
  REQUIRE(result.stats.tt_cutoffs <= result.stats.tt_hits);
  REQUIRE(result.stats.tt_overwrites <= result.stats.tt_stores);
  REQUIRE(result.stats.tt_collisions <= result.stats.tt_stores + result.stats.tt_rejected_stores);
  REQUIRE(result.stats.tt_rejected_stores <= result.stats.nodes);
  REQUIRE(result.stats.tt_invalid_best_move_stores <= result.stats.nodes);
  REQUIRE(result.stats.pvs_researches <= result.stats.nodes);
  REQUIRE(result.stats.aspiration_fail_lows <= result.stats.nodes);
  REQUIRE(result.stats.aspiration_fail_highs <= result.stats.nodes);
  REQUIRE(result.stats.iid_searches <= result.stats.nodes);
  REQUIRE(result.stats.endgame_nodes <= result.stats.nodes);
  REQUIRE(result.stats.selective_cuts <= result.stats.nodes);
}

void require_root_move_set_matches(const SearchResult& actual, const SearchResult& expected) {
  REQUIRE(actual.root_moves.size() == expected.root_moves.size());
  for (const RootMoveInfo& expected_root_move : expected.root_moves) {
    bool found = false;
    for (const RootMoveInfo& actual_root_move : actual.root_moves) {
      if (actual_root_move.move == expected_root_move.move) {
        REQUIRE(actual_root_move.score == expected_root_move.score);
        REQUIRE(actual_root_move.bound == expected_root_move.bound);
        REQUIRE(actual_root_move.depth == expected_root_move.depth);
        REQUIRE(actual_root_move.pv == expected_root_move.pv);
        REQUIRE(actual_root_move.exact == expected_root_move.exact);
        REQUIRE(actual_root_move.selective == expected_root_move.selective);
        found = true;
        break;
      }
    }
    REQUIRE(found);
  }
}

void require_root_move_scores_match(const SearchResult& actual, const SearchResult& expected) {
  REQUIRE(actual.root_moves.size() == expected.root_moves.size());
  for (const RootMoveInfo& expected_root_move : expected.root_moves) {
    bool found = false;
    for (const RootMoveInfo& actual_root_move : actual.root_moves) {
      if (actual_root_move.move == expected_root_move.move) {
        REQUIRE(actual_root_move.score == expected_root_move.score);
        REQUIRE(actual_root_move.bound == expected_root_move.bound);
        REQUIRE(actual_root_move.depth == expected_root_move.depth);
        REQUIRE(actual_root_move.exact == expected_root_move.exact);
        REQUIRE(actual_root_move.selective == expected_root_move.selective);
        found = true;
        break;
      }
    }
    REQUIRE(found);
  }
}

board_core::Move select_legal_move(board_core::Position position, std::size_t choice) {
  const board_core::Bitboard legal_moves = board_core::legal_moves(position);
  REQUIRE(legal_moves != 0);

  std::array<board_core::Move, board_core::kSquareCount> moves{};
  std::size_t move_count = 0;
  for (int square_index = 0; square_index < board_core::kSquareCount; ++square_index) {
    const board_core::Square move_square = board_core::square_from_index(square_index);
    if ((legal_moves & board_core::bit(move_square)) != 0) {
      moves[move_count] = board_core::make_move(move_square);
      ++move_count;
    }
  }

  REQUIRE(move_count > 0);
  return moves[choice % move_count];
}

board_core::Position position_after_fixed_choices(std::initializer_list<std::size_t> choices) {
  board_core::Position position = board_core::initial_position();

  for (const std::size_t choice : choices) {
    const board_core::Move move = select_legal_move(position, choice);
    board_core::MoveDelta delta{};
    REQUIRE(board_core::apply_move(&position, move, &delta));
  }

  return position;
}

NodeCount fixed_depth_node_sum(board_core::Position position, Depth max_depth) {
  NodeCount nodes = 0;
  for (Depth depth = 1; depth <= max_depth; ++depth) {
    DiscDifferenceEvaluator evaluator;
    nodes += search_fixed_depth(position, evaluator, depth).nodes;
  }
  return nodes;
}

SearchOptions enable_aspiration(SearchOptions options) noexcept {
  options.use_aspiration = true;
  return options;
}

void require_aspiration_matches_without(board_core::Position position, Depth depth,
                                        SearchOptions baseline_options) {
  DiscDifferenceEvaluator aspiration_evaluator;
  DiscDifferenceEvaluator baseline_evaluator;

  const SearchResult with_aspiration =
      search_iterative(position, aspiration_evaluator, SearchLimits{.max_depth = depth},
                       enable_aspiration(baseline_options));
  const SearchResult without_aspiration = search_iterative(
      position, baseline_evaluator, SearchLimits{.max_depth = depth}, baseline_options);

  require_same_decision(with_aspiration, without_aspiration);
  require_basic_stats_invariants(with_aspiration);
  require_root_move_scores_match(with_aspiration, without_aspiration);
  require_replayable_pv(position, with_aspiration.pv);
  require_replayable_root_pvs(position, with_aspiration);
}

TEST_CASE("iterative depth zero matches fixed-depth zero", "[search][iterative]") {
  ConstantEvaluator actual_evaluator{11};
  ConstantEvaluator expected_evaluator{11};

  const SearchResult actual = search_iterative(board_core::initial_position(), actual_evaluator,
                                               SearchLimits{.max_depth = Depth{0}});
  const SearchResult expected =
      search_fixed_depth(board_core::initial_position(), expected_evaluator, Depth{0});

  require_same_final_result(actual, expected);
  require_basic_stats_invariants(actual);
  REQUIRE(actual.stats == expected.stats);
  REQUIRE(actual.nodes == expected.nodes);
  REQUIRE(actual_evaluator.calls == expected_evaluator.calls);
}

TEST_CASE("iterative negative max depth clamps to depth zero", "[search][iterative]") {
  ConstantEvaluator actual_evaluator{17};
  ConstantEvaluator expected_evaluator{17};

  const SearchResult actual = search_iterative(board_core::initial_position(), actual_evaluator,
                                               SearchLimits{.max_depth = Depth{-3}});
  const SearchResult expected =
      search_fixed_depth(board_core::initial_position(), expected_evaluator, Depth{0});

  require_same_final_result(actual, expected);
  require_basic_stats_invariants(actual);
  REQUIRE(actual.stats == expected.stats);
  REQUIRE(actual.nodes == expected.nodes);
}

TEST_CASE("iterative final result matches fixed-depth search", "[search][iterative]") {
  const std::array<board_core::Position, 3> positions{
      board_core::initial_position(),
      position_after_fixed_choices({0, 1, 2, 3, 1, 0, 2, 1}),
      position_after_fixed_choices({3, 2, 1, 0, 2, 3, 0, 1, 2, 0}),
  };

  for (const board_core::Position position : positions) {
    for (Depth depth = 1; depth <= 4; ++depth) {
      DiscDifferenceEvaluator actual_evaluator;
      DiscDifferenceEvaluator expected_evaluator;

      const SearchResult actual =
          search_iterative(position, actual_evaluator, SearchLimits{.max_depth = depth});
      const SearchResult expected = search_fixed_depth(position, expected_evaluator, depth);

      require_same_final_result(actual, expected);
      require_basic_stats_invariants(actual);
      require_root_move_set_matches(actual, expected);
      require_replayable_pv(position, actual.pv);
      require_replayable_root_pvs(position, actual);
      if (depth == 1) {
        REQUIRE(actual.nodes == fixed_depth_node_sum(position, depth));
        REQUIRE(actual.stats == expected.stats);
      }
    }
  }
}

TEST_CASE("iterative aspiration uses full-window search at depth one",
          "[search][iterative][aspiration]") {
  ConstantEvaluator aspiration_evaluator{100};
  ConstantEvaluator baseline_evaluator{100};

  const SearchResult with_aspiration =
      search_iterative(board_core::initial_position(), aspiration_evaluator,
                       SearchLimits{.max_depth = Depth{1}}, SearchOptions{.use_aspiration = true});
  const SearchResult without_aspiration = search_iterative(
      board_core::initial_position(), baseline_evaluator, SearchLimits{.max_depth = Depth{1}});

  require_same_final_result(with_aspiration, without_aspiration);
  require_basic_stats_invariants(with_aspiration);
  require_root_move_set_matches(with_aspiration, without_aspiration);
  REQUIRE(with_aspiration.stats == without_aspiration.stats);
  REQUIRE(with_aspiration.nodes == without_aspiration.nodes);
}

TEST_CASE("iterative aspiration preserves deterministic search results",
          "[search][iterative][aspiration]") {
  const std::array<board_core::Position, 3> positions{
      board_core::initial_position(),
      position_after_fixed_choices({0, 1, 2, 3, 1, 0, 2, 1}),
      position_after_fixed_choices({3, 2, 1, 0, 2, 3, 0, 1, 2, 0}),
  };
  const std::array<SearchOptions, 5> option_cases{
      SearchOptions{},
      SearchOptions{.use_pvs = true},
      SearchOptions{.use_midgame_tt = true},
      SearchOptions{.use_tt_best_move_ordering = true},
      SearchOptions{.use_pvs = true, .use_midgame_tt = true, .use_tt_best_move_ordering = true},
  };

  for (const board_core::Position position : positions) {
    for (const SearchOptions baseline_options : option_cases) {
      require_aspiration_matches_without(position, Depth{5}, baseline_options);
    }
  }
}

TEST_CASE("iterative aspiration counts fail-low and fail-high searches",
          "[search][iterative][aspiration]") {
  ConstantEvaluator fail_high_evaluator{100};
  ConstantEvaluator fail_low_evaluator{-100};

  const SearchResult fail_high =
      search_iterative(board_core::initial_position(), fail_high_evaluator,
                       SearchLimits{.max_depth = Depth{2}}, SearchOptions{.use_aspiration = true});
  const SearchResult fail_low =
      search_iterative(board_core::initial_position(), fail_low_evaluator,
                       SearchLimits{.max_depth = Depth{2}}, SearchOptions{.use_aspiration = true});

  require_basic_stats_invariants(fail_high);
  require_basic_stats_invariants(fail_low);
  REQUIRE(fail_high.stats.aspiration_fail_highs > 0);
  REQUIRE(fail_low.stats.aspiration_fail_lows > 0);
}

TEST_CASE("iterative searches previous best root move first", "[search][iterative]") {
  const board_core::Position position =
      position_after_fixed_choices({0, 1, 2, 3, 1, 0, 2, 1, 0, 2});
  CornerPreferenceEvaluator evaluator;

  const SearchResult depth_one = search_fixed_depth(position, evaluator, Depth{1});
  REQUIRE(depth_one.best_move.has_value());

  const SearchResult actual =
      search_iterative(position, evaluator, SearchLimits{.max_depth = Depth{2}});
  const SearchResult expected = search_fixed_depth(position, evaluator, Depth{2});

  require_same_final_result(actual, expected);
  require_basic_stats_invariants(actual);
  require_root_move_set_matches(actual, expected);
  REQUIRE_FALSE(actual.root_moves.empty());
  REQUIRE(actual.root_moves[0].move == *depth_one.best_move);
}

TEST_CASE("iterative TT best-move ordering preserves fixed-depth result", "[search][iterative]") {
  const board_core::Position position =
      position_after_fixed_choices({0, 1, 2, 3, 1, 0, 2, 1, 0, 2});
  DiscDifferenceEvaluator tt_evaluator;
  DiscDifferenceEvaluator no_tt_evaluator;

  const SearchResult actual =
      search_iterative(position, tt_evaluator, SearchLimits{.max_depth = Depth{4}},
                       SearchOptions{.use_tt_best_move_ordering = true});
  const SearchResult expected =
      search_iterative(position, no_tt_evaluator, SearchLimits{.max_depth = Depth{4}});

  require_same_decision(actual, expected);
  require_basic_stats_invariants(actual);
  require_root_move_scores_match(actual, expected);
  require_replayable_pv(position, actual.pv);
  require_replayable_root_pvs(position, actual);
  REQUIRE(expected.stats.tt_probes == 0);
  REQUIRE(expected.stats.tt_hits == 0);
  REQUIRE(expected.stats.tt_stores == 0);
  REQUIRE(actual.stats.tt_probes > 0);
  REQUIRE(actual.stats.tt_hits > 0);
  REQUIRE(actual.stats.tt_stores > 0);
  REQUIRE(actual.stats.tt_cutoffs == 0);
}

TEST_CASE("iterative TT ordering is correctness-neutral across depths", "[search][iterative]") {
  const std::array<board_core::Position, 3> positions{
      board_core::initial_position(),
      position_after_fixed_choices({0, 1, 2, 3, 1, 0, 2, 1}),
      position_after_fixed_choices({3, 2, 1, 0, 2, 3, 0, 1, 2, 0}),
  };

  for (const board_core::Position position : positions) {
    for (Depth depth = 1; depth <= 5; ++depth) {
      DiscDifferenceEvaluator tt_evaluator;
      DiscDifferenceEvaluator no_tt_evaluator;

      const SearchResult with_tt =
          search_iterative(position, tt_evaluator, SearchLimits{.max_depth = depth},
                           SearchOptions{.use_tt_best_move_ordering = true});
      const SearchResult without_tt =
          search_iterative(position, no_tt_evaluator, SearchLimits{.max_depth = depth});

      require_same_decision(with_tt, without_tt);
      require_basic_stats_invariants(with_tt);
      require_root_move_scores_match(with_tt, without_tt);
      REQUIRE(with_tt.stats.tt_cutoffs == 0);
    }
  }
}

TEST_CASE("iterative midgame TT cutoffs preserve search decisions", "[search][iterative]") {
  DiscDifferenceEvaluator tt_evaluator;
  DiscDifferenceEvaluator no_tt_evaluator;

  const SearchResult with_tt =
      search_iterative(board_core::initial_position(), tt_evaluator,
                       SearchLimits{.max_depth = Depth{5}}, SearchOptions{.use_midgame_tt = true});
  const SearchResult without_tt = search_iterative(board_core::initial_position(), no_tt_evaluator,
                                                   SearchLimits{.max_depth = Depth{5}});

  require_same_decision(with_tt, without_tt);
  require_basic_stats_invariants(with_tt);
  require_root_move_scores_match(with_tt, without_tt);
  require_replayable_pv(board_core::initial_position(), with_tt.pv);
  require_replayable_root_pvs(board_core::initial_position(), with_tt);
  REQUIRE(without_tt.stats.tt_probes == 0);
  REQUIRE(without_tt.stats.tt_hits == 0);
  REQUIRE(without_tt.stats.tt_stores == 0);
  REQUIRE(with_tt.stats.tt_probes > 0);
  REQUIRE(with_tt.stats.tt_hits > 0);
  REQUIRE(with_tt.stats.tt_stores > 0);
  REQUIRE(with_tt.stats.tt_cutoffs > 0);
}

TEST_CASE("iterative TT ordering and cutoff options are independent", "[search][iterative]") {
  const board_core::Position position = position_after_fixed_choices({0, 1, 2, 3, 1, 0, 2, 1});
  DiscDifferenceEvaluator baseline_evaluator;
  DiscDifferenceEvaluator ordering_evaluator;
  DiscDifferenceEvaluator cutoff_evaluator;
  DiscDifferenceEvaluator both_evaluator;

  const SearchResult baseline =
      search_iterative(position, baseline_evaluator, SearchLimits{.max_depth = Depth{5}});
  const SearchResult ordering_only =
      search_iterative(position, ordering_evaluator, SearchLimits{.max_depth = Depth{5}},
                       SearchOptions{.use_tt_best_move_ordering = true});
  const SearchResult cutoff_only =
      search_iterative(position, cutoff_evaluator, SearchLimits{.max_depth = Depth{5}},
                       SearchOptions{.use_midgame_tt = true});
  const SearchResult ordering_and_cutoff =
      search_iterative(position, both_evaluator, SearchLimits{.max_depth = Depth{5}},
                       SearchOptions{.use_midgame_tt = true, .use_tt_best_move_ordering = true});

  require_same_decision(ordering_only, baseline);
  require_same_decision(cutoff_only, baseline);
  require_same_decision(ordering_and_cutoff, baseline);
  require_basic_stats_invariants(ordering_only);
  require_basic_stats_invariants(cutoff_only);
  require_basic_stats_invariants(ordering_and_cutoff);
  require_root_move_scores_match(ordering_only, baseline);
  require_root_move_scores_match(cutoff_only, baseline);
  require_root_move_scores_match(ordering_and_cutoff, baseline);

  REQUIRE(baseline.stats.tt_probes == 0);
  REQUIRE(ordering_only.stats.tt_probes > 0);
  REQUIRE(ordering_only.stats.tt_cutoffs == 0);
  REQUIRE(cutoff_only.stats.tt_probes > 0);
  REQUIRE(cutoff_only.stats.tt_cutoffs > 0);
  REQUIRE(ordering_and_cutoff.stats.tt_probes > 0);
  REQUIRE(ordering_and_cutoff.stats.tt_cutoffs > 0);
}

TEST_CASE("iterative TT ordering handles depth zero pass and terminal roots",
          "[search][iterative]") {
  constexpr board_core::Position pass_position{
      .player = board_core::bit(square(1, 0)),
      .opponent = board_core::bit(square(0, 0)),
      .side_to_move = board_core::Color::black,
  };
  constexpr board_core::Bitboard terminal_player = (board_core::Bitboard{1} << 40) - 1;
  constexpr board_core::Position terminal{
      .player = terminal_player,
      .opponent = ~terminal_player,
      .side_to_move = board_core::Color::black,
  };

  const std::array<board_core::Position, 3> positions{
      board_core::initial_position(),
      pass_position,
      terminal,
  };

  for (const board_core::Position position : positions) {
    for (const Depth depth : {Depth{0}, Depth{3}}) {
      ConstantEvaluator tt_evaluator{5};
      ConstantEvaluator no_tt_evaluator{5};

      const SearchResult with_tt = search_iterative(
          position, tt_evaluator, SearchLimits{.max_depth = depth},
          SearchOptions{.use_midgame_tt = true, .use_tt_best_move_ordering = true});
      const SearchResult without_tt =
          search_iterative(position, no_tt_evaluator, SearchLimits{.max_depth = depth});

      require_same_final_result(with_tt, without_tt);
      require_basic_stats_invariants(with_tt);
      require_root_move_set_matches(with_tt, without_tt);
    }
  }
}

TEST_CASE("iterative disables TT best-move ordering by default", "[search][iterative]") {
  DiscDifferenceEvaluator evaluator;

  const SearchResult result = search_iterative(board_core::initial_position(), evaluator,
                                               SearchLimits{.max_depth = Depth{3}});

  require_basic_stats_invariants(result);
  REQUIRE(result.stats.tt_probes == 0);
  REQUIRE(result.stats.tt_hits == 0);
  REQUIRE(result.stats.tt_stores == 0);
}

TEST_CASE("iterative killer and history ordering preserves search decisions",
          "[search][iterative]") {
  const std::array<board_core::Position, 3> positions{
      board_core::initial_position(),
      position_after_fixed_choices({0, 1, 2, 3, 1, 0, 2, 1}),
      position_after_fixed_choices({3, 2, 1, 0, 2, 3, 0, 1, 2, 0}),
  };
  const std::array<SearchOptions, 4> option_cases{
      SearchOptions{.use_history = true},
      SearchOptions{.use_killers = true},
      SearchOptions{.use_history = true, .use_killers = true},
      SearchOptions{.use_pvs = true, .use_history = true, .use_killers = true},
  };

  for (const board_core::Position position : positions) {
    for (const SearchOptions options : option_cases) {
      DiscDifferenceEvaluator ordering_evaluator;
      DiscDifferenceEvaluator baseline_evaluator;

      const SearchResult with_ordering = search_iterative(
          position, ordering_evaluator, SearchLimits{.max_depth = Depth{5}}, options);
      const SearchResult without_ordering =
          search_iterative(position, baseline_evaluator, SearchLimits{.max_depth = Depth{5}},
                           SearchOptions{.use_pvs = options.use_pvs});

      require_same_decision(with_ordering, without_ordering);
      require_basic_stats_invariants(with_ordering);
      require_root_move_scores_match(with_ordering, without_ordering);
      require_replayable_pv(position, with_ordering.pv);
      require_replayable_root_pvs(position, with_ordering);
    }
  }
}

TEST_CASE("iterative killer and history ordering is deterministic", "[search][iterative]") {
  const board_core::Position position = position_after_fixed_choices({0, 1, 2, 3, 1, 0, 2, 1});
  DiscDifferenceEvaluator first_evaluator;
  DiscDifferenceEvaluator second_evaluator;
  const SearchOptions options{.use_pvs = true, .use_history = true, .use_killers = true};

  const SearchResult first =
      search_iterative(position, first_evaluator, SearchLimits{.max_depth = Depth{5}}, options);
  const SearchResult second =
      search_iterative(position, second_evaluator, SearchLimits{.max_depth = Depth{5}}, options);

  require_same_final_result(first, second);
  require_root_move_set_matches(first, second);
  REQUIRE(first.stats == second.stats);
}

TEST_CASE("iterative safely ignores unimplemented search options", "[search][iterative]") {
  const board_core::Position position = position_after_fixed_choices({0, 1, 2, 3, 1, 0, 2, 1});
  DiscDifferenceEvaluator actual_evaluator;
  DiscDifferenceEvaluator expected_evaluator;

  const SearchOptions options{
      .use_iid = true,
      .use_endgame_tt = true,
      .exact_endgame = true,
      .probcut = true,
      .use_pv_table = true,
      .use_parallel = true,
      .multi_pv = 3,
      .endgame_exact_empties = 8,
      .endgame_wld_empties = 10,
      .selectivity_level = 2,
  };

  const SearchResult actual =
      search_iterative(position, actual_evaluator, SearchLimits{.max_depth = Depth{3}}, options);
  const SearchResult expected =
      search_iterative(position, expected_evaluator, SearchLimits{.max_depth = Depth{3}});

  require_same_final_result(actual, expected);
  require_basic_stats_invariants(actual);
  require_root_move_set_matches(actual, expected);
  REQUIRE(actual.stats == expected.stats);
}

TEST_CASE("iterative handles root pass like fixed-depth search", "[search][iterative]") {
  constexpr board_core::Position pass_position{
      .player = board_core::bit(square(1, 0)),
      .opponent = board_core::bit(square(0, 0)),
      .side_to_move = board_core::Color::black,
  };
  ConstantEvaluator actual_evaluator{7};
  ConstantEvaluator expected_evaluator{7};

  const SearchResult actual =
      search_iterative(pass_position, actual_evaluator, SearchLimits{.max_depth = Depth{3}});
  const SearchResult expected = search_fixed_depth(pass_position, expected_evaluator, Depth{3});

  require_same_final_result(actual, expected);
  require_basic_stats_invariants(actual);
  require_root_move_set_matches(actual, expected);
  require_replayable_root_pvs(pass_position, actual);
  REQUIRE(actual.nodes >= expected.nodes);
}

TEST_CASE("iterative terminal root returns the fixed-depth exact result", "[search][iterative]") {
  constexpr board_core::Bitboard player = (board_core::Bitboard{1} << 40) - 1;
  constexpr board_core::Position terminal{
      .player = player,
      .opponent = ~player,
      .side_to_move = board_core::Color::black,
  };
  ConstantEvaluator actual_evaluator{99};
  ConstantEvaluator expected_evaluator{99};

  const SearchResult actual =
      search_iterative(terminal, actual_evaluator, SearchLimits{.max_depth = Depth{5}});
  const SearchResult expected = search_fixed_depth(terminal, expected_evaluator, Depth{5});

  require_same_final_result(actual, expected);
  require_basic_stats_invariants(actual);
  require_root_move_set_matches(actual, expected);
  REQUIRE(actual.exact);
  REQUIRE(actual.nodes >= expected.nodes);
  REQUIRE(actual_evaluator.calls == 0);
}

} // namespace
} // namespace vibe_othello::search
