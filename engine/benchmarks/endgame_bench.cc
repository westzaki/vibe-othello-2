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
using vibe_othello::board_core::format_position;
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
using vibe_othello::board_core::square_from_index;
using vibe_othello::search::BoundType;
using vibe_othello::search::EndgameSearchOptions;
using vibe_othello::search::EndgameStabilityMode;
using vibe_othello::search::Evaluator;
using vibe_othello::search::Line;
using vibe_othello::search::MoveOrderingOptions;
using vibe_othello::search::RootMoveInfo;
using vibe_othello::search::ScoreKind;
using vibe_othello::search::search_iterative;
using vibe_othello::search::SearchLimits;
using vibe_othello::search::SearchMode;
using vibe_othello::search::SearchOptions;
using vibe_othello::search::SearchReportingOptions;
using vibe_othello::search::SearchResult;
using vibe_othello::search::solve_exact_endgame;
using vibe_othello::search::solve_wld_endgame;
using vibe_othello::search::WldResult;

enum class OutputFormat {
  tsv,
  csv,
  jsonl,
};

enum class ParityMode {
  on,
  off,
  both,
};

enum class TTMode {
  off,
  on,
  both,
};

enum class StabilityModeSelection {
  off,
  shadow,
  cutoff,
  all,
};

enum class PvsMode {
  off,
  on,
  both,
};

enum class RootMode {
  all,
  best,
};

enum class SolveMode {
  exact_score,
  wld,
};

enum class EntryMode {
  direct,
  iterative_root,
};

struct Config {
  std::string corpus_path;
  OutputFormat output_format = OutputFormat::tsv;
  ParityMode parity_mode = ParityMode::on;
  TTMode tt_mode = TTMode::off;
  StabilityModeSelection stability_mode = StabilityModeSelection::cutoff;
  PvsMode pvs_mode = PvsMode::on;
  RootMode root_mode = RootMode::all;
  SolveMode solve_mode = SolveMode::exact_score;
  EntryMode entry_mode = EntryMode::direct;
  std::uint32_t repeat = 1;
  std::uint8_t min_empties = 0;
  std::uint8_t max_empties = 12;
  std::uint8_t endgame_wld_empties = 0;
  char delimiter = '\t';
  bool list_positions = false;
  bool max_empties_was_set = false;
  bool endgame_wld_empties_was_set = false;
  std::vector<std::string> position_ids;
  std::vector<std::string> categories;
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
  EntryMode entry_mode = EntryMode::direct;
  std::uint8_t threshold = 0;
  bool triggered = true;
  std::string_view status = "completed";
};

class CountingEvaluator final : public Evaluator {
public:
  explicit constexpr CountingEvaluator(vibe_othello::search::Score score) noexcept
      : score_(score) {}

  vibe_othello::search::Score evaluate(const Position&) const noexcept override {
    ++calls;
    return score_;
  }

  mutable int calls = 0;

private:
  vibe_othello::search::Score score_;
};

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

Position fallback_generated_position(std::uint8_t target_empties) {
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

std::vector<PositionCase> built_in_fallback_positions() {
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
      PositionCase{.id = "two_empty_simple",
                   .category = "generated",
                   .position = fallback_generated_position(2),
                   .expected_empties = 2,
                   .notes = "Deterministically generated from initial position"},
      PositionCase{.id = "three_empty_simple",
                   .category = "generated",
                   .position = fallback_generated_position(3),
                   .expected_empties = 3,
                   .notes = "Deterministically generated from initial position"},
      PositionCase{.id = "four_empty_simple",
                   .category = "generated",
                   .position = fallback_generated_position(4),
                   .expected_empties = 4,
                   .notes = "Deterministically generated from initial position"},
      PositionCase{.id = "six_empty_simple",
                   .category = "generated",
                   .position = fallback_generated_position(6),
                   .expected_empties = 6,
                   .notes = "Deterministically generated from initial position"},
      PositionCase{.id = "eight_empty_simple",
                   .category = "generated",
                   .position = fallback_generated_position(8),
                   .expected_empties = 8,
                   .notes = "Deterministically generated from initial position"},
      PositionCase{.id = "ten_empty_simple",
                   .category = "generated",
                   .position = fallback_generated_position(10),
                   .expected_empties = 10,
                   .notes = "Deterministically generated from initial position"},
      PositionCase{.id = "twelve_empty_simple",
                   .category = "generated",
                   .position = fallback_generated_position(12),
                   .expected_empties = 12,
                   .notes = "Deterministically generated from initial position"},
  };
}

void print_usage(std::ostream& output, std::string_view program) {
  output << "Usage: " << program
         << " [--tsv|--csv|--jsonl] [--parity on|off|both] [--tt off|on|both]"
            " [--stability off|shadow|cutoff|all]"
            " [--pvs off|on|both]"
            " [--root-mode all|best] [--mode exact-score|wld] [--entry direct|iterative-root]"
            " [--endgame-wld-empties N] [--repeat N]"
            " [--min-empties N] [--max-empties N] [--position-id ID] [--category NAME]"
            " [--list-positions] [--corpus PATH]\n\n"
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

bool contains_value(const std::vector<std::string>& values, std::string_view needle) noexcept {
  for (const std::string& value : values) {
    if (value == needle) {
      return true;
    }
  }
  return false;
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

std::optional<std::vector<PositionCase>> try_load_corpus(std::string_view path) {
  std::ifstream input{std::string(path)};
  if (!input.is_open()) {
    return std::nullopt;
  }

  input.close();
  return load_corpus(path);
}

std::vector<PositionCase> default_positions() {
  static constexpr std::string_view kCheckedInCorpus = "engine/fixtures/endgame/positions.tsv";
  if (std::optional<std::vector<PositionCase>> positions = try_load_corpus(kCheckedInCorpus)) {
    return *positions;
  }

  return built_in_fallback_positions();
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

    if (argument == "--parity") {
      require_condition(index + 1 < argc, "--parity requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--parity", &value)) {
      value = {};
    }
    if (!value.empty()) {
      if (value == "on") {
        config.parity_mode = ParityMode::on;
      } else if (value == "off") {
        config.parity_mode = ParityMode::off;
      } else if (value == "both") {
        config.parity_mode = ParityMode::both;
      } else {
        require_condition(false, "invalid parity mode");
      }
      continue;
    }

    if (argument == "--tt") {
      require_condition(index + 1 < argc, "--tt requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--tt", &value)) {
      value = {};
    }
    if (!value.empty()) {
      if (value == "off") {
        config.tt_mode = TTMode::off;
      } else if (value == "on") {
        config.tt_mode = TTMode::on;
      } else if (value == "both") {
        config.tt_mode = TTMode::both;
      } else {
        require_condition(false, "invalid TT mode");
      }
      continue;
    }

    if (argument == "--stability") {
      require_condition(index + 1 < argc, "--stability requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--stability", &value)) {
      value = {};
    }
    if (!value.empty()) {
      if (value == "off") {
        config.stability_mode = StabilityModeSelection::off;
      } else if (value == "shadow") {
        config.stability_mode = StabilityModeSelection::shadow;
      } else if (value == "cutoff") {
        config.stability_mode = StabilityModeSelection::cutoff;
      } else if (value == "all") {
        config.stability_mode = StabilityModeSelection::all;
      } else {
        require_condition(false, "invalid stability mode");
      }
      continue;
    }

    if (argument == "--pvs") {
      require_condition(index + 1 < argc, "--pvs requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--pvs", &value)) {
      value = {};
    }
    if (!value.empty()) {
      if (value == "off") {
        config.pvs_mode = PvsMode::off;
      } else if (value == "on") {
        config.pvs_mode = PvsMode::on;
      } else if (value == "both") {
        config.pvs_mode = PvsMode::both;
      } else {
        require_condition(false, "invalid PVS mode");
      }
      continue;
    }

    if (argument == "--root-mode") {
      require_condition(index + 1 < argc, "--root-mode requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--root-mode", &value)) {
      value = {};
    }
    if (!value.empty()) {
      if (value == "all") {
        config.root_mode = RootMode::all;
      } else if (value == "best") {
        config.root_mode = RootMode::best;
      } else {
        require_condition(false, "invalid root mode");
      }
      continue;
    }

    if (argument == "--mode") {
      require_condition(index + 1 < argc, "--mode requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--mode", &value)) {
      value = {};
    }
    if (!value.empty()) {
      if (value == "exact-score" || value == "exact_score") {
        config.solve_mode = SolveMode::exact_score;
      } else if (value == "wld") {
        config.solve_mode = SolveMode::wld;
      } else {
        require_condition(false, "invalid solve mode");
      }
      continue;
    }

    if (argument == "--entry") {
      require_condition(index + 1 < argc, "--entry requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--entry", &value)) {
      value = {};
    }
    if (!value.empty()) {
      if (value == "direct") {
        config.entry_mode = EntryMode::direct;
      } else if (value == "iterative-root" || value == "iterative_root") {
        config.entry_mode = EntryMode::iterative_root;
      } else {
        require_condition(false, "invalid entry mode");
      }
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

    if (argument == "--min-empties") {
      require_condition(index + 1 < argc, "--min-empties requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--min-empties", &value)) {
      value = {};
    }
    if (!value.empty()) {
      const std::optional<int> min_empties = parse_int(value);
      require_condition(min_empties.has_value() && *min_empties >= 0 &&
                            *min_empties <= vibe_othello::board_core::kSquareCount,
                        "invalid min empties");
      config.min_empties = static_cast<std::uint8_t>(*min_empties);
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
      config.max_empties_was_set = true;
      continue;
    }

    if (argument == "--endgame-wld-empties") {
      require_condition(index + 1 < argc, "--endgame-wld-empties requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--endgame-wld-empties", &value)) {
      value = {};
    }
    if (!value.empty()) {
      const std::optional<int> threshold = parse_int(value);
      require_condition(threshold.has_value() && *threshold >= 0 &&
                            *threshold <= vibe_othello::board_core::kSquareCount,
                        "invalid endgame WLD empty threshold");
      config.endgame_wld_empties = static_cast<std::uint8_t>(*threshold);
      config.endgame_wld_empties_was_set = true;
      continue;
    }

    if (argument == "--position-id") {
      require_condition(index + 1 < argc, "--position-id requires a value");
      config.position_ids.push_back(argv[++index]);
      require_condition(!config.position_ids.back().empty(), "--position-id requires a value");
      continue;
    }
    if (parse_argument_with_value(argument, "--position-id", &value)) {
      require_condition(!value.empty(), "--position-id requires a value");
      config.position_ids.push_back(std::string(value));
      continue;
    }

    if (argument == "--category") {
      require_condition(index + 1 < argc, "--category requires a value");
      config.categories.push_back(argv[++index]);
      require_condition(!config.categories.back().empty(), "--category requires a value");
      continue;
    }
    if (parse_argument_with_value(argument, "--category", &value)) {
      require_condition(!value.empty(), "--category requires a value");
      config.categories.push_back(std::string(value));
      continue;
    }

    if (argument == "--list-positions") {
      config.list_positions = true;
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

  require_condition(config.min_empties <= config.max_empties,
                    "--min-empties must be less than or equal to --max-empties");
  if (config.entry_mode == EntryMode::iterative_root) {
    require_condition(config.solve_mode == SolveMode::wld,
                      "--entry iterative-root currently supports --mode wld only");
    require_condition(config.endgame_wld_empties_was_set,
                      "--entry iterative-root requires --endgame-wld-empties");
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

std::string_view parity_mode_name(bool enabled) noexcept {
  return enabled ? "on" : "off";
}

std::string_view tt_mode_name(bool enabled) noexcept {
  return enabled ? "on" : "off";
}

std::string_view stability_mode_name(EndgameStabilityMode mode) noexcept {
  switch (mode) {
  case EndgameStabilityMode::off:
    return "off";
  case EndgameStabilityMode::shadow:
    return "shadow";
  case EndgameStabilityMode::cutoff:
    return "cutoff";
  }
  return "unknown";
}

std::string_view pvs_mode_name(bool enabled) noexcept {
  return enabled ? "on" : "off";
}

std::string_view root_mode_name(RootMode mode) noexcept {
  switch (mode) {
  case RootMode::all:
    return "all";
  case RootMode::best:
    return "best";
  }

  return "unknown";
}

std::string_view solve_mode_name(SolveMode mode) noexcept {
  switch (mode) {
  case SolveMode::exact_score:
    return "exact_score";
  case SolveMode::wld:
    return "wld";
  }

  return "unknown";
}

std::string_view entry_mode_name(EntryMode mode) noexcept {
  switch (mode) {
  case EntryMode::direct:
    return "direct";
  case EntryMode::iterative_root:
    return "iterative_root";
  }

  return "unknown";
}

std::string_view wld_result_name(const SearchResult& result) noexcept {
  if (result.score == static_cast<vibe_othello::search::Score>(WldResult::win)) {
    return "win";
  }
  if (result.score == static_cast<vibe_othello::search::Score>(WldResult::loss)) {
    return "loss";
  }
  if (result.score == static_cast<vibe_othello::search::Score>(WldResult::draw)) {
    return "draw";
  }
  return "unknown";
}

std::uint8_t multi_pv_for_root_mode(RootMode mode) noexcept {
  switch (mode) {
  case RootMode::all:
    return 0;
  case RootMode::best:
    return 1;
  }

  return 0;
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

std::string_view score_kind_name(ScoreKind score_kind) noexcept {
  switch (score_kind) {
  case ScoreKind::unavailable:
    return "unavailable";
  case ScoreKind::heuristic:
    return "heuristic";
  case ScoreKind::exact_disc_diff:
    return "exact_disc_diff";
  case ScoreKind::win_loss_draw:
    return "win_loss_draw";
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
    output << ",\"score_kind\":";
    print_json_string(output, score_kind_name(root_move.score_kind));
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
                     std::uint8_t actual_empties, bool use_tt, SolveMode solve_mode) {
  const SearchResult& result = timed_result.result;
  if (actual_empties != position_case.expected_empties) {
    std::cerr << "endgame_bench: expected " << static_cast<int>(position_case.expected_empties)
              << " empties for " << position_case.id << " but found "
              << static_cast<int>(actual_empties) << '\n';
    std::exit(1);
  }
  if (!timed_result.triggered) {
    require_condition(timed_result.entry_mode == EntryMode::iterative_root,
                      "only iterative-root runs may be untriggered");
    require_condition(timed_result.threshold < actual_empties,
                      "iterative-root WLD did not trigger at or above threshold");
    require_condition(!result.exact, "untriggered iterative root result was exact");
    require_condition(!result.stopped, "untriggered iterative root result was stopped");
    require_condition(result.stats.endgame_nodes == 0,
                      "untriggered iterative root visited endgame nodes");
    require_condition(result.stats.eval_calls > 0, "untriggered iterative root did not evaluate");
    return;
  }
  if (timed_result.entry_mode == EntryMode::iterative_root) {
    require_condition(timed_result.threshold >= actual_empties,
                      "iterative-root WLD triggered below threshold");
  }
  require_condition(result.exact, "search result was not exact");
  require_condition(!result.stopped, "search result was stopped");
  require_condition(result.stats.eval_calls == 0, "exact endgame called evaluator");
  if (solve_mode == SolveMode::wld) {
    require_condition(result.score_kind == ScoreKind::win_loss_draw,
                      "WLD result score_kind was not win_loss_draw");
    require_condition(result.score >= static_cast<vibe_othello::search::Score>(WldResult::loss) &&
                          result.score <= static_cast<vibe_othello::search::Score>(WldResult::win),
                      "WLD result score was outside the WLD range");
  } else {
    require_condition(result.score_kind == ScoreKind::exact_disc_diff,
                      "exact result score_kind was not exact_disc_diff");
  }
  require_condition(is_terminal(position_case.position) || result.stats.endgame_nodes != 0,
                    "non-terminal exact search visited zero endgame nodes");
  require_condition(result.nodes == result.stats.nodes, "nodes did not match stats.nodes");
  if (result.best_move.has_value()) {
    require_condition(is_legal_root_move(position_case.position, *result.best_move),
                      "best move is not legal from root");
  }
  if (!use_tt) {
    require_condition(result.stats.tt_probes == 0, "TT probes were recorded with TT disabled");
    require_condition(result.stats.tt_hits == 0, "TT hits were recorded with TT disabled");
    require_condition(result.stats.tt_cutoffs == 0, "TT cutoffs were recorded with TT disabled");
    require_condition(result.stats.tt_stores == 0, "TT stores were recorded with TT disabled");
    require_condition(result.stats.tt_replacements == 0,
                      "TT replacements were recorded with TT disabled");
    require_condition(result.stats.tt_bucket_conflicts == 0,
                      "TT bucket conflicts were recorded with TT disabled");
    require_condition(result.stats.tt_rejected_stores == 0,
                      "TT rejected stores were recorded with TT disabled");
    require_condition(result.stats.tt_invalid_best_move_stores == 0,
                      "TT invalid-best-move stores were recorded with TT disabled");
  }
  require_replayable_pv(position_case.position, result.pv, position_case.id);
}

TimedResult run_endgame(Position position, std::uint8_t empties, bool use_parity_ordering,
                        bool use_tt, EndgameStabilityMode stability_mode, bool use_pvs,
                        RootMode root_mode, SolveMode solve_mode, EntryMode entry_mode,
                        std::uint8_t endgame_wld_empties) {
  const SearchOptions options{
      .ordering = MoveOrderingOptions{.use_endgame_parity_ordering = use_parity_ordering},
      .endgame =
          EndgameSearchOptions{
              .exact_endgame = false,
              .use_endgame_tt = use_tt,
              .use_endgame_pvs = use_pvs,
              .endgame_exact_empties = 0,
              .endgame_wld_empties = endgame_wld_empties,
              .stability_mode = stability_mode,
          },
      .reporting = SearchReportingOptions{.multi_pv = multi_pv_for_root_mode(root_mode)},
      .mode = solve_mode == SolveMode::wld ? SearchMode::win_loss_draw : SearchMode::exact_score,
  };
  const auto start = std::chrono::steady_clock::now();
  SearchResult result{};
  if (entry_mode == EntryMode::iterative_root) {
    CountingEvaluator evaluator{77};
    result = search_iterative(position, evaluator, SearchLimits{.max_depth = 0}, options);
  } else {
    result = solve_mode == SolveMode::wld ? solve_wld_endgame(position, {}, options)
                                          : solve_exact_endgame(position, {}, options);
  }
  const auto end = std::chrono::steady_clock::now();
  const bool triggered = result.stats.endgame_nodes != 0 || is_terminal(position);
  if (triggered) {
    require_condition(result.completed_depth == static_cast<vibe_othello::search::Depth>(empties),
                      "endgame did not solve the root empty count");
  }
  return TimedResult{
      .result = result,
      .elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start),
      .entry_mode = entry_mode,
      .threshold = entry_mode == EntryMode::iterative_root ? endgame_wld_empties
                                                           : static_cast<std::uint8_t>(0),
      .triggered = triggered,
      .status = triggered ? "completed" : "not_triggered",
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
            << "repeat" << delimiter << "entry" << delimiter << "threshold" << delimiter
            << "triggered" << delimiter << "status" << delimiter << "parity_ordering" << delimiter
            << "tt_mode" << delimiter << "stability_mode" << delimiter << "pvs" << delimiter
            << "root_mode" << delimiter << "mode" << delimiter << "score" << delimiter
            << "score_kind" << delimiter << "wld_result" << delimiter << "best_move" << delimiter
            << "exact" << delimiter << "stopped" << delimiter << "completed_depth" << delimiter
            << "nodes" << delimiter << "endgame_nodes" << delimiter << "last_flip_solved"
            << delimiter << "eval_calls" << delimiter << "terminal_nodes" << delimiter
            << "pass_nodes" << delimiter << "beta_cutoffs" << delimiter << "alpha_updates"
            << delimiter << "pvs_researches" << delimiter << "root_moves_searched" << delimiter
            << "tt_probes" << delimiter << "tt_hits" << delimiter << "tt_cutoffs" << delimiter
            << "tt_stores" << delimiter << "tt_replacements" << delimiter << "tt_bucket_conflicts"
            << delimiter << "tt_rejected_stores" << delimiter << "tt_invalid_best_move_stores"
            << delimiter << "stability_probes" << delimiter << "stability_lower_candidates"
            << delimiter << "stability_upper_candidates" << delimiter << "stability_cutoffs"
            << delimiter << "stability_shadow_verifications" << delimiter
            << "stability_shadow_false_cutoffs" << delimiter << "elapsed_ms" << delimiter << "nps"
            << '\n';
}

void print_delimited_result(const PositionCase& position_case, std::uint32_t repeat,
                            std::uint8_t empties, bool use_parity_ordering, bool use_tt,
                            EndgameStabilityMode stability_mode, bool use_pvs, RootMode root_mode,
                            SolveMode solve_mode, const TimedResult& timed_result, char delimiter) {
  const SearchResult& result = timed_result.result;
  std::cout << position_case.id << delimiter << position_case.category << delimiter
            << static_cast<int>(empties) << delimiter << repeat << delimiter
            << entry_mode_name(timed_result.entry_mode) << delimiter
            << static_cast<int>(timed_result.threshold) << delimiter
            << (timed_result.triggered ? "true" : "false") << delimiter << timed_result.status
            << delimiter << parity_mode_name(use_parity_ordering) << delimiter
            << tt_mode_name(use_tt) << delimiter << stability_mode_name(stability_mode) << delimiter
            << pvs_mode_name(use_pvs) << delimiter << root_mode_name(root_mode) << delimiter
            << solve_mode_name(solve_mode) << delimiter << result.score << delimiter
            << score_kind_name(result.score_kind) << delimiter
            << (solve_mode == SolveMode::wld && timed_result.triggered ? wld_result_name(result)
                                                                       : "n/a")
            << delimiter << best_move_to_string(result) << delimiter
            << (result.exact ? "true" : "false") << delimiter << (result.stopped ? "true" : "false")
            << delimiter << result.completed_depth << delimiter << result.nodes << delimiter
            << result.stats.endgame_nodes << delimiter << result.stats.endgame_last_flip_solved
            << delimiter << result.stats.eval_calls << delimiter << result.stats.terminal_nodes
            << delimiter << result.stats.pass_nodes << delimiter << result.stats.beta_cutoffs
            << delimiter << result.stats.alpha_updates << delimiter << result.stats.pvs_researches
            << delimiter << result.stats.root_moves_searched << delimiter << result.stats.tt_probes
            << delimiter << result.stats.tt_hits << delimiter << result.stats.tt_cutoffs
            << delimiter << result.stats.tt_stores << delimiter << result.stats.tt_replacements
            << delimiter << result.stats.tt_bucket_conflicts << delimiter
            << result.stats.tt_rejected_stores << delimiter
            << result.stats.tt_invalid_best_move_stores << delimiter
            << result.stats.endgame_stability_probes << delimiter
            << result.stats.endgame_stability_lower_candidates << delimiter
            << result.stats.endgame_stability_upper_candidates << delimiter
            << result.stats.endgame_stability_cutoffs << delimiter
            << result.stats.endgame_stability_shadow_verifications << delimiter
            << result.stats.endgame_stability_shadow_false_cutoffs << delimiter << std::fixed
            << std::setprecision(3) << elapsed_ms(timed_result.elapsed) << delimiter << std::fixed
            << std::setprecision(0) << nodes_per_second(timed_result) << '\n';
}

void print_jsonl_result(const PositionCase& position_case, std::uint32_t repeat,
                        std::uint8_t empties, bool use_parity_ordering, bool use_tt,
                        EndgameStabilityMode stability_mode, bool use_pvs, RootMode root_mode,
                        SolveMode solve_mode, const TimedResult& timed_result) {
  const SearchResult& result = timed_result.result;
  std::cout << '{';
  std::cout << "\"position_id\":";
  print_json_string(std::cout, position_case.id);
  std::cout << ",\"category\":";
  print_json_string(std::cout, position_case.category);
  std::cout << ",\"position\":";
  print_json_string(std::cout, format_position(position_case.position));
  std::cout << ",\"mode\":";
  print_json_string(std::cout, solve_mode_name(solve_mode));
  std::cout << ",\"empties\":" << static_cast<int>(empties);
  std::cout << ",\"repeat\":" << repeat;
  std::cout << ",\"entry\":";
  print_json_string(std::cout, entry_mode_name(timed_result.entry_mode));
  std::cout << ",\"threshold\":" << static_cast<int>(timed_result.threshold);
  std::cout << ",\"triggered\":" << (timed_result.triggered ? "true" : "false");
  std::cout << ",\"status\":";
  print_json_string(std::cout, timed_result.status);
  std::cout << ",\"parity_ordering\":";
  print_json_string(std::cout, parity_mode_name(use_parity_ordering));
  std::cout << ",\"tt_mode\":";
  print_json_string(std::cout, tt_mode_name(use_tt));
  std::cout << ",\"stability_mode\":";
  print_json_string(std::cout, stability_mode_name(stability_mode));
  std::cout << ",\"pvs\":";
  print_json_string(std::cout, pvs_mode_name(use_pvs));
  std::cout << ",\"root_mode\":";
  print_json_string(std::cout, root_mode_name(root_mode));
  std::cout << ",\"score\":" << result.score;
  std::cout << ",\"score_kind\":";
  print_json_string(std::cout, score_kind_name(result.score_kind));
  if (solve_mode == SolveMode::wld && timed_result.triggered) {
    std::cout << ",\"wld_result\":";
    print_json_string(std::cout, wld_result_name(result));
  }
  std::cout << ",\"best_move\":";
  print_json_string(std::cout, best_move_to_string(result));
  std::cout << ",\"exact\":" << (result.exact ? "true" : "false");
  std::cout << ",\"stopped\":" << (result.stopped ? "true" : "false");
  std::cout << ",\"completed_depth\":" << result.completed_depth;
  std::cout << ",\"nodes\":" << result.nodes;
  std::cout << ",\"endgame_nodes\":" << result.stats.endgame_nodes;
  std::cout << ",\"last_flip_solved\":" << result.stats.endgame_last_flip_solved;
  std::cout << ",\"eval_calls\":" << result.stats.eval_calls;
  std::cout << ",\"terminal_nodes\":" << result.stats.terminal_nodes;
  std::cout << ",\"pass_nodes\":" << result.stats.pass_nodes;
  std::cout << ",\"beta_cutoffs\":" << result.stats.beta_cutoffs;
  std::cout << ",\"alpha_updates\":" << result.stats.alpha_updates;
  std::cout << ",\"pvs_researches\":" << result.stats.pvs_researches;
  std::cout << ",\"root_moves_searched\":" << result.stats.root_moves_searched;
  std::cout << ",\"tt_probes\":" << result.stats.tt_probes;
  std::cout << ",\"tt_hits\":" << result.stats.tt_hits;
  std::cout << ",\"tt_cutoffs\":" << result.stats.tt_cutoffs;
  std::cout << ",\"tt_stores\":" << result.stats.tt_stores;
  std::cout << ",\"tt_replacements\":" << result.stats.tt_replacements;
  std::cout << ",\"tt_bucket_conflicts\":" << result.stats.tt_bucket_conflicts;
  std::cout << ",\"tt_rejected_stores\":" << result.stats.tt_rejected_stores;
  std::cout << ",\"tt_invalid_best_move_stores\":" << result.stats.tt_invalid_best_move_stores;
  std::cout << ",\"stability_probes\":" << result.stats.endgame_stability_probes;
  std::cout << ",\"stability_lower_candidates\":"
            << result.stats.endgame_stability_lower_candidates;
  std::cout << ",\"stability_upper_candidates\":"
            << result.stats.endgame_stability_upper_candidates;
  std::cout << ",\"stability_cutoffs\":" << result.stats.endgame_stability_cutoffs;
  std::cout << ",\"stability_shadow_verifications\":"
            << result.stats.endgame_stability_shadow_verifications;
  std::cout << ",\"stability_shadow_false_cutoffs\":"
            << result.stats.endgame_stability_shadow_false_cutoffs;
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

bool position_matches_filters(const Config& config, const PositionCase& position_case,
                              std::uint8_t actual_empties) {
  if (actual_empties < config.min_empties) {
    return false;
  }
  if (actual_empties > config.max_empties &&
      (config.max_empties_was_set || config.position_ids.empty())) {
    return false;
  }
  if (!config.position_ids.empty() && !contains_value(config.position_ids, position_case.id)) {
    return false;
  }
  if (!config.categories.empty() && !contains_value(config.categories, position_case.category)) {
    return false;
  }
  return true;
}

std::vector<const PositionCase*> filter_positions(const Config& config,
                                                  const std::vector<PositionCase>& positions) {
  std::vector<const PositionCase*> filtered;
  for (const PositionCase& position_case : positions) {
    const std::uint8_t actual_empties = empty_count(position_case.position);
    if (position_matches_filters(config, position_case, actual_empties)) {
      filtered.push_back(&position_case);
    }
  }
  return filtered;
}

void print_position_list(const std::vector<const PositionCase*>& positions, char delimiter) {
  std::cout << "id" << delimiter << "category" << delimiter << "empties" << delimiter << "notes"
            << '\n';
  for (const PositionCase* position_case : positions) {
    std::cout << position_case->id << delimiter << position_case->category << delimiter
              << static_cast<int>(empty_count(position_case->position)) << delimiter
              << position_case->notes << '\n';
  }
}

std::array<bool, 2> parity_runs(ParityMode mode, std::uint8_t* size) noexcept {
  switch (mode) {
  case ParityMode::on:
    *size = 1;
    return {true, false};
  case ParityMode::off:
    *size = 1;
    return {false, false};
  case ParityMode::both:
    *size = 2;
    return {false, true};
  }

  *size = 1;
  return {true, false};
}

std::array<bool, 2> tt_runs(TTMode mode, std::uint8_t* size) noexcept {
  switch (mode) {
  case TTMode::off:
    *size = 1;
    return {false, false};
  case TTMode::on:
    *size = 1;
    return {true, false};
  case TTMode::both:
    *size = 2;
    return {false, true};
  }

  *size = 1;
  return {false, false};
}

std::array<EndgameStabilityMode, 3> stability_runs(StabilityModeSelection mode,
                                                   std::uint8_t* size) noexcept {
  switch (mode) {
  case StabilityModeSelection::off:
    *size = 1;
    return {EndgameStabilityMode::off, EndgameStabilityMode::off, EndgameStabilityMode::off};
  case StabilityModeSelection::shadow:
    *size = 1;
    return {EndgameStabilityMode::shadow, EndgameStabilityMode::off, EndgameStabilityMode::off};
  case StabilityModeSelection::cutoff:
    *size = 1;
    return {EndgameStabilityMode::cutoff, EndgameStabilityMode::off, EndgameStabilityMode::off};
  case StabilityModeSelection::all:
    *size = 3;
    return {EndgameStabilityMode::off, EndgameStabilityMode::shadow, EndgameStabilityMode::cutoff};
  }

  *size = 1;
  return {EndgameStabilityMode::cutoff, EndgameStabilityMode::off, EndgameStabilityMode::off};
}

std::array<bool, 2> pvs_runs(PvsMode mode, std::uint8_t* size) noexcept {
  switch (mode) {
  case PvsMode::off:
    *size = 1;
    return {false, false};
  case PvsMode::on:
    *size = 1;
    return {true, false};
  case PvsMode::both:
    *size = 2;
    return {false, true};
  }

  *size = 1;
  return {true, false};
}

} // namespace

int main(int argc, char** argv) {
  const std::optional<Config> config = parse_config(argc, argv);
  if (!config.has_value()) {
    return 0;
  }

  const std::vector<PositionCase> positions =
      config->corpus_path.empty() ? default_positions() : load_corpus(config->corpus_path);
  const std::vector<const PositionCase*> filtered_positions = filter_positions(*config, positions);
  require_condition(!filtered_positions.empty(), "no endgame positions matched filters");

  if (config->list_positions) {
    print_position_list(filtered_positions, config->delimiter);
    return 0;
  }

  if (config->output_format != OutputFormat::jsonl) {
    print_delimited_header(config->delimiter);
  }
  for (const PositionCase* filtered_position : filtered_positions) {
    const PositionCase& position_case = *filtered_position;
    const std::uint8_t actual_empties = empty_count(position_case.position);

    std::uint8_t parity_run_count = 0;
    const std::array<bool, 2> parity_values = parity_runs(config->parity_mode, &parity_run_count);
    for (std::uint8_t parity_index = 0; parity_index < parity_run_count; ++parity_index) {
      const bool use_parity_ordering = parity_values[parity_index];
      std::uint8_t tt_run_count = 0;
      const std::array<bool, 2> tt_values = tt_runs(config->tt_mode, &tt_run_count);
      for (std::uint8_t tt_index = 0; tt_index < tt_run_count; ++tt_index) {
        const bool use_tt = tt_values[tt_index];
        std::uint8_t stability_run_count = 0;
        const std::array<EndgameStabilityMode, 3> stability_values =
            stability_runs(config->stability_mode, &stability_run_count);
        for (std::uint8_t stability_index = 0; stability_index < stability_run_count;
             ++stability_index) {
          const EndgameStabilityMode stability_mode = stability_values[stability_index];
          std::uint8_t pvs_run_count = 0;
          const std::array<bool, 2> pvs_values = pvs_runs(config->pvs_mode, &pvs_run_count);
          for (std::uint8_t pvs_index = 0; pvs_index < pvs_run_count; ++pvs_index) {
            const bool use_pvs = pvs_values[pvs_index];
            for (std::uint32_t repeat = 1; repeat <= config->repeat; ++repeat) {
              const TimedResult timed_result =
                  run_endgame(position_case.position, actual_empties, use_parity_ordering, use_tt,
                              stability_mode, use_pvs, config->root_mode, config->solve_mode,
                              config->entry_mode, config->endgame_wld_empties);
              validate_result(position_case, timed_result, actual_empties, use_tt,
                              config->solve_mode);
              if (config->output_format == OutputFormat::jsonl) {
                print_jsonl_result(position_case, repeat, actual_empties, use_parity_ordering,
                                   use_tt, stability_mode, use_pvs, config->root_mode,
                                   config->solve_mode, timed_result);
              } else {
                print_delimited_result(position_case, repeat, actual_empties, use_parity_ordering,
                                       use_tt, stability_mode, use_pvs, config->root_mode,
                                       config->solve_mode, timed_result, config->delimiter);
              }
            }
          }
        }
      }
    }
  }

  return 0;
}
