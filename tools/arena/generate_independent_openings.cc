#include "arena_core.h"
#include "vibe_othello/board_core/board.h"

#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

namespace arena = vibe_othello::tools::arena;
namespace board = vibe_othello::board_core;

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

struct Args {
  std::filesystem::path output;
  std::filesystem::path report_out;
  std::uint64_t count = 0;
  int plies = 0;
  std::uint64_t seed = 0;
  std::uint64_t max_attempts = 0;
};

template <typename Integer> std::optional<Integer> parse_integer(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }
  Integer value = 0;
  for (const char character : text) {
    if (character < '0' || character > '9') {
      return std::nullopt;
    }
    const Integer digit = static_cast<Integer>(character - '0');
    if (value > (std::numeric_limits<Integer>::max() - digit) / 10) {
      return std::nullopt;
    }
    value = value * 10 + digit;
  }
  return value;
}

std::optional<Args> parse_args(int argc, char** argv) {
  Args args;
  bool has_output = false;
  bool has_report = false;
  bool has_count = false;
  bool has_plies = false;
  bool has_seed = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view option{argv[index]};
    if (option == "--help") {
      std::cout << "usage: vibe-othello-generate-independent-openings\n"
                   "  --output PATH --report-out PATH --count N --plies N --seed N\n"
                   "  [--max-attempts N]\n";
      std::exit(0);
    }
    if (index + 1 >= argc) {
      std::cerr << option << " requires a value\n";
      return std::nullopt;
    }
    const std::string_view value{argv[++index]};
    if (option == "--output") {
      args.output = value;
      has_output = true;
    } else if (option == "--report-out") {
      args.report_out = value;
      has_report = true;
    } else if (option == "--count") {
      const auto parsed = parse_integer<std::uint64_t>(value);
      if (!parsed.has_value() || *parsed == 0) {
        std::cerr << "--count must be positive\n";
        return std::nullopt;
      }
      args.count = *parsed;
      has_count = true;
    } else if (option == "--plies") {
      const auto parsed = parse_integer<int>(value);
      if (!parsed.has_value() || *parsed < 0 || *parsed > 60) {
        std::cerr << "--plies must be in [0, 60]\n";
        return std::nullopt;
      }
      args.plies = *parsed;
      has_plies = true;
    } else if (option == "--seed") {
      const auto parsed = parse_integer<std::uint64_t>(value);
      if (!parsed.has_value()) {
        std::cerr << "--seed must be an unsigned integer\n";
        return std::nullopt;
      }
      args.seed = *parsed;
      has_seed = true;
    } else if (option == "--max-attempts") {
      const auto parsed = parse_integer<std::uint64_t>(value);
      if (!parsed.has_value() || *parsed == 0) {
        std::cerr << "--max-attempts must be positive\n";
        return std::nullopt;
      }
      args.max_attempts = *parsed;
    } else {
      std::cerr << "unknown option: " << option << '\n';
      return std::nullopt;
    }
  }
  if (!has_output || !has_report || !has_count || !has_plies || !has_seed) {
    std::cerr << "--output, --report-out, --count, --plies, and --seed are required\n";
    return std::nullopt;
  }
  if (args.max_attempts == 0) {
    if (args.count > std::numeric_limits<std::uint64_t>::max() / 100) {
      std::cerr << "--count is too large\n";
      return std::nullopt;
    }
    args.max_attempts = args.count * 100;
  }
  if (args.max_attempts < args.count) {
    std::cerr << "--max-attempts must be at least --count\n";
    return std::nullopt;
  }
  return args;
}

class SplitMix64 {
public:
  explicit SplitMix64(std::uint64_t seed) : state_(seed) {}

  std::uint64_t bounded(std::uint64_t bound) {
    const std::uint64_t threshold = (std::uint64_t{0} - bound) % bound;
    while (true) {
      const std::uint64_t value = next();
      if (value >= threshold) {
        return value % bound;
      }
    }
  }

private:
  std::uint64_t next() {
    std::uint64_t value = (state_ += 0x9e3779b97f4a7c15ULL);
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
  }

  std::uint64_t state_;
};

std::uint64_t fnv1a64(std::string_view text) {
  std::uint64_t digest = kFnvOffset;
  for (const unsigned char value : text) {
    digest ^= value;
    digest *= kFnvPrime;
  }
  return digest;
}

std::string checksum(std::string_view text) {
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << fnv1a64(text);
  return output.str();
}

std::optional<std::vector<board::Move>> generate_opening(int plies, SplitMix64* random,
                                                         board::Position* final_position) {
  board::Position position = board::initial_position();
  std::vector<board::Move> moves;
  moves.reserve(static_cast<std::size_t>(plies) + 2);
  int normal_ply = 0;
  while (normal_ply < plies) {
    board::Bitboard legal = board::legal_moves(position);
    if (legal == 0) {
      board::MoveDelta pass_delta{};
      if (!board::apply_pass(&position, &pass_delta)) {
        return std::nullopt;
      }
      moves.push_back(board::make_pass());
      legal = board::legal_moves(position);
      if (legal == 0) {
        return std::nullopt;
      }
    }

    const int legal_count = std::popcount(legal);
    std::uint64_t selected = random->bounded(static_cast<std::uint64_t>(legal_count));
    while (selected != 0) {
      legal &= legal - 1;
      --selected;
    }
    const board::Square square = board::square_from_index(std::countr_zero(legal));
    const board::Move move = board::make_move(square);
    board::MoveDelta delta{};
    if (!board::apply_move(&position, move, &delta)) {
      return std::nullopt;
    }
    moves.push_back(move);
    ++normal_ply;
  }
  *final_position = position;
  return moves;
}

int run(const Args& args) {
  SplitMix64 random(args.seed);
  std::set<std::tuple<board::Bitboard, board::Bitboard, int>> final_positions;
  std::vector<std::vector<board::Move>> openings;
  openings.reserve(static_cast<std::size_t>(args.count));
  std::uint64_t attempts = 0;
  std::uint64_t terminal_rejections = 0;
  std::uint64_t duplicate_rejections = 0;
  while (openings.size() < args.count && attempts < args.max_attempts) {
    ++attempts;
    board::Position position{};
    const auto moves = generate_opening(args.plies, &random, &position);
    if (!moves.has_value()) {
      ++terminal_rejections;
      continue;
    }
    const auto identity = std::make_tuple(position.player, position.opponent,
                                          position.side_to_move == board::Color::black ? 0 : 1);
    if (!final_positions.insert(identity).second) {
      ++duplicate_rejections;
      continue;
    }
    openings.push_back(*moves);
  }
  if (openings.size() != args.count) {
    std::cerr << "generated " << openings.size() << " unique openings after " << attempts
              << " attempts; requested " << args.count << '\n';
    return 1;
  }

  std::ostringstream opening_text;
  for (std::size_t index = 0; index < openings.size(); ++index) {
    opening_text << "independent-random-" << std::setfill('0') << std::setw(6) << index + 1 << ": "
                 << arena::format_moves(openings[index]) << '\n';
  }
  if (!args.output.parent_path().empty()) {
    std::filesystem::create_directories(args.output.parent_path());
  }
  if (!args.report_out.parent_path().empty()) {
    std::filesystem::create_directories(args.report_out.parent_path());
  }
  {
    std::ofstream output(args.output, std::ios::binary);
    if (!output) {
      std::cerr << "failed to open " << args.output << '\n';
      return 1;
    }
    output << opening_text.str();
  }

  const std::string opening_checksum = checksum(opening_text.str());
  std::ofstream report(args.report_out);
  if (!report) {
    std::cerr << "failed to open " << args.report_out << '\n';
    return 1;
  }
  report << "{\n"
         << "  \"schema_version\": 1,\n"
         << "  \"generator_version\": \"board-core-uniform-legal-v1\",\n"
         << "  \"opening_source\": \"independent-board-core-random\",\n"
         << "  \"opening_generation_seed\": " << args.seed << ",\n"
         << "  \"opening_plies\": " << args.plies << ",\n"
         << "  \"opening_pairs\": " << openings.size() << ",\n"
         << "  \"attempts\": " << attempts << ",\n"
         << "  \"duplicate_final_board_rejections\": " << duplicate_rejections << ",\n"
         << "  \"early_terminal_rejections\": " << terminal_rejections << ",\n"
         << "  \"opening_checksum\": \"" << opening_checksum << "\"\n"
         << "}\n";
  std::cout << "opening_pairs=" << openings.size() << " checksum=" << opening_checksum << '\n';
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  const auto args = parse_args(argc, argv);
  if (!args.has_value()) {
    return 2;
  }
  return run(*args);
}
