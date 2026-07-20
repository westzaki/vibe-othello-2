#include "bench_smoke_common.h"

#include "normalized_tsv.h"
#include "vibe_othello/board_core/board.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <utility>

namespace vibe_othello::tools::pattern::bench_smoke {

std::optional<Args> parse_args(int argc, char** argv, std::string_view executable_name) {
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
    std::cerr << "usage: " << executable_name
              << " --positions-tsv PATH --v0a-weights PATH --v0b-weights PATH "
                 "--v0a-artifact-checksum CHECKSUM --v0b-artifact-checksum CHECKSUM "
                 "--report-out PATH [--pattern-set tiny|buro-lite|endgame-lite]\n";
    return std::nullopt;
  }
  return args;
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

std::string checksum_for(std::string_view text) {
  std::uint64_t hash = 14695981039346656037ull;
  for (const char character : text) {
    hash ^= static_cast<unsigned char>(character);
    hash *= 1099511628211ull;
  }
  std::ostringstream output;
  output << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}

int phase_for_disc_count(int disc_count) noexcept {
  const int normalized_count = disc_count < 4 ? 0 : disc_count - 4;
  return std::min(12, (normalized_count * 13) / 60);
}

std::optional<evaluation::PatternWeights> load_weights(const std::string& path,
                                                       const evaluation::PatternSet& pattern_set) {
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
  std::vector<std::uint8_t> artifact(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char*>(artifact.data()), size);
  if (!input) {
    std::cerr << "failed while reading artifact weights: " << path << '\n';
    return std::nullopt;
  }

  const evaluation::PatternManifest manifest{
      .format_version = evaluation::kPatternWeightFormatVersion,
      .bit_order = evaluation::PatternBitOrder::a1_lsb,
      .score_unit = evaluation::PatternScoreUnit::disc_diff,
      .score_scale = 1,
      .phase_count = 13,
      .pattern_set_id = pattern_set.id,
      .patterns = pattern_set.patterns,
  };
  const evaluation::PatternWeightsLoadResult loaded =
      evaluation::load_pattern_weights(manifest, artifact);
  if (!loaded.ok()) {
    std::cerr << "runtime loader rejected artifact: " << path << '\n';
    return std::nullopt;
  }

  std::array<std::uint8_t, evaluation::PatternWeights::kDiscCountEntries> phases{};
  for (std::uint8_t disc_count = 0; disc_count < phases.size(); ++disc_count) {
    phases[disc_count] = static_cast<std::uint8_t>(phase_for_disc_count(disc_count));
  }
  std::optional<evaluation::PatternWeights> weights =
      evaluation::make_pattern_weights(*loaded.weights, phases);
  if (!weights.has_value()) {
    std::cerr << "loaded artifact could not be converted to runtime PatternWeights: " << path
              << '\n';
  }
  return weights;
}

std::optional<std::vector<PositionFixture>> load_positions(const std::string& path,
                                                           PositionFilter filter) {
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
      if (trimmed == kNormalizedHeaderV1) {
        schema_version = 1;
      } else if (trimmed == kNormalizedHeaderV2) {
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
    const std::optional<board_core::Position> position =
        position_from_a1_to_h8_board(fields[board_field]);
    if (!position.has_value()) {
      std::cerr << "line " << line_number << ": could not convert board to Position\n";
      return std::nullopt;
    }

    const int disc_count = std::popcount(board_core::occupied(*position));
    const int empty_count = board_core::kSquareCount - disc_count;
    if (filter == PositionFilter::searchable_depth_one &&
        (disc_count < 4 || empty_count < 1 || board_core::is_terminal(*position))) {
      continue;
    }
    positions.push_back(PositionFixture{
        .position_id = std::move(position_id),
        .position = *position,
        .disc_count = disc_count,
    });
  }

  if (positions.empty()) {
    std::cerr << "normalized positions TSV produced no "
              << (filter == PositionFilter::searchable_depth_one ? "searchable " : "")
              << "positions\n";
    return std::nullopt;
  }
  return positions;
}

bool score_in_search_range(search::Score score) noexcept {
  return search::kScoreLoss < score && score < search::kScoreWin;
}

std::string_view smoke_source_for(const evaluation::PatternSet& pattern_set,
                                  bool search_smoke) noexcept {
  if (pattern_set.id == "fixed-pattern-fixture-v1") {
    return search_smoke ? "tiny-synthetic-v0b-search-smoke" : "tiny-synthetic-v0b-smoke";
  }
  if (pattern_set.id == "pattern-v2-endgame-lite") {
    return search_smoke ? "endgame-lite-synthetic-v0b-search-smoke"
                        : "endgame-lite-synthetic-v0b-smoke";
  }
  return search_smoke ? "buro-lite-synthetic-v0b-search-smoke" : "buro-lite-synthetic-v0b-smoke";
}

bool write_report(const std::string& path, std::string_view report_body, std::string_view checksum,
                  std::string_view report_label) {
  std::ofstream output(path);
  if (!output) {
    std::cerr << "cannot write " << report_label << " smoke report: " << path << '\n';
    return false;
  }

  const std::size_t marker_index = report_body.rfind("\n}");
  if (marker_index == std::string_view::npos) {
    std::cerr << "internal report formatting error\n";
    return false;
  }
  output << report_body.substr(0, marker_index);
  output << ",\n  \"checksum\": \"" << checksum << "\"\n}\n";
  return static_cast<bool>(output);
}

} // namespace vibe_othello::tools::pattern::bench_smoke
