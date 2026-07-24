#include "nboard_session.h"

#include "nboard_protocol.h"

#include <chrono>
#include <limits>
#include <string>
#include <utility>

namespace vibe_othello::tools::arena {
namespace {

constexpr std::size_t kMaximumResponseLines = 10000;

std::chrono::milliseconds remaining_time(std::chrono::steady_clock::time_point deadline) {
  const auto now = std::chrono::steady_clock::now();
  if (now >= deadline) {
    return std::chrono::milliseconds{0};
  }
  return std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
}

} // namespace

NBoardSession::~NBoardSession() {
  stop();
}

bool NBoardSession::start(const NBoardSessionOptions& options, std::string* error) {
  if (options.protocol_version <= 0) {
    *error = "NBoard protocol version must be positive";
    return false;
  }
  if (options.startup_timeout <= std::chrono::milliseconds{0} ||
      options.command_timeout <= std::chrono::milliseconds{0}) {
    *error = "NBoard timeouts must be positive";
    return false;
  }
  if (!process_.start(options.process, error)) {
    return false;
  }
  command_timeout_ = options.command_timeout;
  ping_id_ = 0;
  if (!process_.write_line("nboard " + std::to_string(options.protocol_version), error) ||
      !synchronize(options.startup_timeout, error)) {
    stop();
    return false;
  }
  return true;
}

bool NBoardSession::set_depth(int depth, std::string* error) {
  if (depth <= 0) {
    *error = "NBoard depth must be positive";
    return false;
  }
  return write_and_synchronize("set depth " + std::to_string(depth), error);
}

bool NBoardSession::set_game(std::span<const board_core::Move> moves, std::string* error) {
  const std::optional<std::string> ggf = format_nboard_ggf(moves, error);
  if (!ggf.has_value()) {
    return false;
  }
  return write_and_synchronize("set game " + *ggf, error);
}

bool NBoardSession::play_move(board_core::Move move, std::string* error) {
  return write_and_synchronize("move " + format_nboard_move(move), error);
}

std::optional<NBoardGoResult> NBoardSession::go(std::chrono::milliseconds timeout,
                                                std::string* error) {
  if (timeout <= std::chrono::milliseconds{0}) {
    *error = "NBoard go timeout must be positive";
    return std::nullopt;
  }
  if (!process_.write_line("go", error)) {
    return std::nullopt;
  }

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::vector<std::string> output_lines;
  output_lines.reserve(16);
  while (output_lines.size() < kMaximumResponseLines) {
    const ProcessReadResult read = process_.read_line(remaining_time(deadline));
    if (read.status == ProcessReadStatus::timeout) {
      *error = "timed out waiting for NBoard move response";
      return std::nullopt;
    }
    if (read.status == ProcessReadStatus::closed) {
      *error = "NBoard engine closed output while waiting for a move";
      return std::nullopt;
    }
    if (read.status == ProcessReadStatus::io_error) {
      *error = read.error;
      return std::nullopt;
    }
    output_lines.push_back(read.line);
    const std::optional<board_core::Move> move = parse_nboard_move_response(read.line);
    if (move.has_value()) {
      return NBoardGoResult{
          .move = *move,
          .response_line = read.line,
          .output_lines = std::move(output_lines),
      };
    }
  }
  *error = "NBoard move response exceeded the line limit";
  return std::nullopt;
}

bool NBoardSession::synchronize(std::chrono::milliseconds timeout, std::string* error) {
  if (!process_.started()) {
    *error = "NBoard process is not started";
    return false;
  }
  if (ping_id_ == std::numeric_limits<unsigned int>::max()) {
    ping_id_ = 0;
  }
  const std::string id = std::to_string(++ping_id_);
  if (!process_.write_line("ping " + id, error)) {
    return false;
  }

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  for (std::size_t line_count = 0; line_count < kMaximumResponseLines; ++line_count) {
    const ProcessReadResult read = process_.read_line(remaining_time(deadline));
    if (read.status == ProcessReadStatus::timeout) {
      *error = "timed out waiting for NBoard pong " + id;
      return false;
    }
    if (read.status == ProcessReadStatus::closed) {
      *error = "NBoard engine closed output while waiting for pong " + id;
      return false;
    }
    if (read.status == ProcessReadStatus::io_error) {
      *error = read.error;
      return false;
    }
    if (read.line == "pong " + id) {
      return true;
    }
  }
  *error = "NBoard synchronization exceeded the line limit";
  return false;
}

void NBoardSession::stop() noexcept {
  if (process_.started()) {
    std::string ignored_error;
    static_cast<void>(process_.write_line("quit", &ignored_error));
  }
  process_.stop();
}

bool NBoardSession::write_and_synchronize(std::string command, std::string* error) {
  return process_.write_line(command, error) && synchronize(command_timeout_, error);
}

} // namespace vibe_othello::tools::arena
