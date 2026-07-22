#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/coordinates.h"
#include "vibe_othello/board_core/serialization.h"
#include "vibe_othello/evaluation/pattern_artifact.h"
#include "vibe_othello/evaluation/phase_aware_evaluator.h"
#include "vibe_othello/search/search.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

namespace board = vibe_othello::board_core;
namespace evaluation = vibe_othello::evaluation;
namespace search = vibe_othello::search;

struct Args {
  std::filesystem::path artifact_manifest;
  std::filesystem::path positions_path;
  std::filesystem::path output_path;
  std::string repository_sha;
  std::string search_config_id;
  search::Depth depth = 8;
  std::uint8_t exact_endgame_empties = 8;
  std::uint32_t sample_rate = search::kShadowCalibrationSampleRateScale;
  std::uint32_t max_samples_per_search = 1;
  std::uint64_t sampling_seed = 0;
  std::size_t position_limit = 0;
  std::uint32_t partition_count = 1;
  std::uint32_t partition_index = 0;
  int root_phase = -1;
  std::vector<search::ShadowCalibrationDepthPairV1> depth_pairs;
};

std::vector<std::string_view> split(std::string_view text, char delimiter) {
  std::vector<std::string_view> fields;
  std::size_t offset = 0;
  while (offset <= text.size()) {
    const std::size_t next = text.find(delimiter, offset);
    if (next == std::string_view::npos) {
      fields.push_back(text.substr(offset));
      break;
    }
    fields.push_back(text.substr(offset, next - offset));
    offset = next + 1;
  }
  return fields;
}

template <typename Integer> std::optional<Integer> parse_integer(std::string_view text) noexcept {
  Integer value = 0;
  const auto [pointer, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || pointer != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<search::ShadowCalibrationDepthPairV1>
parse_depth_pair(std::string_view text) noexcept {
  const std::size_t delimiter = text.find(':');
  if (delimiter == std::string_view::npos ||
      text.find(':', delimiter + 1) != std::string_view::npos) {
    return std::nullopt;
  }
  const std::optional<int> deep = parse_integer<int>(text.substr(0, delimiter));
  const std::optional<int> shallow = parse_integer<int>(text.substr(delimiter + 1));
  if (!deep.has_value() || !shallow.has_value() || *deep <= *shallow || *shallow <= 0 ||
      *deep > std::numeric_limits<search::Depth>::max()) {
    return std::nullopt;
  }
  return search::ShadowCalibrationDepthPairV1{
      .deep_depth = static_cast<search::Depth>(*deep),
      .shallow_depth = static_cast<search::Depth>(*shallow),
  };
}

std::optional<Args> parse_args(int argc, char** argv) {
  Args args;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    const auto value = [&]() -> std::optional<std::string_view> {
      if (index + 1 >= argc) {
        return std::nullopt;
      }
      return std::string_view{argv[++index]};
    };
    if (argument == "--artifact-manifest") {
      const auto next = value();
      if (!next.has_value()) {
        return std::nullopt;
      }
      args.artifact_manifest = *next;
    } else if (argument == "--positions") {
      const auto next = value();
      if (!next.has_value()) {
        return std::nullopt;
      }
      args.positions_path = *next;
    } else if (argument == "--output") {
      const auto next = value();
      if (!next.has_value()) {
        return std::nullopt;
      }
      args.output_path = *next;
    } else if (argument == "--repository-sha") {
      const auto next = value();
      if (!next.has_value()) {
        return std::nullopt;
      }
      args.repository_sha = *next;
    } else if (argument == "--search-config-id") {
      const auto next = value();
      if (!next.has_value()) {
        return std::nullopt;
      }
      args.search_config_id = *next;
    } else if (argument == "--depth-pair") {
      const auto next = value();
      const auto pair = next.has_value() ? parse_depth_pair(*next) : std::nullopt;
      if (!pair.has_value()) {
        return std::nullopt;
      }
      args.depth_pairs.push_back(*pair);
    } else if (argument == "--depth") {
      const auto next = value();
      const auto parsed = next.has_value() ? parse_integer<int>(*next) : std::nullopt;
      if (!parsed.has_value() || *parsed <= 0 ||
          *parsed > std::numeric_limits<search::Depth>::max()) {
        return std::nullopt;
      }
      args.depth = static_cast<search::Depth>(*parsed);
    } else if (argument == "--exact-endgame-empties") {
      const auto next = value();
      const auto parsed = next.has_value() ? parse_integer<int>(*next) : std::nullopt;
      if (!parsed.has_value() || *parsed < 0 || *parsed > 60) {
        return std::nullopt;
      }
      args.exact_endgame_empties = static_cast<std::uint8_t>(*parsed);
    } else if (argument == "--sample-rate") {
      const auto next = value();
      const auto parsed = next.has_value() ? parse_integer<std::uint32_t>(*next) : std::nullopt;
      if (!parsed.has_value() || *parsed == 0 ||
          *parsed > search::kShadowCalibrationSampleRateScale) {
        return std::nullopt;
      }
      args.sample_rate = *parsed;
    } else if (argument == "--max-samples-per-search") {
      const auto next = value();
      const auto parsed = next.has_value() ? parse_integer<std::uint32_t>(*next) : std::nullopt;
      if (!parsed.has_value() || *parsed == 0) {
        return std::nullopt;
      }
      args.max_samples_per_search = *parsed;
    } else if (argument == "--sampling-seed") {
      const auto next = value();
      const auto parsed = next.has_value() ? parse_integer<std::uint64_t>(*next) : std::nullopt;
      if (!parsed.has_value()) {
        return std::nullopt;
      }
      args.sampling_seed = *parsed;
    } else if (argument == "--position-limit") {
      const auto next = value();
      const auto parsed = next.has_value() ? parse_integer<std::size_t>(*next) : std::nullopt;
      if (!parsed.has_value()) {
        return std::nullopt;
      }
      args.position_limit = *parsed;
    } else if (argument == "--partition-count") {
      const auto next = value();
      const auto parsed = next.has_value() ? parse_integer<std::uint32_t>(*next) : std::nullopt;
      if (!parsed.has_value() || *parsed == 0) {
        return std::nullopt;
      }
      args.partition_count = *parsed;
    } else if (argument == "--partition-index") {
      const auto next = value();
      const auto parsed = next.has_value() ? parse_integer<std::uint32_t>(*next) : std::nullopt;
      if (!parsed.has_value()) {
        return std::nullopt;
      }
      args.partition_index = *parsed;
    } else if (argument == "--root-phase") {
      const auto next = value();
      const auto parsed = next.has_value() ? parse_integer<int>(*next) : std::nullopt;
      if (!parsed.has_value() || *parsed < 0 || *parsed > 12) {
        return std::nullopt;
      }
      args.root_phase = *parsed;
    } else {
      return std::nullopt;
    }
  }

  bool duplicate_pair = false;
  for (std::size_t index = 0; index < args.depth_pairs.size(); ++index) {
    duplicate_pair =
        duplicate_pair || std::find(args.depth_pairs.begin(),
                                    args.depth_pairs.begin() + static_cast<std::ptrdiff_t>(index),
                                    args.depth_pairs[index]) !=
                              args.depth_pairs.begin() + static_cast<std::ptrdiff_t>(index);
  }
  if (args.artifact_manifest.empty() || args.positions_path.empty() || args.output_path.empty() ||
      args.repository_sha.empty() || args.search_config_id.empty() || args.depth_pairs.empty() ||
      duplicate_pair || args.partition_index >= args.partition_count) {
    return std::nullopt;
  }
  return args;
}

std::uint64_t fnv1a64(std::string_view text) noexcept {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char value : text) {
    hash ^= value;
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::uint8_t phase_for(board::Position position) noexcept {
  const int occupied = std::popcount(position.player | position.opponent);
  return static_cast<std::uint8_t>(std::min(12, std::max(0, occupied - 4) * 13 / 60));
}

std::optional<board::Position> position_from_relative_board(std::string_view text) noexcept {
  if (text.size() != board::kSquareCount) {
    return std::nullopt;
  }
  board::Bitboard player = 0;
  board::Bitboard opponent = 0;
  for (std::size_t index = 0; index < text.size(); ++index) {
    const board::Bitboard square = board::bit(board::square_from_index(static_cast<int>(index)));
    if (text[index] == 'X') {
      player |= square;
    } else if (text[index] == 'O') {
      opponent |= square;
    } else if (text[index] != '-') {
      return std::nullopt;
    }
  }
  const board::Position position{
      .player = player,
      .opponent = opponent,
      .side_to_move = board::Color::black,
  };
  return board::is_valid(position) ? std::optional{position} : std::nullopt;
}

void write_json_string(std::ostream& output, std::string_view text) {
  output << '"';
  for (const unsigned char value : text) {
    switch (value) {
    case '"':
      output << "\\\"";
      break;
    case '\\':
      output << "\\\\";
      break;
    case '\b':
      output << "\\b";
      break;
    case '\f':
      output << "\\f";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
    default:
      if (value < 0x20U) {
        output << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<int>(value) << std::dec << std::setfill(' ');
      } else {
        output << static_cast<char>(value);
      }
      break;
    }
  }
  output << '"';
}

std::string_view bound_name(search::BoundType bound) noexcept {
  switch (bound) {
  case search::BoundType::upper:
    return "upper";
  case search::BoundType::exact:
    return "exact";
  case search::BoundType::lower:
    return "lower";
  }
  return "unknown";
}

std::string_view role_name(search::ShadowSearchRole role) noexcept {
  switch (role) {
  case search::ShadowSearchRole::pv:
    return "pv";
  case search::ShadowSearchRole::non_pv_scout:
    return "non_pv_scout";
  case search::ShadowSearchRole::other:
    return "other";
  }
  return "unknown";
}

std::string_view node_type_name(search::ShadowNodeType type) noexcept {
  switch (type) {
  case search::ShadowNodeType::pv:
    return "pv";
  case search::ShadowNodeType::cut:
    return "cut";
  case search::ShadowNodeType::all:
    return "all";
  }
  return "unknown";
}

std::string_view window_result_name(search::ShadowWindowResult result) noexcept {
  switch (result) {
  case search::ShadowWindowResult::fail_low:
    return "fail_low";
  case search::ShadowWindowResult::exact:
    return "exact";
  case search::ShadowWindowResult::fail_high:
    return "fail_high";
  }
  return "unknown";
}

std::string_view search_mode_name(search::SearchMode mode) noexcept {
  switch (mode) {
  case search::SearchMode::move:
    return "move";
  case search::SearchMode::analyze:
    return "analyze";
  case search::SearchMode::exact_score:
    return "exact_score";
  case search::SearchMode::win_loss_draw:
    return "win_loss_draw";
  }
  return "unknown";
}

void write_move(std::ostream& output, const std::optional<board::Move>& move) {
  if (!move.has_value()) {
    output << "null";
    return;
  }
  if (move->kind == board::MoveKind::pass) {
    write_json_string(output, "pass");
    return;
  }
  std::string coordinate;
  coordinate.push_back(static_cast<char>('a' + board::file_of(move->square)));
  coordinate.push_back(static_cast<char>('1' + board::rank_of(move->square)));
  write_json_string(output, coordinate);
}

void write_bool(std::ostream& output, bool value) {
  output << (value ? "true" : "false");
}

class JsonlSink final : public search::ShadowCalibrationSink {
public:
  explicit JsonlSink(std::ostream* output) : output_(output) {}

  void record(const search::ShadowCalibrationSample& sample) noexcept override {
    if (failed_) {
      return;
    }
    if (sample.same_deep_pair_index == 0) {
      try {
        skip_group_ = !seen_positions_.insert(sample.canonical_position_hash).second;
      } catch (...) {
        failed_ = true;
        return;
      }
      duplicate_groups_ += skip_group_ ? 1U : 0U;
    }
    if (skip_group_) {
      return;
    }
    std::ostream& output = *output_;
    output << '{';
    output << "\"schema_version\":" << sample.schema_version << ",\"repo_sha\":";
    write_json_string(output, sample.repo_sha);
    output << ",\"search_config_id\":";
    write_json_string(output, sample.search_config_id);
    output << ",\"evaluator_id\":";
    write_json_string(output, sample.evaluator_id);
    output << ",\"artifact_id\":";
    write_json_string(output, sample.artifact_id);
    output << ",\"collection_config_id\":";
    write_json_string(output, sample.collection_config_id);
    std::ostringstream hash;
    hash << std::hex << std::setw(16) << std::setfill('0') << sample.canonical_position_hash;
    output << ",\"canonical_position_hash\":";
    write_json_string(output, hash.str());
    output << ",\"phase\":" << static_cast<int>(sample.phase)
           << ",\"occupied_count\":" << static_cast<int>(sample.occupied_count)
           << ",\"empties\":" << static_cast<int>(sample.empties)
           << ",\"ply\":" << static_cast<int>(sample.ply) << ",\"search_role\":";
    write_json_string(output, role_name(sample.search_role));
    output << ",\"node_type\":";
    write_json_string(output, node_type_name(sample.node_type));
    output << ",\"pv_node\":";
    write_bool(output, sample.pv_node);
    output << ",\"cut_node\":";
    write_bool(output, sample.cut_node);
    output << ",\"all_node\":";
    write_bool(output, sample.all_node);
    output << ",\"collection_pair_index\":" << sample.collection_pair_index
           << ",\"collection_pair_count\":" << sample.collection_pair_count
           << ",\"same_deep_pair_index\":" << sample.same_deep_pair_index
           << ",\"same_deep_pair_count\":" << sample.same_deep_pair_count
           << ",\"deep_depth\":" << sample.deep_depth
           << ",\"shallow_depth\":" << sample.shallow_depth
           << ",\"official_alpha\":" << sample.official_alpha
           << ",\"official_beta\":" << sample.official_beta
           << ",\"official_deep_score\":" << sample.official_deep_score
           << ",\"official_deep_bound\":";
    write_json_string(output, bound_name(sample.official_deep_bound));
    output << ",\"shallow_verification_score\":" << sample.shallow_verification_score
           << ",\"deep_verification_score\":" << sample.deep_verification_score
           << ",\"shallow_verification_bound\":";
    write_json_string(output, bound_name(sample.shallow_verification_bound));
    output << ",\"deep_verification_bound\":";
    write_json_string(output, bound_name(sample.deep_verification_bound));
    output << ",\"shallow_verification_best_move\":";
    write_move(output, sample.shallow_verification_best_move);
    output << ",\"deep_verification_best_move\":";
    write_move(output, sample.deep_verification_best_move);
    output << ",\"verification_best_move_agreement\":";
    write_bool(output, sample.verification_best_move_agreement);
    output << ",\"pass_state\":";
    write_bool(output, sample.pass_state);
    output << ",\"terminal_state\":";
    write_bool(output, sample.terminal_state);
    output << ",\"search_mode\":";
    write_json_string(output, search_mode_name(sample.search_mode));
    output << ",\"exact_handoff_enabled\":";
    write_bool(output, sample.exact_handoff_enabled);
    output << ",\"exact_handoff_threshold\":" << static_cast<int>(sample.exact_handoff_threshold)
           << ",\"exact_handoff_distance\":" << static_cast<int>(sample.exact_handoff_distance)
           << ",\"exact_handoff_eligible\":";
    write_bool(output, sample.exact_handoff_eligible);
    output << ",\"actual_official_deep_result\":";
    write_json_string(output, window_result_name(sample.actual_official_deep_result));
    output << ",\"hypothetical_cut_high\":";
    write_bool(output, sample.hypothetical_cut_high);
    output << ",\"hypothetical_cut_low\":";
    write_bool(output, sample.hypothetical_cut_low);
    output << ",\"false_cut_high_candidate\":";
    write_bool(output, sample.false_cut_high_candidate);
    output << ",\"false_cut_low_candidate\":";
    write_bool(output, sample.false_cut_low_candidate);
    output << ",\"sampling_seed\":" << sample.sampling_seed << ",\"search_identity\":";
    write_json_string(output, sample.search_identity);
    output << "}\n";
    ++samples_;
    failed_ = !output.good();
  }

  [[nodiscard]] bool failed() const noexcept {
    return failed_;
  }

  [[nodiscard]] std::uint64_t samples() const noexcept {
    return samples_;
  }

  [[nodiscard]] std::uint64_t duplicate_groups() const noexcept {
    return duplicate_groups_;
  }

private:
  std::ostream* output_;
  std::unordered_set<std::uint64_t> seen_positions_;
  std::uint64_t samples_ = 0;
  std::uint64_t duplicate_groups_ = 0;
  bool skip_group_ = false;
  bool failed_ = false;
};

struct InputLayout {
  std::size_t identity_index = 0;
  std::size_t board_index = 0;
  bool side_to_move_relative_board = true;
};

std::optional<InputLayout> input_layout(std::string_view header) {
  const std::vector<std::string_view> fields = split(header, '\t');
  const auto identity = std::find(fields.begin(), fields.end(), "position_id");
  const auto fallback_identity = std::find(fields.begin(), fields.end(), "id");
  const auto board = std::find(fields.begin(), fields.end(), "board_a1_to_h8");
  const auto serialized_position = std::find(fields.begin(), fields.end(), "position");
  if ((board == fields.end() && serialized_position == fields.end()) ||
      (identity == fields.end() && fallback_identity == fields.end())) {
    return std::nullopt;
  }
  const auto selected_identity = identity == fields.end() ? fallback_identity : identity;
  const auto selected_board = board == fields.end() ? serialized_position : board;
  return InputLayout{
      .identity_index = static_cast<std::size_t>(selected_identity - fields.begin()),
      .board_index = static_cast<std::size_t>(selected_board - fields.begin()),
      .side_to_move_relative_board = board != fields.end(),
  };
}

search::SearchOptions search_options(const Args& args, JsonlSink* sink,
                                     const evaluation::LoadedPatternArtifact& artifact) {
  return search::SearchOptions{
      .midgame =
          search::MidgameSearchOptions{
              .use_pvs = true,
              .use_aspiration = true,
              .use_iid = true,
              .use_midgame_tt = true,
          },
      .ordering =
          search::MoveOrderingOptions{
              .use_tt_best_move_ordering = true,
              .use_history = true,
              .use_killers = true,
              .use_midgame_mobility_ordering = true,
              .use_endgame_parity_ordering = true,
          },
      .endgame =
          search::EndgameSearchOptions{
              .exact_endgame = args.exact_endgame_empties != 0,
              .use_endgame_tt = true,
              .endgame_exact_empties = args.exact_endgame_empties,
              .endgame_wld_empties = 0,
          },
      .reporting = search::SearchReportingOptions{.multi_pv = 1},
      .selective =
          search::SelectiveSearchOptionsV1{
              .enable_shadow_calibration = true,
              .sample_rate = args.sample_rate,
              .max_samples_per_search = args.max_samples_per_search,
              .ordered_depth_pairs = args.depth_pairs,
              .include_pv_nodes = false,
              .include_pass_nodes = false,
              .include_near_exact_nodes = false,
              .sampling_seed = args.sampling_seed,
              .repo_sha = args.repository_sha,
              .search_config_id = args.search_config_id,
              .evaluator_id = artifact.pattern_set_id,
              .artifact_id = artifact.artifact_id,
              .sink = sink,
          },
      .mode = search::SearchMode::move,
  };
}

int run(const Args& args) {
  evaluation::PatternArtifactLoadResult load_result =
      evaluation::load_pattern_artifact(args.artifact_manifest);
  if (!load_result.ok()) {
    std::cerr << load_result.error << '\n';
    return 2;
  }
  evaluation::LoadedPatternArtifact artifact = std::move(*load_result.artifact);
  evaluation::PhaseAwareEvaluator evaluator(
      std::move(artifact.weights), std::move(artifact.feature_set), artifact.trained_phases,
      artifact.fallback_additive_through_phase);

  std::ifstream input(args.positions_path);
  if (!input) {
    std::cerr << "failed to open position corpus\n";
    return 2;
  }
  std::ofstream output(args.output_path, std::ios::trunc);
  if (!output) {
    std::cerr << "failed to open JSONL output\n";
    return 2;
  }
  JsonlSink sink(&output);
  const search::SearchOptions options = search_options(args, &sink, artifact);

  std::string line;
  if (!std::getline(input, line)) {
    std::cerr << "position corpus is empty\n";
    return 2;
  }
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  const std::optional<InputLayout> layout = input_layout(line);
  if (!layout.has_value()) {
    std::cerr << "position corpus requires position_id (or id) and board_a1_to_h8 columns\n";
    return 2;
  }

  std::size_t input_rows = 0;
  std::size_t selected_rows = 0;
  std::size_t stopped_searches = 0;
  search::NodeCount official_nodes = 0;
  search::NodeCount shadow_nodes = 0;
  while (std::getline(input, line)) {
    ++input_rows;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const std::vector<std::string_view> fields = split(line, '\t');
    if (fields.size() <= std::max(layout->identity_index, layout->board_index)) {
      std::cerr << "malformed position row " << (input_rows + 1) << '\n';
      return 2;
    }
    const std::string_view identity = fields[layout->identity_index];
    if (fnv1a64(identity) % args.partition_count != args.partition_index) {
      continue;
    }
    const std::optional<board::Position> position =
        layout->side_to_move_relative_board
            ? position_from_relative_board(fields[layout->board_index])
            : board::parse_position(fields[layout->board_index]);
    if (!position.has_value()) {
      std::cerr << "invalid side-to-move-relative board at row " << (input_rows + 1) << '\n';
      return 2;
    }
    if (args.root_phase >= 0 && phase_for(*position) != args.root_phase) {
      continue;
    }
    if (args.position_limit != 0 && selected_rows >= args.position_limit) {
      break;
    }

    search::SearchSession session;
    const search::SearchResult result =
        search::search_fixed_depth(session, *position, evaluator, args.depth, options);
    ++selected_rows;
    stopped_searches += result.stopped ? 1U : 0U;
    official_nodes += result.nodes;
    shadow_nodes += result.shadow_calibration.shadow_shallow_nodes +
                    result.shadow_calibration.shadow_deep_verification_nodes;
    if (sink.failed()) {
      std::cerr << "failed while writing JSONL output\n";
      return 2;
    }
  }

  output.flush();
  if (!output.good()) {
    std::cerr << "failed to finalize JSONL output\n";
    return 2;
  }
  std::cout << "{\"input_rows\":" << input_rows << ",\"selected_rows\":" << selected_rows
            << ",\"samples\":" << sink.samples()
            << ",\"duplicate_sample_groups\":" << sink.duplicate_groups()
            << ",\"stopped_searches\":" << stopped_searches
            << ",\"official_nodes\":" << official_nodes << ",\"shadow_nodes\":" << shadow_nodes
            << "}\n";
  return selected_rows == 0 || sink.samples() == 0 || stopped_searches != 0 ? 2 : 0;
}

} // namespace

int main(int argc, char** argv) {
  const std::optional<Args> args = parse_args(argc, argv);
  if (!args.has_value()) {
    std::cerr
        << "usage: vibe-othello-collect-shadow-samples --artifact-manifest PATH --positions PATH "
           "--output PATH --repository-sha SHA --search-config-id ID --depth-pair DEEP:SHALLOW "
           "[--depth N] [--exact-endgame-empties N] [--sample-rate N] "
           "[--max-samples-per-search N] [--sampling-seed N] [--position-limit N] "
           "[--partition-count N --partition-index N] [--root-phase N]\n";
    return 2;
  }
  try {
    return run(*args);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
  }
}
