#include "arena_core.h"
#include "vibe_othello/board_core/position.h"

#include <bit>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using vibe_othello::board_core::Bitboard;
using vibe_othello::board_core::Color;
using vibe_othello::board_core::Move;
using vibe_othello::board_core::MoveDelta;
using vibe_othello::board_core::MoveKind;
using vibe_othello::board_core::Position;
using vibe_othello::tools::arena::BestMoveResponse;
using vibe_othello::tools::arena::GameRecord;
using vibe_othello::tools::arena::Opening;
using vibe_othello::tools::arena::Summary;

struct Args {
  std::string baseline_cmd;
  std::string candidate_cmd;
  std::filesystem::path openings;
  std::filesystem::path out;
  bool swap_colors = false;
  int timeout_ms = 5000;
};

enum class EngineRole {
  baseline,
  candidate,
};

struct EngineCallResult {
  std::string stdout_text;
  std::string reason;
};

std::string role_name(EngineRole role) {
  return role == EngineRole::candidate ? "candidate" : "baseline";
}

std::string shell_quote(std::string_view text) {
  std::string quoted = "'";
  for (const char ch : text) {
    if (ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted.push_back(ch);
    }
  }
  quoted.push_back('\'');
  return quoted;
}

std::optional<int> parse_int(std::string_view text) {
  try {
    std::size_t parsed = 0;
    const int value = std::stoi(std::string{text}, &parsed);
    if (parsed != text.size()) {
      return std::nullopt;
    }
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

std::vector<std::string_view> split_words(std::string_view text) {
  std::vector<std::string_view> words;
  std::size_t pos = 0;
  while (pos < text.size()) {
    while (pos < text.size() && text[pos] == ' ') {
      ++pos;
    }
    const std::size_t begin = pos;
    while (pos < text.size() && text[pos] != ' ') {
      ++pos;
    }
    if (begin != pos) {
      words.push_back(text.substr(begin, pos - begin));
    }
  }
  return words;
}

std::optional<int> infer_depth(std::string_view command) {
  const std::vector<std::string_view> words = split_words(command);
  for (std::size_t index = 0; index + 1 < words.size(); ++index) {
    if (words[index] == "--depth") {
      return parse_int(words[index + 1]);
    }
  }
  return std::nullopt;
}

std::optional<int> manifest_depth(const Args& args) {
  const std::optional<int> baseline_depth = infer_depth(args.baseline_cmd);
  const std::optional<int> candidate_depth = infer_depth(args.candidate_cmd);
  if (baseline_depth.has_value() && candidate_depth.has_value() &&
      *baseline_depth == *candidate_depth) {
    return baseline_depth;
  }
  return std::nullopt;
}

void print_usage() {
  std::cerr << "usage: vibe-othello-arena --baseline-cmd CMD --candidate-cmd CMD --openings FILE "
               "--out DIR [--swap-colors] [--timeout-ms 5000]\n";
}

std::optional<Args> parse_args(int argc, char** argv) {
  Args args;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    if (arg == "--baseline-cmd" || arg == "--candidate-cmd" || arg == "--openings" ||
        arg == "--out" || arg == "--timeout-ms") {
      if (index + 1 >= argc) {
        std::cerr << arg << " requires a value\n";
        return std::nullopt;
      }
      const std::string_view value{argv[++index]};
      if (arg == "--baseline-cmd") {
        args.baseline_cmd = value;
      } else if (arg == "--candidate-cmd") {
        args.candidate_cmd = value;
      } else if (arg == "--openings") {
        args.openings = value;
      } else if (arg == "--out") {
        args.out = value;
      } else {
        const std::optional<int> timeout = parse_int(value);
        if (!timeout.has_value() || *timeout <= 0) {
          std::cerr << "--timeout-ms must be a positive integer\n";
          return std::nullopt;
        }
        args.timeout_ms = *timeout;
      }
    } else if (arg == "--swap-colors") {
      args.swap_colors = true;
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      return std::nullopt;
    }
  }

  if (args.baseline_cmd.empty() || args.candidate_cmd.empty() || args.openings.empty() ||
      args.out.empty()) {
    print_usage();
    return std::nullopt;
  }
  return args;
}

EngineCallResult run_engine_command(std::string_view engine_cmd, std::string_view moves,
                                    int timeout_ms) {
  int pipe_fds[2] = {-1, -1};
  if (pipe(pipe_fds) != 0) {
    return EngineCallResult{.reason = "crash"};
  }

  const std::string command = std::string{engine_cmd} + " --moves " + shell_quote(moves);
  const pid_t pid = fork();
  if (pid < 0) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return EngineCallResult{.reason = "crash"};
  }

  if (pid == 0) {
    setpgid(0, 0);
    close(pipe_fds[0]);
    dup2(pipe_fds[1], STDOUT_FILENO);
    const int dev_null = open("/dev/null", O_WRONLY);
    if (dev_null >= 0) {
      dup2(dev_null, STDERR_FILENO);
    }
    execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }

  close(pipe_fds[1]);
  fcntl(pipe_fds[0], F_SETFL, fcntl(pipe_fds[0], F_GETFL, 0) | O_NONBLOCK);

  std::string stdout_text;
  int status = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  bool exited = false;
  bool timed_out = false;
  while (true) {
    char buffer[4096];
    while (true) {
      const ssize_t bytes = read(pipe_fds[0], buffer, sizeof(buffer));
      if (bytes > 0) {
        stdout_text.append(buffer, static_cast<std::size_t>(bytes));
      } else {
        break;
      }
    }

    const pid_t wait_result = waitpid(pid, &status, WNOHANG);
    if (wait_result == pid) {
      exited = true;
      break;
    }

    if (std::chrono::steady_clock::now() >= deadline) {
      timed_out = true;
      kill(-pid, SIGKILL);
      waitpid(pid, &status, 0);
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  char buffer[4096];
  while (true) {
    const ssize_t bytes = read(pipe_fds[0], buffer, sizeof(buffer));
    if (bytes > 0) {
      stdout_text.append(buffer, static_cast<std::size_t>(bytes));
    } else {
      break;
    }
  }
  close(pipe_fds[0]);

  if (timed_out) {
    return EngineCallResult{.stdout_text = stdout_text, .reason = "timeout"};
  }
  if (!exited || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    return EngineCallResult{.stdout_text = stdout_text, .reason = "crash"};
  }
  return EngineCallResult{.stdout_text = stdout_text, .reason = "ok"};
}

std::string first_line(std::string_view text) {
  if (const std::size_t newline = text.find('\n'); newline != std::string_view::npos) {
    return std::string{text.substr(0, newline)};
  }
  return std::string{text};
}

int disc_count(Bitboard bits) {
  return std::popcount(bits);
}

std::string winner_from_discs(int black_discs, int white_discs) {
  if (black_discs > white_discs) {
    return "black";
  }
  if (white_discs > black_discs) {
    return "white";
  }
  return "draw";
}

std::string candidate_result(std::string_view winner, EngineRole black, EngineRole white) {
  if (winner == "draw") {
    return "draw";
  }
  const EngineRole winner_role = winner == "black" ? black : white;
  return winner_role == EngineRole::candidate ? "win" : "loss";
}

int candidate_disc_diff(int black_discs, int white_discs, EngineRole black) {
  const int black_diff = black_discs - white_discs;
  return black == EngineRole::candidate ? black_diff : -black_diff;
}

GameRecord adjudicated_game(int game_id, const Opening& opening, std::span<const Move> moves,
                            EngineRole black, EngineRole white, EngineRole offender,
                            std::string reason) {
  const bool offender_is_black = offender == black;
  const int black_discs = offender_is_black ? 0 : 64;
  const int white_discs = offender_is_black ? 64 : 0;
  const std::string winner = offender_is_black ? "white" : "black";
  return GameRecord{
      .game_id = game_id,
      .opening = opening.id,
      .black = role_name(black),
      .white = role_name(white),
      .moves = vibe_othello::tools::arena::format_moves(moves),
      .black_discs = black_discs,
      .white_discs = white_discs,
      .winner = winner,
      .candidate_result = candidate_result(winner, black, white),
      .candidate_disc_diff = candidate_disc_diff(black_discs, white_discs, black),
      .reason = std::move(reason),
  };
}

GameRecord play_game(int game_id, const Opening& opening, EngineRole black, EngineRole white,
                     const Args& args) {
  Position position{};
  std::string error;
  if (!vibe_othello::tools::arena::replay_moves(opening.moves, &position, &error)) {
    return adjudicated_game(game_id, opening, opening.moves, black, white, EngineRole::candidate,
                            "invalid_opening");
  }

  std::vector<Move> moves = opening.moves;
  while (!vibe_othello::board_core::is_terminal(position)) {
    const Bitboard legal_moves = vibe_othello::board_core::legal_moves(position);
    if (legal_moves == 0) {
      MoveDelta delta{};
      vibe_othello::board_core::apply_pass(&position, &delta);
      moves.push_back(vibe_othello::board_core::make_pass());
      continue;
    }

    const EngineRole side_to_move = position.side_to_move == Color::black ? black : white;
    const std::string& engine_cmd =
        side_to_move == EngineRole::candidate ? args.candidate_cmd : args.baseline_cmd;
    const EngineCallResult call = run_engine_command(
        engine_cmd, vibe_othello::tools::arena::format_moves(moves), args.timeout_ms);
    if (call.reason != "ok") {
      return adjudicated_game(game_id, opening, moves, black, white, side_to_move, call.reason);
    }

    const std::optional<BestMoveResponse> response =
        vibe_othello::tools::arena::parse_bestmove_response(first_line(call.stdout_text));
    if (!response.has_value()) {
      return adjudicated_game(game_id, opening, moves, black, white, side_to_move, "parse_error");
    }
    if (!response->move.has_value() || response->move->kind != MoveKind::normal ||
        (legal_moves & vibe_othello::board_core::bit(response->move->square)) == 0) {
      return adjudicated_game(game_id, opening, moves, black, white, side_to_move, "illegal_move");
    }

    MoveDelta delta{};
    if (!vibe_othello::board_core::apply_move(&position, *response->move, &delta)) {
      return adjudicated_game(game_id, opening, moves, black, white, side_to_move, "illegal_move");
    }
    moves.push_back(*response->move);
  }

  const int black_discs = disc_count(vibe_othello::board_core::black_discs(position));
  const int white_discs = disc_count(vibe_othello::board_core::white_discs(position));
  const std::string winner = winner_from_discs(black_discs, white_discs);
  return GameRecord{
      .game_id = game_id,
      .opening = opening.id,
      .black = role_name(black),
      .white = role_name(white),
      .moves = vibe_othello::tools::arena::format_moves(moves),
      .black_discs = black_discs,
      .white_discs = white_discs,
      .winner = winner,
      .candidate_result = candidate_result(winner, black, white),
      .candidate_disc_diff = candidate_disc_diff(black_discs, white_discs, black),
      .reason = "terminal",
  };
}

void write_manifest(const Args& args) {
  const std::optional<int> depth = manifest_depth(args);
  std::ofstream out{args.out / "manifest.json"};
  out << "{\n"
      << "  \"baseline_cmd\": \"" << vibe_othello::tools::arena::json_escape(args.baseline_cmd)
      << "\",\n"
      << "  \"candidate_cmd\": \"" << vibe_othello::tools::arena::json_escape(args.candidate_cmd)
      << "\",\n"
      << "  \"openings\": \"" << vibe_othello::tools::arena::json_escape(args.openings.string())
      << "\",\n"
      << "  \"swap_colors\": " << (args.swap_colors ? "true" : "false") << ",\n"
      << "  \"depth\": ";
  if (depth.has_value()) {
    out << *depth;
  } else {
    out << "null";
  }
  out << ",\n"
      << "  \"timeout_ms\": " << args.timeout_ms << "\n"
      << "}\n";
}

void write_games(const std::filesystem::path& out_dir, std::span<const GameRecord> games) {
  std::ofstream out{out_dir / "games.jsonl"};
  for (const GameRecord& game : games) {
    out << "{\"game_id\":" << game.game_id << ",\"opening\":\""
        << vibe_othello::tools::arena::json_escape(game.opening) << "\",\"black\":\"" << game.black
        << "\",\"white\":\"" << game.white << "\",\"moves\":\""
        << vibe_othello::tools::arena::json_escape(game.moves)
        << "\",\"black_discs\":" << game.black_discs << ",\"white_discs\":" << game.white_discs
        << ",\"winner\":\"" << game.winner << "\",\"candidate_result\":\"" << game.candidate_result
        << "\",\"candidate_disc_diff\":" << game.candidate_disc_diff << ",\"reason\":\""
        << game.reason << "\"}\n";
  }
}

void write_summary(const std::filesystem::path& out_dir, const Summary& summary) {
  std::ofstream out{out_dir / "summary.json"};
  out << "{\n"
      << "  \"games\": " << summary.games << ",\n"
      << "  \"candidate_wins\": " << summary.candidate_wins << ",\n"
      << "  \"candidate_draws\": " << summary.candidate_draws << ",\n"
      << "  \"candidate_losses\": " << summary.candidate_losses << ",\n"
      << "  \"candidate_score\": " << summary.candidate_score << ",\n"
      << "  \"candidate_win_rate\": " << summary.candidate_win_rate << ",\n"
      << "  \"candidate_avg_disc_diff\": " << summary.candidate_avg_disc_diff << ",\n"
      << "  \"invalid_games\": " << summary.invalid_games << "\n"
      << "}\n";
}

} // namespace

int main(int argc, char** argv) {
  const std::optional<Args> args = parse_args(argc, argv);
  if (!args.has_value()) {
    return 2;
  }

  std::ifstream openings_file{args->openings};
  if (!openings_file) {
    std::cerr << "failed to open openings file: " << args->openings << '\n';
    return 2;
  }
  const std::string openings_text{std::istreambuf_iterator<char>{openings_file},
                                  std::istreambuf_iterator<char>{}};

  std::string error;
  const std::optional<std::vector<Opening>> openings =
      vibe_othello::tools::arena::parse_openings_file(openings_text, &error);
  if (!openings.has_value()) {
    std::cerr << "invalid openings file: " << error << '\n';
    return 2;
  }

  std::filesystem::create_directories(args->out);

  std::vector<GameRecord> games;
  int game_id = 1;
  for (const Opening& opening : *openings) {
    games.push_back(
        play_game(game_id++, opening, EngineRole::candidate, EngineRole::baseline, *args));
    if (args->swap_colors) {
      games.push_back(
          play_game(game_id++, opening, EngineRole::baseline, EngineRole::candidate, *args));
    }
  }

  write_manifest(*args);
  write_games(args->out, games);
  write_summary(args->out, vibe_othello::tools::arena::summarize(games));

  std::cout << "wrote " << games.size() << " games to " << args->out << '\n';
  return 0;
}
