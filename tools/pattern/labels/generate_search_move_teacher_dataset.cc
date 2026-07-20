#include "normalized_tsv.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/evaluation/early_midgame_heuristic_evaluator.h"
#include "vibe_othello/evaluation/pattern_artifact.h"
#include "vibe_othello/evaluation/phase_aware_evaluator.h"
#include "vibe_othello/search/search.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace board_core = vibe_othello::board_core;
namespace evaluation = vibe_othello::evaluation;
namespace search = vibe_othello::search;

using vibe_othello::tools::pattern::kNormalizedHeaderV2;
using vibe_othello::tools::pattern::parse_int;
using vibe_othello::tools::pattern::parse_u64;
using vibe_othello::tools::pattern::split_tabs;
using vibe_othello::tools::pattern::trim_trailing_cr;

constexpr std::string_view kMoveTeacherHeaderV3 =
    "root_board_id\troot_record_id\troot_split\troot_phase\troot_empty_count\tmove\tchild_"
    "board_id\tchild_board_a1_to_h8\tchild_empty_count\tchild_phase\troot_move_score_side_to_"
    "move\tchild_label_score_side_to_move\tchild_baseline_score_side_to_move\tis_best_move\tbest_"
    "move_tie_count\tmove_rank\tbest_score_margin\tteacher_kind\tteacher_source\tteacher_artifact_"
    "id\tteacher_artifact_checksum\tteacher_depth\tteacher_nodes\tteacher_search_config_id";
constexpr std::string_view kTeacherKind = "artifact_search";
constexpr std::string_view kFullCoverageTeacherSource = "search-move-teacher-v1";
constexpr std::string_view kExplicitPhaseAwareTeacherSource =
    "search-move-teacher-v2-explicit-phase-aware";
constexpr std::string_view kChildLabelKind = "teacher_search_final_disc_diff";

enum class SearchPreset {
  basic,
  full,
};

enum class TeacherCoveragePolicy {
  require_all,
  explicit_phase_aware,
};

struct Args {
  std::string normalized_tsv_path;
  std::string teacher_manifest_path;
  std::string teacher_weights_path;
  std::string move_teacher_out_path;
  std::string child_normalized_out_path;
  std::string report_out_path;
  search::Depth max_depth = 0;
  search::NodeCount max_nodes = 0;
  std::chrono::milliseconds max_time{0};
  SearchPreset search_preset = SearchPreset::full;
  TeacherCoveragePolicy teacher_coverage_policy = TeacherCoveragePolicy::require_all;
  std::uint8_t exact_endgame_empties = 0;
  int min_phase = 0;
  int max_phase = 9;
  std::size_t progress_every = 0;
  bool saw_max_depth = false;
  bool saw_max_nodes = false;
  bool saw_max_time = false;
  bool saw_preset = false;
  bool saw_exact_threshold = false;
};

struct RootRow {
  std::string record_id;
  std::string position_id;
  std::string game_group_id;
  std::string board_id;
  std::string split;
  std::string board;
  int occupied_count = 0;
  int phase = 0;
  int empty_count = 0;
};

struct MoveRow {
  std::string root_board_id;
  std::string root_record_id;
  std::string root_split;
  int root_phase = 0;
  int root_empty_count = 0;
  std::string move;
  std::string child_board_id;
  std::string child_board;
  int child_empty_count = 0;
  int child_phase = 0;
  int child_occupied_count = 0;
  int child_player_disc_count = 0;
  int child_opponent_disc_count = 0;
  int root_move_score_side_to_move = 0;
  int child_label_score_side_to_move = 0;
  int child_baseline_score_side_to_move = 0;
  bool is_best_move = false;
  int best_move_tie_count = 0;
  int move_rank = 0;
  int best_score_margin = 0;
  int teacher_depth = 0;
  search::NodeCount teacher_nodes = 0;
  std::string root_game_group_id;
};

struct SearchConfig {
  SearchPreset preset = SearchPreset::full;
  search::SearchLimits limits;
  search::SearchOptions options;
  std::uint8_t exact_endgame_empties = 0;
  std::string id;
};

class DiscDiffClampedEvaluator final : public search::Evaluator {
public:
  explicit DiscDiffClampedEvaluator(std::unique_ptr<search::Evaluator> evaluator)
      : evaluator_(std::move(evaluator)) {}

  search::Score evaluate(const board_core::Position& position) const noexcept override {
    return std::clamp(evaluator_->evaluate(position),
                      -static_cast<search::Score>(board_core::kSquareCount),
                      static_cast<search::Score>(board_core::kSquareCount));
  }

  std::uint64_t transposition_table_revision() const noexcept override {
    return evaluator_->transposition_table_revision();
  }

private:
  std::unique_ptr<search::Evaluator> evaluator_;
};

struct Teacher {
  std::string artifact_id;
  std::string artifact_checksum;
  std::string pattern_set_id;
  std::string source;
  std::string coverage_policy;
  std::vector<std::uint8_t> trained_phases;
  std::vector<std::uint8_t> fallback_phases;
  std::optional<std::uint8_t> fallback_additive_through_phase;
  std::string score_normalization;
  std::unique_ptr<search::Evaluator> evaluator;
};

struct Report {
  std::size_t input_rows = 0;
  std::size_t phase_filtered_rows = 0;
  std::size_t duplicate_board_rows = 0;
  std::size_t selected_roots = 0;
  std::size_t completed_roots = 0;
  std::size_t rejected_roots = 0;
  std::size_t terminal_roots = 0;
  std::size_t roots_with_pass_move = 0;
  std::size_t move_rows = 0;
  std::size_t child_normalized_rows = 0;
  std::size_t duplicate_child_normalized_rows = 0;
  std::size_t child_split_collisions = 0;
  std::size_t accepted_node_limited_children = 0;
  std::size_t roots_with_mixed_teacher_depth = 0;
  std::uint64_t teacher_nodes_sum = 0;
  std::string rejected_root_id;
  std::string rejection_reason;
  std::uint64_t output_checksum = 14695981039346656037ull;
  double wall_time_sec = 0.0;
};

int phase_for_occupied_count(int occupied_count) noexcept {
  return std::min(12, ((occupied_count - 4) * 13) / 60);
}

std::uint64_t fnv1a64_update(std::uint64_t hash, std::string_view text) noexcept {
  for (const char character : text) {
    hash ^= static_cast<unsigned char>(character);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string checksum_string(std::uint64_t checksum) {
  std::ostringstream output;
  output << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << checksum;
  return output.str();
}

std::uint32_t sha256_rotr(std::uint32_t value, int shift) noexcept {
  return (value >> shift) | (value << (32 - shift));
}

std::string sha256_hex(std::string_view text) {
  constexpr std::array<std::uint32_t, 64> kRoundConstants{
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
      0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
      0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
      0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
      0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
      0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
      0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
      0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
      0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
      0xc67178f2u,
  };
  std::vector<unsigned char> bytes(text.begin(), text.end());
  const std::uint64_t bit_length = static_cast<std::uint64_t>(bytes.size()) * 8ull;
  bytes.push_back(0x80u);
  while (bytes.size() % 64 != 56) {
    bytes.push_back(0u);
  }
  for (int shift = 56; shift >= 0; shift -= 8) {
    bytes.push_back(static_cast<unsigned char>((bit_length >> shift) & 0xffu));
  }
  std::array<std::uint32_t, 8> hash{
      0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
  };
  for (std::size_t chunk = 0; chunk < bytes.size(); chunk += 64) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
      const std::size_t offset = chunk + index * 4;
      words[index] = (static_cast<std::uint32_t>(bytes[offset]) << 24) |
                     (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
                     (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
                     static_cast<std::uint32_t>(bytes[offset + 3]);
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
      const std::uint32_t s0 = sha256_rotr(words[index - 15], 7) ^
                               sha256_rotr(words[index - 15], 18) ^ (words[index - 15] >> 3);
      const std::uint32_t s1 = sha256_rotr(words[index - 2], 17) ^
                               sha256_rotr(words[index - 2], 19) ^ (words[index - 2] >> 10);
      words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }
    std::uint32_t a = hash[0];
    std::uint32_t b = hash[1];
    std::uint32_t c = hash[2];
    std::uint32_t d = hash[3];
    std::uint32_t e = hash[4];
    std::uint32_t f = hash[5];
    std::uint32_t g = hash[6];
    std::uint32_t h = hash[7];
    for (std::size_t index = 0; index < words.size(); ++index) {
      const std::uint32_t s1 = sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^ sha256_rotr(e, 25);
      const std::uint32_t choice = (e & f) ^ ((~e) & g);
      const std::uint32_t temp1 = h + s1 + choice + kRoundConstants[index] + words[index];
      const std::uint32_t s0 = sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^ sha256_rotr(a, 22);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temp2 = s0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
  }
  std::ostringstream output;
  output << std::hex << std::nouppercase << std::setfill('0');
  for (const std::uint32_t value : hash) {
    output << std::setw(8) << value;
  }
  return output.str();
}

std::string canonical_board_id(std::string_view board) {
  return "board-" + sha256_hex("board-v1\t" + std::string(board)).substr(0, 16);
}

void mix_output_checksum(std::string_view line, Report* report) noexcept {
  report->output_checksum = fnv1a64_update(report->output_checksum, line);
  report->output_checksum = fnv1a64_update(report->output_checksum, "\n");
}

std::string json_escape(std::string_view text) {
  std::string output;
  for (const char character : text) {
    switch (character) {
    case '\\':
      output += "\\\\";
      break;
    case '"':
      output += "\\\"";
      break;
    case '\n':
      output += "\\n";
      break;
    case '\r':
      output += "\\r";
      break;
    case '\t':
      output += "\\t";
      break;
    default:
      output.push_back(character);
      break;
    }
  }
  return output;
}

std::string report_path(std::string_view path_text) {
  const std::filesystem::path path{std::string(path_text)};
  return path.is_absolute() ? path.filename().string() : path.lexically_normal().string();
}

std::optional<board_core::Position> position_from_relative_board(std::string_view board) noexcept {
  if (board.size() != board_core::kSquareCount) {
    return std::nullopt;
  }
  board_core::Bitboard player = 0;
  board_core::Bitboard opponent = 0;
  for (std::size_t index = 0; index < board.size(); ++index) {
    const board_core::Bitboard bit =
        board_core::bit(board_core::square_from_index(static_cast<int>(index)));
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
  return board_core::is_valid(position) ? std::optional<board_core::Position>{position}
                                        : std::nullopt;
}

std::string relative_board_from_position(board_core::Position position) {
  std::string board(board_core::kSquareCount, '-');
  for (int index = 0; index < board_core::kSquareCount; ++index) {
    const board_core::Bitboard bit = board_core::bit(board_core::square_from_index(index));
    if ((position.player & bit) != 0) {
      board[static_cast<std::size_t>(index)] = 'X';
    } else if ((position.opponent & bit) != 0) {
      board[static_cast<std::size_t>(index)] = 'O';
    }
  }
  return board;
}

bool validate_board_counts(std::string_view board, int occupied_count, int player_count,
                           int opponent_count, int empty_count) {
  if (!position_from_relative_board(board).has_value()) {
    return false;
  }
  const int actual_player = static_cast<int>(std::count(board.begin(), board.end(), 'X'));
  const int actual_opponent = static_cast<int>(std::count(board.begin(), board.end(), 'O'));
  const int actual_empty = static_cast<int>(std::count(board.begin(), board.end(), '-'));
  return actual_player == player_count && actual_opponent == opponent_count &&
         actual_empty == empty_count && actual_player + actual_opponent == occupied_count &&
         occupied_count + empty_count == board_core::kSquareCount;
}

bool parse_normalized_row(std::string_view line, RootRow* row, std::string* error) {
  const std::vector<std::string_view> fields = split_tabs(trim_trailing_cr(line));
  if (fields.size() != 17) {
    *error = "expected 17 TSV fields for normalized schema v2";
    return false;
  }
  if (fields[0].empty() || fields[1].empty() || fields[2].empty() || fields[3].empty() ||
      fields[4].empty() || fields[5].empty()) {
    *error = "normalized identity fields must be non-empty";
    return false;
  }
  if (fields[6] != "train" && fields[6] != "validation" && fields[6] != "test") {
    *error = "split must be train, validation, or test";
    return false;
  }
  if (fields[10] != "side_to_move") {
    *error = "label_perspective must be side_to_move";
    return false;
  }
  const std::optional<int> occupied = parse_int(fields[12]);
  const std::optional<int> phase = parse_int(fields[13]);
  const std::optional<int> player = parse_int(fields[14]);
  const std::optional<int> opponent = parse_int(fields[15]);
  const std::optional<int> empty = parse_int(fields[16]);
  if (!occupied.has_value() || !phase.has_value() || !player.has_value() || !opponent.has_value() ||
      !empty.has_value()) {
    *error = "count fields must be integers";
    return false;
  }
  if (*occupied < 4 || *occupied > 64 || *empty < 0 || *empty > 60 || *phase < 0 || *phase > 12 ||
      *phase != phase_for_occupied_count(*occupied) ||
      !validate_board_counts(fields[7], *occupied, *player, *opponent, *empty)) {
    *error = "board or count fields violate normalized schema v2";
    return false;
  }
  *row = RootRow{
      .record_id = std::string(fields[0]),
      .position_id = std::string(fields[1]),
      .game_group_id = std::string(fields[2]),
      .board_id = std::string(fields[3]),
      .split = std::string(fields[6]),
      .board = std::string(fields[7]),
      .occupied_count = *occupied,
      .phase = *phase,
      .empty_count = *empty,
  };
  return true;
}

bool load_roots(const Args& args, std::vector<RootRow>* roots, Report* report) {
  std::ifstream input(args.normalized_tsv_path);
  if (!input) {
    std::cerr << "cannot read normalized TSV: " << args.normalized_tsv_path << '\n';
    return false;
  }
  std::string line;
  if (!std::getline(input, line) || trim_trailing_cr(line) != kNormalizedHeaderV2) {
    std::cerr << "normalized TSV must use schema v2 header\n";
    return false;
  }
  std::map<std::string, std::string> contents_by_id;
  std::map<std::string, RootRow> selected;
  int line_number = 1;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty()) {
      continue;
    }
    ++report->input_rows;
    RootRow row;
    std::string error;
    if (!parse_normalized_row(line, &row, &error)) {
      std::cerr << "line " << line_number << ": " << error << '\n';
      return false;
    }
    const auto [contents_it, inserted] = contents_by_id.emplace(row.board_id, row.board);
    if (!inserted) {
      ++report->duplicate_board_rows;
      if (contents_it->second != row.board) {
        std::cerr << "line " << line_number << ": board_id has conflicting board contents\n";
        return false;
      }
    }
    if (row.phase < args.min_phase || row.phase > args.max_phase) {
      ++report->phase_filtered_rows;
      continue;
    }
    selected.try_emplace(row.board_id, std::move(row));
  }
  roots->reserve(selected.size());
  for (auto& [board_id, root] : selected) {
    (void)board_id;
    roots->push_back(std::move(root));
  }
  report->selected_roots = roots->size();
  return !roots->empty();
}

std::string move_to_string(board_core::Move move) {
  if (move.kind == board_core::MoveKind::pass) {
    return "pass";
  }
  return std::string{static_cast<char>('a' + board_core::file_of(move.square)),
                     static_cast<char>('1' + board_core::rank_of(move.square))};
}

std::vector<board_core::Move> legal_root_moves(board_core::Position position, bool* terminal) {
  *terminal = false;
  const board_core::Bitboard legal = board_core::legal_moves(position);
  std::vector<board_core::Move> moves;
  for (int index = 0; index < board_core::kSquareCount; ++index) {
    const board_core::Square square = board_core::square_from_index(index);
    if ((legal & board_core::bit(square)) != 0) {
      moves.push_back(board_core::make_move(square));
    }
  }
  if (!moves.empty()) {
    return moves;
  }
  if (board_core::is_terminal(position)) {
    *terminal = true;
    return moves;
  }
  board_core::MoveDelta delta{};
  if (board_core::make_move_delta(position, board_core::make_pass(), &delta)) {
    moves.push_back(board_core::make_pass());
  }
  return moves;
}

std::string preset_name(SearchPreset preset) {
  return preset == SearchPreset::basic ? "basic" : "full";
}

std::string_view coverage_policy_name(TeacherCoveragePolicy policy) {
  return policy == TeacherCoveragePolicy::require_all ? "require-all" : "explicit-phase-aware";
}

SearchConfig make_search_config(const Args& args) {
  const bool full = args.search_preset == SearchPreset::full;
  SearchConfig config{
      .preset = args.search_preset,
      .limits =
          search::SearchLimits{
              .max_depth = args.max_depth,
              .max_nodes = args.max_nodes,
              .max_time = args.max_time,
          },
      .options =
          search::SearchOptions{
              .midgame =
                  search::MidgameSearchOptions{
                      .use_pvs = full,
                      .use_aspiration = full,
                      .use_iid = full,
                      .use_midgame_tt = full,
                  },
              .ordering =
                  search::MoveOrderingOptions{
                      .use_tt_best_move_ordering = full,
                      .use_history = full,
                      .use_killers = full,
                      .use_endgame_parity_ordering = true,
                  },
              .endgame =
                  search::EndgameSearchOptions{
                      .exact_endgame = args.exact_endgame_empties != 0,
                      .use_endgame_tt = full,
                      .endgame_exact_empties = args.exact_endgame_empties,
                      .endgame_wld_empties = 0,
                  },
              .reporting = search::SearchReportingOptions{},
              .mode = search::SearchMode::move,
          },
      .exact_endgame_empties = args.exact_endgame_empties,
  };
  std::ostringstream canonical;
  canonical << "search-move-teacher-config-v1\n"
            << preset_name(config.preset) << '\n'
            << config.limits.max_depth << '\n'
            << config.limits.max_nodes << '\n'
            << config.limits.max_time.count() << '\n'
            << static_cast<int>(config.exact_endgame_empties) << '\n'
            << full << '\n'
            << coverage_policy_name(args.teacher_coverage_policy);
  config.id =
      "fnv1a64:" + checksum_string(fnv1a64_update(14695981039346656037ull, canonical.str()));
  return config;
}

std::optional<Teacher> load_teacher(const Args& args) {
  evaluation::PatternArtifactLoadResult result =
      evaluation::load_pattern_artifact(args.teacher_manifest_path, args.teacher_weights_path);
  if (!result.ok()) {
    std::cerr << result.error << '\n';
    return std::nullopt;
  }
  try {
    evaluation::LoadedPatternArtifact artifact = std::move(*result.artifact);
    if (!artifact.trained_phases.has_value()) {
      std::cerr << "teacher artifact must report trained_phases; legacy implicit coverage "
                   "cannot be used for teacher generation\n";
      return std::nullopt;
    }
    if (args.teacher_coverage_policy == TeacherCoveragePolicy::require_all) {
      if (artifact.trained_phases->size() != 13) {
        std::cerr << "teacher artifact must explicitly cover every phase 0..12; use "
                     "--teacher-coverage-policy explicit-phase-aware only when fallback "
                     "teaching is an intentional, provenance-reviewed bootstrap\n";
        return std::nullopt;
      }
      for (std::size_t index = 0; index < artifact.trained_phases->size(); ++index) {
        if ((*artifact.trained_phases)[index] != index) {
          std::cerr << "teacher artifact trained_phases must be exactly [0, ..., 12]\n";
          return std::nullopt;
        }
      }
    }
    std::vector<std::uint8_t> fallback_phases;
    for (int phase = 0; phase < 13; ++phase) {
      if (std::find(artifact.trained_phases->begin(), artifact.trained_phases->end(),
                    static_cast<std::uint8_t>(phase)) == artifact.trained_phases->end()) {
        fallback_phases.push_back(static_cast<std::uint8_t>(phase));
      }
    }
    if (args.teacher_coverage_policy == TeacherCoveragePolicy::require_all &&
        !fallback_phases.empty()) {
      std::cerr << "teacher artifact trained_phases must be exactly [0, ..., 12]\n";
      return std::nullopt;
    }
    std::vector<std::uint8_t> trained_phases = *artifact.trained_phases;
    const std::optional<std::uint8_t> fallback_additive_through_phase =
        artifact.fallback_additive_through_phase;
    std::unique_ptr<search::Evaluator> evaluator =
        std::make_unique<evaluation::PhaseAwareEvaluator>(
            std::move(artifact.weights), std::move(artifact.feature_set),
            std::move(artifact.trained_phases), fallback_additive_through_phase);
    std::string score_normalization = "artifact-disc-diff";
    if (args.teacher_coverage_policy == TeacherCoveragePolicy::explicit_phase_aware ||
        fallback_additive_through_phase.has_value()) {
      evaluator = std::make_unique<DiscDiffClampedEvaluator>(std::move(evaluator));
      score_normalization = "clamp-phase-aware-to-disc-diff-v1";
    }
    Teacher teacher{
        .artifact_id = artifact.artifact_id,
        .artifact_checksum = artifact.weights_checksum,
        .pattern_set_id = artifact.pattern_set_id,
        .source = args.teacher_coverage_policy == TeacherCoveragePolicy::require_all
                      ? std::string{kFullCoverageTeacherSource}
                      : std::string{kExplicitPhaseAwareTeacherSource},
        .coverage_policy = std::string{coverage_policy_name(args.teacher_coverage_policy)},
        .trained_phases = trained_phases,
        .fallback_phases = std::move(fallback_phases),
        .fallback_additive_through_phase = fallback_additive_through_phase,
        .score_normalization = std::move(score_normalization),
        .evaluator = std::move(evaluator),
    };
    return teacher;
  } catch (const std::exception& error) {
    std::cerr << "teacher artifact evaluator construction failed: " << error.what() << '\n';
    return std::nullopt;
  }
}

bool acceptable_result(const search::SearchResult& result, bool terminal_child,
                       const SearchConfig& config, bool* accepted_node_limited,
                       std::string* error) {
  *accepted_node_limited = false;
  if (result.score_kind == search::ScoreKind::unavailable) {
    *error = "search published no completed score";
    return false;
  }
  if (terminal_child) {
    if (result.stopped || result.score_kind != search::ScoreKind::exact_disc_diff) {
      *error = "terminal child did not publish an exact completed score";
      return false;
    }
    return true;
  }
  if (result.exact && !result.stopped) {
    return true;
  }
  if (!result.stopped && result.completed_depth == config.limits.max_depth) {
    return true;
  }
  if (result.stopped && config.limits.max_time.count() == 0 && config.limits.max_nodes != 0 &&
      result.completed_depth > 0) {
    *accepted_node_limited = true;
    return true;
  }
  *error = result.stopped ? "search stopped before the configured deterministic result policy"
                          : "search did not complete requested depth";
  return false;
}

std::optional<MoveRow> evaluate_child(const RootRow& root, board_core::Position position,
                                      board_core::Move move, const Teacher& teacher,
                                      const SearchConfig& config, bool* accepted_node_limited,
                                      std::string* error) {
  board_core::MoveDelta delta{};
  const bool applied = move.kind == board_core::MoveKind::pass
                           ? board_core::apply_pass(&position, &delta)
                           : board_core::apply_move(&position, move, &delta);
  if (!applied) {
    *error = "move application failed";
    return std::nullopt;
  }
  const bool terminal_child = board_core::is_terminal(position);
  const search::SearchResult result =
      search::search_iterative(position, *teacher.evaluator, config.limits, config.options);
  if (!acceptable_result(result, terminal_child, config, accepted_node_limited, error)) {
    return std::nullopt;
  }
  if (result.score < -64 || result.score > 64) {
    *error = "teacher search score is outside normalized disc-diff range: " +
             std::to_string(result.score);
    return std::nullopt;
  }
  const int player_count = std::popcount(position.player);
  const int opponent_count = std::popcount(position.opponent);
  const int occupied_count = player_count + opponent_count;
  const int empty_count = board_core::kSquareCount - occupied_count;
  const evaluation::EarlyMidgameHeuristicEvaluator baseline_evaluator;
  const int child_baseline_score = baseline_evaluator.evaluate(position);
  const std::string move_text = move_to_string(move);
  const std::string child_board = relative_board_from_position(position);
  return MoveRow{
      .root_board_id = root.board_id,
      .root_record_id = root.record_id,
      .root_split = root.split,
      .root_phase = root.phase,
      .root_empty_count = root.empty_count,
      .move = move_text,
      .child_board_id = canonical_board_id(child_board),
      .child_board = child_board,
      .child_empty_count = empty_count,
      .child_phase = phase_for_occupied_count(occupied_count),
      .child_occupied_count = occupied_count,
      .child_player_disc_count = player_count,
      .child_opponent_disc_count = opponent_count,
      .root_move_score_side_to_move = -result.score,
      .child_label_score_side_to_move = result.score,
      .child_baseline_score_side_to_move = child_baseline_score,
      .teacher_depth = static_cast<int>(result.completed_depth),
      .teacher_nodes = result.nodes,
      .root_game_group_id = root.game_group_id,
  };
}

void rank_root_moves(std::vector<MoveRow>* rows) {
  const int best_score =
      std::max_element(rows->begin(), rows->end(), [](const MoveRow& left, const MoveRow& right) {
        return left.root_move_score_side_to_move < right.root_move_score_side_to_move;
      })->root_move_score_side_to_move;
  const int tie_count =
      static_cast<int>(std::count_if(rows->begin(), rows->end(), [&](const MoveRow& row) {
        return row.root_move_score_side_to_move == best_score;
      }));
  std::sort(rows->begin(), rows->end(), [](const MoveRow& left, const MoveRow& right) {
    if (left.root_move_score_side_to_move != right.root_move_score_side_to_move) {
      return left.root_move_score_side_to_move > right.root_move_score_side_to_move;
    }
    return left.move < right.move;
  });
  for (std::size_t index = 0; index < rows->size(); ++index) {
    MoveRow& row = (*rows)[index];
    row.is_best_move = row.root_move_score_side_to_move == best_score;
    row.best_move_tie_count = tie_count;
    row.move_rank = static_cast<int>(index + 1);
    row.best_score_margin = best_score - row.root_move_score_side_to_move;
  }
  std::sort(rows->begin(), rows->end(),
            [](const MoveRow& left, const MoveRow& right) { return left.move < right.move; });
}

std::string move_teacher_line(const MoveRow& row, const Teacher& teacher,
                              const SearchConfig& config) {
  std::ostringstream output;
  output << row.root_board_id << '\t' << row.root_record_id << '\t' << row.root_split << '\t'
         << row.root_phase << '\t' << row.root_empty_count << '\t' << row.move << '\t'
         << row.child_board_id << '\t' << row.child_board << '\t' << row.child_empty_count << '\t'
         << row.child_phase << '\t' << row.root_move_score_side_to_move << '\t'
         << row.child_label_score_side_to_move << '\t' << row.child_baseline_score_side_to_move
         << '\t' << (row.is_best_move ? '1' : '0') << '\t' << row.best_move_tie_count << '\t'
         << row.move_rank << '\t' << row.best_score_margin << '\t' << kTeacherKind << '\t'
         << teacher.source << '\t' << teacher.artifact_id << '\t' << teacher.artifact_checksum
         << '\t' << row.teacher_depth << '\t' << row.teacher_nodes << '\t' << config.id;
  return output.str();
}

std::string child_normalized_line(const MoveRow& row, const Teacher& teacher) {
  std::ostringstream output;
  output << row.child_board_id << '\t' << row.child_board_id << '\t' << row.root_game_group_id
         << '\t' << row.child_board_id << '\t' << row.child_board_id << ":source\t"
         << teacher.source << '\t' << row.root_split << '\t' << row.child_board << '\t'
         << kChildLabelKind << "\tdisc\tside_to_move\t" << row.child_label_score_side_to_move
         << '\t' << row.child_occupied_count << '\t' << row.child_phase << '\t'
         << row.child_player_disc_count << '\t' << row.child_opponent_disc_count << '\t'
         << row.child_empty_count;
  return output.str();
}

bool select_child_normalized_rows(const std::vector<MoveRow>& move_rows,
                                  std::vector<const MoveRow*>* child_rows, Report* report) {
  std::map<std::string, const MoveRow*> by_child_id;
  for (const MoveRow& row : move_rows) {
    const auto [it, inserted] = by_child_id.emplace(row.child_board_id, &row);
    if (inserted) {
      continue;
    }
    ++report->duplicate_child_normalized_rows;
    const MoveRow& existing = *it->second;
    if (existing.child_board != row.child_board) {
      report->rejected_roots = 1;
      report->rejected_root_id = row.root_board_id;
      report->rejection_reason = "canonical child_board_id has conflicting board contents";
      return false;
    }
    if (existing.root_split != row.root_split) {
      ++report->child_split_collisions;
      report->rejected_roots = 1;
      report->rejected_root_id = row.root_board_id;
      report->rejection_reason =
          "canonical child_board_id crosses normalized splits: child=" + row.child_board_id +
          " existing_root=" + existing.root_board_id + " existing_split=" + existing.root_split +
          " root=" + row.root_board_id + " split=" + row.root_split;
      return false;
    }
  }
  child_rows->reserve(by_child_id.size());
  for (const auto& [child_id, row] : by_child_id) {
    (void)child_id;
    child_rows->push_back(row);
  }
  report->child_normalized_rows = child_rows->size();
  return true;
}

bool run_contract_self_test() {
  if (sha256_hex("abc") != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
    std::cerr << "sha256 board identity self-test failed\n";
    return false;
  }
  const std::string board(board_core::kSquareCount, '-');
  MoveRow first{.root_board_id = "root-a",
                .root_split = "train",
                .child_board_id = canonical_board_id(board),
                .child_board = board};
  MoveRow duplicate{.root_board_id = "root-b",
                    .root_split = "train",
                    .child_board_id = first.child_board_id,
                    .child_board = board};
  Report dedupe_report;
  std::vector<const MoveRow*> deduped;
  if (!select_child_normalized_rows({first, duplicate}, &deduped, &dedupe_report) ||
      deduped.size() != 1 || dedupe_report.duplicate_child_normalized_rows != 1) {
    std::cerr << "child normalized dedupe self-test failed\n";
    return false;
  }
  MoveRow cross_split = duplicate;
  cross_split.root_split = "validation";
  Report collision_report;
  std::vector<const MoveRow*> collision_rows;
  if (select_child_normalized_rows({first, cross_split}, &collision_rows, &collision_report) ||
      collision_report.child_split_collisions != 1) {
    std::cerr << "child split collision self-test failed\n";
    return false;
  }
  std::cout << "contract_self_test=ok\n";
  return true;
}

bool write_outputs(const Args& args, const std::vector<MoveRow>& rows, const Teacher& teacher,
                   const SearchConfig& config, const std::vector<const MoveRow*>& child_rows,
                   Report* report) {
  std::ofstream move_output(args.move_teacher_out_path);
  std::ofstream child_output(args.child_normalized_out_path);
  if (!move_output || !child_output) {
    std::cerr << "cannot open teacher output TSVs\n";
    return false;
  }
  move_output << kMoveTeacherHeaderV3 << '\n';
  child_output << kNormalizedHeaderV2 << '\n';
  for (const MoveRow& row : rows) {
    const std::string move_line = move_teacher_line(row, teacher, config);
    move_output << move_line << '\n';
    mix_output_checksum(move_line, report);
  }
  for (const MoveRow* row : child_rows) {
    const std::string child_line = child_normalized_line(*row, teacher);
    child_output << child_line << '\n';
    mix_output_checksum(child_line, report);
  }
  return static_cast<bool>(move_output) && static_cast<bool>(child_output);
}

bool write_report(const Args& args, const Teacher* teacher, const SearchConfig* config,
                  const Report& report) {
  std::ofstream output(args.report_out_path);
  if (!output) {
    std::cerr << "cannot write report: " << args.report_out_path << '\n';
    return false;
  }
  output << "{\n";
  output << "  \"schema_version\": 2,\n";
  output << "  \"move_teacher_schema\": \"move-teacher-tsv-v3\",\n";
  output << "  \"teacher_kind\": \"" << kTeacherKind << "\",\n";
  output << "  \"teacher_source\": \"" << (teacher == nullptr ? "" : json_escape(teacher->source))
         << "\",\n";
  output << "  \"normalized_input_path\": \"" << json_escape(report_path(args.normalized_tsv_path))
         << "\",\n";
  output << "  \"phase_range\": [" << args.min_phase << ", " << args.max_phase << "],\n";
  output << "  \"input_rows\": " << report.input_rows << ",\n";
  output << "  \"phase_filtered_rows\": " << report.phase_filtered_rows << ",\n";
  output << "  \"duplicate_board_rows\": " << report.duplicate_board_rows << ",\n";
  output << "  \"selected_roots\": " << report.selected_roots << ",\n";
  output << "  \"completed_roots\": " << report.completed_roots << ",\n";
  output << "  \"rejected_roots\": " << report.rejected_roots << ",\n";
  output << "  \"terminal_roots\": " << report.terminal_roots << ",\n";
  output << "  \"roots_with_pass_move\": " << report.roots_with_pass_move << ",\n";
  output << "  \"move_rows\": " << report.move_rows << ",\n";
  output << "  \"child_normalized_rows\": " << report.child_normalized_rows << ",\n";
  output << "  \"duplicate_child_normalized_rows\": " << report.duplicate_child_normalized_rows
         << ",\n";
  output << "  \"child_split_collisions\": " << report.child_split_collisions << ",\n";
  output << "  \"accepted_node_limited_children\": " << report.accepted_node_limited_children
         << ",\n";
  output << "  \"roots_with_mixed_teacher_depth\": " << report.roots_with_mixed_teacher_depth
         << ",\n";
  output << "  \"teacher_nodes_sum\": " << report.teacher_nodes_sum << ",\n";
  output << "  \"complete\": " << (report.rejected_roots == 0 ? "true" : "false") << ",\n";
  output << "  \"rejected_root_id\": \"" << json_escape(report.rejected_root_id) << "\",\n";
  output << "  \"rejection_reason\": \"" << json_escape(report.rejection_reason) << "\",\n";
  if (teacher != nullptr) {
    output << "  \"teacher_artifact_id\": \"" << json_escape(teacher->artifact_id) << "\",\n";
    output << "  \"teacher_artifact_checksum\": \"" << json_escape(teacher->artifact_checksum)
           << "\",\n";
    output << "  \"teacher_pattern_set_id\": \"" << json_escape(teacher->pattern_set_id) << "\",\n";
    output << "  \"teacher_coverage_policy\": \"" << json_escape(teacher->coverage_policy)
           << "\",\n";
    output << "  \"teacher_score_normalization\": \"" << json_escape(teacher->score_normalization)
           << "\",\n";
    output << "  \"teacher_trained_phases\": [";
    for (std::size_t index = 0; index < teacher->trained_phases.size(); ++index) {
      if (index != 0) {
        output << ", ";
      }
      output << static_cast<int>(teacher->trained_phases[index]);
    }
    output << "],\n";
    output << "  \"teacher_fallback_phases\": [";
    for (std::size_t index = 0; index < teacher->fallback_phases.size(); ++index) {
      if (index != 0) {
        output << ", ";
      }
      output << static_cast<int>(teacher->fallback_phases[index]);
    }
    output << "],\n";
    output << "  \"teacher_fallback_additive_through_phase\": ";
    if (teacher->fallback_additive_through_phase.has_value()) {
      output << static_cast<int>(*teacher->fallback_additive_through_phase);
    } else {
      output << "null";
    }
    output << ",\n";
  }
  if (config != nullptr) {
    output << "  \"teacher_search_config_id\": \"" << config->id << "\",\n";
    output << "  \"search\": {\"preset\": \"" << preset_name(config->preset)
           << "\", \"max_depth\": " << config->limits.max_depth
           << ", \"max_nodes\": " << config->limits.max_nodes
           << ", \"max_time_ms\": " << config->limits.max_time.count()
           << ", \"exact_endgame_empties\": " << static_cast<int>(config->exact_endgame_empties)
           << "},\n";
  }
  output << "  \"output_checksum\": \"" << checksum_string(report.output_checksum) << "\",\n";
  output << "  \"wall_time_sec\": " << std::fixed << std::setprecision(6) << report.wall_time_sec
         << "\n";
  output << "}\n";
  return true;
}

void print_usage() {
  std::cerr << "usage: vibe-othello-generate-search-move-teacher-dataset "
               "--normalized-tsv PATH --teacher-manifest PATH --teacher-weights PATH "
               "--move-teacher-out PATH --child-normalized-out PATH --report-out PATH "
               "--max-depth N --max-nodes N --max-time-ms N --search-preset basic|full "
               "[--teacher-coverage-policy require-all|explicit-phase-aware] "
               "--exact-endgame-empties N [--min-phase 0] [--max-phase 9] "
               "[--progress-every N]\n";
}

bool next_value(int* index, int argc, char** argv, std::string* value) {
  if (*index + 1 >= argc) {
    std::cerr << argv[*index] << " requires a value\n";
    return false;
  }
  *value = argv[++(*index)];
  return true;
}

std::optional<Args> parse_args(int argc, char** argv) {
  Args args;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    std::string value;
    if (!next_value(&index, argc, argv, &value)) {
      return std::nullopt;
    }
    if (arg == "--normalized-tsv") {
      args.normalized_tsv_path = value;
    } else if (arg == "--teacher-manifest") {
      args.teacher_manifest_path = value;
    } else if (arg == "--teacher-weights") {
      args.teacher_weights_path = value;
    } else if (arg == "--move-teacher-out") {
      args.move_teacher_out_path = value;
    } else if (arg == "--child-normalized-out") {
      args.child_normalized_out_path = value;
    } else if (arg == "--report-out") {
      args.report_out_path = value;
    } else if (arg == "--max-depth") {
      const std::optional<int> parsed = parse_int(value);
      if (!parsed.has_value() || *parsed < 1 || *parsed > 127) {
        std::cerr << "--max-depth must be in [1, 127]\n";
        return std::nullopt;
      }
      args.max_depth = static_cast<search::Depth>(*parsed);
      args.saw_max_depth = true;
    } else if (arg == "--max-nodes") {
      const std::optional<std::uint64_t> parsed = parse_u64(value);
      if (!parsed.has_value()) {
        std::cerr << "--max-nodes must be a non-negative integer\n";
        return std::nullopt;
      }
      args.max_nodes = *parsed;
      args.saw_max_nodes = true;
    } else if (arg == "--max-time-ms") {
      const std::optional<int> parsed = parse_int(value);
      if (!parsed.has_value() || *parsed < 0) {
        std::cerr << "--max-time-ms must be non-negative\n";
        return std::nullopt;
      }
      args.max_time = std::chrono::milliseconds{*parsed};
      args.saw_max_time = true;
    } else if (arg == "--search-preset") {
      if (value == "basic") {
        args.search_preset = SearchPreset::basic;
      } else if (value == "full") {
        args.search_preset = SearchPreset::full;
      } else {
        std::cerr << "--search-preset must be basic or full\n";
        return std::nullopt;
      }
      args.saw_preset = true;
    } else if (arg == "--teacher-coverage-policy") {
      if (value == "require-all") {
        args.teacher_coverage_policy = TeacherCoveragePolicy::require_all;
      } else if (value == "explicit-phase-aware") {
        args.teacher_coverage_policy = TeacherCoveragePolicy::explicit_phase_aware;
      } else {
        std::cerr << "--teacher-coverage-policy must be require-all or explicit-phase-aware\n";
        return std::nullopt;
      }
    } else if (arg == "--exact-endgame-empties") {
      const std::optional<int> parsed = parse_int(value);
      if (!parsed.has_value() || *parsed < 0 || *parsed > 60) {
        std::cerr << "--exact-endgame-empties must be in [0, 60]\n";
        return std::nullopt;
      }
      args.exact_endgame_empties = static_cast<std::uint8_t>(*parsed);
      args.saw_exact_threshold = true;
    } else if (arg == "--min-phase" || arg == "--max-phase") {
      const std::optional<int> parsed = parse_int(value);
      if (!parsed.has_value() || *parsed < 0 || *parsed > 12) {
        std::cerr << arg << " must be in [0, 12]\n";
        return std::nullopt;
      }
      if (arg == "--min-phase") {
        args.min_phase = *parsed;
      } else {
        args.max_phase = *parsed;
      }
    } else if (arg == "--progress-every") {
      const std::optional<std::uint64_t> parsed = parse_u64(value);
      if (!parsed.has_value()) {
        std::cerr << "--progress-every must be non-negative\n";
        return std::nullopt;
      }
      args.progress_every = static_cast<std::size_t>(*parsed);
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      return std::nullopt;
    }
  }
  if (args.normalized_tsv_path.empty() || args.teacher_manifest_path.empty() ||
      args.teacher_weights_path.empty() || args.move_teacher_out_path.empty() ||
      args.child_normalized_out_path.empty() || args.report_out_path.empty() ||
      !args.saw_max_depth || !args.saw_max_nodes || !args.saw_max_time || !args.saw_preset ||
      !args.saw_exact_threshold || args.min_phase > args.max_phase) {
    print_usage();
    return std::nullopt;
  }
  if (args.max_nodes == 0 && args.max_time.count() != 0) {
    std::cerr
        << "wall-clock-only teacher search is not supported; use fixed depth or fixed nodes\n";
    return std::nullopt;
  }
  if (args.exact_endgame_empties != 0 && args.max_nodes == 0 && args.max_time.count() == 0) {
    std::cerr << "exact endgame threshold requires a node or time cap\n";
    return std::nullopt;
  }
  return args;
}

} // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--self-test-contract") {
    return run_contract_self_test() ? 0 : 1;
  }
  const auto started = std::chrono::steady_clock::now();
  const std::optional<Args> args = parse_args(argc, argv);
  if (!args.has_value()) {
    return 2;
  }
  Report report;
  std::vector<RootRow> roots;
  if (!load_roots(*args, &roots, &report)) {
    report.rejected_roots = 1;
    report.rejection_reason = "normalized input did not produce eligible roots";
    report.wall_time_sec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    (void)write_report(*args, nullptr, nullptr, report);
    return 1;
  }
  const std::optional<Teacher> teacher = load_teacher(*args);
  if (!teacher.has_value()) {
    report.rejected_roots = 1;
    report.rejection_reason = "teacher artifact rejected";
    report.wall_time_sec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    (void)write_report(*args, nullptr, nullptr, report);
    return 1;
  }
  const SearchConfig config = make_search_config(*args);
  std::vector<MoveRow> all_rows;
  for (std::size_t root_index = 0; root_index < roots.size(); ++root_index) {
    const RootRow& root = roots[root_index];
    const std::optional<board_core::Position> position = position_from_relative_board(root.board);
    if (!position.has_value()) {
      report.rejected_roots = 1;
      report.rejected_root_id = root.board_id;
      report.rejection_reason = "root board could not be parsed";
      break;
    }
    bool terminal_root = false;
    const std::vector<board_core::Move> moves = legal_root_moves(*position, &terminal_root);
    if (terminal_root) {
      report.rejected_roots = 1;
      report.terminal_roots = 1;
      report.rejected_root_id = root.board_id;
      report.rejection_reason = "terminal roots do not have a legal move ranking";
      break;
    }
    if (moves.empty()) {
      report.rejected_roots = 1;
      report.rejected_root_id = root.board_id;
      report.rejection_reason = "root has neither legal moves nor a legal pass";
      break;
    }
    if (moves.size() == 1 && moves.front().kind == board_core::MoveKind::pass) {
      ++report.roots_with_pass_move;
    }
    std::vector<MoveRow> root_rows;
    bool root_ok = true;
    for (const board_core::Move move : moves) {
      bool accepted_node_limited = false;
      std::string error;
      std::optional<MoveRow> row =
          evaluate_child(root, *position, move, *teacher, config, &accepted_node_limited, &error);
      if (!row.has_value()) {
        report.rejected_roots = 1;
        report.rejected_root_id = root.board_id;
        report.rejection_reason = move_to_string(move) + ": " + error;
        root_ok = false;
        break;
      }
      if (accepted_node_limited) {
        ++report.accepted_node_limited_children;
      }
      report.teacher_nodes_sum += row->teacher_nodes;
      root_rows.push_back(std::move(*row));
    }
    if (!root_ok) {
      break;
    }
    const auto [minimum_depth, maximum_depth] = std::minmax_element(
        root_rows.begin(), root_rows.end(), [](const MoveRow& left, const MoveRow& right) {
          return left.teacher_depth < right.teacher_depth;
        });
    if ((*minimum_depth).teacher_depth != (*maximum_depth).teacher_depth) {
      ++report.roots_with_mixed_teacher_depth;
    }
    rank_root_moves(&root_rows);
    all_rows.insert(all_rows.end(), root_rows.begin(), root_rows.end());
    ++report.completed_roots;
    if (args->progress_every != 0 && (root_index + 1) % args->progress_every == 0) {
      std::cerr << "progress roots=" << (root_index + 1) << " selected=" << roots.size()
                << " moves=" << all_rows.size() << '\n';
    }
  }
  report.move_rows = all_rows.size();
  report.wall_time_sec =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  if (report.rejected_roots != 0) {
    (void)write_report(*args, &*teacher, &config, report);
    std::cerr << "teacher generation rejected root " << report.rejected_root_id << ": "
              << report.rejection_reason << '\n';
    return 1;
  }
  std::vector<const MoveRow*> child_rows;
  if (!select_child_normalized_rows(all_rows, &child_rows, &report)) {
    (void)write_report(*args, &*teacher, &config, report);
    std::cerr << "teacher generation rejected root " << report.rejected_root_id << ": "
              << report.rejection_reason << '\n';
    return 1;
  }
  if (!write_outputs(*args, all_rows, *teacher, config, child_rows, &report) ||
      !write_report(*args, &*teacher, &config, report)) {
    return 1;
  }
  std::cout << "move_teacher=" << args->move_teacher_out_path << '\n';
  std::cout << "child_normalized=" << args->child_normalized_out_path << '\n';
  std::cout << "report=" << args->report_out_path << '\n';
  std::cout << "checksum=" << checksum_string(report.output_checksum) << '\n';
  return 0;
}
