#include "vibe_othello/board_core/board.h"
#include "vibe_othello/search/search.h"

#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using vibe_othello::board_core::apply_move;
using vibe_othello::board_core::bit;
using vibe_othello::board_core::Bitboard;
using vibe_othello::board_core::initial_position;
using vibe_othello::board_core::legal_moves;
using vibe_othello::board_core::make_move;
using vibe_othello::board_core::make_pass;
using vibe_othello::board_core::Move;
using vibe_othello::board_core::MoveDelta;
using vibe_othello::board_core::MoveKind;
using vibe_othello::board_core::Position;
using vibe_othello::board_core::Square;
using vibe_othello::board_core::square_from_file_rank;
using vibe_othello::board_core::square_from_index;
using vibe_othello::search::Depth;
using vibe_othello::search::Evaluator;
using vibe_othello::search::Score;
using vibe_othello::search::search_fixed_depth;
using vibe_othello::search::search_iterative;
using vibe_othello::search::SearchLimits;
using vibe_othello::search::SearchOptions;
using vibe_othello::search::SearchResult;

enum class ModeFilter {
  all,
  fixed,
  iterative,
};

enum class BenchmarkMode {
  fixed,
  iterative,
};

enum class TTMode {
  off,
  ordering,
  midgame,
  both,
};

struct Config {
  std::vector<Depth> depths{Depth{6}, Depth{7}, Depth{8}};
  ModeFilter mode_filter = ModeFilter::all;
  TTMode tt_mode = TTMode::off;
  char delimiter = '\t';
};

struct PositionCase {
  std::string_view name;
  Position position;
};

struct TimedResult {
  SearchResult result;
  std::chrono::nanoseconds elapsed;
};

class DiscDifferenceEvaluator final : public Evaluator {
public:
  Score evaluate(const Position& position) const noexcept override {
    return static_cast<Score>(std::popcount(position.player)) -
           static_cast<Score>(std::popcount(position.opponent));
  }
};

constexpr Square square(int file, int rank) noexcept {
  return square_from_file_rank(file, rank);
}

constexpr Position pass_root_position() noexcept {
  return Position{
      .player = bit(square(1, 0)),
      .opponent = bit(square(0, 0)),
      .side_to_move = vibe_othello::board_core::Color::black,
  };
}

void require_condition(bool condition, std::string_view message) {
  if (condition) {
    return;
  }

  std::cerr << "search_bench: " << message << '\n';
  std::exit(1);
}

Move select_legal_move(Position position, std::size_t choice) {
  const Bitboard legal = legal_moves(position);
  if (legal == 0) {
    return make_pass();
  }

  std::array<Move, vibe_othello::board_core::kSquareCount> moves{};
  std::size_t move_count = 0;
  for (int square_index = 0; square_index < vibe_othello::board_core::kSquareCount;
       ++square_index) {
    const Square move_square = square_from_index(square_index);
    if ((legal & bit(move_square)) != 0) {
      moves[move_count] = make_move(move_square);
      ++move_count;
    }
  }

  require_condition(move_count > 0, "legal move mask did not expand");
  return moves[choice % move_count];
}

Position position_after_fixed_choices(std::initializer_list<std::size_t> choices) {
  Position position = initial_position();
  for (const std::size_t choice : choices) {
    MoveDelta delta{};
    require_condition(apply_move(&position, select_legal_move(position, choice), &delta),
                      "failed to build benchmark position");
  }
  return position;
}

std::vector<PositionCase> benchmark_positions() {
  return {
      PositionCase{.name = "initial", .position = initial_position()},
      PositionCase{.name = "early_balanced",
                   .position = position_after_fixed_choices({1, 9, 10, 5, 13, 2, 5, 4})},
      PositionCase{.name = "early_high_mobility",
                   .position = position_after_fixed_choices({8, 4, 8, 5, 8, 7, 11, 2})},
      PositionCase{.name = "midgame_balanced",
                   .position = position_after_fixed_choices(
                       {13, 1, 1, 10, 9, 1, 0, 13, 10, 2, 11, 14, 12, 15, 5, 8, 4, 8, 2, 15})},
      PositionCase{.name = "midgame_high_mobility",
                   .position =
                       position_after_fixed_choices({13, 3,  12, 1,  12, 11, 0, 1, 1, 14, 5,
                                                     4,  10, 12, 12, 14, 14, 8, 4, 3, 8,  3})},
      PositionCase{.name = "corner_available",
                   .position =
                       position_after_fixed_choices({11, 3, 0, 6, 12, 8, 1, 5, 3, 5, 9, 9})},
      PositionCase{.name = "pass_position", .position = pass_root_position()},
      PositionCase{
          .name = "late_midgame",
          .position = position_after_fixed_choices(
              {12, 11, 6, 6, 7,  11, 13, 13, 0, 15, 9, 12, 5,  7,  13, 15, 15, 13, 12, 8, 14, 5,
               3,  4,  2, 1, 12, 14, 5,  14, 4, 0,  9, 11, 13, 15, 12, 13, 2,  7,  5,  5, 7,  6})},
  };
}

std::vector<BenchmarkMode> modes_for_filter(ModeFilter filter) {
  switch (filter) {
  case ModeFilter::all:
    return {BenchmarkMode::fixed, BenchmarkMode::iterative};
  case ModeFilter::fixed:
    return {BenchmarkMode::fixed};
  case ModeFilter::iterative:
    return {BenchmarkMode::iterative};
  }

  return {};
}

std::string_view mode_name(BenchmarkMode mode) noexcept {
  switch (mode) {
  case BenchmarkMode::fixed:
    return "fixed";
  case BenchmarkMode::iterative:
    return "iterative";
  }

  return "unknown";
}

std::optional<ModeFilter> parse_mode_filter(std::string_view value) noexcept {
  if (value == "all") {
    return ModeFilter::all;
  }
  if (value == "fixed") {
    return ModeFilter::fixed;
  }
  if (value == "iterative") {
    return ModeFilter::iterative;
  }
  return std::nullopt;
}

std::string_view tt_mode_name(TTMode mode) noexcept {
  switch (mode) {
  case TTMode::off:
    return "off";
  case TTMode::ordering:
    return "ordering";
  case TTMode::midgame:
    return "midgame";
  case TTMode::both:
    return "both";
  }

  return "unknown";
}

std::optional<TTMode> parse_tt_mode(std::string_view value) noexcept {
  if (value == "off") {
    return TTMode::off;
  }
  if (value == "ordering") {
    return TTMode::ordering;
  }
  if (value == "midgame") {
    return TTMode::midgame;
  }
  if (value == "both") {
    return TTMode::both;
  }
  return std::nullopt;
}

SearchOptions search_options_for_tt_mode(TTMode mode) noexcept {
  switch (mode) {
  case TTMode::off:
    return SearchOptions{};
  case TTMode::ordering:
    return SearchOptions{.use_tt_best_move_ordering = true};
  case TTMode::midgame:
    return SearchOptions{.use_midgame_tt = true};
  case TTMode::both:
    return SearchOptions{.use_midgame_tt = true, .use_tt_best_move_ordering = true};
  }

  return SearchOptions{};
}

void print_usage(std::ostream& output, std::string_view program) {
  output << "Usage: " << program
         << " [--depth N|START..END|START-END]"
            " [--mode all|fixed|iterative] [--tt off|ordering|midgame|both] [--tsv|--csv]\n\n"
         << "Default depth range is 6..8.\n";
}

bool parse_argument_with_value(std::string_view argument, std::string_view name,
                               std::string_view* value) noexcept {
  if (!argument.starts_with(name) || argument.size() <= name.size() ||
      argument[name.size()] != '=') {
    return false;
  }

  *value = argument.substr(name.size() + 1);
  return true;
}

std::optional<int> parse_int(std::string_view value) noexcept {
  if (value.empty()) {
    return std::nullopt;
  }

  int result = 0;
  const char* begin = value.data();
  const char* end = value.data() + value.size();
  const std::from_chars_result parsed = std::from_chars(begin, end, result);
  if (parsed.ec != std::errc{} || parsed.ptr != end) {
    return std::nullopt;
  }
  return result;
}

std::optional<std::vector<Depth>> parse_depths(std::string_view value) {
  std::size_t separator = value.find("..");
  std::size_t separator_size = 2;
  if (separator == std::string_view::npos) {
    separator = value.find('-');
    separator_size = 1;
  }

  if (separator == std::string_view::npos) {
    const std::optional<int> depth = parse_int(value);
    if (!depth.has_value() || *depth < 0 || *depth > std::numeric_limits<Depth>::max()) {
      return std::nullopt;
    }
    return std::vector<Depth>{static_cast<Depth>(*depth)};
  }

  const std::optional<int> first = parse_int(value.substr(0, separator));
  const std::optional<int> last = parse_int(value.substr(separator + separator_size));
  if (!first.has_value() || !last.has_value() || *first < 0 || *last < *first ||
      *last > std::numeric_limits<Depth>::max()) {
    return std::nullopt;
  }

  std::vector<Depth> depths;
  depths.reserve(static_cast<std::size_t>(*last - *first + 1));
  for (int depth = *first; depth <= *last; ++depth) {
    depths.push_back(static_cast<Depth>(depth));
  }
  return depths;
}

std::optional<Config> parse_config(int argc, char** argv) {
  Config config{};
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    std::string_view value;

    if (argument == "--help" || argument == "-h") {
      print_usage(std::cout, argv[0]);
      return std::nullopt;
    }

    if (argument == "--tsv") {
      config.delimiter = '\t';
      continue;
    }
    if (argument == "--csv") {
      config.delimiter = ',';
      continue;
    }

    if (argument == "--depth") {
      require_condition(index + 1 < argc, "--depth requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--depth", &value)) {
      value = {};
    }
    if (!value.empty()) {
      const std::optional<std::vector<Depth>> depths = parse_depths(value);
      require_condition(depths.has_value(), "invalid depth");
      config.depths = *depths;
      continue;
    }

    if (argument == "--mode") {
      require_condition(index + 1 < argc, "--mode requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--mode", &value)) {
      value = {};
    }
    if (!value.empty()) {
      const std::optional<ModeFilter> mode_filter = parse_mode_filter(value);
      require_condition(mode_filter.has_value(), "unknown mode");
      config.mode_filter = *mode_filter;
      continue;
    }

    if (argument == "--tt") {
      require_condition(index + 1 < argc, "--tt requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--tt", &value)) {
      value = {};
    }
    if (!value.empty()) {
      const std::optional<TTMode> tt_mode = parse_tt_mode(value);
      require_condition(tt_mode.has_value(), "unknown TT mode");
      config.tt_mode = *tt_mode;
      continue;
    }

    std::cerr << "search_bench: unknown argument: " << argument << '\n';
    print_usage(std::cerr, argv[0]);
    std::exit(2);
  }

  return config;
}

std::string move_to_string(Move move) {
  if (move.kind == MoveKind::pass) {
    return "pass";
  }

  const int file = vibe_othello::board_core::file_of(move.square);
  const int rank = vibe_othello::board_core::rank_of(move.square);
  if (file < 0 || rank < 0) {
    return "none";
  }

  std::string text;
  text.push_back(static_cast<char>('a' + file));
  text.push_back(static_cast<char>('1' + rank));
  return text;
}

std::string best_move_to_string(const SearchResult& result) {
  if (!result.best_move.has_value()) {
    return "none";
  }
  return move_to_string(*result.best_move);
}

TimedResult run_search(BenchmarkMode mode, TTMode tt_mode, Position position, Depth depth) {
  DiscDifferenceEvaluator evaluator;
  const auto start = std::chrono::steady_clock::now();

  SearchResult result =
      mode == BenchmarkMode::fixed
          ? search_fixed_depth(position, evaluator, depth)
          : search_iterative(position, evaluator, SearchLimits{.max_depth = depth},
                             search_options_for_tt_mode(tt_mode));

  return TimedResult{
      .result = result,
      .elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - start),
  };
}

void print_header(char delimiter) {
  std::cout << "position_name" << delimiter << "mode" << delimiter << "tt_mode" << delimiter
            << "depth" << delimiter << "score" << delimiter << "best_move" << delimiter << "nodes"
            << delimiter << "eval_calls" << delimiter << "beta_cutoffs" << delimiter
            << "alpha_updates" << delimiter << "tt_probes" << delimiter << "tt_hits" << delimiter
            << "tt_stores" << delimiter << "tt_cutoffs" << delimiter << "elapsed_ms" << delimiter
            << "nps" << '\n';
}

void print_result(PositionCase position_case, BenchmarkMode mode, TTMode tt_mode, Depth depth,
                  TimedResult timed_result, char delimiter) {
  const double elapsed_ms = std::chrono::duration<double, std::milli>(timed_result.elapsed).count();
  const double elapsed_seconds = std::chrono::duration<double>(timed_result.elapsed).count();
  const double nps = elapsed_seconds > 0.0
                         ? static_cast<double>(timed_result.result.nodes) / elapsed_seconds
                         : 0.0;

  std::cout << position_case.name << delimiter << mode_name(mode) << delimiter
            << tt_mode_name(tt_mode) << delimiter << depth << delimiter << timed_result.result.score
            << delimiter << best_move_to_string(timed_result.result) << delimiter
            << timed_result.result.nodes << delimiter << timed_result.result.stats.eval_calls
            << delimiter << timed_result.result.stats.beta_cutoffs << delimiter
            << timed_result.result.stats.alpha_updates << delimiter
            << timed_result.result.stats.tt_probes << delimiter << timed_result.result.stats.tt_hits
            << delimiter << timed_result.result.stats.tt_stores << delimiter
            << timed_result.result.stats.tt_cutoffs << delimiter << std::fixed
            << std::setprecision(3) << elapsed_ms << delimiter << std::fixed << std::setprecision(0)
            << nps << '\n';
}

} // namespace

int main(int argc, char** argv) {
  const std::optional<Config> config = parse_config(argc, argv);
  if (!config.has_value()) {
    return 0;
  }

  const std::vector<PositionCase> positions = benchmark_positions();
  const std::vector<BenchmarkMode> modes = modes_for_filter(config->mode_filter);

  print_header(config->delimiter);
  for (PositionCase position_case : positions) {
    for (BenchmarkMode mode : modes) {
      for (Depth depth : config->depths) {
        const TTMode tt_mode = mode == BenchmarkMode::iterative ? config->tt_mode : TTMode::off;
        print_result(position_case, mode, tt_mode, depth,
                     run_search(mode, tt_mode, position_case.position, depth), config->delimiter);
      }
    }
  }

  return 0;
}
