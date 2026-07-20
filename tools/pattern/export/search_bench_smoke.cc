#include "bench_smoke_common.h"
#include "pattern_set_options.h"
#include "vibe_othello/board_core/coordinates.h"
#include "vibe_othello/evaluation/pattern_evaluator.h"
#include "vibe_othello/search/result.h"
#include "vibe_othello/search/search.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace bench = vibe_othello::tools::pattern::bench_smoke;

constexpr vibe_othello::search::Depth kSearchDepth = 1;

struct SearchRow {
  std::string position_id;
  int disc_count = 0;
  int phase = 0;
  std::string evaluator;
  std::string best_move;
  vibe_othello::search::Score score = 0;
  vibe_othello::search::NodeCount nodes = 0;
  vibe_othello::search::Depth depth = 0;
};

struct PairDiff {
  std::string position_id;
  vibe_othello::search::Score score_delta = 0;
  bool best_move_different = false;
};

std::string move_to_string(vibe_othello::board_core::Move move) {
  namespace board_core = vibe_othello::board_core;

  if (move.kind == board_core::MoveKind::pass) {
    return "pass";
  }
  const int file = board_core::file_of(move.square);
  const int rank = board_core::rank_of(move.square);
  if (file < 0 || rank < 0) {
    return "none";
  }
  return {static_cast<char>('a' + file), static_cast<char>('1' + rank)};
}

std::string best_move_to_string(const vibe_othello::search::SearchResult& result) {
  return result.best_move.has_value() ? move_to_string(*result.best_move) : "none";
}

vibe_othello::search::SearchOptions smoke_search_options() noexcept {
  vibe_othello::search::SearchOptions options{};
  options.midgame.use_pvs = false;
  options.midgame.use_aspiration = false;
  options.midgame.use_iid = false;
  options.midgame.use_midgame_tt = false;
  options.ordering.use_tt_best_move_ordering = false;
  options.ordering.use_history = false;
  options.ordering.use_killers = false;
  options.ordering.use_endgame_parity_ordering = false;
  options.endgame.exact_endgame = false;
  options.endgame.use_endgame_tt = false;
  options.endgame.endgame_exact_empties = 0;
  options.endgame.endgame_wld_empties = 0;
  options.reporting.multi_pv = 0;
  return options;
}

std::optional<SearchRow> run_search_row(const bench::PositionFixture& fixture,
                                        const std::string& evaluator_name,
                                        const vibe_othello::search::Evaluator& evaluator) {
  const vibe_othello::search::SearchLimits limits{.max_depth = kSearchDepth};
  const vibe_othello::search::SearchOptions options = smoke_search_options();
  const vibe_othello::search::SearchResult first =
      vibe_othello::search::search_iterative(fixture.position, evaluator, limits, options);
  const vibe_othello::search::SearchResult second =
      vibe_othello::search::search_iterative(fixture.position, evaluator, limits, options);

  if (first.score != second.score || first.nodes != second.nodes ||
      first.completed_depth != second.completed_depth ||
      best_move_to_string(first) != best_move_to_string(second)) {
    std::cerr << "fixed-depth search result is not deterministic for " << fixture.position_id
              << " evaluator " << evaluator_name << '\n';
    std::exit(1);
  }
  if (first.score_kind != vibe_othello::search::ScoreKind::heuristic || first.exact ||
      first.stats.eval_calls == 0 || first.stats.endgame_nodes != 0) {
    return std::nullopt;
  }

  return SearchRow{
      .position_id = fixture.position_id,
      .disc_count = fixture.disc_count,
      .phase = bench::phase_for_disc_count(fixture.disc_count),
      .evaluator = evaluator_name,
      .best_move = best_move_to_string(first),
      .score = first.score,
      .nodes = first.nodes,
      .depth = first.completed_depth,
  };
}

std::string report_without_checksum(const bench::Args& args, std::span<const SearchRow> rows,
                                    std::span<const PairDiff> pair_diffs, int score_different_count,
                                    int best_move_different_count,
                                    const vibe_othello::evaluation::PatternSet& pattern_set) {
  std::ostringstream output;
  output << "{\n";
  output << "  \"schema_version\": 1,\n";
  output << "  \"source\": \"" << bench::smoke_source_for(pattern_set, true) << "\",\n";
  output << "  \"artifact_checksums\": {\n";
  output << "    \"v0a\": \"" << bench::json_escape(args.v0a_artifact_checksum) << "\",\n";
  output << "    \"v0b\": \"" << bench::json_escape(args.v0b_artifact_checksum) << "\"\n";
  output << "  },\n";
  output << "  \"pattern_set_id\": \"" << bench::json_escape(pattern_set.id) << "\",\n";
  output << "  \"phase_count\": 13,\n";
  output << "  \"search_config\": {\n";
  output << "    \"mode\": \"fixed_depth\",\n";
  output << "    \"entry_point\": \"search_iterative\",\n";
  output << "    \"depth\": " << kSearchDepth << ",\n";
  output << "    \"depth_policy\": \"smoke-only leaf-evaluator propagation\",\n";
  output << "    \"time_control\": \"off\",\n";
  output << "    \"midgame_tt\": \"off\",\n";
  output << "    \"endgame_exact\": \"off\",\n";
  output << "    \"endgame_tt\": \"off\",\n";
  output << "    \"endgame_parity_ordering\": \"off\",\n";
  output << "    \"threading\": \"single\",\n";
  output << "    \"elapsed_ms_policy\": \"fixed-zero-for-deterministic-smoke\"\n";
  output << "  },\n";
  output << "  \"positions_count\": " << pair_diffs.size() << ",\n";
  output << "  \"result_rows\": [\n";
  for (std::size_t index = 0; index < rows.size(); ++index) {
    const SearchRow& row = rows[index];
    output << "    {\"position_id\": \"" << bench::json_escape(row.position_id)
           << "\", \"disc_count\": " << row.disc_count << ", \"phase\": " << row.phase
           << ", \"evaluator\": \"" << bench::json_escape(row.evaluator) << "\", \"best_move\": \""
           << bench::json_escape(row.best_move) << "\", \"score\": " << row.score
           << ", \"nodes\": " << row.nodes << ", \"depth\": " << row.depth
           << ", \"elapsed_ms\": 0}";
    output << (index + 1 == rows.size() ? "\n" : ",\n");
  }
  output << "  ],\n";
  output << "  \"position_diffs\": [\n";
  for (std::size_t index = 0; index < pair_diffs.size(); ++index) {
    const PairDiff& diff = pair_diffs[index];
    output << "    {\"position_id\": \"" << bench::json_escape(diff.position_id)
           << "\", \"score_delta\": " << diff.score_delta
           << ", \"best_move_different\": " << (diff.best_move_different ? "true" : "false") << "}";
    output << (index + 1 == pair_diffs.size() ? "\n" : ",\n");
  }
  output << "  ],\n";
  output << "  \"v0a_v0b_score_different_count\": " << score_different_count << ",\n";
  output << "  \"v0a_v0b_best_move_different_count\": " << best_move_different_count << ",\n";
  output << "  \"notes\": [\n";
  output << "    \"local smoke only\",\n";
  output << "    \"not production benchmark\",\n";
  output << "    \"not strength claim\",\n";
  output << "    \"depth 1 only checks evaluator signal propagation into search score\",\n";
  output << "    \"synthetic artifacts are temp-only\",\n";
  output << "    \"publication remains gated / unknown\"\n";
  output << "  ]\n";
  output << "}";
  return output.str();
}

} // namespace

int main(int argc, char** argv) {
  namespace eval = vibe_othello::evaluation;
  namespace pattern = vibe_othello::tools::pattern;

  const std::optional<bench::Args> args =
      bench::parse_args(argc, argv, "vibe-othello-pattern-search-bench-smoke");
  if (!args.has_value()) {
    return 2;
  }
  const std::optional<pattern::PatternSetOption> selected_pattern_set =
      pattern::select_pattern_set(args->pattern_set, pattern::IndexMode::raw);
  if (!selected_pattern_set.has_value() || selected_pattern_set->pattern_set == nullptr) {
    std::cerr << "--pattern-set must be " << pattern::pattern_set_option_names() << '\n';
    return 2;
  }
  const eval::PatternSet& pattern_set = *selected_pattern_set->pattern_set;

  std::optional<eval::PatternWeights> v0a_weights =
      bench::load_weights(args->v0a_weights_path, pattern_set);
  std::optional<eval::PatternWeights> v0b_weights =
      bench::load_weights(args->v0b_weights_path, pattern_set);
  std::optional<std::vector<bench::PositionFixture>> positions =
      bench::load_positions(args->positions_tsv_path, bench::PositionFilter::searchable_depth_one);
  if (!v0a_weights.has_value() || !v0b_weights.has_value() || !positions.has_value()) {
    return 1;
  }

  const eval::PatternFeatureSet& feature_set = selected_pattern_set->feature_set;
  const eval::PatternEvaluator v0a_evaluator{std::move(*v0a_weights), feature_set};
  const eval::PatternEvaluator v0b_evaluator{std::move(*v0b_weights), feature_set};

  std::vector<SearchRow> rows;
  std::vector<PairDiff> pair_diffs;
  rows.reserve(positions->size() * 2);
  pair_diffs.reserve(positions->size());
  int score_different_count = 0;
  int best_move_different_count = 0;
  for (const bench::PositionFixture& fixture : *positions) {
    std::optional<SearchRow> v0a_row = run_search_row(fixture, "v0a", v0a_evaluator);
    std::optional<SearchRow> v0b_row = run_search_row(fixture, "v0b", v0b_evaluator);
    if (!v0a_row.has_value() || !v0b_row.has_value()) {
      continue;
    }
    const bool score_different = v0a_row->score != v0b_row->score;
    const bool best_move_different = v0a_row->best_move != v0b_row->best_move;
    score_different_count += score_different ? 1 : 0;
    best_move_different_count += best_move_different ? 1 : 0;
    pair_diffs.push_back(PairDiff{
        .position_id = fixture.position_id,
        .score_delta = v0b_row->score - v0a_row->score,
        .best_move_different = best_move_different,
    });
    rows.push_back(std::move(*v0a_row));
    rows.push_back(std::move(*v0b_row));
  }

  if (rows.empty()) {
    std::cerr << "no fixed positions reached heuristic leaf evaluation\n";
    return 1;
  }
  if (score_different_count == 0) {
    std::cerr << "v0b search score did not differ from v0a for any fixed position\n";
    return 1;
  }

  const std::string body = report_without_checksum(*args, rows, pair_diffs, score_different_count,
                                                   best_move_different_count, pattern_set);
  const std::string checksum = bench::checksum_for(body);
  if (!bench::write_report(args->report_out_path, body, checksum, "search")) {
    return 1;
  }

  std::cout << "schema_version=1\n";
  std::cout << "source=" << bench::smoke_source_for(pattern_set, true) << '\n';
  std::cout << "pattern_set_id=" << pattern_set.id << '\n';
  std::cout << "phase_count=13\n";
  std::cout << "search_depth=" << kSearchDepth << '\n';
  std::cout << "positions_count=" << pair_diffs.size() << '\n';
  std::cout << "result_rows=" << rows.size() << '\n';
  std::cout << "v0a_v0b_score_different_count=" << score_different_count << '\n';
  std::cout << "v0a_v0b_best_move_different_count=" << best_move_different_count << '\n';
  std::cout << "checksum=" << checksum << '\n';
  return 0;
}
