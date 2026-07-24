#ifndef VIBE_OTHELLO_TOOLS_ARENA_NBOARD_SESSION_H_
#define VIBE_OTHELLO_TOOLS_ARENA_NBOARD_SESSION_H_

#include "persistent_process.h"
#include "vibe_othello/board_core/board.h"

#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vibe_othello::tools::arena {

struct NBoardSessionOptions {
  PersistentProcessOptions process;
  int protocol_version = 2;
  std::chrono::milliseconds startup_timeout{5000};
  std::chrono::milliseconds command_timeout{5000};
};

struct NBoardGoResult {
  board_core::Move move;
  std::string response_line;
  std::vector<std::string> output_lines;
};

class NBoardSession {
public:
  NBoardSession() = default;
  ~NBoardSession();

  NBoardSession(const NBoardSession&) = delete;
  NBoardSession& operator=(const NBoardSession&) = delete;
  NBoardSession(NBoardSession&&) = delete;
  NBoardSession& operator=(NBoardSession&&) = delete;

  [[nodiscard]] bool start(const NBoardSessionOptions& options, std::string* error);
  [[nodiscard]] bool set_depth(int depth, std::string* error);
  [[nodiscard]] bool set_game(std::span<const board_core::Move> moves, std::string* error);
  [[nodiscard]] bool play_move(board_core::Move move, std::string* error);
  [[nodiscard]] std::optional<NBoardGoResult> go(std::chrono::milliseconds timeout,
                                                 std::string* error);
  [[nodiscard]] bool synchronize(std::chrono::milliseconds timeout, std::string* error);

  [[nodiscard]] bool started() const noexcept {
    return process_.started();
  }
  [[nodiscard]] int process_id() const noexcept {
    return process_.process_id();
  }
  void stop() noexcept;

private:
  [[nodiscard]] bool write_and_synchronize(std::string command, std::string* error);

  PersistentProcess process_;
  std::chrono::milliseconds command_timeout_{5000};
  unsigned int ping_id_ = 0;
};

} // namespace vibe_othello::tools::arena

#endif // VIBE_OTHELLO_TOOLS_ARENA_NBOARD_SESSION_H_
