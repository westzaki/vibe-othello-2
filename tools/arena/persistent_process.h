#ifndef VIBE_OTHELLO_TOOLS_ARENA_PERSISTENT_PROCESS_H_
#define VIBE_OTHELLO_TOOLS_ARENA_PERSISTENT_PROCESS_H_

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vibe_othello::tools::arena {

struct PersistentProcessOptions {
  std::vector<std::string> argv;
  std::filesystem::path working_directory;
  std::filesystem::path stderr_path;
  std::vector<std::pair<std::string, std::string>> environment;
};

enum class ProcessReadStatus {
  line,
  timeout,
  closed,
  io_error,
};

struct ProcessReadResult {
  ProcessReadStatus status = ProcessReadStatus::io_error;
  std::string line;
  std::string error;
};

class PersistentProcess {
public:
  PersistentProcess() = default;
  ~PersistentProcess();

  PersistentProcess(const PersistentProcess&) = delete;
  PersistentProcess& operator=(const PersistentProcess&) = delete;
  PersistentProcess(PersistentProcess&&) = delete;
  PersistentProcess& operator=(PersistentProcess&&) = delete;

  [[nodiscard]] bool start(const PersistentProcessOptions& options, std::string* error);
  [[nodiscard]] bool write_line(std::string_view line, std::string* error);
  [[nodiscard]] ProcessReadResult read_line(std::chrono::milliseconds timeout);
  [[nodiscard]] bool started() const noexcept {
    return process_id_ > 0;
  }
  [[nodiscard]] int process_id() const noexcept {
    return process_id_;
  }

  void stop() noexcept;

private:
  int process_id_ = -1;
  int input_fd_ = -1;
  int output_fd_ = -1;
  std::string read_buffer_;
};

} // namespace vibe_othello::tools::arena

#endif // VIBE_OTHELLO_TOOLS_ARENA_PERSISTENT_PROCESS_H_
