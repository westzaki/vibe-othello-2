#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/serialization.h"
#include "vibe_othello/evaluation/pattern_artifact.h"
#include "vibe_othello/evaluation/phase_aware_evaluator.h"
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
#include <utility>
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
using vibe_othello::search::ScoreKind;
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

enum class BoolSelection {
  off,
  on,
  both,
};

enum class EvalSelection {
  disc,
  simple,
  pattern_v2_incremental,
  pattern_v2_stateless,
  pattern_v2_both,
  all,
};

enum class EvalMode {
  disc,
  simple,
  pattern_v2_incremental,
  pattern_v2_stateless,
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
  BoolSelection pvs = BoolSelection::off;
  BoolSelection aspiration = BoolSelection::off;
  BoolSelection history = BoolSelection::off;
  BoolSelection killers = BoolSelection::off;
  BoolSelection iid = BoolSelection::off;
  EvalSelection eval = EvalSelection::disc;
  std::uint32_t max_time_ms = 0;
  std::uint8_t exact_endgame_empties = 0;
  BoolSelection endgame_tt = BoolSelection::off;
  BoolSelection endgame_parity = BoolSelection::on;
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

struct BenchmarkVariant {
  EvalMode eval_mode = EvalMode::disc;
  TTMode tt_mode = TTMode::off;
  bool use_pvs = false;
  bool use_aspiration = false;
  bool use_history = false;
  bool use_killers = false;
  bool use_iid = false;
  std::uint32_t max_time_ms = 0;
  std::uint8_t exact_endgame_empties = 0;
  bool use_endgame_tt = false;
  bool use_endgame_parity = true;
};

class DiscDifferenceEvaluator final : public Evaluator {
public:
  Score evaluate(const Position& position) const noexcept override {
    return static_cast<Score>(std::popcount(position.player)) -
           static_cast<Score>(std::popcount(position.opponent));
  }
};

class SimpleEvaluator final : public Evaluator {
public:
  Score evaluate(const Position& position) const noexcept override {
    return disc_difference(position) + corner_difference(position) * Score{4};
  }

private:
  static Score disc_difference(const Position& position) noexcept {
    return static_cast<Score>(std::popcount(position.player)) -
           static_cast<Score>(std::popcount(position.opponent));
  }

  static Score corner_difference(const Position& position) noexcept {
    Score score = 0;
    for (const Square corner : {square_from_file_rank(0, 0), square_from_file_rank(7, 0),
                                square_from_file_rank(0, 7), square_from_file_rank(7, 7)}) {
      const Bitboard corner_bit = bit(corner);
      if ((position.player & corner_bit) != 0) {
        ++score;
      } else if ((position.opponent & corner_bit) != 0) {
        --score;
      }
    }
    return score;
  }
};

class StatelessPhaseAwareEvaluator final : public Evaluator {
public:
  explicit StatelessPhaseAwareEvaluator(
      const vibe_othello::evaluation::PhaseAwareEvaluator* evaluator)
      : evaluator_(evaluator) {}

  Score evaluate(const Position& position) const noexcept override {
    return evaluator_->evaluate_reference(position);
  }

private:
  const vibe_othello::evaluation::PhaseAwareEvaluator* evaluator_;
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

std::string_view bool_mode_name(bool enabled) noexcept {
  return enabled ? "on" : "off";
}

std::string_view eval_mode_name(EvalMode mode) noexcept {
  switch (mode) {
  case EvalMode::disc:
    return "disc_difference";
  case EvalMode::simple:
    return "simple";
  case EvalMode::pattern_v2_incremental:
    return "pattern_v2_incremental";
  case EvalMode::pattern_v2_stateless:
    return "pattern_v2_stateless";
  }

  return "unknown";
}

std::optional<BoolSelection> parse_bool_selection(std::string_view value) noexcept {
  if (value == "off") {
    return BoolSelection::off;
  }
  if (value == "on") {
    return BoolSelection::on;
  }
  if (value == "both") {
    return BoolSelection::both;
  }
  return std::nullopt;
}

std::optional<EvalSelection> parse_eval_selection(std::string_view value) noexcept {
  if (value == "disc") {
    return EvalSelection::disc;
  }
  if (value == "simple") {
    return EvalSelection::simple;
  }
  if (value == "pattern-v2" || value == "pattern-v2-incremental") {
    return EvalSelection::pattern_v2_incremental;
  }
  if (value == "pattern-v2-stateless") {
    return EvalSelection::pattern_v2_stateless;
  }
  if (value == "pattern-v2-both") {
    return EvalSelection::pattern_v2_both;
  }
  if (value == "all") {
    return EvalSelection::all;
  }
  return std::nullopt;
}

std::vector<bool> bool_values(BoolSelection selection) {
  switch (selection) {
  case BoolSelection::off:
    return {false};
  case BoolSelection::on:
    return {true};
  case BoolSelection::both:
    return {false, true};
  }

  return {};
}

std::vector<EvalMode> eval_values(EvalSelection selection) {
  switch (selection) {
  case EvalSelection::disc:
    return {EvalMode::disc};
  case EvalSelection::simple:
    return {EvalMode::simple};
  case EvalSelection::pattern_v2_incremental:
    return {EvalMode::pattern_v2_incremental};
  case EvalSelection::pattern_v2_stateless:
    return {EvalMode::pattern_v2_stateless};
  case EvalSelection::pattern_v2_both:
    return {EvalMode::pattern_v2_stateless, EvalMode::pattern_v2_incremental};
  case EvalSelection::all:
    return {EvalMode::disc, EvalMode::simple, EvalMode::pattern_v2_stateless,
            EvalMode::pattern_v2_incremental};
  }

  return {};
}

bool uses_pattern_evaluator(EvalSelection selection) noexcept {
  return selection == EvalSelection::pattern_v2_incremental ||
         selection == EvalSelection::pattern_v2_stateless ||
         selection == EvalSelection::pattern_v2_both || selection == EvalSelection::all;
}

SearchOptions search_options_for_variant(BenchmarkVariant variant) noexcept {
  SearchOptions options{};
  switch (variant.tt_mode) {
  case TTMode::off:
    break;
  case TTMode::ordering:
    options.ordering.use_tt_best_move_ordering = true;
    break;
  case TTMode::midgame:
    options.midgame.use_midgame_tt = true;
    break;
  case TTMode::both:
    options.midgame.use_midgame_tt = true;
    options.ordering.use_tt_best_move_ordering = true;
    break;
  }

  options.midgame.use_pvs = variant.use_pvs;
  options.midgame.use_aspiration = variant.use_aspiration;
  options.midgame.use_iid = variant.use_iid;
  options.ordering.use_history = variant.use_history;
  options.ordering.use_killers = variant.use_killers;
  options.endgame.use_endgame_tt = variant.use_endgame_tt;
  options.ordering.use_endgame_parity_ordering = variant.use_endgame_parity;
  options.endgame.exact_endgame = variant.exact_endgame_empties > 0;
  options.endgame.endgame_exact_empties = variant.exact_endgame_empties;
  options.reporting.multi_pv = 1;
  return options;
}

std::string variant_id(BenchmarkVariant variant) {
  std::string id = "eval=";
  id += eval_mode_name(variant.eval_mode);
  id += ";tt=";
  id += tt_mode_name(variant.tt_mode);
  id += ";pvs=";
  id += bool_mode_name(variant.use_pvs);
  id += ";aspiration=";
  id += bool_mode_name(variant.use_aspiration);
  id += ";history=";
  id += bool_mode_name(variant.use_history);
  id += ";killers=";
  id += bool_mode_name(variant.use_killers);
  id += ";iid=";
  id += bool_mode_name(variant.use_iid);
  id += ";exact_endgame=";
  id += std::to_string(variant.exact_endgame_empties);
  id += ";endgame_tt=";
  id += bool_mode_name(variant.use_endgame_tt);
  id += ";endgame_parity=";
  id += bool_mode_name(variant.use_endgame_parity);
  id += ";time_ms=";
  id += std::to_string(variant.max_time_ms);
  return id;
}

std::vector<BenchmarkVariant> variants_for_mode(BenchmarkMode mode, const Config& config) {
  std::vector<BenchmarkVariant> variants;
  const std::vector<EvalMode> eval_modes = eval_values(config.eval);
  const std::vector<bool> pvs_values =
      mode == BenchmarkMode::iterative ? bool_values(config.pvs) : std::vector<bool>{false};
  const std::vector<bool> aspiration_values =
      mode == BenchmarkMode::iterative ? bool_values(config.aspiration) : std::vector<bool>{false};
  const std::vector<bool> history_values =
      mode == BenchmarkMode::iterative ? bool_values(config.history) : std::vector<bool>{false};
  const std::vector<bool> killer_values =
      mode == BenchmarkMode::iterative ? bool_values(config.killers) : std::vector<bool>{false};
  const std::vector<bool> iid_values =
      mode == BenchmarkMode::iterative ? bool_values(config.iid) : std::vector<bool>{false};
  const std::vector<bool> endgame_tt_values =
      mode == BenchmarkMode::iterative ? bool_values(config.endgame_tt) : std::vector<bool>{false};
  const std::vector<bool> endgame_parity_values = mode == BenchmarkMode::iterative
                                                      ? bool_values(config.endgame_parity)
                                                      : std::vector<bool>{true};

  for (const EvalMode eval_mode : eval_modes) {
    for (const bool use_pvs : pvs_values) {
      for (const bool use_aspiration : aspiration_values) {
        for (const bool use_history : history_values) {
          for (const bool use_killers : killer_values) {
            for (const bool use_iid : iid_values) {
              for (const bool use_endgame_tt : endgame_tt_values) {
                for (const bool use_endgame_parity : endgame_parity_values) {
                  variants.push_back(BenchmarkVariant{
                      .eval_mode = eval_mode,
                      .tt_mode = mode == BenchmarkMode::iterative ? config.tt_mode : TTMode::off,
                      .use_pvs = use_pvs,
                      .use_aspiration = use_aspiration,
                      .use_history = use_history,
                      .use_killers = use_killers,
                      .use_iid = use_iid,
                      .max_time_ms = mode == BenchmarkMode::iterative ? config.max_time_ms : 0,
                      .exact_endgame_empties = mode == BenchmarkMode::iterative
                                                   ? config.exact_endgame_empties
                                                   : std::uint8_t{0},
                      .use_endgame_tt = use_endgame_tt,
                      .use_endgame_parity = use_endgame_parity,
                  });
                }
              }
            }
          }
        }
      }
    }
  }
  return variants;
}

void print_usage(std::ostream& output, std::string_view program) {
  output << "Usage: " << program
         << " [--depth N|START..END|START-END]"
            " [--mode all|fixed|iterative] [--tt off|ordering|midgame|both]"
            " [--pvs off|on|both] [--aspiration off|on|both]"
            " [--history off|on|both] [--killers off|on|both] [--iid off|on|both]"
            " [--eval disc|simple|pattern-v2|pattern-v2-stateless|pattern-v2-both|all]"
            " [--time-ms N] [--exact-endgame N]"
            " [--endgame-tt off|on|both] [--endgame-parity off|on|both]"
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

    if (argument == "--pvs") {
      require_condition(index + 1 < argc, "--pvs requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--pvs", &value)) {
      value = {};
    }
    if (!value.empty()) {
      const std::optional<BoolSelection> selection = parse_bool_selection(value);
      require_condition(selection.has_value(), "unknown PVS mode");
      config.pvs = *selection;
      continue;
    }

    if (argument == "--aspiration") {
      require_condition(index + 1 < argc, "--aspiration requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--aspiration", &value)) {
      value = {};
    }
    if (!value.empty()) {
      const std::optional<BoolSelection> selection = parse_bool_selection(value);
      require_condition(selection.has_value(), "unknown aspiration mode");
      config.aspiration = *selection;
      continue;
    }

    if (argument == "--history") {
      require_condition(index + 1 < argc, "--history requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--history", &value)) {
      value = {};
    }
    if (!value.empty()) {
      const std::optional<BoolSelection> selection = parse_bool_selection(value);
      require_condition(selection.has_value(), "unknown history mode");
      config.history = *selection;
      continue;
    }

    if (argument == "--killers") {
      require_condition(index + 1 < argc, "--killers requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--killers", &value)) {
      value = {};
    }
    if (!value.empty()) {
      const std::optional<BoolSelection> selection = parse_bool_selection(value);
      require_condition(selection.has_value(), "unknown killers mode");
      config.killers = *selection;
      continue;
    }

    if (argument == "--iid") {
      require_condition(index + 1 < argc, "--iid requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--iid", &value)) {
      value = {};
    }
    if (!value.empty()) {
      const std::optional<BoolSelection> selection = parse_bool_selection(value);
      require_condition(selection.has_value(), "unknown IID mode");
      config.iid = *selection;
      continue;
    }

    if (argument == "--eval") {
      require_condition(index + 1 < argc, "--eval requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--eval", &value)) {
      value = {};
    }
    if (!value.empty()) {
      const std::optional<EvalSelection> selection = parse_eval_selection(value);
      require_condition(selection.has_value(), "unknown evaluator");
      config.eval = *selection;
      continue;
    }

    if (argument == "--time-ms") {
      require_condition(index + 1 < argc, "--time-ms requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--time-ms", &value)) {
      value = {};
    }
    if (!value.empty()) {
      const std::optional<int> time_ms = parse_int(value);
      require_condition(time_ms.has_value() && *time_ms >= 0, "invalid time limit");
      config.max_time_ms = static_cast<std::uint32_t>(*time_ms);
      continue;
    }

    if (argument == "--exact-endgame") {
      require_condition(index + 1 < argc, "--exact-endgame requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--exact-endgame", &value)) {
      value = {};
    }
    if (!value.empty()) {
      const std::optional<int> empties = parse_int(value);
      require_condition(empties.has_value() && *empties >= 0 &&
                            *empties <= vibe_othello::board_core::kSquareCount,
                        "invalid exact endgame threshold");
      config.exact_endgame_empties = static_cast<std::uint8_t>(*empties);
      continue;
    }

    if (argument == "--endgame-tt") {
      require_condition(index + 1 < argc, "--endgame-tt requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--endgame-tt", &value)) {
      value = {};
    }
    if (!value.empty()) {
      const std::optional<BoolSelection> selection = parse_bool_selection(value);
      require_condition(selection.has_value(), "unknown endgame TT mode");
      config.endgame_tt = *selection;
      continue;
    }

    if (argument == "--endgame-parity") {
      require_condition(index + 1 < argc, "--endgame-parity requires a value");
      value = argv[++index];
    } else if (!parse_argument_with_value(argument, "--endgame-parity", &value)) {
      value = {};
    }
    if (!value.empty()) {
      const std::optional<BoolSelection> selection = parse_bool_selection(value);
      require_condition(selection.has_value(), "unknown endgame parity mode");
      config.endgame_parity = *selection;
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

TimedResult run_search(BenchmarkMode mode, BenchmarkVariant variant, Position position, Depth depth,
                       const Evaluator* pattern_incremental, const Evaluator* pattern_stateless) {
  DiscDifferenceEvaluator evaluator;
  SimpleEvaluator simple_evaluator;
  const Evaluator* selected_evaluator = &evaluator;
  switch (variant.eval_mode) {
  case EvalMode::disc:
    break;
  case EvalMode::simple:
    selected_evaluator = &simple_evaluator;
    break;
  case EvalMode::pattern_v2_incremental:
    require_condition(pattern_incremental != nullptr, "incremental pattern evaluator is missing");
    selected_evaluator = pattern_incremental;
    break;
  case EvalMode::pattern_v2_stateless:
    require_condition(pattern_stateless != nullptr, "stateless pattern evaluator is missing");
    selected_evaluator = pattern_stateless;
    break;
  }
  const auto start = std::chrono::steady_clock::now();

  SearchResult result =
      mode == BenchmarkMode::fixed
          ? search_fixed_depth(position, *selected_evaluator, depth)
          : search_iterative(position, *selected_evaluator,
                             SearchLimits{
                                 .max_depth = depth,
                                 .max_time = std::chrono::milliseconds{variant.max_time_ms},
                             },
                             search_options_for_variant(variant));

  return TimedResult{
      .result = result,
      .elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - start),
  };
}

void print_delimited_header(char delimiter) {
  std::cout << "position_name" << delimiter << "mode" << delimiter << "variant_id" << delimiter
            << "tt_mode" << delimiter << "evaluator" << delimiter << "pvs" << delimiter
            << "aspiration" << delimiter << "history" << delimiter << "killers" << delimiter
            << "iid" << delimiter << "exact_endgame" << delimiter << "endgame_exact_empties"
            << delimiter << "endgame_tt" << delimiter << "endgame_parity" << delimiter << "depth"
            << delimiter << "score" << delimiter << "score_kind" << delimiter << "best_move"
            << delimiter << "nodes" << delimiter << "eval_calls" << delimiter
            << "incremental_eval_enabled" << delimiter << "incremental_state_initializations"
            << delimiter << "incremental_eval_calls" << delimiter << "stateless_eval_calls"
            << delimiter << "incremental_updates" << delimiter << "incremental_touched_instances"
            << delimiter << "terminal_nodes" << delimiter << "pass_nodes" << delimiter
            << "beta_cutoffs" << delimiter << "alpha_updates" << delimiter << "pvs_researches"
            << delimiter << "aspiration_fail_lows" << delimiter << "aspiration_fail_highs"
            << delimiter << "iid_searches" << delimiter << "endgame_nodes" << delimiter
            << "tt_probes" << delimiter << "tt_hits" << delimiter << "tt_stores" << delimiter
            << "tt_cutoffs" << delimiter << "tt_replacements" << delimiter << "tt_bucket_conflicts"
            << delimiter << "tt_rejected_stores" << delimiter << "tt_invalid_best_move_stores"
            << delimiter << "completed_depth" << delimiter << "stopped" << delimiter << "elapsed_ms"
            << delimiter << "nps" << '\n';
}

void print_delimited_result(const PositionCase& position_case, BenchmarkMode mode,
                            BenchmarkVariant variant, Depth depth, TimedResult timed_result,
                            char delimiter) {
  const double elapsed_ms = std::chrono::duration<double, std::milli>(timed_result.elapsed).count();
  const double elapsed_seconds = std::chrono::duration<double>(timed_result.elapsed).count();
  const double nps = elapsed_seconds > 0.0
                         ? static_cast<double>(timed_result.result.nodes) / elapsed_seconds
                         : 0.0;
  const std::string id = variant_id(variant);

  std::cout << position_case.id << delimiter << mode_name(mode) << delimiter << id << delimiter
            << tt_mode_name(variant.tt_mode) << delimiter << eval_mode_name(variant.eval_mode)
            << delimiter << bool_mode_name(variant.use_pvs) << delimiter
            << bool_mode_name(variant.use_aspiration) << delimiter
            << bool_mode_name(variant.use_history) << delimiter
            << bool_mode_name(variant.use_killers) << delimiter << bool_mode_name(variant.use_iid)
            << delimiter << bool_mode_name(variant.exact_endgame_empties > 0) << delimiter
            << static_cast<int>(variant.exact_endgame_empties) << delimiter
            << bool_mode_name(variant.use_endgame_tt) << delimiter
            << bool_mode_name(variant.use_endgame_parity) << delimiter << depth << delimiter
            << timed_result.result.score << delimiter
            << score_kind_name(timed_result.result.score_kind) << delimiter
            << best_move_to_string(timed_result.result) << delimiter << timed_result.result.nodes
            << delimiter << timed_result.result.stats.eval_calls << delimiter
            << bool_mode_name(timed_result.result.stats.incremental_eval_enabled) << delimiter
            << timed_result.result.stats.incremental_state_initializations << delimiter
            << timed_result.result.stats.incremental_eval_calls << delimiter
            << timed_result.result.stats.stateless_eval_calls << delimiter
            << timed_result.result.stats.incremental_updates << delimiter
            << timed_result.result.stats.incremental_touched_instances << delimiter
            << timed_result.result.stats.terminal_nodes << delimiter
            << timed_result.result.stats.pass_nodes << delimiter
            << timed_result.result.stats.beta_cutoffs << delimiter
            << timed_result.result.stats.alpha_updates << delimiter
            << timed_result.result.stats.pvs_researches << delimiter
            << timed_result.result.stats.aspiration_fail_lows << delimiter
            << timed_result.result.stats.aspiration_fail_highs << delimiter
            << timed_result.result.stats.iid_searches << delimiter
            << timed_result.result.stats.endgame_nodes << delimiter
            << timed_result.result.stats.tt_probes << delimiter << timed_result.result.stats.tt_hits
            << delimiter << timed_result.result.stats.tt_stores << delimiter
            << timed_result.result.stats.tt_cutoffs << delimiter
            << timed_result.result.stats.tt_replacements << delimiter
            << timed_result.result.stats.tt_bucket_conflicts << delimiter
            << timed_result.result.stats.tt_rejected_stores << delimiter
            << timed_result.result.stats.tt_invalid_best_move_stores << delimiter << std::fixed
            << static_cast<int>(timed_result.result.completed_depth) << delimiter
            << bool_mode_name(timed_result.result.stopped) << delimiter << std::setprecision(3)
            << elapsed_ms << delimiter << std::fixed << std::setprecision(0) << nps << '\n';
}

void print_jsonl_result(const PositionCase& position_case, BenchmarkMode mode,
                        BenchmarkVariant variant, Depth depth, TimedResult timed_result) {
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
  std::cout << ",\"variant_id\":";
  print_json_string(std::cout, variant_id(variant));
  std::cout << ",\"tt_mode\":";
  print_json_string(std::cout, tt_mode_name(variant.tt_mode));
  std::cout << ",\"depth\":" << depth;
  std::cout << ",\"evaluator\":";
  print_json_string(std::cout, eval_mode_name(variant.eval_mode));
  std::cout << ",\"pvs\":";
  print_json_string(std::cout, bool_mode_name(variant.use_pvs));
  std::cout << ",\"aspiration\":";
  print_json_string(std::cout, bool_mode_name(variant.use_aspiration));
  std::cout << ",\"history\":";
  print_json_string(std::cout, bool_mode_name(variant.use_history));
  std::cout << ",\"killers\":";
  print_json_string(std::cout, bool_mode_name(variant.use_killers));
  std::cout << ",\"iid\":";
  print_json_string(std::cout, bool_mode_name(variant.use_iid));
  std::cout << ",\"exact_endgame\":" << (variant.exact_endgame_empties > 0 ? "true" : "false");
  std::cout << ",\"endgame_exact_empties\":" << static_cast<int>(variant.exact_endgame_empties);
  std::cout << ",\"endgame_tt\":";
  print_json_string(std::cout, bool_mode_name(variant.use_endgame_tt));
  std::cout << ",\"endgame_parity\":";
  print_json_string(std::cout, bool_mode_name(variant.use_endgame_parity));
  std::cout << ",\"score\":" << timed_result.result.score;
  std::cout << ",\"completed_depth\":" << static_cast<int>(timed_result.result.completed_depth);
  std::cout << ",\"stopped\":" << (timed_result.result.stopped ? "true" : "false");
  std::cout << ",\"score_kind\":";
  print_json_string(std::cout, score_kind_name(timed_result.result.score_kind));
  std::cout << ",\"best_move\":";
  print_json_string(std::cout, best_move_to_string(timed_result.result));
  std::cout << ",\"pv\":";
  print_json_line(std::cout, timed_result.result.pv);
  std::cout << ",\"root_moves\":";
  print_json_root_moves(std::cout, timed_result.result.root_moves);
  std::cout << ",\"nodes\":" << timed_result.result.nodes;
  std::cout << ",\"eval_calls\":" << timed_result.result.stats.eval_calls;
  std::cout << ",\"incremental_eval_enabled\":"
            << (timed_result.result.stats.incremental_eval_enabled ? "true" : "false");
  std::cout << ",\"incremental_state_initializations\":"
            << timed_result.result.stats.incremental_state_initializations;
  std::cout << ",\"incremental_eval_calls\":" << timed_result.result.stats.incremental_eval_calls;
  std::cout << ",\"stateless_eval_calls\":" << timed_result.result.stats.stateless_eval_calls;
  std::cout << ",\"incremental_updates\":" << timed_result.result.stats.incremental_updates;
  std::cout << ",\"incremental_touched_instances\":"
            << timed_result.result.stats.incremental_touched_instances;
  std::cout << ",\"terminal_nodes\":" << timed_result.result.stats.terminal_nodes;
  std::cout << ",\"pass_nodes\":" << timed_result.result.stats.pass_nodes;
  std::cout << ",\"beta_cutoffs\":" << timed_result.result.stats.beta_cutoffs;
  std::cout << ",\"alpha_updates\":" << timed_result.result.stats.alpha_updates;
  std::cout << ",\"pvs_researches\":" << timed_result.result.stats.pvs_researches;
  std::cout << ",\"aspiration_fail_lows\":" << timed_result.result.stats.aspiration_fail_lows;
  std::cout << ",\"aspiration_fail_highs\":" << timed_result.result.stats.aspiration_fail_highs;
  std::cout << ",\"iid_searches\":" << timed_result.result.stats.iid_searches;
  std::cout << ",\"endgame_nodes\":" << timed_result.result.stats.endgame_nodes;
  std::cout << ",\"tt_probes\":" << timed_result.result.stats.tt_probes;
  std::cout << ",\"tt_hits\":" << timed_result.result.stats.tt_hits;
  std::cout << ",\"tt_stores\":" << timed_result.result.stats.tt_stores;
  std::cout << ",\"tt_cutoffs\":" << timed_result.result.stats.tt_cutoffs;
  std::cout << ",\"tt_replacements\":" << timed_result.result.stats.tt_replacements;
  std::cout << ",\"tt_bucket_conflicts\":" << timed_result.result.stats.tt_bucket_conflicts;
  std::cout << ",\"tt_rejected_stores\":" << timed_result.result.stats.tt_rejected_stores;
  std::cout << ",\"tt_invalid_best_move_stores\":"
            << timed_result.result.stats.tt_invalid_best_move_stores;
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
  std::optional<vibe_othello::evaluation::PhaseAwareEvaluator> pattern_incremental;
  std::optional<StatelessPhaseAwareEvaluator> pattern_stateless;
  if (uses_pattern_evaluator(config->eval)) {
    vibe_othello::evaluation::PatternArtifactLoadResult artifact_result =
        vibe_othello::evaluation::load_default_pattern_artifact(
            vibe_othello::evaluation::default_eval_root(VIBE_OTHELLO_SOURCE_DIR));
    require_condition(artifact_result.ok(), "failed to load committed default artifact");
    vibe_othello::evaluation::LoadedPatternArtifact artifact = std::move(*artifact_result.artifact);
    pattern_incremental.emplace(std::move(artifact.weights), std::move(artifact.feature_set),
                                std::move(artifact.trained_phases),
                                artifact.fallback_additive_through_phase);
    pattern_stateless.emplace(&*pattern_incremental);
  }

  if (config->output_format != OutputFormat::jsonl) {
    print_delimited_header(config->delimiter);
  }
  for (const PositionCase& position_case : positions) {
    const std::vector<Depth>& depths =
        config->depths_overridden ? config->depths : position_case.depths;
    for (BenchmarkMode mode : modes) {
      const std::vector<BenchmarkVariant> variants = variants_for_mode(mode, *config);
      for (Depth depth : depths) {
        for (const BenchmarkVariant variant : variants) {
          TimedResult timed_result =
              run_search(mode, variant, position_case.position, depth,
                         pattern_incremental ? &*pattern_incremental : nullptr,
                         pattern_stateless ? &*pattern_stateless : nullptr);
          if (config->output_format == OutputFormat::jsonl) {
            print_jsonl_result(position_case, mode, variant, depth, timed_result);
          } else {
            print_delimited_result(position_case, mode, variant, depth, timed_result,
                                   config->delimiter);
          }
        }
      }
    }
  }

  return 0;
}
