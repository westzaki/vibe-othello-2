#include "arena_core.h"
#include "vibe_othello/board_core/board.h"

#include <bit>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

using vibe_othello::board_core::Move;
using vibe_othello::board_core::MoveDelta;
using vibe_othello::board_core::MoveKind;
using vibe_othello::board_core::Position;

struct Args {
  bool echo = false;
  bool ignore_ping = false;
  std::optional<int> expected_protocol;
  std::optional<std::string> report_environment;
};

struct EngineState {
  std::optional<int> protocol_version;
  std::optional<int> depth;
  std::optional<Position> position;
};

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

std::optional<Args> parse_args(int argc, char** argv) {
  Args args;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--echo") {
      args.echo = true;
    } else if (argument == "--ignore-ping") {
      args.ignore_ping = true;
    } else if (argument == "--expect-protocol") {
      if (index + 1 >= argc) {
        return std::nullopt;
      }
      args.expected_protocol = parse_int(argv[++index]);
      if (!args.expected_protocol.has_value() ||
          (*args.expected_protocol != 1 && *args.expected_protocol != 2)) {
        return std::nullopt;
      }
    } else if (argument == "--report-runtime") {
      if (index + 1 >= argc) {
        return std::nullopt;
      }
      args.report_environment = argv[++index];
    } else {
      return std::nullopt;
    }
  }
  return args;
}

std::string_view value_after(std::string_view line, std::string_view prefix) {
  if (!line.starts_with(prefix)) {
    return {};
  }
  line.remove_prefix(prefix.size());
  return line;
}

std::optional<Move> parse_nboard_move(std::string_view token) {
  std::string normalized{token};
  for (char& character : normalized) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  if (normalized == "pa" || normalized == "pass") {
    return vibe_othello::board_core::make_pass();
  }
  return vibe_othello::tools::arena::parse_move_token(normalized);
}

bool apply_move(Position* position, Move move) {
  MoveDelta delta{};
  return move.kind == MoveKind::pass ? vibe_othello::board_core::apply_pass(position, &delta)
                                     : vibe_othello::board_core::apply_move(position, move, &delta);
}

bool parse_game(std::string_view ggf, Position* position) {
  constexpr std::string_view kBoardMarker = "BO[8 ";
  const std::size_t board_marker = ggf.find(kBoardMarker);
  if (!ggf.starts_with("(;GM[Othello]") || board_marker == std::string_view::npos ||
      !ggf.ends_with(";)") || board_marker + kBoardMarker.size() >= ggf.size()) {
    return false;
  }
  const std::size_t board_end = ggf.find(']', board_marker + kBoardMarker.size());
  if (board_end == std::string_view::npos) {
    return false;
  }

  *position = vibe_othello::board_core::initial_position();
  std::size_t cursor = board_end + 1;
  while (cursor < ggf.size()) {
    if (ggf.substr(cursor) == ";)") {
      return true;
    }
    if (cursor + 3 > ggf.size() || (ggf[cursor] != 'B' && ggf[cursor] != 'W') ||
        ggf[cursor + 1] != '[') {
      return false;
    }
    const bool black_move = ggf[cursor] == 'B';
    if (black_move != (position->side_to_move == vibe_othello::board_core::Color::black)) {
      return false;
    }
    const std::size_t move_end = ggf.find(']', cursor + 2);
    if (move_end == std::string_view::npos) {
      return false;
    }
    const std::optional<Move> move =
        parse_nboard_move(ggf.substr(cursor + 2, move_end - cursor - 2));
    if (!move.has_value() || !apply_move(position, *move)) {
      return false;
    }
    cursor = move_end + 1;
  }
  return false;
}

std::string format_nboard_move(Move move) {
  if (move.kind == MoveKind::pass) {
    return "PA";
  }
  std::string text = vibe_othello::tools::arena::format_move(move);
  for (char& character : text) {
    character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
  }
  return text;
}

bool reject(std::string_view message) {
  std::cerr << "fake NBoard protocol error: " << message << std::endl;
  return false;
}

bool handle_command(std::string_view line, const Args& args, EngineState* state) {
  if (const std::string_view version = value_after(line, "nboard "); !version.empty()) {
    const std::optional<int> parsed = parse_int(version);
    if (!parsed.has_value() || (*parsed != 1 && *parsed != 2) ||
        (args.expected_protocol.has_value() && *parsed != *args.expected_protocol)) {
      return reject("unexpected protocol version");
    }
    state->protocol_version = *parsed;
    std::cout << "set myname FakeNBoard" << std::endl;
    return true;
  }
  if (!state->protocol_version.has_value()) {
    return reject("command received before nboard initialization");
  }
  if (const std::string_view ping = value_after(line, "ping "); !ping.empty()) {
    if (!args.ignore_ping) {
      std::cout << "status synchronized" << std::endl;
      std::cout << "pong " << ping << std::endl;
    }
    return true;
  }

  const std::string_view depth_prefix = *state->protocol_version == 1 ? "depth " : "set depth ";
  if (const std::string_view depth = value_after(line, depth_prefix); !depth.empty()) {
    const std::optional<int> parsed = parse_int(depth);
    if (!parsed.has_value() || *parsed <= 0) {
      return reject("invalid depth command");
    }
    state->depth = *parsed;
    return true;
  }

  const std::string_view game_prefix = *state->protocol_version == 1 ? "game " : "set game ";
  if (const std::string_view game = value_after(line, game_prefix); !game.empty()) {
    Position position{};
    if (!parse_game(game, &position)) {
      return reject("invalid game command");
    }
    state->position = position;
    return true;
  }

  if (const std::string_view move_text = value_after(line, "move "); !move_text.empty()) {
    const std::optional<Move> move = parse_nboard_move(move_text);
    if (!state->position.has_value() || !move.has_value() ||
        !apply_move(&*state->position, *move)) {
      return reject("invalid or illegal move command");
    }
    return true;
  }

  if (line == "go") {
    if (!state->depth.has_value() || !state->position.has_value()) {
      return reject("go received before depth and game setup");
    }
    const vibe_othello::board_core::Bitboard legal =
        vibe_othello::board_core::legal_moves(*state->position);
    const Move move = legal == 0
                          ? vibe_othello::board_core::make_pass()
                          : vibe_othello::board_core::make_move(vibe_othello::board_core::Square{
                                static_cast<std::uint8_t>(std::countr_zero(legal))});
    std::cout << "status thinking" << std::endl;
    std::cout << "nodestats 123 0.01" << std::endl;
    std::cout << "=== " << format_nboard_move(move)
              << (*state->protocol_version == 1 ? " 1.25 0.01" : "/1.25/0.01") << std::endl;
    std::cout << "status waiting" << std::endl;
    return true;
  }
  return reject("unsupported command: " + std::string{line});
}

} // namespace

int main(int argc, char** argv) {
  const std::optional<Args> args = parse_args(argc, argv);
  if (!args.has_value()) {
    return 2;
  }
  if (args->report_environment.has_value()) {
    const char* value = std::getenv(args->report_environment->c_str());
    std::cout << "cwd " << std::filesystem::current_path().string() << std::endl;
    std::cout << "env " << (value == nullptr ? "" : value) << std::endl;
  }

  EngineState state;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (args->echo) {
      std::cout << line << std::endl;
      if (line == "quit") {
        return 0;
      }
      continue;
    }
    if (line == "quit") {
      return 0;
    }
    if (!handle_command(line, *args, &state)) {
      return 3;
    }
  }
  return 0;
}
