#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/serialization.h"
#include "vibe_othello/search/search.h"

#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
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
using vibe_othello::board_core::parse_position;
using vibe_othello::board_core::Position;
using vibe_othello::board_core::Square;
using vibe_othello::board_core::square_from_file_rank;
using vibe_othello::board_core::square_from_index;
using vibe_othello::search::BoundType;
using vibe_othello::search::Depth;
using vibe_othello::search::Evaluator;
using vibe_othello::search::Line;
using vibe_othello::search::RootMoveInfo;
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

enum class OutputFormat {
  tsv,
  csv,
  jsonl,
};

struct Config {
  std::vector<Depth> depths{Depth{6}, Depth{7}, Depth{8}};
  std::string corpus_path;
  ModeFilter mode_filter = ModeFilter::all;
  TTMode tt_mode = TTMode::off;
  OutputFormat output_format = OutputFormat::tsv;
  bool depths_overridden = false;
  char delimiter = '\t';
};

struct PositionCase {
  std::string id;
  std::string category;
  Position position;
  std::vector<Depth> depths;
  std::string notes;
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

std::vector<Depth> default_depths() {
  return {Depth{6}, Depth{7}, Depth{8}};
}

std::vector<PositionCase> benchmark_positions() {
  return {
      PositionCase{.id = "initial",
                   .category = "opening",
                   .position = initial_position(),
                   .depths = default_depths(),
                   .notes = "Initial position"},
      PositionCase{.id = "early_balanced",
                   .category = "early",
                   .position = position_after_fixed_choices({1, 9, 10, 5, 13, 2, 5, 4}),
                   .depths = default_depths(),
                   .notes = "Balanced early position"},
      PositionCase{.id = "early_high_mobility",
                   .category = "early",
                   .position = position_after_fixed_choices({8, 4, 8, 5, 8, 7, 11, 2}),
                   .depths = default_depths(),
                   .notes = "Early high-mobility position"},
      PositionCase{.id = "midgame_balanced",
                   .category = "midgame",
                   .position = position_after_fixed_choices(
                       {13, 1, 1, 10, 9, 1, 0, 13, 10, 2, 11, 14, 12, 15, 5, 8, 4, 8, 2, 15}),
                   .depths = default_depths(),
                   .notes = "Balanced midgame position"},
      PositionCase{.id = "midgame_high_mobility",
                   .category = "midgame",
                   .position =
                       position_after_fixed_choices({13, 3,  12, 1,  12, 11, 0, 1, 1, 14, 5,
                                                     4,  10, 12, 12, 14, 14, 8, 4, 3, 8,  3}),
                   .depths = default_depths(),
                   .notes = "High-mobility midgame position"},
      PositionCase{.id = "corner_available",
                   .category = "tactical",
                   .position = position_after_fixed_choices({11, 3, 0, 6, 12, 8, 1, 5, 3, 5, 9, 9}),
                   .depths = default_depths(),
                   .notes = "Corner is available"},
      PositionCase{.id = "pass_position",
                   .category = "edge_case",
                   .position = pass_root_position(),
                   .depths = default_depths(),
                   .notes = "Root pass position"},
      PositionCase{
          .id = "late_midgame",
          .category = "late_midgame",
          .position = position_after_fixed_choices(
              {12, 11, 6, 6, 7,  11, 13, 13, 0, 15, 9, 12, 5,  7,  13, 15, 15, 13, 12, 8, 14, 5,
               3,  4,  2, 1, 12, 14, 5,  14, 4, 0,  9, 11, 13, 15, 12, 13, 2,  7,  5,  5, 7,  6}),
          .depths = default_depths(),
          .notes = "Late midgame position"},
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
            " [--mode all|fixed|iterative] [--tt off|ordering|midgame|both]"
            " [--corpus PATH] [--tsv|--csv|--jsonl]\n\n"
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

std::vector<std::string_view> split_tabs(std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t begin = 0;
  while (begin <= line.size()) {
    const std::size_t tab = line.find('\t', begin);
    if (tab == std::string_view::npos) {
      fields.push_back(line.substr(begin));
      break;
    }
    fields.push_back(line.substr(begin, tab - begin));
    begin = tab + 1;
  }
  return fields;
}

std::vector<PositionCase> load_corpus(std::string_view path) {
  std::ifstream input{std::string(path)};
  require_condition(input.is_open(), "failed to open corpus");

  std::vector<PositionCase> positions;
  std::string line;
  bool saw_header = false;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }

    const std::vector<std::string_view> fields = split_tabs(line);
    if (!saw_header) {
      require_condition(fields.size() == 5 && fields[0] == "id" && fields[1] == "category" &&
                            fields[2] == "position" && fields[3] == "depths" &&
                            fields[4] == "notes",
                        "invalid corpus header");
      saw_header = true;
      continue;
    }

    require_condition(fields.size() == 5, "invalid corpus row");
    const std::optional<Position> position = parse_position(fields[2]);
    require_condition(position.has_value(), "invalid corpus position");
    const std::optional<std::vector<Depth>> depths = parse_depths(fields[3]);
    require_condition(depths.has_value(), "invalid corpus depths");
    positions.push_back(PositionCase{
        .id = std::string(fields[0]),
        .category = std::string(fields[1]),
        .position = *position,
        .depths = *depths,
        .notes = std::string(fields[4]),
    });
  }

  require_condition(saw_header, "missing corpus header");
  require_condition(!positions.empty(), "empty corpus");
  return positions;
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
      config.output_format = OutputFormat::tsv;
      config.delimiter = '\t';
      continue;
    }
    if (argument == "--csv") {
      config.output_format = OutputFormat::csv;
      config.delimiter = ',';
      continue;
    }
    if (argument == "--jsonl") {
      config.output_format = OutputFormat::jsonl;
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
      config.depths_overridden = true;
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

    if (argument == "--corpus") {
      require_condition(index + 1 < argc, "--corpus requires a value");
      config.corpus_path = argv[++index];
      continue;
    }
    if (parse_argument_with_value(argument, "--corpus", &value)) {
      require_condition(!value.empty(), "--corpus requires a value");
      config.corpus_path = std::string(value);
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

std::string_view bound_name(BoundType bound) noexcept {
  switch (bound) {
  case BoundType::exact:
    return "exact";
  case BoundType::lower:
    return "lower";
  case BoundType::upper:
    return "upper";
  }

  return "unknown";
}

void print_json_string(std::ostream& output, std::string_view text) {
  output << '"';
  for (const char ch : text) {
    switch (ch) {
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
      if (static_cast<unsigned char>(ch) < 0x20) {
        output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
               << static_cast<int>(static_cast<unsigned char>(ch)) << std::dec << std::setfill(' ');
      } else {
        output << ch;
      }
      break;
    }
  }
  output << '"';
}

void print_json_line(std::ostream& output, Line line) {
  output << '[';
  for (std::uint8_t index = 0; index < line.size; ++index) {
    if (index != 0) {
      output << ',';
    }
    print_json_string(output, move_to_string(line.moves[index]));
  }
  output << ']';
}

void print_json_root_moves(std::ostream& output, const std::vector<RootMoveInfo>& root_moves) {
  output << '[';
  bool first = true;
  for (const RootMoveInfo& root_move : root_moves) {
    if (!first) {
      output << ',';
    }
    first = false;
    output << '{';
    output << "\"move\":";
    print_json_string(output, move_to_string(root_move.move));
    output << ",\"score\":" << root_move.score;
    output << ",\"bound\":";
    print_json_string(output, bound_name(root_move.bound));
    output << ",\"depth\":" << root_move.depth;
    output << ",\"exact\":" << (root_move.exact ? "true" : "false");
    output << ",\"selective\":" << (root_move.selective ? "true" : "false");
    output << '}';
  }
  output << ']';
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

void print_delimited_header(char delimiter) {
  std::cout << "position_name" << delimiter << "mode" << delimiter << "tt_mode" << delimiter
            << "depth" << delimiter << "score" << delimiter << "best_move" << delimiter << "nodes"
            << delimiter << "eval_calls" << delimiter << "beta_cutoffs" << delimiter
            << "alpha_updates" << delimiter << "tt_probes" << delimiter << "tt_hits" << delimiter
            << "tt_stores" << delimiter << "tt_cutoffs" << delimiter << "tt_overwrites" << delimiter
            << "tt_collisions" << delimiter << "tt_rejected_stores" << delimiter << "elapsed_ms"
            << delimiter << "nps" << '\n';
}

void print_delimited_result(const PositionCase& position_case, BenchmarkMode mode, TTMode tt_mode,
                            Depth depth, TimedResult timed_result, char delimiter) {
  const double elapsed_ms = std::chrono::duration<double, std::milli>(timed_result.elapsed).count();
  const double elapsed_seconds = std::chrono::duration<double>(timed_result.elapsed).count();
  const double nps = elapsed_seconds > 0.0
                         ? static_cast<double>(timed_result.result.nodes) / elapsed_seconds
                         : 0.0;

  std::cout << position_case.id << delimiter << mode_name(mode) << delimiter
            << tt_mode_name(tt_mode) << delimiter << depth << delimiter << timed_result.result.score
            << delimiter << best_move_to_string(timed_result.result) << delimiter
            << timed_result.result.nodes << delimiter << timed_result.result.stats.eval_calls
            << delimiter << timed_result.result.stats.beta_cutoffs << delimiter
            << timed_result.result.stats.alpha_updates << delimiter
            << timed_result.result.stats.tt_probes << delimiter << timed_result.result.stats.tt_hits
            << delimiter << timed_result.result.stats.tt_stores << delimiter
            << timed_result.result.stats.tt_cutoffs << delimiter
            << timed_result.result.stats.tt_overwrites << delimiter
            << timed_result.result.stats.tt_collisions << delimiter
            << timed_result.result.stats.tt_rejected_stores << delimiter << std::fixed
            << std::setprecision(3) << elapsed_ms << delimiter << std::fixed << std::setprecision(0)
            << nps << '\n';
}

void print_jsonl_result(const PositionCase& position_case, BenchmarkMode mode, TTMode tt_mode,
                        Depth depth, TimedResult timed_result) {
  const double elapsed_seconds = std::chrono::duration<double>(timed_result.elapsed).count();
  const double nps = elapsed_seconds > 0.0
                         ? static_cast<double>(timed_result.result.nodes) / elapsed_seconds
                         : 0.0;

  std::cout << '{';
  std::cout << "\"position_id\":";
  print_json_string(std::cout, position_case.id);
  std::cout << ",\"category\":";
  print_json_string(std::cout, position_case.category);
  std::cout << ",\"mode\":";
  print_json_string(std::cout, mode_name(mode));
  std::cout << ",\"tt_mode\":";
  print_json_string(std::cout, tt_mode_name(tt_mode));
  std::cout << ",\"depth\":" << depth;
  std::cout << ",\"evaluator\":\"disc_difference\"";
  std::cout << ",\"score\":" << timed_result.result.score;
  std::cout << ",\"best_move\":";
  print_json_string(std::cout, best_move_to_string(timed_result.result));
  std::cout << ",\"pv\":";
  print_json_line(std::cout, timed_result.result.pv);
  std::cout << ",\"root_moves\":";
  print_json_root_moves(std::cout, timed_result.result.root_moves);
  std::cout << ",\"nodes\":" << timed_result.result.nodes;
  std::cout << ",\"eval_calls\":" << timed_result.result.stats.eval_calls;
  std::cout << ",\"beta_cutoffs\":" << timed_result.result.stats.beta_cutoffs;
  std::cout << ",\"alpha_updates\":" << timed_result.result.stats.alpha_updates;
  std::cout << ",\"tt_probes\":" << timed_result.result.stats.tt_probes;
  std::cout << ",\"tt_hits\":" << timed_result.result.stats.tt_hits;
  std::cout << ",\"tt_stores\":" << timed_result.result.stats.tt_stores;
  std::cout << ",\"tt_cutoffs\":" << timed_result.result.stats.tt_cutoffs;
  std::cout << ",\"tt_overwrites\":" << timed_result.result.stats.tt_overwrites;
  std::cout << ",\"tt_collisions\":" << timed_result.result.stats.tt_collisions;
  std::cout << ",\"tt_rejected_stores\":" << timed_result.result.stats.tt_rejected_stores;
  std::cout << ",\"elapsed_ns\":" << timed_result.elapsed.count();
  std::cout << ",\"nps\":" << std::setprecision(17) << nps;
  std::cout << "}\n";
}

} // namespace

int main(int argc, char** argv) {
  const std::optional<Config> config = parse_config(argc, argv);
  if (!config.has_value()) {
    return 0;
  }

  const std::vector<PositionCase> positions =
      config->corpus_path.empty() ? benchmark_positions() : load_corpus(config->corpus_path);
  const std::vector<BenchmarkMode> modes = modes_for_filter(config->mode_filter);

  if (config->output_format != OutputFormat::jsonl) {
    print_delimited_header(config->delimiter);
  }
  for (const PositionCase& position_case : positions) {
    const std::vector<Depth>& depths =
        config->depths_overridden ? config->depths : position_case.depths;
    for (BenchmarkMode mode : modes) {
      for (Depth depth : depths) {
        const TTMode tt_mode = mode == BenchmarkMode::iterative ? config->tt_mode : TTMode::off;
        TimedResult timed_result = run_search(mode, tt_mode, position_case.position, depth);
        if (config->output_format == OutputFormat::jsonl) {
          print_jsonl_result(position_case, mode, tt_mode, depth, timed_result);
        } else {
          print_delimited_result(position_case, mode, tt_mode, depth, timed_result,
                                 config->delimiter);
        }
      }
    }
  }

  return 0;
}
