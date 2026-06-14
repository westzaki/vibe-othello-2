#include "search/reference_endgame.h"
#include "vibe_othello/board_core/board.h"
#include "vibe_othello/board_core/serialization.h"
#include "vibe_othello/search/search.h"

#include <bit>
#include <charconv>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef VIBE_OTHELLO_SOURCE_DIR
#error "VIBE_OTHELLO_SOURCE_DIR must be defined for endgame corpus tests"
#endif

namespace vibe_othello::search {
namespace {

class CountingEvaluator final : public Evaluator {
public:
  Score evaluate(const board_core::Position&) const noexcept override {
    ++calls;
    return 0;
  }

  mutable int calls = 0;
};

struct EndgameCorpusCase {
  std::string id;
  std::string position;
  std::uint8_t expected_empties;
};

std::uint8_t empty_count(board_core::Position position) noexcept {
  return static_cast<std::uint8_t>(std::popcount(~board_core::occupied(position)));
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

std::optional<int> parse_int(std::string_view text) noexcept {
  int value = 0;
  const char* begin = text.data();
  const char* end = text.data() + text.size();
  const std::from_chars_result result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end) {
    return std::nullopt;
  }
  return value;
}

std::vector<EndgameCorpusCase> load_endgame_corpus() {
  const std::string path =
      std::string{VIBE_OTHELLO_SOURCE_DIR} + "/engine/testdata/endgame/positions.tsv";
  std::ifstream input{path};
  REQUIRE(input.is_open());

  std::vector<EndgameCorpusCase> cases;
  std::string line;
  bool saw_header = false;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }

    const std::vector<std::string_view> fields = split_tabs(line);
    if (!saw_header) {
      REQUIRE(fields.size() == 5);
      REQUIRE(fields[0] == "id");
      REQUIRE(fields[1] == "category");
      REQUIRE(fields[2] == "position");
      REQUIRE(fields[3] == "expected_empties");
      REQUIRE(fields[4] == "notes");
      saw_header = true;
      continue;
    }

    REQUIRE(fields.size() == 5);
    const std::optional<int> expected_empties = parse_int(fields[3]);
    REQUIRE(expected_empties.has_value());
    REQUIRE(*expected_empties >= 0);
    REQUIRE(*expected_empties <= board_core::kSquareCount);
    cases.push_back(EndgameCorpusCase{
        .id = std::string{fields[0]},
        .position = std::string{fields[2]},
        .expected_empties = static_cast<std::uint8_t>(*expected_empties),
    });
  }

  REQUIRE(saw_header);
  REQUIRE_FALSE(cases.empty());
  return cases;
}

board_core::Position parse_position_or_fail(std::string_view text) {
  const std::optional<board_core::Position> position = board_core::parse_position(text);
  REQUIRE(position.has_value());
  return *position;
}

SearchResult production_exact_endgame(board_core::Position position) {
  CountingEvaluator evaluator;
  const std::uint8_t empties = empty_count(position);
  const SearchResult result =
      search_iterative(position, evaluator, SearchLimits{.max_depth = Depth{0}},
                       SearchOptions{.exact_endgame = true, .endgame_exact_empties = empties});
  REQUIRE(evaluator.calls == 0);
  return result;
}

Score root_score_for_move(const SearchResult& result, board_core::Move move) {
  for (const RootMoveInfo& root_move : result.root_moves) {
    if (root_move.move == move) {
      return root_move.score;
    }
  }
  FAIL("root move was missing from comparison result");
  return 0;
}

void require_matches_reference(const EndgameCorpusCase& corpus_case) {
  INFO("position_id: " << corpus_case.id);
  const board_core::Position position = parse_position_or_fail(corpus_case.position);
  REQUIRE(empty_count(position) == corpus_case.expected_empties);

  const SearchResult reference = test_support::reference_exact_endgame(position);
  const SearchResult production = production_exact_endgame(position);

  REQUIRE(production.score == reference.score);
  REQUIRE(production.completed_depth == reference.completed_depth);
  REQUIRE(production.root_moves.size() == reference.root_moves.size());
  REQUIRE(production.best_move.has_value() == reference.best_move.has_value());
  if (production.best_move.has_value()) {
    REQUIRE(root_score_for_move(reference, *production.best_move) == reference.score);
  }
  for (const RootMoveInfo& root_move : production.root_moves) {
    REQUIRE(root_score_for_move(reference, root_move.move) == root_move.score);
  }
}

TEST_CASE("checked-in exact endgame corpus matches reference solver",
          "[search][endgame][corpus]") {
  for (const EndgameCorpusCase& corpus_case : load_endgame_corpus()) {
    require_matches_reference(corpus_case);
  }
}

} // namespace
} // namespace vibe_othello::search
