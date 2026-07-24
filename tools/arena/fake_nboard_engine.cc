#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct Args {
  bool echo = false;
  bool ignore_ping = false;
  std::optional<int> expected_protocol;
  std::optional<std::string> report_environment;
};

std::optional<int> parse_int(std::string_view text) {
  try {
    std::size_t parsed = 0;
    const int value = std::stoi(std::string{text}, &parsed);
    if (parsed != text.size()) {
      return std::nullopt;
    }
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<Args> parse_args(int argc, char** argv) {
  Args args;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--echo") {
      args.echo = true;
    } else if (argument == "--ignore-ping") {
      args.ignore_ping = true;
    } else if (argument == "--expect-protocol") {
      if (index + 1 >= argc) {
        return std::nullopt;
      }
      args.expected_protocol = parse_int(argv[++index]);
      if (!args.expected_protocol.has_value() || *args.expected_protocol <= 0) {
        return std::nullopt;
      }
    } else if (argument == "--report-runtime") {
      if (index + 1 >= argc) {
        return std::nullopt;
      }
      args.report_environment = argv[++index];
    } else {
      return std::nullopt;
    }
  }
  return args;
}

std::string_view value_after(std::string_view line, std::string_view prefix) {
  if (!line.starts_with(prefix)) {
    return {};
  }
  line.remove_prefix(prefix.size());
  return line;
}

} // namespace

int main(int argc, char** argv) {
  const std::optional<Args> args = parse_args(argc, argv);
  if (!args.has_value()) {
    return 2;
  }
  if (args->report_environment.has_value()) {
    const char* value = std::getenv(args->report_environment->c_str());
    std::cout << "cwd " << std::filesystem::current_path().string() << std::endl;
    std::cout << "env " << (value == nullptr ? "" : value) << std::endl;
  }

  std::string line;
  while (std::getline(std::cin, line)) {
    if (args->echo) {
      std::cout << line << std::endl;
      if (line == "quit") {
        return 0;
      }
      continue;
    }
    if (line == "quit") {
      return 0;
    }
    if (const std::string_view version = value_after(line, "nboard "); !version.empty()) {
      if (args->expected_protocol.has_value() && parse_int(version) != args->expected_protocol) {
        return 3;
      }
      std::cout << "set myname FakeNBoard" << std::endl;
      continue;
    }
    if (const std::string_view ping = value_after(line, "ping "); !ping.empty()) {
      if (!args->ignore_ping) {
        std::cout << "status synchronized" << std::endl;
        std::cout << "pong " << ping << std::endl;
      }
      continue;
    }
    if (line == "go") {
      std::cout << "status thinking" << std::endl;
      std::cout << "nodestats 123 0.01" << std::endl;
      std::cout << "=== D3/1.25/0.01" << std::endl;
      std::cout << "status waiting" << std::endl;
    }
  }
  return 0;
}
