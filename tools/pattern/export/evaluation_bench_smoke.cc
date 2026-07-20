#include "bench_smoke_common.h"
#include "pattern_set_options.h"
#include "vibe_othello/evaluation/pattern_evaluator.h"

#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace bench = vibe_othello::tools::pattern::bench_smoke;

struct ScoreRow {
  std::string position_id;
  int disc_count = 0;
  int phase = 0;
  vibe_othello::search::Score v0a_score = 0;
  vibe_othello::search::Score v0b_score = 0;
};

std::string report_without_checksum(const bench::Args& args, std::span<const ScoreRow> rows,
                                    int different_count,
                                    const vibe_othello::evaluation::PatternSet& pattern_set) {
  std::ostringstream output;
  output << "{\n";
  output << "  \"schema_version\": 1,\n";
  output << "  \"source\": \"" << bench::smoke_source_for(pattern_set, false) << "\",\n";
  output << "  \"artifact_checksums\": {\n";
  output << "    \"v0a\": \"" << bench::json_escape(args.v0a_artifact_checksum) << "\",\n";
  output << "    \"v0b\": \"" << bench::json_escape(args.v0b_artifact_checksum) << "\"\n";
  output << "  },\n";
  output << "  \"pattern_set_id\": \"" << bench::json_escape(pattern_set.id) << "\",\n";
  output << "  \"phase_count\": 13,\n";
  output << "  \"positions_count\": " << rows.size() << ",\n";
  output << "  \"score_rows\": [\n";
  for (std::size_t index = 0; index < rows.size(); ++index) {
    const ScoreRow& row = rows[index];
    output << "    {\"position_id\": \"" << bench::json_escape(row.position_id)
           << "\", \"disc_count\": " << row.disc_count << ", \"phase\": " << row.phase
           << ", \"v0a_score\": " << row.v0a_score << ", \"v0b_score\": " << row.v0b_score
           << ", \"score_delta\": " << (row.v0b_score - row.v0a_score) << "}";
    output << (index + 1 == rows.size() ? "\n" : ",\n");
  }
  output << "  ],\n";
  output << "  \"v0a_v0b_different_count\": " << different_count << ",\n";
  output << "  \"notes\": [\n";
  output << "    \"local smoke only\",\n";
  output << "    \"not production benchmark\",\n";
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
      bench::parse_args(argc, argv, "vibe-othello-pattern-evaluation-bench-smoke");
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
      bench::load_positions(args->positions_tsv_path, bench::PositionFilter::all);
  if (!v0a_weights.has_value() || !v0b_weights.has_value() || !positions.has_value()) {
    return 1;
  }

  const eval::PatternFeatureSet& feature_set = selected_pattern_set->feature_set;
  const eval::PatternEvaluator v0a_evaluator{std::move(*v0a_weights), feature_set};
  const eval::PatternEvaluator v0b_evaluator{std::move(*v0b_weights), feature_set};

  std::vector<ScoreRow> rows;
  rows.reserve(positions->size());
  int different_count = 0;
  for (const bench::PositionFixture& fixture : *positions) {
    const vibe_othello::search::Score v0a_score = v0a_evaluator.evaluate(fixture.position);
    const vibe_othello::search::Score v0a_score_again = v0a_evaluator.evaluate(fixture.position);
    const vibe_othello::search::Score v0b_score = v0b_evaluator.evaluate(fixture.position);
    const vibe_othello::search::Score v0b_score_again = v0b_evaluator.evaluate(fixture.position);
    if (v0a_score != v0a_score_again || v0b_score != v0b_score_again) {
      std::cerr << "PatternEvaluator result is not deterministic\n";
      return 1;
    }
    if (!bench::score_in_search_range(v0a_score) || !bench::score_in_search_range(v0b_score)) {
      std::cerr << "PatternEvaluator score is outside the safe search::Score range\n";
      return 1;
    }
    different_count += v0a_score != v0b_score ? 1 : 0;
    rows.push_back(ScoreRow{
        .position_id = fixture.position_id,
        .disc_count = fixture.disc_count,
        .phase = bench::phase_for_disc_count(fixture.disc_count),
        .v0a_score = v0a_score,
        .v0b_score = v0b_score,
    });
  }

  if (different_count == 0) {
    std::cerr << "v0b did not differ from v0a for any fixed position\n";
    return 1;
  }

  const std::string body = report_without_checksum(*args, rows, different_count, pattern_set);
  const std::string checksum = bench::checksum_for(body);
  if (!bench::write_report(args->report_out_path, body, checksum, "evaluation")) {
    return 1;
  }

  std::cout << "schema_version=1\n";
  std::cout << "source=" << bench::smoke_source_for(pattern_set, false) << '\n';
  std::cout << "pattern_set_id=" << pattern_set.id << '\n';
  std::cout << "phase_count=13\n";
  std::cout << "positions_count=" << rows.size() << '\n';
  std::cout << "v0a_v0b_different_count=" << different_count << '\n';
  std::cout << "checksum=" << checksum << '\n';
  return 0;
}
