#include "arena_core.h"
#include "vibe_othello/board_core/position.h"
#include "vibe_othello/evaluation/pattern_artifact.h"
#include "vibe_othello/evaluation/phase_aware_evaluator.h"
#include "vibe_othello/search/search.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace arena = vibe_othello::tools::arena;
namespace board_core = vibe_othello::board_core;
namespace evaluation = vibe_othello::evaluation;
namespace search = vibe_othello::search;

constexpr std::string_view kArenaVersion = "full-game-artifact-arena-v1";
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

enum class SearchPreset {
  basic,
  full,
};

struct Args {
  std::filesystem::path candidate_manifest;
  std::optional<std::filesystem::path> candidate_weights;
  std::string candidate_name = "candidate";
  std::filesystem::path baseline_manifest;
  std::optional<std::filesystem::path> baseline_weights;
  std::string baseline_name = "baseline";
  std::filesystem::path openings_path;
  std::filesystem::path report_out;
  search::Depth depth = 3;
  search::NodeCount max_nodes = 0;
  std::chrono::milliseconds max_time{0};
  SearchPreset search_preset = SearchPreset::full;
  std::uint8_t exact_endgame_empties = 0;
  std::uint64_t seed = 0;
  int opening_limit = 0;
  int progress_every = 0;
};

struct SearchConfig {
  SearchPreset preset = SearchPreset::full;
  search::SearchLimits limits;
  search::SearchOptions options;
  std::uint8_t exact_endgame_empties = 0;
};

struct ArtifactIdentity {
  std::string display_name;
  std::string artifact_id;
  std::string pattern_set_id;
  std::string weights_checksum;
  std::vector<std::uint8_t> trained_phases;
  bool trained_phases_reported = false;
  std::string evaluator_policy;
  std::string runtime_identity_checksum;
  std::filesystem::path manifest_path;
  std::filesystem::path weights_path;
};

struct LoadedEvaluator {
  ArtifactIdentity identity;
  evaluation::PhaseAwareEvaluator evaluator;
};

struct SelectedOpening {
  arena::Opening opening;
  int source_index = 0;
  std::uint64_t selection_hash = 0;
  std::string key;
};

struct GameRecord {
  int game_id = 0;
  std::string opening_key;
  std::string opening_id;
  int opening_source_index = 0;
  std::string side_assignment;
  int black_discs = 0;
  int white_discs = 0;
  int candidate_disc_diff = 0;
  int normal_moves_after_opening = 0;
  int passes_after_opening = 0;
  std::string candidate_result;
  std::string reason;
  bool failed = false;
  bool illegal = false;
};

struct BucketStats {
  int games = 0;
  int candidate_wins = 0;
  int baseline_wins = 0;
  int draws = 0;
  int failed_games = 0;
  int illegal_games = 0;
  int normal_moves_after_opening = 0;
  int passes_after_opening = 0;
  int disc_diff_sum = 0;
  std::vector<int> disc_diffs;
};

struct ArenaStats {
  BucketStats overall;
  std::map<std::string, BucketStats> by_side_assignment;
  std::map<std::string, BucketStats> by_opening;
};

void print_usage() {
  std::cerr << "usage: vibe-othello-full-game-artifact-arena "
               "--candidate-manifest PATH --baseline-manifest PATH --openings FILE "
               "--report-out PATH [--candidate-weights PATH] [--baseline-weights PATH] "
               "[--candidate-name NAME] [--baseline-name NAME] [--depth 3] [--nodes 0] "
               "[--time-ms 0] [--search-preset basic|full] [--exact-endgame-empties 0] "
               "[--seed 0] [--opening-limit 0] [--progress-every 0]\n";
}

std::optional<int> parse_int(std::string_view text) noexcept {
  int value = 0;
  const auto [pointer, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || pointer != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::uint64_t> parse_u64(std::string_view text) noexcept {
  std::uint64_t value = 0;
  const auto [pointer, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || pointer != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<Args> parse_args(int argc, char** argv) {
  Args args;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    auto next_value = [&](std::string* value) {
      if (index + 1 >= argc) {
        std::cerr << arg << " requires a value\n";
        return false;
      }
      *value = argv[++index];
      return true;
    };

    std::string value;
    if (arg == "--candidate-manifest") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      args.candidate_manifest = value;
    } else if (arg == "--candidate-weights") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      args.candidate_weights = std::filesystem::path{value};
    } else if (arg == "--candidate-name") {
      if (!next_value(&args.candidate_name)) {
        return std::nullopt;
      }
    } else if (arg == "--baseline-manifest") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      args.baseline_manifest = value;
    } else if (arg == "--baseline-weights") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      args.baseline_weights = std::filesystem::path{value};
    } else if (arg == "--baseline-name") {
      if (!next_value(&args.baseline_name)) {
        return std::nullopt;
      }
    } else if (arg == "--openings") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      args.openings_path = value;
    } else if (arg == "--report-out") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      args.report_out = value;
    } else if (arg == "--depth") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<int> depth = parse_int(value);
      if (!depth.has_value() || *depth <= 0) {
        std::cerr << "--depth must be a positive integer\n";
        return std::nullopt;
      }
      args.depth = static_cast<search::Depth>(*depth);
    } else if (arg == "--nodes") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<std::uint64_t> nodes = parse_u64(value);
      if (!nodes.has_value()) {
        std::cerr << "--nodes must be a non-negative integer\n";
        return std::nullopt;
      }
      args.max_nodes = static_cast<search::NodeCount>(*nodes);
    } else if (arg == "--time-ms") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<std::uint64_t> time_ms = parse_u64(value);
      if (!time_ms.has_value() ||
          *time_ms > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        std::cerr << "--time-ms must be a non-negative integer within milliseconds range\n";
        return std::nullopt;
      }
      args.max_time = std::chrono::milliseconds{static_cast<std::int64_t>(*time_ms)};
    } else if (arg == "--search-preset") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      if (value == "basic") {
        args.search_preset = SearchPreset::basic;
      } else if (value == "full") {
        args.search_preset = SearchPreset::full;
      } else {
        std::cerr << "--search-preset must be basic or full\n";
        return std::nullopt;
      }
    } else if (arg == "--exact-endgame-empties") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<int> empties = parse_int(value);
      if (!empties.has_value() || *empties < 0 || *empties > 64) {
        std::cerr << "--exact-endgame-empties must be an integer in [0, 64]\n";
        return std::nullopt;
      }
      args.exact_endgame_empties = static_cast<std::uint8_t>(*empties);
    } else if (arg == "--seed") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<std::uint64_t> seed = parse_u64(value);
      if (!seed.has_value()) {
        std::cerr << "--seed must be a non-negative integer\n";
        return std::nullopt;
      }
      args.seed = *seed;
    } else if (arg == "--opening-limit") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<int> limit = parse_int(value);
      if (!limit.has_value() || *limit < 0) {
        std::cerr << "--opening-limit must be a non-negative integer\n";
        return std::nullopt;
      }
      args.opening_limit = *limit;
    } else if (arg == "--progress-every") {
      if (!next_value(&value)) {
        return std::nullopt;
      }
      const std::optional<int> progress_every = parse_int(value);
      if (!progress_every.has_value() || *progress_every < 0) {
        std::cerr << "--progress-every must be a non-negative integer\n";
        return std::nullopt;
      }
      args.progress_every = *progress_every;
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      return std::nullopt;
    }
  }

  if (args.candidate_manifest.empty() || args.baseline_manifest.empty() ||
      args.openings_path.empty() || args.report_out.empty()) {
    print_usage();
    return std::nullopt;
  }
  if (args.exact_endgame_empties != 0 && args.max_nodes == 0 && args.max_time.count() == 0) {
    std::cerr << "--exact-endgame-empties requires --nodes or --time-ms because exact root "
                 "search ignores depth\n";
    return std::nullopt;
  }
  return args;
}

void mix_fnv1a(std::string_view text, std::uint64_t* hash) noexcept {
  for (const unsigned char byte : text) {
    *hash ^= byte;
    *hash *= kFnvPrime;
  }
}

std::string checksum_for(std::string_view text) {
  std::uint64_t hash = kFnvOffsetBasis;
  mix_fnv1a(text, &hash);
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}

std::string search_preset_name(SearchPreset preset) {
  return preset == SearchPreset::basic ? "basic" : "full";
}

SearchConfig make_search_config(const Args& args) {
  const bool full = args.search_preset == SearchPreset::full;
  return SearchConfig{
      .preset = args.search_preset,
      .limits =
          search::SearchLimits{
              .max_depth = args.depth,
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
              .experimental = search::ExperimentalSearchOptions{},
              .mode = search::SearchMode::move,
          },
      .exact_endgame_empties = args.exact_endgame_empties,
  };
}

std::string runtime_identity_checksum(const ArtifactIdentity& identity) {
  std::ostringstream payload;
  payload << identity.pattern_set_id << '\n'
          << identity.weights_checksum << '\n'
          << identity.evaluator_policy << '\n'
          << (identity.trained_phases_reported ? "1" : "0");
  for (const std::uint8_t phase : identity.trained_phases) {
    payload << ',' << static_cast<int>(phase);
  }
  return checksum_for(payload.str());
}

std::optional<LoadedEvaluator>
load_evaluator(std::string display_name, const std::filesystem::path& manifest_path,
               const std::optional<std::filesystem::path>& weights_override) {
  evaluation::PatternArtifactLoadResult result =
      weights_override.has_value()
          ? evaluation::load_pattern_artifact(manifest_path, *weights_override)
          : evaluation::load_pattern_artifact(manifest_path);
  if (!result.ok()) {
    std::cerr << result.error << '\n';
    return std::nullopt;
  }

  try {
    evaluation::LoadedPatternArtifact artifact = std::move(*result.artifact);
    ArtifactIdentity identity{
        .display_name = std::move(display_name),
        .artifact_id = artifact.artifact_id,
        .pattern_set_id = artifact.pattern_set_id,
        .weights_checksum = artifact.weights_checksum,
        .trained_phases = artifact.trained_phases.value_or(std::vector<std::uint8_t>{}),
        .trained_phases_reported = artifact.trained_phases.has_value(),
        .evaluator_policy = artifact.trained_phases.has_value()
                                ? "phase-aware-covered-phases"
                                : "phase-aware-legacy-all-phase-learned",
        .manifest_path = artifact.manifest_path,
        .weights_path = artifact.weights_path,
    };
    identity.runtime_identity_checksum = runtime_identity_checksum(identity);
    return LoadedEvaluator{
        .identity = std::move(identity),
        .evaluator = evaluation::PhaseAwareEvaluator{std::move(artifact.weights),
                                                     std::move(artifact.feature_set),
                                                     std::move(artifact.trained_phases)},
    };
  } catch (const std::exception& error) {
    std::cerr << "phase-aware evaluator rejected artifact for " << display_name << ": "
              << error.what() << '\n';
    return std::nullopt;
  }
}

std::optional<std::string> read_text_file(const std::filesystem::path& path,
                                          std::string_view description) {
  std::ifstream input(path);
  if (!input) {
    std::cerr << "cannot read " << description << ": " << path << '\n';
    return std::nullopt;
  }
  return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

std::uint64_t opening_selection_hash(std::uint64_t seed, const arena::Opening& opening,
                                     int source_index) noexcept {
  std::uint64_t hash = kFnvOffsetBasis;
  const std::string seed_text = std::to_string(seed);
  mix_fnv1a(seed_text, &hash);
  mix_fnv1a("\n", &hash);
  mix_fnv1a(opening.id, &hash);
  mix_fnv1a("\n", &hash);
  const std::string moves = arena::format_moves(opening.moves);
  mix_fnv1a(moves, &hash);
  mix_fnv1a("\n", &hash);
  mix_fnv1a(std::to_string(source_index), &hash);
  return hash;
}

std::vector<SelectedOpening> select_openings(std::span<const arena::Opening> openings,
                                             std::uint64_t seed, int opening_limit) {
  std::vector<SelectedOpening> selected;
  selected.reserve(openings.size());
  for (std::size_t index = 0; index < openings.size(); ++index) {
    const arena::Opening& opening = openings[index];
    const int source_index = static_cast<int>(index) + 1;
    selected.push_back(SelectedOpening{
        .opening = opening,
        .source_index = source_index,
        .selection_hash = opening_selection_hash(seed, opening, source_index),
        .key = opening.id + "#" + std::to_string(source_index),
    });
  }
  if (opening_limit <= 0 || selected.size() <= static_cast<std::size_t>(opening_limit)) {
    return selected;
  }
  std::sort(selected.begin(), selected.end(),
            [](const SelectedOpening& lhs, const SelectedOpening& rhs) {
              if (lhs.selection_hash != rhs.selection_hash) {
                return lhs.selection_hash < rhs.selection_hash;
              }
              return lhs.source_index < rhs.source_index;
            });
  selected.resize(static_cast<std::size_t>(opening_limit));
  return selected;
}

int disc_count(board_core::Bitboard discs) noexcept {
  return std::popcount(discs);
}

GameRecord adjudicated_failure(int game_id, const SelectedOpening& opening, bool candidate_is_black,
                               bool candidate_offender, std::string reason, bool illegal,
                               int normal_moves_after_opening, int passes_after_opening) {
  const bool offender_is_black = candidate_offender == candidate_is_black;
  const int black_discs = offender_is_black ? 0 : 64;
  const int white_discs = offender_is_black ? 64 : 0;
  return GameRecord{
      .game_id = game_id,
      .opening_key = opening.key,
      .opening_id = opening.opening.id,
      .opening_source_index = opening.source_index,
      .side_assignment = candidate_is_black ? "candidate_black" : "candidate_white",
      .black_discs = black_discs,
      .white_discs = white_discs,
      .candidate_disc_diff =
          candidate_is_black ? black_discs - white_discs : white_discs - black_discs,
      .normal_moves_after_opening = normal_moves_after_opening,
      .passes_after_opening = passes_after_opening,
      .candidate_result = candidate_offender ? "loss" : "win",
      .reason = std::move(reason),
      .failed = true,
      .illegal = illegal,
  };
}

GameRecord play_game(int game_id, const SelectedOpening& opening, bool candidate_is_black,
                     const LoadedEvaluator& candidate, const LoadedEvaluator& baseline,
                     const SearchConfig& search_config) {
  board_core::Position position{};
  std::string replay_error;
  if (!arena::replay_moves(opening.opening.moves, &position, &replay_error)) {
    return adjudicated_failure(game_id, opening, candidate_is_black, true, "invalid_opening", false,
                               0, 0);
  }

  int normal_moves_after_opening = 0;
  int passes_after_opening = 0;
  while (!board_core::is_terminal(position)) {
    const board_core::Bitboard legal_moves = board_core::legal_moves(position);
    if (legal_moves == 0) {
      board_core::MoveDelta delta{};
      if (!board_core::apply_pass(&position, &delta)) {
        return adjudicated_failure(game_id, opening, candidate_is_black, true, "illegal_pass", true,
                                   normal_moves_after_opening, passes_after_opening);
      }
      ++passes_after_opening;
      continue;
    }

    const bool candidate_to_move =
        (position.side_to_move == board_core::Color::black) == candidate_is_black;
    const search::Evaluator& evaluator =
        candidate_to_move ? static_cast<const search::Evaluator&>(candidate.evaluator)
                          : static_cast<const search::Evaluator&>(baseline.evaluator);
    const search::SearchResult result =
        search::search_iterative(position, evaluator, search_config.limits, search_config.options);
    if (!result.best_move.has_value()) {
      return adjudicated_failure(game_id, opening, candidate_is_black, candidate_to_move,
                                 result.stopped ? "search_stopped_without_best_move"
                                                : "missing_best_move",
                                 false, normal_moves_after_opening, passes_after_opening);
    }
    if (result.best_move->kind != board_core::MoveKind::normal ||
        (legal_moves & board_core::bit(result.best_move->square)) == 0) {
      return adjudicated_failure(game_id, opening, candidate_is_black, candidate_to_move,
                                 "illegal_best_move", true, normal_moves_after_opening,
                                 passes_after_opening);
    }

    board_core::MoveDelta delta{};
    if (!board_core::apply_move(&position, *result.best_move, &delta)) {
      return adjudicated_failure(game_id, opening, candidate_is_black, candidate_to_move,
                                 "apply_move_failed", true, normal_moves_after_opening,
                                 passes_after_opening);
    }
    ++normal_moves_after_opening;
  }

  const int black_discs = disc_count(board_core::black_discs(position));
  const int white_discs = disc_count(board_core::white_discs(position));
  const int candidate_disc_diff =
      candidate_is_black ? black_discs - white_discs : white_discs - black_discs;
  return GameRecord{
      .game_id = game_id,
      .opening_key = opening.key,
      .opening_id = opening.opening.id,
      .opening_source_index = opening.source_index,
      .side_assignment = candidate_is_black ? "candidate_black" : "candidate_white",
      .black_discs = black_discs,
      .white_discs = white_discs,
      .candidate_disc_diff = candidate_disc_diff,
      .normal_moves_after_opening = normal_moves_after_opening,
      .passes_after_opening = passes_after_opening,
      .candidate_result = candidate_disc_diff > 0   ? "win"
                          : candidate_disc_diff < 0 ? "loss"
                                                    : "draw",
      .reason = "terminal",
  };
}

void add_to_bucket(BucketStats* bucket, const GameRecord& game) {
  ++bucket->games;
  bucket->disc_diff_sum += game.candidate_disc_diff;
  bucket->disc_diffs.push_back(game.candidate_disc_diff);
  bucket->normal_moves_after_opening += game.normal_moves_after_opening;
  bucket->passes_after_opening += game.passes_after_opening;
  if (game.candidate_result == "win") {
    ++bucket->candidate_wins;
  } else if (game.candidate_result == "loss") {
    ++bucket->baseline_wins;
  } else {
    ++bucket->draws;
  }
  if (game.failed) {
    ++bucket->failed_games;
  }
  if (game.illegal) {
    ++bucket->illegal_games;
  }
}

ArenaStats summarize(std::span<const GameRecord> games) {
  ArenaStats stats;
  for (const GameRecord& game : games) {
    add_to_bucket(&stats.overall, game);
    add_to_bucket(&stats.by_side_assignment[game.side_assignment], game);
    add_to_bucket(&stats.by_opening[game.opening_key], game);
  }
  return stats;
}

double score_rate(const BucketStats& bucket) noexcept {
  if (bucket.games == 0) {
    return 0.0;
  }
  return static_cast<double>(bucket.candidate_wins + bucket.draws * 0.5) /
         static_cast<double>(bucket.games);
}

double average_disc_diff(const BucketStats& bucket) noexcept {
  if (bucket.games == 0) {
    return 0.0;
  }
  return static_cast<double>(bucket.disc_diff_sum) / static_cast<double>(bucket.games);
}

double median_disc_diff(const BucketStats& bucket) {
  if (bucket.disc_diffs.empty()) {
    return 0.0;
  }
  std::vector<int> values = bucket.disc_diffs;
  std::sort(values.begin(), values.end());
  const std::size_t middle = values.size() / 2;
  if (values.size() % 2 != 0) {
    return static_cast<double>(values[middle]);
  }
  return (static_cast<double>(values[middle - 1]) + static_cast<double>(values[middle])) / 2.0;
}

void write_json_string(std::ostream& output, std::string_view value) {
  output << '"' << arena::json_escape(value) << '"';
}

void write_bool(std::ostream& output, bool value) {
  output << (value ? "true" : "false");
}

void write_trained_phases(std::ostream& output, const ArtifactIdentity& identity) {
  if (!identity.trained_phases_reported) {
    output << "null";
    return;
  }
  output << '[';
  for (std::size_t index = 0; index < identity.trained_phases.size(); ++index) {
    if (index != 0) {
      output << ", ";
    }
    output << static_cast<int>(identity.trained_phases[index]);
  }
  output << ']';
}

void write_artifact_identity(std::ostream& output, const ArtifactIdentity& identity) {
  output << "{";
  output << "\"name\": ";
  write_json_string(output, identity.display_name);
  output << ", \"artifact_id\": ";
  write_json_string(output, identity.artifact_id);
  output << ", \"pattern_set_id\": ";
  write_json_string(output, identity.pattern_set_id);
  output << ", \"weights_checksum\": ";
  write_json_string(output, identity.weights_checksum);
  output << ", \"trained_phases\": ";
  write_trained_phases(output, identity);
  output << ", \"evaluator_policy\": ";
  write_json_string(output, identity.evaluator_policy);
  output << ", \"runtime_identity_checksum\": ";
  write_json_string(output, identity.runtime_identity_checksum);
  output << ", \"manifest_path\": ";
  write_json_string(output, identity.manifest_path.string());
  output << ", \"weights_path\": ";
  write_json_string(output, identity.weights_path.string());
  output << "}";
}

void write_search_options(std::ostream& output, const search::SearchOptions& options) {
  output << "{";
  output << "\"use_pvs\": ";
  write_bool(output, options.midgame.use_pvs);
  output << ", \"use_aspiration\": ";
  write_bool(output, options.midgame.use_aspiration);
  output << ", \"use_iid\": ";
  write_bool(output, options.midgame.use_iid);
  output << ", \"use_midgame_tt\": ";
  write_bool(output, options.midgame.use_midgame_tt);
  output << ", \"use_tt_best_move_ordering\": ";
  write_bool(output, options.ordering.use_tt_best_move_ordering);
  output << ", \"use_history\": ";
  write_bool(output, options.ordering.use_history);
  output << ", \"use_killers\": ";
  write_bool(output, options.ordering.use_killers);
  output << ", \"use_endgame_parity_ordering\": ";
  write_bool(output, options.ordering.use_endgame_parity_ordering);
  output << ", \"exact_endgame\": ";
  write_bool(output, options.endgame.exact_endgame);
  output << ", \"use_endgame_tt\": ";
  write_bool(output, options.endgame.use_endgame_tt);
  output << ", \"endgame_exact_empties\": "
         << static_cast<int>(options.endgame.endgame_exact_empties);
  output << ", \"endgame_wld_empties\": " << static_cast<int>(options.endgame.endgame_wld_empties);
  output << ", \"probcut\": ";
  write_bool(output, options.experimental.probcut);
  output << ", \"use_pv_table\": ";
  write_bool(output, options.experimental.use_pv_table);
  output << ", \"use_parallel\": ";
  write_bool(output, options.experimental.use_parallel);
  output << ", \"selectivity_level\": " << static_cast<int>(options.experimental.selectivity_level);
  output << "}";
}

void write_search_config(std::ostream& output, const SearchConfig& config) {
  output << "{";
  output << "\"entrypoint\": \"search_iterative\"";
  output << ", \"preset\": ";
  write_json_string(output, search_preset_name(config.preset));
  output << ", \"limit_scope\": \"per-move\"";
  output << ", \"depth\": " << config.limits.max_depth;
  output << ", \"nodes\": " << config.limits.max_nodes;
  output << ", \"time_ms\": " << config.limits.max_time.count();
  output << ", \"exact_endgame_empties\": " << static_cast<int>(config.exact_endgame_empties);
  output << ", \"resolved_options\": ";
  write_search_options(output, config.options);
  output << "}";
}

void write_bucket(std::ostream& output, const BucketStats& bucket) {
  output << "{";
  output << "\"games\": " << bucket.games;
  output << ", \"candidate_wins\": " << bucket.candidate_wins;
  output << ", \"candidate_losses\": " << bucket.baseline_wins;
  output << ", \"draws\": " << bucket.draws;
  output << ", \"candidate_score_rate\": " << score_rate(bucket);
  output << ", \"average_disc_diff_candidate_perspective\": " << average_disc_diff(bucket);
  output << ", \"median_disc_diff_candidate_perspective\": " << median_disc_diff(bucket);
  output << ", \"failed_games\": " << bucket.failed_games;
  output << ", \"illegal_games\": " << bucket.illegal_games;
  output << ", \"normal_moves_after_opening\": " << bucket.normal_moves_after_opening;
  output << ", \"passes_after_opening\": " << bucket.passes_after_opening;
  output << "}";
}

void write_bucket_map(std::ostream& output, const std::map<std::string, BucketStats>& buckets) {
  output << "{";
  bool first = true;
  for (const auto& [key, bucket] : buckets) {
    if (!first) {
      output << ", ";
    }
    first = false;
    write_json_string(output, key);
    output << ": ";
    write_bucket(output, bucket);
  }
  output << "}";
}

void write_selected_openings(std::ostream& output, std::span<const SelectedOpening> openings) {
  output << "[";
  for (std::size_t index = 0; index < openings.size(); ++index) {
    if (index != 0) {
      output << ", ";
    }
    const SelectedOpening& opening = openings[index];
    output << "{\"key\": ";
    write_json_string(output, opening.key);
    output << ", \"id\": ";
    write_json_string(output, opening.opening.id);
    output << ", \"source_index\": " << opening.source_index;
    output << ", \"moves\": ";
    write_json_string(output, arena::format_moves(opening.opening.moves));
    output << ", \"selection_hash\": \"0x" << std::hex << std::setfill('0') << std::setw(16)
           << opening.selection_hash << std::dec << "\"}";
  }
  output << "]";
}

void write_games(std::ostream& output, std::span<const GameRecord> games) {
  output << "[";
  for (std::size_t index = 0; index < games.size(); ++index) {
    if (index != 0) {
      output << ",\n    ";
    }
    const GameRecord& game = games[index];
    output << "{\"game_id\": " << game.game_id;
    output << ", \"opening_key\": ";
    write_json_string(output, game.opening_key);
    output << ", \"opening_id\": ";
    write_json_string(output, game.opening_id);
    output << ", \"opening_source_index\": " << game.opening_source_index;
    output << ", \"side_assignment\": ";
    write_json_string(output, game.side_assignment);
    output << ", \"black_discs\": " << game.black_discs;
    output << ", \"white_discs\": " << game.white_discs;
    output << ", \"candidate_disc_diff\": " << game.candidate_disc_diff;
    output << ", \"normal_moves_after_opening\": " << game.normal_moves_after_opening;
    output << ", \"passes_after_opening\": " << game.passes_after_opening;
    output << ", \"candidate_result\": ";
    write_json_string(output, game.candidate_result);
    output << ", \"reason\": ";
    write_json_string(output, game.reason);
    output << ", \"failed\": ";
    write_bool(output, game.failed);
    output << ", \"illegal\": ";
    write_bool(output, game.illegal);
    output << "}";
  }
  output << "]";
}

bool same_runtime_artifact(const LoadedEvaluator& candidate, const LoadedEvaluator& baseline) {
  return candidate.identity.runtime_identity_checksum ==
         baseline.identity.runtime_identity_checksum;
}

bool same_artifact_neutral(const ArenaStats& stats, bool same_artifact) {
  return same_artifact && stats.overall.failed_games == 0 && stats.overall.illegal_games == 0 &&
         stats.overall.candidate_wins == stats.overall.baseline_wins &&
         std::abs(score_rate(stats.overall) - 0.5) <= 0.0000001 &&
         std::abs(average_disc_diff(stats.overall)) <= 0.0000001;
}

std::string deterministic_payload(const LoadedEvaluator& candidate, const LoadedEvaluator& baseline,
                                  std::string_view openings_checksum,
                                  std::span<const SelectedOpening> openings,
                                  const SearchConfig& search_config, std::uint64_t seed,
                                  int opening_limit, std::span<const GameRecord> games) {
  std::ostringstream output;
  output << kArenaVersion << '\n';
  output << candidate.identity.runtime_identity_checksum << '\n';
  output << baseline.identity.runtime_identity_checksum << '\n';
  output << openings_checksum << '\n' << seed << '\n' << opening_limit << '\n';
  output << search_preset_name(search_config.preset) << '\n'
         << search_config.limits.max_depth << '\n'
         << search_config.limits.max_nodes << '\n'
         << search_config.limits.max_time.count() << '\n'
         << static_cast<int>(search_config.exact_endgame_empties) << '\n';
  const search::SearchOptions& options = search_config.options;
  output << options.midgame.use_pvs << options.midgame.use_aspiration << options.midgame.use_iid
         << options.midgame.use_midgame_tt << options.ordering.use_tt_best_move_ordering
         << options.ordering.use_history << options.ordering.use_killers
         << options.ordering.use_endgame_parity_ordering << options.endgame.exact_endgame
         << options.endgame.use_endgame_tt << options.experimental.probcut
         << options.experimental.use_pv_table << options.experimental.use_parallel << '\n';
  for (const SelectedOpening& opening : openings) {
    output << opening.key << '\n'
           << opening.selection_hash << '\n'
           << arena::format_moves(opening.opening.moves) << '\n';
  }
  for (const GameRecord& game : games) {
    output << game.game_id << '\n'
           << game.opening_key << '\n'
           << game.side_assignment << '\n'
           << game.black_discs << '\n'
           << game.white_discs << '\n'
           << game.candidate_disc_diff << '\n'
           << game.normal_moves_after_opening << '\n'
           << game.passes_after_opening << '\n'
           << game.candidate_result << '\n'
           << game.reason << '\n'
           << game.failed << game.illegal << '\n';
  }
  return output.str();
}

bool write_report(const Args& args, const LoadedEvaluator& candidate,
                  const LoadedEvaluator& baseline, std::string_view openings_checksum,
                  int input_opening_count, std::span<const SelectedOpening> openings,
                  const SearchConfig& search_config, std::span<const GameRecord> games,
                  const ArenaStats& stats, double elapsed_sec) {
  const std::filesystem::path parent = args.report_out.parent_path();
  if (!parent.empty()) {
    std::error_code error;
    std::filesystem::create_directories(parent, error);
    if (error) {
      std::cerr << "cannot create report directory: " << parent << ": " << error.message() << '\n';
      return false;
    }
  }
  std::ofstream output(args.report_out);
  if (!output) {
    std::cerr << "cannot write report: " << args.report_out << '\n';
    return false;
  }
  const bool same_artifact = same_runtime_artifact(candidate, baseline);
  const std::string report_checksum =
      checksum_for(deterministic_payload(candidate, baseline, openings_checksum, openings,
                                         search_config, args.seed, args.opening_limit, games));
  output << std::fixed << std::setprecision(6);
  output << "{\n";
  output << "  \"schema_version\": 1,\n";
  output << "  \"arena_version\": ";
  write_json_string(output, kArenaVersion);
  output << ",\n  \"candidate\": ";
  write_artifact_identity(output, candidate.identity);
  output << ",\n  \"baseline\": ";
  write_artifact_identity(output, baseline.identity);
  output << ",\n  \"search_config\": ";
  write_search_config(output, search_config);
  output << ",\n  \"opening_source_path\": ";
  write_json_string(output, args.openings_path.string());
  output << ",\n  \"opening_source_checksum\": ";
  write_json_string(output, openings_checksum);
  output << ",\n  \"input_opening_count\": " << input_opening_count;
  output << ",\n  \"opening_count\": " << openings.size();
  output << ",\n  \"opening_limit\": ";
  if (args.opening_limit == 0) {
    output << "null";
  } else {
    output << args.opening_limit;
  }
  output << ",\n  \"seed\": " << args.seed;
  output
      << ",\n  \"opening_selection\": \"input-order when unlimited; FNV-1a sample when limited\"";
  output << ",\n  \"selected_openings\": ";
  write_selected_openings(output, openings);
  output << ",\n  \"games\": " << games.size();
  output << ",\n  \"game_records\": ";
  write_games(output, games);
  output << ",\n  \"results\": {\n    \"overall\": ";
  write_bucket(output, stats.overall);
  output << ",\n    \"by_side_assignment\": ";
  write_bucket_map(output, stats.by_side_assignment);
  output << ",\n    \"by_opening\": ";
  write_bucket_map(output, stats.by_opening);
  output << "\n  },\n";
  output << "  \"failed_games\": " << stats.overall.failed_games << ",\n";
  output << "  \"illegal_games\": " << stats.overall.illegal_games << ",\n";
  output << "  \"elapsed_sec\": " << elapsed_sec << ",\n";
  output << "  \"same_artifact_sanity\": {\"same_runtime_artifact\": ";
  write_bool(output, same_artifact);
  output << ", \"paired_color_swap\": true, \"neutral\": ";
  write_bool(output, same_artifact_neutral(stats, same_artifact));
  output << "},\n";
  output << "  \"report_checksum_algorithm\": \"fnv1a64\",\n";
  output << "  \"report_checksum_scope\": \"deterministic config, runtime identities, selected "
            "openings, and game results; excludes paths and elapsed time\",\n";
  output << "  \"report_checksum\": ";
  write_json_string(output, report_checksum);
  output << ",\n  \"non_claim_notes\": [\n";
  output << "    \"local-only artifact-vs-artifact full-game harness\",\n";
  output << "    \"not an Elo result\",\n";
  output << "    \"not a production strength or artifact promotion claim\",\n";
  output << "    \"time-limited searches can change completed depths across machines; "
            "deterministic checksum smoke uses no time limit\",\n";
  output << "    \"generated arena reports, artifacts, and logs must not be committed\"\n";
  output << "  ]\n}\n";
  return true;
}

} // namespace

int main(int argc, char** argv) {
  const std::optional<Args> args = parse_args(argc, argv);
  if (!args.has_value()) {
    return 2;
  }
  const std::optional<std::string> openings_text =
      read_text_file(args->openings_path, "openings file");
  if (!openings_text.has_value()) {
    return 1;
  }
  std::string opening_error;
  const std::optional<std::vector<arena::Opening>> parsed_openings =
      arena::parse_openings_file(*openings_text, &opening_error);
  if (!parsed_openings.has_value()) {
    std::cerr << "invalid openings file: " << opening_error << '\n';
    return 1;
  }
  const std::vector<SelectedOpening> openings =
      select_openings(*parsed_openings, args->seed, args->opening_limit);
  if (openings.empty()) {
    std::cerr << "no openings selected\n";
    return 1;
  }

  std::optional<LoadedEvaluator> candidate =
      load_evaluator(args->candidate_name, args->candidate_manifest, args->candidate_weights);
  if (!candidate.has_value()) {
    return 1;
  }
  std::optional<LoadedEvaluator> baseline =
      load_evaluator(args->baseline_name, args->baseline_manifest, args->baseline_weights);
  if (!baseline.has_value()) {
    return 1;
  }
  const SearchConfig search_config = make_search_config(*args);
  const auto started = std::chrono::steady_clock::now();
  std::vector<GameRecord> games;
  games.reserve(openings.size() * 2U);
  int game_id = 1;
  for (const SelectedOpening& opening : openings) {
    games.push_back(play_game(game_id++, opening, true, *candidate, *baseline, search_config));
    games.push_back(play_game(game_id++, opening, false, *candidate, *baseline, search_config));
    if (args->progress_every > 0 &&
        games.size() % static_cast<std::size_t>(args->progress_every) == 0) {
      std::cerr << "full-game-artifact-arena progress games=" << games.size() << '\n';
    }
  }
  const double elapsed_sec =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  const ArenaStats stats = summarize(games);
  if (!write_report(*args, *candidate, *baseline, checksum_for(*openings_text),
                    static_cast<int>(parsed_openings->size()), openings, search_config, games,
                    stats, elapsed_sec)) {
    return 1;
  }
  std::cout << "games=" << games.size() << '\n';
  std::cout << "candidate_score_rate=" << std::fixed << std::setprecision(6)
            << score_rate(stats.overall) << '\n';
  std::cout << "report=" << args->report_out << '\n';
  return 0;
}
