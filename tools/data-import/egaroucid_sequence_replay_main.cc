#include "egaroucid_sequence_replay.h"

#include <iostream>
#include <string>
#include <string_view>

namespace {

std::string_view trim_trailing_cr(std::string_view text) noexcept {
  if (!text.empty() && text.back() == '\r') {
    text.remove_suffix(1);
  }
  return text;
}

void write_result(std::string_view occurrence_id,
                  const vibe_othello::tools::data_import::EgaroucidReplayResult& result) {
  std::cout << "begin\t" << occurrence_id << '\n';
  if (!result.accepted) {
    std::cout << "error\t" << result.error << '\n';
    std::cout << "end\n";
    std::cout.flush();
    return;
  }

  std::cout << "ok\t" << result.pass_count << '\t' << (result.terminal ? 1 : 0) << '\t'
            << result.canonical_moves << '\n';
  for (const auto& snapshot : result.snapshots) {
    std::cout << "row\t" << snapshot.ply << '\t' << snapshot.board_a1_to_h8 << '\t'
              << snapshot.label_score_side_to_move << '\t' << snapshot.player_disc_count << '\t'
              << snapshot.opponent_disc_count << '\t' << snapshot.empty_count << '\n';
  }
  std::cout << "end\n";
  std::cout.flush();
}

} // namespace

int main() {
  namespace importer = vibe_othello::tools::data_import;

  std::string line;
  while (std::getline(std::cin, line)) {
    const std::string_view trimmed = trim_trailing_cr(line);
    const std::size_t tab = trimmed.find('\t');
    if (tab == std::string_view::npos) {
      write_result("", importer::EgaroucidReplayResult{
                           .accepted = false,
                           .error = "request is missing occurrence id",
                       });
      continue;
    }

    const std::string_view occurrence_id = trimmed.substr(0, tab);
    const std::string_view transcript = trimmed.substr(tab + 1);
    write_result(occurrence_id, importer::replay_egaroucid_sequence(transcript));
  }

  return 0;
}
