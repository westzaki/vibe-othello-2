#include "arena_core.h"
#include "nboard_session.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>
#include <vector>

#ifndef VIBE_OTHELLO_FAKE_NBOARD_ENGINE
#define VIBE_OTHELLO_FAKE_NBOARD_ENGINE ""
#endif

namespace vibe_othello::tools::arena {
namespace {

using namespace std::chrono_literals;

TEST_CASE("NBoard session stays synchronized across multiple searches", "[arena][nboard]") {
  for (const int protocol_version : {1, 2}) {
    NBoardSession session;
    const NBoardSessionOptions options{
        .process =
            PersistentProcessOptions{
                .argv = {VIBE_OTHELLO_FAKE_NBOARD_ENGINE, "--expect-protocol",
                         std::to_string(protocol_version)},
            },
        .protocol_version = protocol_version,
        .startup_timeout = 1s,
        .command_timeout = 1s,
    };
    std::string error;

    REQUIRE(session.start(options, &error));
    const int process_id = session.process_id();
    REQUIRE(session.set_depth(4, &error));

    const std::vector<board_core::Move> opening{
        board_core::make_move(*parse_square("f5")),
        board_core::make_move(*parse_square("f6")),
    };
    REQUIRE(session.set_game(opening, &error));
    REQUIRE(session.play_move(board_core::make_move(*parse_square("d3")), &error));

    const auto first = session.go(1s, &error);
    REQUIRE(first.has_value());
    REQUIRE(first->move == board_core::make_move(*parse_square("d3")));
    REQUIRE(first->output_lines.size() == 3);

    REQUIRE(session.synchronize(1s, &error));
    const auto second = session.go(1s, &error);
    REQUIRE(second.has_value());
    REQUIRE(session.process_id() == process_id);
    session.stop();
  }
}

TEST_CASE("NBoard session fails a missing pong handshake", "[arena][nboard]") {
  NBoardSession session;
  const NBoardSessionOptions options{
      .process =
          PersistentProcessOptions{
              .argv = {VIBE_OTHELLO_FAKE_NBOARD_ENGINE, "--ignore-ping"},
          },
      .protocol_version = 2,
      .startup_timeout = 20ms,
      .command_timeout = 20ms,
  };
  std::string error;

  REQUIRE_FALSE(session.start(options, &error));
  REQUIRE(error.find("pong") != std::string::npos);
  REQUIRE_FALSE(session.started());
}

} // namespace
} // namespace vibe_othello::tools::arena
