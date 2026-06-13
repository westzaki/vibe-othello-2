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
#include <iomanip>
#include <iostream>
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
using vibe_othello::board_core::is_terminal;
using vibe_othello::board_core::legal_moves;
using vibe_othello::board_core::make_move;
using vibe_othello::board_core::make_move_delta;
using vibe_othello::board_core::make_pass;
using vibe_othello::board_core::Move;
using vibe_othello::board_core::MoveDelta;
using vibe_othello::board_core::MoveKind;
using vibe_othello::board_core::occupied;
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
using vibe_othello::search::search_iterative;
using vibe_othello::search::SearchLimits;
using vibe_othello::search::SearchOptions;
using vibe_othello::search::SearchResult;

enum class OutputFormat {
  tsv,
  csv,
  jsonl,
};

struct Config {
  std::string corpus_path;
  OutputFormat output_format = OutputFormat::tsv;
  std::uint32_t repeat = 1;
  std::uint8_t max_empties = 12;
  char delimiter = '\t';
};

struct PositionCase {
  std::string id;
  std::string category;
  Position position;
  std::uint8_t expected_empties = 0;
  std::string notes;
};

struct TimedResult {
  SearchResult result;
  std::chrono::nanoseconds elapsed;
};

class DummyEvaluator final : public Evaluator {
public:
  Score evaluate(const Position&) const noexcept override {
    ++calls;
    return 0;
  }

  mutable int calls = 0;
};

constexpr Square square(int file, int rank) noexcept {
  return square_from_file_rank(file, rank);
}

void require_condition(bool condition, std::string_view message) {
  if (condition) {
    return;
  }

  std::cerr << "endgame_bench: " << message << '\n';
  std::exit(1);
}

std::uint8_t empty_count(Position position) noexcept {
  return static_cast<std::uint8_t>(std::popcount(~occupied(position)));
}

Position parse_position_or_fail(std::string_view text) {
  const std::optional<Position> position = parse_position(text);
  require_condition(position.has_value(), "invalid built-in position");
  return *position;
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

Position generated_position(std::uint8_t target_empties) {
  static constexpr std::array<std::size_t, 64> kChoices{
      12, 11, 6, 6, 7,  11, 13, 13, 0, 15, 9, 12, 5,  7,  13, 15, 15, 13, 12, 8, 14, 5,
      3,  4,  2, 1, 12, 14, 5,  14, 4, 0,  9, 11, 13, 15, 12, 13, 2,  7,  5,  5, 7,  6,
      3,  8,  1, 9, 4,  2,  10, 6,  0, 11, 5, 13, 7,  3,  12, 1,  8,  4,  14, 2,
  };

  Position position = initial_position();
  std::size_t ply = 0;
  while (empty_count(position) > target_empties) {
    require_condition(!is_terminal(position), "generated corpus reached terminal early");
    const Move move = select_legal_move(position, kChoices[ply % kChoices.size()]);
    MoveDelta delta{};
    require_condition(apply_move(&position, move, &delta), "failed to generate corpus position");
    if (move.kind == MoveKind::normal) {
      ++ply;
    }
  }

  require_condition(empty_count(position) == target_empties,
                    "generated corpus did not reach requested empty count");
  return position;
}

std::vector<PositionCase> benchmark_positions() {
  return {
      PositionCase{.id = "zero_empty_terminal",
                   .category = "zero_empty",
                   .position = parse_position_or_fail(
                       "BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/WWWWWWWW/WWWWWWWW/WWWWWWWW b"),
                   .expected_empties = 0,
                   .notes = "Full-board terminal position"},
      PositionCase{.id = "one_empty_forced_move",
                   .category = "one_empty",
                   .position = parse_position_or_fail(
                       "BBBBBBW./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b"),
                   .expected_empties = 1,
                   .notes = "One empty square with one legal normal move"},
      PositionCase{.id = "one_empty_forced_pass",
                   .category = "forced_pass",
                   .position = parse_position_or_fail(
                       "BBBBBWB./BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB/BBBBBBBB b"),
                   .expected_empties = 1,
                   .notes = "One empty square where root must pass"},
      PositionCase{.id = "four_empty_simple",
                   .category = "generated",
                   .position = generated_position(4),
                   .expected_empties = 4,
                   .notes = "Deterministically generated from initial position"},
      PositionCase{.id = "six_empty_simple",
                   .category = "generated",
                   .position = generated_position(6),
                   .expected_empties = 6,
                   .notes = "Deterministically generated from initial position"},
      PositionCase{.id = "eight_empty_simple",
                   .category = "generated",
                   .position = generated_position(8),
                   .expected_empties = 8,
                   .notes = "Deterministically generated from initial position"},
      PositionCase{.id = "ten_empty_simple",
                   .category = "generated",
                   .position = generated_position(10),
                   .expected_empties = 10,
                   .notes = "Deterministically generated from initial position"},
      PositionCase{.id = "twelve_empty_simple",
                   .category = "generated",
                   .position = generated_position(12),
                   .expected_empties = 12,
                   .notes = "Deterministically generated from initial position"},
  };
}

void print_usage(std::ostream& output, std::string_view program) {
  output << "Usage: " << program
         << " [--tsv|--csv|--jsonl] [--repeat N] [--max-empties N] [--corpus PATH]\n\n"
         << "External TSV schema: id<TAB>category<TAB>position<TAB>expected_empties<TAB>notes\n";
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
                            fields[2] == "position" && fields[3] == "expected_empties" &&
                            fields[4] == "notes",
                        "invalid corpus header");
      saw_header = true;
      continue;
    }

    require_condition(fields.size() == 5, "invalid corpus row");
    const std::optional<Position> position = parse_position(fields[2]);
    require_condition(position.has_value(), "invalid corpus position");
    const std::optional<int> expected_empties = parse_int(fields[3]);
    require_condition(expected_empties.has_value() && *expected_empties >= 0 &&
                          *expected_empties <= vibe_othello::board_core::kSquareCount,
                      "invalid expected empties");
    positions.push_back(PositionCase{
        .id = std::string(fields[0]),
        .category = std::string(fields[1]),
        .position = *position,
        .expected_empties = static_cast<std::uint8_t>(*expected_empties),
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

    if (argument == "--repeat") {
      require_condition(index + 1 < argc, "--repeat requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--repeat", &value)) {
      value = {};
    }
    if (!value.empty()) {
      const std::optional<int> repeat = parse_int(value);
      require_condition(repeat.has_value() && *repeat > 0, "invalid repeat");
      config.repeat = static_cast<std::uint32_t>(*repeat);
      continue;
    }

    if (argument == "--max-empties") {
      require_condition(index + 1 < argc, "--max-empties requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--max-empties", &value)) {
      value = {};
    }
    if (!value.empty()) {
      const std::optional<int> max_empties = parse_int(value);
      require_condition(max_empties.has_value() && *max_empties >= 0 &&
                            *max_empties <= vibe_othello::board_core::kSquareCount,
                        "invalid max empties");
      config.max_empties = static_cast<std::uint8_t>(*max_empties);
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

    std::cerr << "endgame_bench: unknown argument: " << argument << '\n';
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

bool is_legal_root_move(Position position, Move move) {
  MoveDelta delta{};
  return make_move_delta(position, move, &delta);
}

void require_replayable_pv(Position position, Line pv, std::string_view id) {
  for (std::uint8_t index = 0; index < pv.size; ++index) {
    MoveDelta delta{};
    if (!apply_move(&position, pv.moves[index], &delta)) {
      std::cerr << "endgame_bench: PV is not replayable for " << id << " at ply "
                << static_cast<int>(index) << '\n';
      std::exit(1);
    }
  }
}

void validate_result(const PositionCase& position_case, const TimedResult& timed_result,
                     std::uint8_t actual_empties) {
  const SearchResult& result = timed_result.result;
  if (actual_empties != position_case.expected_empties) {
    std::cerr << "endgame_bench: expected " << static_cast<int>(position_case.expected_empties)
              << " empties for " << position_case.id << " but found "
              << static_cast<int>(actual_empties) << '\n';
    std::exit(1);
  }
  require_condition(result.exact, "search result was not exact");
  require_condition(!result.stopped, "search result was stopped");
  require_condition(result.stats.eval_calls == 0, "exact endgame called evaluator");
  require_condition(is_terminal(position_case.position) || result.stats.endgame_nodes != 0,
                    "non-terminal exact search visited zero endgame nodes");
  require_condition(result.nodes == result.stats.nodes, "nodes did not match stats.nodes");
  if (result.best_move.has_value()) {
    require_condition(is_legal_root_move(position_case.position, *result.best_move),
                      "best move is not legal from root");
  }
  require_replayable_pv(position_case.position, result.pv, position_case.id);
}

TimedResult run_exact_endgame(Position position, std::uint8_t empties) {
  DummyEvaluator evaluator;
  const SearchOptions options{
      .exact_endgame = true,
      .endgame_exact_empties = empties,
  };
  const auto start = std::chrono::steady_clock::now();
  SearchResult result =
      search_iterative(position, evaluator, SearchLimits{.max_depth = Depth{0}}, options);
  const auto end = std::chrono::steady_clock::now();
  require_condition(evaluator.calls == 0, "dummy evaluator was called");
  return TimedResult{
      .result = result,
      .elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start),
  };
}

double elapsed_ms(std::chrono::nanoseconds elapsed) {
  return std::chrono::duration<double, std::milli>(elapsed).count();
}

double nodes_per_second(const TimedResult& timed_result) {
  const double elapsed_seconds = std::chrono::duration<double>(timed_result.elapsed).count();
  return elapsed_seconds > 0.0 ? static_cast<double>(timed_result.result.nodes) / elapsed_seconds
                               : 0.0;
}

void print_delimited_header(char delimiter) {
  std::cout << "position_id" << delimiter << "category" << delimiter << "empties" << delimiter
            << "repeat" << delimiter << "score" << delimiter << "best_move" << delimiter << "exact"
            << delimiter << "stopped" << delimiter << "completed_depth" << delimiter << "nodes"
            << delimiter << "endgame_nodes" << delimiter << "terminal_nodes" << delimiter
            << "pass_nodes" << delimiter << "beta_cutoffs" << delimiter << "alpha_updates"
            << delimiter << "root_moves_searched" << delimiter << "elapsed_ms" << delimiter << "nps"
            << '\n';
}

void print_delimited_result(const PositionCase& position_case, std::uint32_t repeat,
                            std::uint8_t empties, const TimedResult& timed_result, char delimiter) {
  const SearchResult& result = timed_result.result;
  std::cout << position_case.id << delimiter << position_case.category << delimiter
            << static_cast<int>(empties) << delimiter << repeat << delimiter << result.score
            << delimiter << best_move_to_string(result) << delimiter
            << (result.exact ? "true" : "false") << delimiter << (result.stopped ? "true" : "false")
            << delimiter << result.completed_depth << delimiter << result.nodes << delimiter
            << result.stats.endgame_nodes << delimiter << result.stats.terminal_nodes << delimiter
            << result.stats.pass_nodes << delimiter << result.stats.beta_cutoffs << delimiter
            << result.stats.alpha_updates << delimiter << result.stats.root_moves_searched
            << delimiter << std::fixed << std::setprecision(3) << elapsed_ms(timed_result.elapsed)
            << delimiter << std::fixed << std::setprecision(0) << nodes_per_second(timed_result)
            << '\n';
}

void print_jsonl_result(const PositionCase& position_case, std::uint32_t repeat,
                        std::uint8_t empties, const TimedResult& timed_result) {
  const SearchResult& result = timed_result.result;
  std::cout << '{';
  std::cout << "\"position_id\":";
  print_json_string(std::cout, position_case.id);
  std::cout << ",\"category\":";
  print_json_string(std::cout, position_case.category);
  std::cout << ",\"empties\":" << static_cast<int>(empties);
  std::cout << ",\"repeat\":" << repeat;
  std::cout << ",\"score\":" << result.score;
  std::cout << ",\"best_move\":";
  print_json_string(std::cout, best_move_to_string(result));
  std::cout << ",\"exact\":" << (result.exact ? "true" : "false");
  std::cout << ",\"stopped\":" << (result.stopped ? "true" : "false");
  std::cout << ",\"completed_depth\":" << result.completed_depth;
  std::cout << ",\"nodes\":" << result.nodes;
  std::cout << ",\"endgame_nodes\":" << result.stats.endgame_nodes;
  std::cout << ",\"terminal_nodes\":" << result.stats.terminal_nodes;
  std::cout << ",\"pass_nodes\":" << result.stats.pass_nodes;
  std::cout << ",\"beta_cutoffs\":" << result.stats.beta_cutoffs;
  std::cout << ",\"alpha_updates\":" << result.stats.alpha_updates;
  std::cout << ",\"root_moves_searched\":" << result.stats.root_moves_searched;
  std::cout << ",\"elapsed_ms\":" << std::setprecision(17) << elapsed_ms(timed_result.elapsed);
  std::cout << ",\"nps\":" << std::setprecision(17) << nodes_per_second(timed_result);
  std::cout << ",\"pv\":";
  print_json_line(std::cout, result.pv);
  std::cout << ",\"root_moves\":";
  print_json_root_moves(std::cout, result.root_moves);
  std::cout << ",\"notes\":";
  print_json_string(std::cout, position_case.notes);
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

  if (config->output_format != OutputFormat::jsonl) {
    print_delimited_header(config->delimiter);
  }
  for (const PositionCase& position_case : positions) {
    const std::uint8_t actual_empties = empty_count(position_case.position);
    if (actual_empties > config->max_empties) {
      continue;
    }

    for (std::uint32_t repeat = 1; repeat <= config->repeat; ++repeat) {
      const TimedResult timed_result = run_exact_endgame(position_case.position, actual_empties);
      validate_result(position_case, timed_result, actual_empties);
      if (config->output_format == OutputFormat::jsonl) {
        print_jsonl_result(position_case, repeat, actual_empties, timed_result);
      } else {
        print_delimited_result(position_case, repeat, actual_empties, timed_result,
                               config->delimiter);
      }
    }
  }

  return 0;
}
