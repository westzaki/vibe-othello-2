#include "pattern_set_options.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/coordinates.h"
#include "vibe_othello/board_core/position.h"
#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/pattern_evaluator.h"
#include "vibe_othello/evaluation/pattern_feature_set.h"
#include "vibe_othello/evaluation/pattern_weights.h"
#include "vibe_othello/search/result.h"
#include "vibe_othello/search/search.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr vibe_othello::search::Depth kSearchDepth = 1;

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
  vibe_othello::board_core::Position position;
  int disc_count = 0;
};

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

std::optional<Args> parse_args(int argc, char** argv) {
  Args args;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    auto next_value = [&](std::string* output) -> bool {
      if (index + 1 >= argc) {
        std::cerr << arg << " requires a value\n";
        return false;
      }
      *output = argv[++index];
      return true;
    };

    if (arg == "--positions-tsv") {
      if (!next_value(&args.positions_tsv_path)) {
        return std::nullopt;
      }
    } else if (arg == "--v0a-weights") {
      if (!next_value(&args.v0a_weights_path)) {
        return std::nullopt;
      }
    } else if (arg == "--v0b-weights") {
      if (!next_value(&args.v0b_weights_path)) {
        return std::nullopt;
      }
    } else if (arg == "--v0a-artifact-checksum") {
      if (!next_value(&args.v0a_artifact_checksum)) {
        return std::nullopt;
      }
    } else if (arg == "--v0b-artifact-checksum") {
      if (!next_value(&args.v0b_artifact_checksum)) {
        return std::nullopt;
      }
    } else if (arg == "--report-out") {
      if (!next_value(&args.report_out_path)) {
        return std::nullopt;
      }
    } else if (arg == "--pattern-set") {
      if (!next_value(&args.pattern_set)) {
        return std::nullopt;
      }
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      return std::nullopt;
    }
  }

  if (args.positions_tsv_path.empty() || args.v0a_weights_path.empty() ||
      args.v0b_weights_path.empty() || args.v0a_artifact_checksum.empty() ||
      args.v0b_artifact_checksum.empty() || args.report_out_path.empty()) {
    std::cerr << "usage: vibe-othello-pattern-search-bench-smoke "
                 "--positions-tsv PATH --v0a-weights PATH --v0b-weights PATH "
                 "--v0a-artifact-checksum CHECKSUM --v0b-artifact-checksum CHECKSUM "
                 "--report-out PATH [--pattern-set tiny|buro-lite]\n";
    return std::nullopt;
  }
  return args;
}

std::string_view trim_trailing_cr(std::string_view text) noexcept {
  if (!text.empty() && text.back() == '\r') {
    text.remove_suffix(1);
  }
  return text;
}

std::vector<std::string_view> split_tabs(std::string_view text) {
  std::vector<std::string_view> fields;
  std::size_t offset = 0;
  while (offset <= text.size()) {
    const std::size_t next = text.find('\t', offset);
    if (next == std::string_view::npos) {
      fields.push_back(text.substr(offset));
      break;
    }
    fields.push_back(text.substr(offset, next - offset));
    offset = next + 1;
  }
  return fields;
}

std::string json_escape(std::string_view text) {
  std::string result;
  for (const char character : text) {
    if (character == '"' || character == '\\') {
      result.push_back('\\');
      result.push_back(character);
    } else if (character == '\n') {
      result += "\\n";
    } else {
      result.push_back(character);
    }
  }
  return result;
}

void mix_fnv1a(std::string_view text, std::uint64_t* hash) noexcept {
  for (const char character : text) {
    *hash ^= static_cast<unsigned char>(character);
    *hash *= 1099511628211ull;
  }
}

std::string checksum_for(std::string_view text) {
  std::uint64_t hash = 14695981039346656037ull;
  mix_fnv1a(text, &hash);
  std::ostringstream output;
  output << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}

std::array<std::uint8_t, vibe_othello::evaluation::PatternWeights::kDiscCountEntries>
phase_by_disc_count_13() {
  std::array<std::uint8_t, vibe_othello::evaluation::PatternWeights::kDiscCountEntries> phases{};
  for (std::uint8_t disc_count = 0; disc_count < phases.size(); ++disc_count) {
    const int normalized_count = disc_count < 4 ? 0 : static_cast<int>(disc_count) - 4;
    const int phase = std::min(12, (normalized_count * 13) / 60);
    phases[disc_count] = static_cast<std::uint8_t>(phase);
  }
  return phases;
}

std::optional<std::vector<std::uint8_t>> read_binary(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    std::cerr << "cannot read artifact weights: " << path << '\n';
    return std::nullopt;
  }

  input.seekg(0, std::ios::end);
  const std::streamoff size = input.tellg();
  if (size < 0) {
    std::cerr << "cannot determine artifact weights size: " << path << '\n';
    return std::nullopt;
  }
  input.seekg(0, std::ios::beg);

  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  if (!input) {
    std::cerr << "failed while reading artifact weights: " << path << '\n';
    return std::nullopt;
  }
  return bytes;
}

std::optional<vibe_othello::evaluation::PatternWeights>
load_weights(const std::string& path, const vibe_othello::evaluation::PatternSet& pattern_set) {
  namespace eval = vibe_othello::evaluation;

  const std::optional<std::vector<std::uint8_t>> artifact = read_binary(path);
  if (!artifact.has_value()) {
    return std::nullopt;
  }

  const eval::PatternManifest manifest{
      .format_version = eval::kPatternWeightFormatVersion,
      .bit_order = eval::PatternBitOrder::a1_lsb,
      .score_unit = eval::PatternScoreUnit::disc_diff,
      .score_scale = 1,
      .phase_count = 13,
      .pattern_set_id = pattern_set.id,
      .patterns = pattern_set.patterns,
  };
  const eval::PatternWeightsLoadResult loaded = eval::load_pattern_weights(manifest, *artifact);
  if (!loaded.ok()) {
    std::cerr << "runtime loader rejected artifact: " << path << '\n';
    return std::nullopt;
  }

  const std::optional<eval::PatternWeights> weights =
      eval::make_pattern_weights(*loaded.weights, phase_by_disc_count_13());
  if (!weights.has_value()) {
    std::cerr << "loaded artifact could not be converted to runtime PatternWeights: " << path
              << '\n';
    return std::nullopt;
  }
  return weights;
}

std::optional<vibe_othello::board_core::Position>
position_from_a1_to_h8_board(std::string_view board) noexcept {
  namespace board_core = vibe_othello::board_core;

  if (board.size() != board_core::kSquareCount) {
    return std::nullopt;
  }

  board_core::Bitboard player = 0;
  board_core::Bitboard opponent = 0;
  for (std::size_t index = 0; index < board.size(); ++index) {
    const board_core::Square square = board_core::square_from_index(static_cast<int>(index));
    const board_core::Bitboard bit = board_core::bit(square);
    if (board[index] == 'X') {
      player |= bit;
    } else if (board[index] == 'O') {
      opponent |= bit;
    } else if (board[index] != '-') {
      return std::nullopt;
    }
  }

  board_core::Position position{
      .player = player,
      .opponent = opponent,
      .side_to_move = board_core::Color::black,
  };
  if (!board_core::is_valid(position)) {
    return std::nullopt;
  }
  return position;
}

std::optional<std::vector<PositionFixture>> load_positions(const std::string& path) {
  static constexpr std::string_view kExpectedHeaderV1 =
      "record_id\tposition_id\tsource_dataset_id\tsplit\tboard_a1_to_h8\tlabel_kind\tlabel_"
      "unit\tlabel_perspective\tlabel_score_side_to_move\toccupied_count\tphase\tplayer_disc_"
      "count\topponent_disc_count\tempty_count";
  static constexpr std::string_view kExpectedHeaderV2 =
      "record_id\tposition_id\tgame_group_id\tboard_id\tsource_occurrence_id\tsource_dataset_id\t"
      "split\tboard_a1_to_h8\tlabel_kind\tlabel_unit\tlabel_perspective\tlabel_score_side_to_"
      "move\toccupied_count\tphase\tplayer_disc_count\topponent_disc_count\tempty_count";

  std::ifstream input(path);
  if (!input) {
    std::cerr << "cannot read normalized positions TSV: " << path << '\n';
    return std::nullopt;
  }

  std::vector<PositionFixture> positions;
  std::set<std::string> seen_position_ids;
  std::string line;
  int line_number = 0;
  int schema_version = 0;
  while (std::getline(input, line)) {
    ++line_number;
    const std::string_view trimmed = trim_trailing_cr(line);
    if (line_number == 1) {
      if (trimmed == kExpectedHeaderV1) {
        schema_version = 1;
      } else if (trimmed == kExpectedHeaderV2) {
        schema_version = 2;
      } else {
        std::cerr << "line 1: unexpected normalized TSV header\n";
        return std::nullopt;
      }
      continue;
    }
    if (trimmed.empty()) {
      continue;
    }

    const std::vector<std::string_view> fields = split_tabs(trimmed);
    const std::size_t expected_fields = schema_version == 2 ? 17 : 14;
    if (fields.size() != expected_fields) {
      std::cerr << "line " << line_number << ": expected " << expected_fields << " TSV fields\n";
      return std::nullopt;
    }
    const std::string position_id{fields[1]};
    if (!seen_position_ids.insert(position_id).second) {
      continue;
    }
    const std::size_t board_field = schema_version == 2 ? 7 : 4;
    const std::optional<vibe_othello::board_core::Position> position =
        position_from_a1_to_h8_board(fields[board_field]);
    if (!position.has_value()) {
      std::cerr << "line " << line_number << ": could not convert board to Position\n";
      return std::nullopt;
    }

    const int disc_count = std::popcount(vibe_othello::board_core::occupied(*position));
    const int empty_count = vibe_othello::board_core::kSquareCount - disc_count;
    if (disc_count < 4 || empty_count < kSearchDepth ||
        vibe_othello::board_core::is_terminal(*position)) {
      continue;
    }

    positions.push_back(PositionFixture{
        .position_id = position_id,
        .position = *position,
        .disc_count = disc_count,
    });
  }

  if (positions.empty()) {
    std::cerr << "normalized positions TSV produced no searchable positions\n";
    return std::nullopt;
  }
  return positions;
}

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

  std::string text;
  text.push_back(static_cast<char>('a' + file));
  text.push_back(static_cast<char>('1' + rank));
  return text;
}

std::string best_move_to_string(const vibe_othello::search::SearchResult& result) {
  if (!result.best_move.has_value()) {
    return "none";
  }
  return move_to_string(*result.best_move);
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
  options.experimental.use_parallel = false;
  return options;
}

std::optional<SearchRow> run_search_row(const PositionFixture& fixture,
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
      .phase = phase_by_disc_count_13()[static_cast<std::size_t>(fixture.disc_count)],
      .evaluator = evaluator_name,
      .best_move = best_move_to_string(first),
      .score = first.score,
      .nodes = first.nodes,
      .depth = first.completed_depth,
  };
}

std::string report_without_checksum(const Args& args, std::span<const SearchRow> rows,
                                    std::span<const PairDiff> pair_diffs, int score_different_count,
                                    int best_move_different_count,
                                    const vibe_othello::evaluation::PatternSet& pattern_set) {
  const bool tiny_pattern_set = pattern_set.id == "fixed-pattern-fixture-v1";
  const std::string_view source =
      tiny_pattern_set ? "tiny-egaroucid-v0b-search-smoke" : "buro-lite-egaroucid-v0b-search-smoke";
  std::ostringstream output;
  output << "{\n";
  output << "  \"schema_version\": 1,\n";
  output << "  \"source\": \"" << source << "\",\n";
  output << "  \"artifact_checksums\": {\n";
  output << "    \"v0a\": \"" << json_escape(args.v0a_artifact_checksum) << "\",\n";
  output << "    \"v0b\": \"" << json_escape(args.v0b_artifact_checksum) << "\"\n";
  output << "  },\n";
  output << "  \"pattern_set_id\": \"" << json_escape(pattern_set.id) << "\",\n";
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
    output << "    {\"position_id\": \"" << json_escape(row.position_id)
           << "\", \"disc_count\": " << row.disc_count << ", \"phase\": " << row.phase
           << ", \"evaluator\": \"" << json_escape(row.evaluator) << "\", \"best_move\": \""
           << json_escape(row.best_move) << "\", \"score\": " << row.score
           << ", \"nodes\": " << row.nodes << ", \"depth\": " << row.depth
           << ", \"elapsed_ms\": 0}";
    output << (index + 1 == rows.size() ? "\n" : ",\n");
  }
  output << "  ],\n";
  output << "  \"position_diffs\": [\n";
  for (std::size_t index = 0; index < pair_diffs.size(); ++index) {
    const PairDiff& diff = pair_diffs[index];
    output << "    {\"position_id\": \"" << json_escape(diff.position_id)
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
  output << "    \"Egaroucid-derived artifacts are temp-only\",\n";
  output << "    \"publication remains gated / unknown\"\n";
  output << "  ]\n";
  output << "}";
  return output.str();
}

bool write_report(const std::string& path, std::string_view report_body,
                  std::string_view checksum) {
  std::ofstream output(path);
  if (!output) {
    std::cerr << "cannot write search smoke report: " << path << '\n';
    return false;
  }

  const std::string marker = "\n}";
  const std::size_t marker_index = report_body.rfind(marker);
  if (marker_index == std::string_view::npos) {
    std::cerr << "internal report formatting error\n";
    return false;
  }
  output << report_body.substr(0, marker_index);
  output << ",\n  \"checksum\": \"" << checksum << "\"\n}";
  output << '\n';
  return static_cast<bool>(output);
}

} // namespace

int main(int argc, char** argv) {
  namespace eval = vibe_othello::evaluation;
  namespace pattern = vibe_othello::tools::pattern;

  const std::optional<Args> args = parse_args(argc, argv);
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
      load_weights(args->v0a_weights_path, pattern_set);
  std::optional<eval::PatternWeights> v0b_weights =
      load_weights(args->v0b_weights_path, pattern_set);
  std::optional<std::vector<PositionFixture>> positions = load_positions(args->positions_tsv_path);
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
  for (const PositionFixture& fixture : *positions) {
    std::optional<SearchRow> v0a_row = run_search_row(fixture, "v0a", v0a_evaluator);
    std::optional<SearchRow> v0b_row = run_search_row(fixture, "v0b", v0b_evaluator);
    if (!v0a_row.has_value() || !v0b_row.has_value()) {
      continue;
    }
    const bool score_different = v0a_row->score != v0b_row->score;
    const bool best_move_different = v0a_row->best_move != v0b_row->best_move;
    if (score_different) {
      ++score_different_count;
    }
    if (best_move_different) {
      ++best_move_different_count;
    }
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
  const std::string checksum = checksum_for(body);
  if (!write_report(args->report_out_path, body, checksum)) {
    return 1;
  }

  std::cout << "schema_version=1\n";
  std::cout << "source="
            << (pattern_set.id == "fixed-pattern-fixture-v1"
                    ? "tiny-egaroucid-v0b-search-smoke"
                    : "buro-lite-egaroucid-v0b-search-smoke")
            << '\n';
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
