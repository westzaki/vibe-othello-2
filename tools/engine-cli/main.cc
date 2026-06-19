#include "vibe_othello/board_core/board.h"
#include "vibe_othello/search/search.h"

#include <bit>
#include <charconv>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using vibe_othello::board_core::Move;
using vibe_othello::board_core::MoveDelta;
using vibe_othello::board_core::MoveKind;
using vibe_othello::board_core::Position;
using vibe_othello::board_core::Square;
using vibe_othello::search::Depth;
using vibe_othello::search::Evaluator;
using vibe_othello::search::Score;

class DiscDifferenceEvaluator final : public Evaluator {
public:
  Score evaluate(const Position& position) const noexcept override {
    return static_cast<Score>(std::popcount(position.player)) -
           static_cast<Score>(std::popcount(position.opponent));
  }
};

struct Args {
  std::string moves;
  Depth depth = 0;
};

std::optional<int> parse_int(std::string_view text) {
  int value = 0;
  const char* begin = text.data();
  const char* end = text.data() + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

std::optional<Square> parse_square(std::string_view text) {
  if (text.size() != 2) {
    return std::nullopt;
  }
  const char file_char = text[0];
  const char rank_char = text[1];
  if (file_char < 'a' || file_char > 'h' || rank_char < '1' || rank_char > '8') {
    return std::nullopt;
  }
  return vibe_othello::board_core::square_from_file_rank(file_char - 'a', rank_char - '1');
}

std::string format_move(Move move) {
  if (move.kind == MoveKind::pass) {
    return "pass";
  }
  const int file = vibe_othello::board_core::file_of(move.square);
  const int rank = vibe_othello::board_core::rank_of(move.square);
  std::string text;
  text.push_back(static_cast<char>('a' + file));
  text.push_back(static_cast<char>('1' + rank));
  return text;
}

std::string format_best_move(const std::optional<Move>& move) {
  if (!move.has_value()) {
    return "none";
  }
  return format_move(*move);
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

bool replay_moves(std::string_view moves, Position* position, std::string* error) {
  *position = vibe_othello::board_core::initial_position();
  for (std::string_view token : split_words(moves)) {
    Move move = vibe_othello::board_core::make_pass();
    if (token == "pass") {
      if (vibe_othello::board_core::has_legal_move(*position)) {
        *error = "pass is illegal while legal moves exist";
        return false;
      }
    } else {
      const std::optional<Square> square = parse_square(token);
      if (!square.has_value()) {
        *error = "invalid move coordinate";
        return false;
      }
      move = vibe_othello::board_core::make_move(*square);
    }

    MoveDelta delta{};
    const bool ok = move.kind == MoveKind::pass
                        ? vibe_othello::board_core::apply_pass(position, &delta)
                        : vibe_othello::board_core::apply_move(position, move, &delta);
    if (!ok) {
      *error = "illegal move sequence";
      return false;
    }
  }
  return true;
}

std::optional<Args> parse_args(int argc, char** argv) {
  if (argc < 2 || std::string_view{argv[1]} != "bestmove") {
    std::cerr << "usage: vibe-othello-engine-cli bestmove --moves \"d3 c3\" --depth 4\n";
    return std::nullopt;
  }

  Args args;
  bool saw_depth = false;
  for (int index = 2; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    if (arg == "--moves") {
      if (index + 1 >= argc) {
        std::cerr << "--moves requires a value\n";
        return std::nullopt;
      }
      args.moves = argv[++index];
    } else if (arg == "--depth") {
      if (index + 1 >= argc) {
        std::cerr << "--depth requires a value\n";
        return std::nullopt;
      }
      const std::optional<int> depth = parse_int(argv[++index]);
      if (!depth.has_value() || *depth < 0) {
        std::cerr << "--depth must be a non-negative integer\n";
        return std::nullopt;
      }
      args.depth = static_cast<Depth>(*depth);
      saw_depth = true;
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      return std::nullopt;
    }
  }
  if (!saw_depth) {
    std::cerr << "--depth is required\n";
    return std::nullopt;
  }
  return args;
}

} // namespace

int main(int argc, char** argv) {
  const std::optional<Args> args = parse_args(argc, argv);
  if (!args.has_value()) {
    return 2;
  }

  Position position{};
  std::string error;
  if (!replay_moves(args->moves, &position, &error)) {
    std::cerr << error << '\n';
    return 2;
  }

  const DiscDifferenceEvaluator evaluator;
  const vibe_othello::search::SearchResult result =
      vibe_othello::search::search_fixed_depth(position, evaluator, args->depth);

  std::cout << "bestmove " << format_best_move(result.best_move) << " score " << result.score
            << " depth " << result.completed_depth << '\n';
  return 0;
}
