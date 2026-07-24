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
  for (const NBoardProtocolVersion protocol_version :
       {NBoardProtocolVersion::v1, NBoardProtocolVersion::v2}) {
    NBoardSession session;
    const NBoardSessionOptions options{
        .process =
            PersistentProcessOptions{
                .argv = {VIBE_OTHELLO_FAKE_NBOARD_ENGINE, "--expect-protocol",
                         std::to_string(static_cast<int>(protocol_version))},
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
        board_core::make_move(*parse_square("d3")),
        board_core::make_move(*parse_square("c3")),
    };
    REQUIRE(session.set_game(opening, &error));

    board_core::Position position{};
    REQUIRE(replay_moves(opening, &position, &error));

    const auto first = session.go(1s, &error);
    REQUIRE(first.has_value());
    REQUIRE(first->move.kind == board_core::MoveKind::normal);
    REQUIRE((board_core::legal_moves(position) & board_core::bit(first->move.square)) != 0);
    REQUIRE(first->output_lines.size() == 3);
    board_core::MoveDelta delta{};
    REQUIRE(board_core::apply_move(&position, first->move, &delta));
    REQUIRE(session.play_move(first->move, &error));

    REQUIRE(session.synchronize(1s, &error));
    const auto second = session.go(1s, &error);
    REQUIRE(second.has_value());
    REQUIRE(second->move.kind == board_core::MoveKind::normal);
    REQUIRE((board_core::legal_moves(position) & board_core::bit(second->move.square)) != 0);
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
      .protocol_version = NBoardProtocolVersion::v2,
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
