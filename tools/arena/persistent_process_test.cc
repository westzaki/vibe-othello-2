#include "persistent_process.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>

#ifndef VIBE_OTHELLO_FAKE_NBOARD_ENGINE
#define VIBE_OTHELLO_FAKE_NBOARD_ENGINE ""
#endif

namespace vibe_othello::tools::arena {
namespace {

using namespace std::chrono_literals;

TEST_CASE("persistent process reuses one child for multiple commands", "[arena][process]") {
  PersistentProcess process;
  const PersistentProcessOptions options{
      .argv = {VIBE_OTHELLO_FAKE_NBOARD_ENGINE, "--echo"},
  };
  std::string error;

  REQUIRE(process.start(options, &error));
  const int process_id = process.process_id();
  REQUIRE(process_id > 0);

  REQUIRE(process.write_line("first", &error));
  ProcessReadResult first = process.read_line(1s);
  REQUIRE(first.status == ProcessReadStatus::line);
  REQUIRE(first.line == "first");

  REQUIRE(process.write_line("second", &error));
  ProcessReadResult second = process.read_line(1s);
  REQUIRE(second.status == ProcessReadStatus::line);
  REQUIRE(second.line == "second");
  REQUIRE(process.process_id() == process_id);

  const ProcessReadResult idle = process.read_line(10ms);
  REQUIRE(idle.status == ProcessReadStatus::timeout);
  process.stop();
  REQUIRE_FALSE(process.started());
}

TEST_CASE("persistent process reports startup errors", "[arena][process]") {
  PersistentProcess process;
  const PersistentProcessOptions options{
      .argv = {"/definitely/not/a/vibe-othello-executable"},
  };
  std::string error;

  REQUIRE_FALSE(process.start(options, &error));
  REQUIRE_FALSE(error.empty());
  REQUIRE_FALSE(process.started());
}

TEST_CASE("persistent process applies working directory and environment", "[arena][process]") {
  PersistentProcess process;
  const std::filesystem::path working_directory = std::filesystem::temp_directory_path();
  const PersistentProcessOptions options{
      .argv = {VIBE_OTHELLO_FAKE_NBOARD_ENGINE, "--echo", "--report-runtime",
               "VIBE_OTHELLO_FAKE_VALUE"},
      .working_directory = working_directory,
      .environment = {{"VIBE_OTHELLO_FAKE_VALUE", "configured"}},
  };
  std::string error;

  REQUIRE(process.start(options, &error));
  const ProcessReadResult cwd = process.read_line(1s);
  const ProcessReadResult environment = process.read_line(1s);

  REQUIRE(cwd.status == ProcessReadStatus::line);
  REQUIRE(cwd.line.starts_with("cwd "));
  REQUIRE(std::filesystem::equivalent(cwd.line.substr(4), working_directory));
  REQUIRE(environment.status == ProcessReadStatus::line);
  REQUIRE(environment.line == "env configured");
}

TEST_CASE("persistent process rejects multiline commands", "[arena][process]") {
  PersistentProcess process;
  const PersistentProcessOptions options{
      .argv = {VIBE_OTHELLO_FAKE_NBOARD_ENGINE, "--echo"},
  };
  std::string error;

  REQUIRE(process.start(options, &error));
  REQUIRE_FALSE(process.write_line("first\nsecond", &error));
  REQUIRE_FALSE(error.empty());
}

} // namespace
} // namespace vibe_othello::tools::arena
