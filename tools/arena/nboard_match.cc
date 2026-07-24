#include "arena_core.h"
#include "nboard_session.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/evaluation/pattern_artifact.h"
#include "vibe_othello/evaluation/phase_aware_evaluator.h"
#include "vibe_othello/search/search.h"
#include "vibe_othello/search/search_session.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vibe_othello::tools::arena {
namespace {

struct Args {
  std::filesystem::path nboard_exe;
  std::filesystem::path nboard_working_directory;
  std::filesystem::path nboard_stderr;
  std::string nboard_name;
  std::string nboard_runtime_id;
  std::vector<std::string> nboard_args;
  NBoardProtocolVersion nboard_protocol = NBoardProtocolVersion::v2;
  int nboard_depth = 1;
  std::filesystem::path artifact_manifest;
  std::filesystem::path openings_path;
  std::filesystem::path report_out;
  int max_openings = 1;
  int time_ms = 1000;
  std::uint64_t tt_bytes = 256ULL * 1024ULL * 1024ULL;
  int exact_endgame_empties = 12;
  bool warmup = true;
};

struct LoadedEvaluator {
  std::string artifact_id;
  evaluation::PhaseAwareEvaluator evaluator;
};

void print_usage(std::ostream& output) {
  output << "usage: vibe-othello-nboard-match --nboard-exe PATH "
            "--nboard-working-directory DIR --nboard-name NAME --nboard-runtime-id ID "
            "[--nboard-arg ARG ...] "
            "--artifact-manifest PATH --openings PATH [options]\n"
            "options:\n"
            "  --nboard-stderr PATH         external engine stderr log\n"
            "  --nboard-protocol 1|2        NBoard command dialect (default: 2)\n"
            "  --nboard-depth N             NBoard depth value (default: 1)\n"
            "  --max-openings N             paired openings to play (default: 1)\n"
            "  --time-ms N                  Vibe time per move (default: 1000)\n"
            "  --tt-bytes N                 Vibe TT byte budget (default: 268435456)\n"
            "  --exact-endgame-empties N    exact-search threshold (default: 12)\n"
            "  --report-out PATH            optional JSON report\n"
            "  --no-warmup                  disable one search per engine warm-up\n";
}

template <typename Integer> std::optional<Integer> parse_integer(std::string_view text) {
  Integer value{};
  const auto [ptr, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<Args> parse_args(int argc, char** argv, std::string* error) {
  Args args;
  for (int index = 1; index < argc; ++index) {
    const std::string_view flag = argv[index];
    if (flag == "--help") {
      print_usage(std::cout);
      return std::nullopt;
    }
    if (flag == "--no-warmup") {
      args.warmup = false;
      continue;
    }
    if (index + 1 >= argc) {
      *error = "missing value for " + std::string{flag};
      return std::nullopt;
    }
    const std::string value = argv[++index];
    if (flag == "--nboard-exe") {
      args.nboard_exe = value;
    } else if (flag == "--nboard-working-directory") {
      args.nboard_working_directory = value;
    } else if (flag == "--nboard-stderr") {
      args.nboard_stderr = value;
    } else if (flag == "--nboard-name") {
      args.nboard_name = value;
    } else if (flag == "--nboard-runtime-id") {
      args.nboard_runtime_id = value;
    } else if (flag == "--nboard-arg") {
      args.nboard_args.push_back(value);
    } else if (flag == "--artifact-manifest") {
      args.artifact_manifest = value;
    } else if (flag == "--openings") {
      args.openings_path = value;
    } else if (flag == "--report-out") {
      args.report_out = value;
    } else if (flag == "--nboard-protocol") {
      const auto parsed = parse_integer<int>(value);
      if (!parsed.has_value() || (*parsed != 1 && *parsed != 2)) {
        *error = "invalid --nboard-protocol: expected 1 or 2";
        return std::nullopt;
      }
      args.nboard_protocol = static_cast<NBoardProtocolVersion>(*parsed);
    } else if (flag == "--nboard-depth") {
      const auto parsed = parse_integer<int>(value);
      if (!parsed.has_value()) {
        *error = "invalid --nboard-depth";
        return std::nullopt;
      }
      args.nboard_depth = *parsed;
    } else if (flag == "--max-openings") {
      const auto parsed = parse_integer<int>(value);
      if (!parsed.has_value()) {
        *error = "invalid --max-openings";
        return std::nullopt;
      }
      args.max_openings = *parsed;
    } else if (flag == "--time-ms") {
      const auto parsed = parse_integer<int>(value);
      if (!parsed.has_value()) {
        *error = "invalid --time-ms";
        return std::nullopt;
      }
      args.time_ms = *parsed;
    } else if (flag == "--tt-bytes") {
      const auto parsed = parse_integer<std::uint64_t>(value);
      if (!parsed.has_value()) {
        *error = "invalid --tt-bytes";
        return std::nullopt;
      }
      args.tt_bytes = *parsed;
    } else if (flag == "--exact-endgame-empties") {
      const auto parsed = parse_integer<int>(value);
      if (!parsed.has_value()) {
        *error = "invalid --exact-endgame-empties";
        return std::nullopt;
      }
      args.exact_endgame_empties = *parsed;
    } else {
      *error = "unknown option: " + std::string{flag};
      return std::nullopt;
    }
  }

  if (args.nboard_exe.empty() || args.nboard_working_directory.empty() ||
      args.nboard_name.empty() || args.nboard_runtime_id.empty() ||
      args.artifact_manifest.empty() || args.openings_path.empty()) {
    *error = "--nboard-exe, --nboard-working-directory, --nboard-name, --nboard-runtime-id, "
             "--artifact-manifest, and --openings are required";
    return std::nullopt;
  }
  if (args.nboard_depth <= 0 || args.max_openings <= 0 || args.time_ms <= 0 || args.tt_bytes == 0 ||
      args.exact_endgame_empties < 0 || args.exact_endgame_empties > 60) {
    *error = "numeric options are outside their supported range";
    return std::nullopt;
  }
  return args;
}

std::optional<std::string> read_file(const std::filesystem::path& path, std::string* error) {
  std::ifstream input(path);
  if (!input) {
    *error = "cannot read file: " + path.string();
    return std::nullopt;
  }
  return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

std::optional<LoadedEvaluator> load_evaluator(const std::filesystem::path& manifest,
                                              std::string* error) {
  evaluation::PatternArtifactLoadResult result = evaluation::load_pattern_artifact(manifest);
  if (!result.ok()) {
    *error = result.error;
    return std::nullopt;
  }
  try {
    evaluation::LoadedPatternArtifact artifact = std::move(*result.artifact);
    const std::string artifact_id = artifact.artifact_id;
    return LoadedEvaluator{
        .artifact_id = artifact_id,
        .evaluator = evaluation::PhaseAwareEvaluator{std::move(artifact.weights),
                                                     std::move(artifact.feature_set),
                                                     std::move(artifact.trained_phases),
                                                     artifact.fallback_additive_through_phase},
    };
  } catch (const std::exception& exception) {
    *error = "cannot construct phase-aware evaluator: " + std::string{exception.what()};
    return std::nullopt;
  }
}

search::SearchOptions strongest_options(int exact_endgame_empties) {
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
              .exact_endgame = exact_endgame_empties != 0,
              .use_endgame_tt = true,
              .endgame_exact_empties = static_cast<std::uint8_t>(exact_endgame_empties),
              .endgame_wld_empties = 0,
          },
      .reporting = search::SearchReportingOptions{.multi_pv = 1},
      .mode = search::SearchMode::move,
  };
}

std::pair<int, int> black_white_discs(const board_core::Position& position) {
  const int player_discs = std::popcount(position.player);
  const int opponent_discs = std::popcount(position.opponent);
  return position.side_to_move == board_core::Color::black
             ? std::pair{player_discs, opponent_discs}
             : std::pair{opponent_discs, player_discs};
}

std::string candidate_result(int candidate_disc_diff) {
  if (candidate_disc_diff > 0) {
    return "win";
  }
  if (candidate_disc_diff < 0) {
    return "loss";
  }
  return "draw";
}

std::optional<GameRecord> play_game(int game_id, const Opening& opening, bool candidate_is_black,
                                    NBoardSession* nboard, const LoadedEvaluator& loaded,
                                    const Args& args, std::string* error) {
  board_core::Position position{};
  if (!replay_moves(opening.moves, &position, error) || !nboard->set_game(opening.moves, error)) {
    return std::nullopt;
  }

  const search::SearchSessionConfig session_config{
      .profile = search::SearchPlatformProfile::native,
      .transposition_table =
          search::TranspositionTableConfig{
              .capacity = static_cast<std::size_t>(args.tt_bytes),
              .unit = search::TranspositionTableCapacityUnit::bytes,
          },
  };
  search::SearchSession session{session_config};
  const search::SearchLimits limits{
      .max_depth = 0,
      .max_nodes = 0,
      .max_time = std::chrono::milliseconds{args.time_ms},
      .infinite = true,
  };
  const search::SearchOptions options = strongest_options(args.exact_endgame_empties);
  std::vector<board_core::Move> played_moves = opening.moves;
  int ply = static_cast<int>(played_moves.size());

  while (!board_core::is_terminal(position)) {
    const board_core::Bitboard legal = board_core::legal_moves(position);
    if (legal == 0) {
      const board_core::Move pass = board_core::make_pass();
      board_core::MoveDelta delta{};
      if (!board_core::apply_pass(&position, &delta) || !nboard->play_move(pass, error)) {
        if (error->empty()) {
          *error = "failed to apply forced pass";
        }
        return std::nullopt;
      }
      played_moves.push_back(pass);
      ++ply;
      std::cout << "game=" << game_id << " ply=" << ply << " move=pass forced\n" << std::flush;
      continue;
    }

    const bool candidate_to_move =
        (position.side_to_move == board_core::Color::black) == candidate_is_black;
    board_core::Move move{};
    const auto started = std::chrono::steady_clock::now();
    std::string actor;
    if (candidate_to_move) {
      actor = "vibe";
      const search::SearchResult result =
          search::search_iterative(session, position, loaded.evaluator, limits, options);
      if (!result.best_move.has_value()) {
        *error = "Vibe search returned no move";
        return std::nullopt;
      }
      move = *result.best_move;
    } else {
      actor = args.nboard_name;
      const auto go =
          nboard->go(std::chrono::milliseconds{std::max(15000, args.time_ms * 5)}, error);
      if (!go.has_value()) {
        return std::nullopt;
      }
      move = go->move;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);

    if (move.kind != board_core::MoveKind::normal || (legal & board_core::bit(move.square)) == 0) {
      *error = actor + " returned illegal move " + format_move(move);
      return std::nullopt;
    }
    board_core::MoveDelta delta{};
    if (!board_core::apply_move(&position, move, &delta) || !nboard->play_move(move, error)) {
      if (error->empty()) {
        *error = "failed to apply move " + format_move(move);
      }
      return std::nullopt;
    }
    played_moves.push_back(move);
    ++ply;
    std::cout << "game=" << game_id << " ply=" << ply << " actor=" << actor
              << " move=" << format_move(move) << " elapsed_ms=" << elapsed.count() << '\n'
              << std::flush;
  }

  const auto [black_discs, white_discs] = black_white_discs(position);
  const int diff = candidate_is_black ? black_discs - white_discs : white_discs - black_discs;
  return GameRecord{
      .game_id = game_id,
      .opening = opening.id,
      .black = candidate_is_black ? "vibe" : args.nboard_name,
      .white = candidate_is_black ? args.nboard_name : "vibe",
      .moves = format_moves(played_moves),
      .black_discs = black_discs,
      .white_discs = white_discs,
      .winner = black_discs == white_discs  ? "draw"
                : black_discs > white_discs ? "black"
                                            : "white",
      .candidate_result = candidate_result(diff),
      .candidate_disc_diff = diff,
      .reason = "terminal",
  };
}

bool warm_up(NBoardSession* nboard, const LoadedEvaluator& loaded, const Args& args,
             std::string* error) {
  const std::vector<board_core::Move> no_moves;
  if (!nboard->set_game(no_moves, error) ||
      !nboard->go(std::chrono::milliseconds{std::max(15000, args.time_ms * 5)}, error)
           .has_value()) {
    return false;
  }

  const search::SearchSessionConfig session_config{
      .profile = search::SearchPlatformProfile::native,
      .transposition_table =
          search::TranspositionTableConfig{
              .capacity = static_cast<std::size_t>(args.tt_bytes),
              .unit = search::TranspositionTableCapacityUnit::bytes,
          },
  };
  search::SearchSession session{session_config};
  const search::SearchLimits limits{
      .max_time = std::chrono::milliseconds{args.time_ms},
      .infinite = true,
  };
  const search::SearchResult result =
      search::search_iterative(session, board_core::initial_position(), loaded.evaluator, limits,
                               strongest_options(args.exact_endgame_empties));
  if (!result.best_move.has_value()) {
    *error = "Vibe warm-up returned no move";
    return false;
  }
  return true;
}

bool write_report(const Args& args, const LoadedEvaluator& loaded,
                  std::span<const GameRecord> games, std::string* error) {
  if (args.report_out.empty()) {
    return true;
  }
  std::ofstream output(args.report_out);
  if (!output) {
    *error = "cannot write report: " + args.report_out.string();
    return false;
  }
  const Summary summary = summarize(games);
  output << "{\n"
         << "  \"schema_version\": 3,\n"
         << "  \"candidate\": \"vibe\",\n"
         << "  \"baseline\": \"" << json_escape(args.nboard_name) << "\",\n"
         << "  \"artifact_id\": \"" << json_escape(loaded.artifact_id) << "\",\n"
         << "  \"search_preset\": \"full\",\n"
         << "  \"time_ms\": " << args.time_ms << ",\n"
         << "  \"tt_bytes\": " << args.tt_bytes << ",\n"
         << "  \"exact_endgame_empties\": " << args.exact_endgame_empties << ",\n"
         << "  \"warmup\": " << (args.warmup ? "true" : "false") << ",\n"
         << "  \"nboard\": {\n"
         << "    \"engine_name\": \"" << json_escape(args.nboard_name) << "\",\n"
         << "    \"executable\": \"" << json_escape(args.nboard_exe.string()) << "\",\n"
         << "    \"runtime_id\": \"" << json_escape(args.nboard_runtime_id) << "\",\n"
         << "    \"protocol_version\": " << static_cast<int>(args.nboard_protocol) << ",\n"
         << "    \"depth\": " << args.nboard_depth << ",\n"
         << "    \"arguments\": [";
  for (std::size_t index = 0; index < args.nboard_args.size(); ++index) {
    output << (index == 0 ? "" : ", ") << '"' << json_escape(args.nboard_args[index]) << '"';
  }
  output << "]\n"
         << "  },\n"
         << "  \"summary\": {\"games\": " << summary.games
         << ", \"wins\": " << summary.candidate_wins << ", \"draws\": " << summary.candidate_draws
         << ", \"losses\": " << summary.candidate_losses
         << ", \"score\": " << summary.candidate_score << "},\n"
         << "  \"games\": [\n";
  for (std::size_t index = 0; index < games.size(); ++index) {
    const GameRecord& game = games[index];
    output << "    {\"game_id\": " << game.game_id << ", \"opening\": \""
           << json_escape(game.opening) << "\", \"black\": \"" << json_escape(game.black)
           << "\", \"white\": \"" << json_escape(game.white)
           << "\", \"black_discs\": " << game.black_discs
           << ", \"white_discs\": " << game.white_discs << ", \"candidate_result\": \""
           << game.candidate_result << "\", \"candidate_disc_diff\": " << game.candidate_disc_diff
           << ", \"moves\": \"" << json_escape(game.moves) << "\"}"
           << (index + 1 == games.size() ? "\n" : ",\n");
  }
  output << "  ]\n}\n";
  if (!output) {
    *error = "failed while writing report: " + args.report_out.string();
    return false;
  }
  return true;
}

int run(const Args& args) {
  std::string error;
  const auto opening_text = read_file(args.openings_path, &error);
  if (!opening_text.has_value()) {
    std::cerr << error << '\n';
    return 1;
  }
  auto openings = parse_openings_file(*opening_text, &error);
  if (!openings.has_value()) {
    std::cerr << error << '\n';
    return 1;
  }
  openings->resize(std::min(openings->size(), static_cast<std::size_t>(args.max_openings)));

  auto loaded = load_evaluator(args.artifact_manifest, &error);
  if (!loaded.has_value()) {
    std::cerr << error << '\n';
    return 1;
  }

  std::vector<std::string> argv{args.nboard_exe.string()};
  argv.insert(argv.end(), args.nboard_args.begin(), args.nboard_args.end());
  NBoardSession nboard;
  const NBoardSessionOptions nboard_options{
      .process =
          PersistentProcessOptions{
              .argv = std::move(argv),
              .working_directory = args.nboard_working_directory,
              .stderr_path = args.nboard_stderr,
              .environment = {{"OMP_NUM_THREADS", "1"}},
          },
      .protocol_version = args.nboard_protocol,
      .startup_timeout = std::chrono::seconds{30},
      .command_timeout = std::chrono::seconds{30},
  };
  if (!nboard.start(nboard_options, &error) || !nboard.set_depth(args.nboard_depth, &error)) {
    std::cerr << "NBoard startup failed: " << error << '\n';
    return 1;
  }
  std::cout << "started NBoard name=" << args.nboard_name
            << " protocol=" << static_cast<int>(args.nboard_protocol)
            << " runtime_id=" << args.nboard_runtime_id << " pid=" << nboard.process_id()
            << " artifact=" << loaded->artifact_id << " preset=full time_ms=" << args.time_ms
            << " tt_bytes=" << args.tt_bytes << '\n'
            << std::flush;

  if (args.warmup) {
    std::cout << "warm-up: one search per engine\n" << std::flush;
    if (!warm_up(&nboard, *loaded, args, &error)) {
      std::cerr << "warm-up failed: " << error << '\n';
      return 1;
    }
  }

  std::vector<GameRecord> games;
  games.reserve(openings->size() * 2);
  int game_id = 0;
  for (const Opening& opening : *openings) {
    for (const bool candidate_is_black : {true, false}) {
      ++game_id;
      std::cout << "game=" << game_id << " opening=" << opening.id
                << " vibe_color=" << (candidate_is_black ? "black" : "white") << '\n'
                << std::flush;
      auto game = play_game(game_id, opening, candidate_is_black, &nboard, *loaded, args, &error);
      if (!game.has_value()) {
        std::cerr << "game " << game_id << " failed: " << error << '\n';
        return 1;
      }
      std::cout << "game=" << game_id << " result=" << game->candidate_result
                << " candidate_disc_diff=" << game->candidate_disc_diff
                << " final=" << game->black_discs << '-' << game->white_discs << '\n'
                << std::flush;
      games.push_back(std::move(*game));
    }
  }

  const Summary summary = summarize(games);
  std::cout << "summary games=" << summary.games << " wins=" << summary.candidate_wins
            << " draws=" << summary.candidate_draws << " losses=" << summary.candidate_losses
            << " score=" << summary.candidate_score << '/' << summary.games << '\n';
  if (!write_report(args, *loaded, games, &error)) {
    std::cerr << error << '\n';
    return 1;
  }
  return 0;
}

} // namespace
} // namespace vibe_othello::tools::arena

int main(int argc, char** argv) {
  std::string error;
  const auto args = vibe_othello::tools::arena::parse_args(argc, argv, &error);
  if (!args.has_value()) {
    if (!error.empty()) {
      std::cerr << error << '\n';
      vibe_othello::tools::arena::print_usage(std::cerr);
      return 2;
    }
    return 0;
  }
  return vibe_othello::tools::arena::run(*args);
}
