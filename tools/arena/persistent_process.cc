#include "persistent_process.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace vibe_othello::tools::arena {
namespace {

constexpr std::size_t kMaximumBufferedLineBytes = 1024U * 1024U;

void close_fd(int* fd) noexcept {
  if (*fd >= 0) {
    close(*fd);
    *fd = -1;
  }
}

void child_fail(int error_fd, int error_number) noexcept {
  const auto* data = reinterpret_cast<const char*>(&error_number);
  std::size_t written = 0;
  while (written < sizeof(error_number)) {
    const ssize_t result = write(error_fd, data + written, sizeof(error_number) - written);
    if (result > 0) {
      written += static_cast<std::size_t>(result);
    } else if (result < 0 && errno == EINTR) {
      continue;
    } else {
      break;
    }
  }
  _exit(127);
}

bool wait_for_exit(pid_t process_id, std::chrono::milliseconds timeout) noexcept {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (true) {
    int status = 0;
    const pid_t result = waitpid(process_id, &status, WNOHANG);
    if (result == process_id || (result < 0 && errno == ECHILD)) {
      return true;
    }
    if (result < 0 && errno != EINTR) {
      return false;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

void ignore_sigpipe() {
  static std::once_flag flag;
  std::call_once(flag, [] {
    struct sigaction action {};
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    sigaction(SIGPIPE, &action, nullptr);
  });
}

std::string take_buffered_line(std::string* buffer) {
  const std::size_t newline = buffer->find('\n');
  if (newline == std::string::npos) {
    return {};
  }
  std::string line = buffer->substr(0, newline);
  buffer->erase(0, newline + 1);
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  return line;
}

} // namespace

PersistentProcess::~PersistentProcess() {
  stop();
}

bool PersistentProcess::start(const PersistentProcessOptions& options, std::string* error) {
  if (started()) {
    *error = "process is already started";
    return false;
  }
  if (options.argv.empty() || options.argv.front().empty()) {
    *error = "process argv must contain an executable";
    return false;
  }
  for (const std::string& argument : options.argv) {
    if (argument.find('\0') != std::string::npos) {
      *error = "process arguments must not contain NUL bytes";
      return false;
    }
  }
  for (const auto& [name, value] : options.environment) {
    if (name.empty() || name.find('=') != std::string::npos ||
        name.find('\0') != std::string::npos || value.find('\0') != std::string::npos) {
      *error = "process environment contains an invalid name or NUL byte";
      return false;
    }
  }

  int input_pipe[2] = {-1, -1};
  int output_pipe[2] = {-1, -1};
  int error_pipe[2] = {-1, -1};
  if (pipe(input_pipe) != 0 || pipe(output_pipe) != 0 || pipe(error_pipe) != 0) {
    const int error_number = errno;
    close_fd(&input_pipe[0]);
    close_fd(&input_pipe[1]);
    close_fd(&output_pipe[0]);
    close_fd(&output_pipe[1]);
    close_fd(&error_pipe[0]);
    close_fd(&error_pipe[1]);
    *error = "failed to create process pipes: " + std::string{std::strerror(error_number)};
    return false;
  }
  if (fcntl(error_pipe[1], F_SETFD, FD_CLOEXEC) != 0) {
    const int error_number = errno;
    close_fd(&input_pipe[0]);
    close_fd(&input_pipe[1]);
    close_fd(&output_pipe[0]);
    close_fd(&output_pipe[1]);
    close_fd(&error_pipe[0]);
    close_fd(&error_pipe[1]);
    *error = "failed to configure process error pipe: " + std::string{std::strerror(error_number)};
    return false;
  }

  std::vector<char*> child_argv;
  child_argv.reserve(options.argv.size() + 1);
  for (const std::string& argument : options.argv) {
    child_argv.push_back(const_cast<char*>(argument.c_str()));
  }
  child_argv.push_back(nullptr);

  const pid_t process_id = fork();
  if (process_id < 0) {
    const int error_number = errno;
    close_fd(&input_pipe[0]);
    close_fd(&input_pipe[1]);
    close_fd(&output_pipe[0]);
    close_fd(&output_pipe[1]);
    close_fd(&error_pipe[0]);
    close_fd(&error_pipe[1]);
    *error = "failed to fork process: " + std::string{std::strerror(error_number)};
    return false;
  }

  if (process_id == 0) {
    close(input_pipe[1]);
    close(output_pipe[0]);
    close(error_pipe[0]);
    setpgid(0, 0);

    if (!options.working_directory.empty() && chdir(options.working_directory.c_str()) != 0) {
      child_fail(error_pipe[1], errno);
    }
    for (const auto& [name, value] : options.environment) {
      if (setenv(name.c_str(), value.c_str(), 1) != 0) {
        child_fail(error_pipe[1], errno);
      }
    }

    if (dup2(input_pipe[0], STDIN_FILENO) < 0 || dup2(output_pipe[1], STDOUT_FILENO) < 0) {
      child_fail(error_pipe[1], errno);
    }
    int stderr_fd = -1;
    if (!options.stderr_path.empty()) {
      stderr_fd = open(options.stderr_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    } else {
      stderr_fd = open("/dev/null", O_WRONLY);
    }
    if (stderr_fd < 0 || dup2(stderr_fd, STDERR_FILENO) < 0) {
      child_fail(error_pipe[1], errno);
    }

    close(input_pipe[0]);
    close(output_pipe[1]);
    if (stderr_fd != STDERR_FILENO) {
      close(stderr_fd);
    }
    execvp(child_argv.front(), child_argv.data());
    child_fail(error_pipe[1], errno);
  }

  close(input_pipe[0]);
  close(output_pipe[1]);
  close(error_pipe[1]);
  setpgid(process_id, process_id);

  int child_error = 0;
  ssize_t error_bytes = 0;
  do {
    error_bytes = read(error_pipe[0], &child_error, sizeof(child_error));
  } while (error_bytes < 0 && errno == EINTR);
  close(error_pipe[0]);
  if (error_bytes > 0) {
    close(input_pipe[1]);
    close(output_pipe[0]);
    waitpid(process_id, nullptr, 0);
    *error = "failed to start child process: " + std::string{std::strerror(child_error)};
    return false;
  }
  if (error_bytes < 0) {
    const int error_number = errno;
    close(input_pipe[1]);
    close(output_pipe[0]);
    kill(-process_id, SIGKILL);
    waitpid(process_id, nullptr, 0);
    *error = "failed to read child startup status: " + std::string{std::strerror(error_number)};
    return false;
  }

  const int output_flags = fcntl(output_pipe[0], F_GETFL, 0);
  if (output_flags < 0 || fcntl(output_pipe[0], F_SETFL, output_flags | O_NONBLOCK) != 0) {
    const int error_number = errno;
    close(input_pipe[1]);
    close(output_pipe[0]);
    kill(-process_id, SIGKILL);
    waitpid(process_id, nullptr, 0);
    *error = "failed to configure child output: " + std::string{std::strerror(error_number)};
    return false;
  }

  ignore_sigpipe();
  process_id_ = static_cast<int>(process_id);
  input_fd_ = input_pipe[1];
  output_fd_ = output_pipe[0];
  read_buffer_.clear();
  return true;
}

bool PersistentProcess::write_line(std::string_view line, std::string* error) {
  if (!started() || input_fd_ < 0) {
    *error = "process input is closed";
    return false;
  }
  if (line.find('\n') != std::string_view::npos || line.find('\r') != std::string_view::npos) {
    *error = "process command must contain exactly one line";
    return false;
  }

  std::string command{line};
  command.push_back('\n');
  std::size_t offset = 0;
  while (offset < command.size()) {
    const ssize_t written = write(input_fd_, command.data() + offset, command.size() - offset);
    if (written > 0) {
      offset += static_cast<std::size_t>(written);
    } else if (written < 0 && errno == EINTR) {
      continue;
    } else {
      *error = "failed to write child input: " + std::string{std::strerror(errno)};
      return false;
    }
  }
  return true;
}

ProcessReadResult PersistentProcess::read_line(std::chrono::milliseconds timeout) {
  if (const std::size_t newline = read_buffer_.find('\n'); newline != std::string::npos) {
    return ProcessReadResult{
        .status = ProcessReadStatus::line,
        .line = take_buffered_line(&read_buffer_),
    };
  }
  if (output_fd_ < 0) {
    if (!read_buffer_.empty()) {
      std::string final_line = std::move(read_buffer_);
      read_buffer_.clear();
      if (!final_line.empty() && final_line.back() == '\r') {
        final_line.pop_back();
      }
      return ProcessReadResult{.status = ProcessReadStatus::line, .line = std::move(final_line)};
    }
    return ProcessReadResult{.status = ProcessReadStatus::closed};
  }

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (true) {
    const auto now = std::chrono::steady_clock::now();
    const auto remaining =
        now >= deadline ? std::chrono::milliseconds{0}
                        : std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    pollfd descriptor{
        .fd = output_fd_,
        .events = POLLIN,
        .revents = 0,
    };
    const int poll_result = poll(&descriptor, 1, static_cast<int>(remaining.count()));
    if (poll_result == 0) {
      return ProcessReadResult{.status = ProcessReadStatus::timeout};
    }
    if (poll_result < 0) {
      if (errno == EINTR) {
        continue;
      }
      return ProcessReadResult{
          .status = ProcessReadStatus::io_error,
          .error = "failed to poll child output: " + std::string{std::strerror(errno)},
      };
    }

    bool reached_eof = false;
    while (true) {
      char buffer[4096];
      const ssize_t bytes = read(output_fd_, buffer, sizeof(buffer));
      if (bytes > 0) {
        read_buffer_.append(buffer, static_cast<std::size_t>(bytes));
        if (read_buffer_.size() > kMaximumBufferedLineBytes) {
          return ProcessReadResult{
              .status = ProcessReadStatus::io_error,
              .error = "child output line exceeded the 1 MiB limit",
          };
        }
      } else if (bytes == 0) {
        reached_eof = true;
        close_fd(&output_fd_);
        break;
      } else if (errno == EINTR) {
        continue;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else {
        return ProcessReadResult{
            .status = ProcessReadStatus::io_error,
            .error = "failed to read child output: " + std::string{std::strerror(errno)},
        };
      }
    }

    if (read_buffer_.find('\n') != std::string::npos) {
      return ProcessReadResult{
          .status = ProcessReadStatus::line,
          .line = take_buffered_line(&read_buffer_),
      };
    }
    if (reached_eof) {
      if (!read_buffer_.empty()) {
        std::string final_line = std::move(read_buffer_);
        read_buffer_.clear();
        if (!final_line.empty() && final_line.back() == '\r') {
          final_line.pop_back();
        }
        return ProcessReadResult{.status = ProcessReadStatus::line, .line = std::move(final_line)};
      }
      return ProcessReadResult{.status = ProcessReadStatus::closed};
    }
  }
}

void PersistentProcess::stop() noexcept {
  close_fd(&input_fd_);
  close_fd(&output_fd_);
  read_buffer_.clear();
  if (process_id_ <= 0) {
    process_id_ = -1;
    return;
  }

  const pid_t process_id = static_cast<pid_t>(process_id_);
  if (!wait_for_exit(process_id, std::chrono::milliseconds(50))) {
    kill(-process_id, SIGTERM);
    if (!wait_for_exit(process_id, std::chrono::milliseconds(100))) {
      kill(-process_id, SIGKILL);
      while (waitpid(process_id, nullptr, 0) < 0 && errno == EINTR) {
      }
    }
  }
  process_id_ = -1;
}

} // namespace vibe_othello::tools::arena
