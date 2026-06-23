#include "pattern_set_options.h"
#include "vibe_othello/board_core/coordinates.h"
#include "vibe_othello/board_core/position.h"
#include "vibe_othello/evaluation/pattern.h"
#include "vibe_othello/evaluation/pattern_evaluator.h"
#include "vibe_othello/evaluation/pattern_feature_set.h"
#include "vibe_othello/evaluation/pattern_weights.h"
#include "vibe_othello/search/score.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

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

struct ScoreRow {
  std::string position_id;
  int disc_count = 0;
  int phase = 0;
  vibe_othello::search::Score v0a_score = 0;
  vibe_othello::search::Score v0b_score = 0;
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
    std::cerr << "usage: vibe-othello-pattern-evaluation-bench-smoke "
                 "--positions-tsv PATH --v0a-weights PATH --v0b-weights PATH "
                 "--v0a-artifact-checksum CHECKSUM --v0b-artifact-checksum CHECKSUM "
                 "--report-out PATH [--pattern-set tiny|buro-lite|endgame-lite]\n";
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
    positions.push_back(PositionFixture{
        .position_id = position_id,
        .position = *position,
        .disc_count = std::popcount(vibe_othello::board_core::occupied(*position))});
  }

  if (positions.empty()) {
    std::cerr << "normalized positions TSV produced no positions\n";
    return std::nullopt;
  }
  return positions;
}

bool score_in_search_range(vibe_othello::search::Score score) noexcept {
  return vibe_othello::search::kScoreLoss < score && score < vibe_othello::search::kScoreWin;
}

std::string_view smoke_source_for(const vibe_othello::evaluation::PatternSet& pattern_set) {
  if (pattern_set.id == "fixed-pattern-fixture-v1") {
    return "tiny-synthetic-v0b-smoke";
  }
  if (pattern_set.id == "pattern-v2-endgame-lite") {
    return "endgame-lite-synthetic-v0b-smoke";
  }
  return "buro-lite-synthetic-v0b-smoke";
}

std::string report_without_checksum(const Args& args, std::span<const ScoreRow> rows,
                                    int different_count,
                                    const vibe_othello::evaluation::PatternSet& pattern_set) {
  const std::string_view source = smoke_source_for(pattern_set);
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
  output << "  \"positions_count\": " << rows.size() << ",\n";
  output << "  \"score_rows\": [\n";
  for (std::size_t index = 0; index < rows.size(); ++index) {
    const ScoreRow& row = rows[index];
    output << "    {\"position_id\": \"" << json_escape(row.position_id)
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

bool write_report(const std::string& path, std::string_view report_body,
                  std::string_view checksum) {
  std::ofstream output(path);
  if (!output) {
    std::cerr << "cannot write evaluation smoke report: " << path << '\n';
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

  std::vector<ScoreRow> rows;
  rows.reserve(positions->size());
  int different_count = 0;
  for (const PositionFixture& fixture : *positions) {
    const vibe_othello::search::Score v0a_score = v0a_evaluator.evaluate(fixture.position);
    const vibe_othello::search::Score v0a_score_again = v0a_evaluator.evaluate(fixture.position);
    const vibe_othello::search::Score v0b_score = v0b_evaluator.evaluate(fixture.position);
    const vibe_othello::search::Score v0b_score_again = v0b_evaluator.evaluate(fixture.position);
    if (v0a_score != v0a_score_again || v0b_score != v0b_score_again) {
      std::cerr << "PatternEvaluator result is not deterministic\n";
      return 1;
    }
    if (!score_in_search_range(v0a_score) || !score_in_search_range(v0b_score)) {
      std::cerr << "PatternEvaluator score is outside the safe search::Score range\n";
      return 1;
    }
    if (v0a_score != v0b_score) {
      ++different_count;
    }
    const int phase = phase_by_disc_count_13()[static_cast<std::size_t>(fixture.disc_count)];
    rows.push_back(ScoreRow{
        .position_id = fixture.position_id,
        .disc_count = fixture.disc_count,
        .phase = phase,
        .v0a_score = v0a_score,
        .v0b_score = v0b_score,
    });
  }

  if (different_count == 0) {
    std::cerr << "v0b did not differ from v0a for any fixed position\n";
    return 1;
  }

  const std::string body = report_without_checksum(*args, rows, different_count, pattern_set);
  const std::string checksum = checksum_for(body);
  if (!write_report(args->report_out_path, body, checksum)) {
    return 1;
  }

  std::cout << "schema_version=1\n";
  std::cout << "source=" << smoke_source_for(pattern_set) << '\n';
  std::cout << "pattern_set_id=" << pattern_set.id << '\n';
  std::cout << "phase_count=13\n";
  std::cout << "positions_count=" << rows.size() << '\n';
  std::cout << "v0a_v0b_different_count=" << different_count << '\n';
  std::cout << "checksum=" << checksum << '\n';
  return 0;
}
