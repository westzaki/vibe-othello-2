#ifndef VIBE_OTHELLO_TOOLS_DATA_IMPORT_SEQUENCE_REPLAY_CLI_H_
#define VIBE_OTHELLO_TOOLS_DATA_IMPORT_SEQUENCE_REPLAY_CLI_H_

#include "sequence_replay.h"

#include <istream>
#include <ostream>
#include <string>
#include <string_view>

namespace vibe_othello::tools::data_import {

using SequenceReplayFunction = SequenceReplayResult (*)(std::string_view);

inline void write_sequence_replay_result(std::ostream& output, std::string_view occurrence_id,
                                         const SequenceReplayResult& result) {
  output << "begin\t" << occurrence_id << '\n';
  if (!result.accepted) {
    output << "error\t" << result.error << "\nend\n";
    output.flush();
    return;
  }

  output << "ok\t" << result.pass_count << '\t' << (result.terminal ? 1 : 0) << '\t'
         << result.canonical_moves << '\n';
  for (const SequenceReplaySnapshot& snapshot : result.snapshots) {
    output << "row\t" << snapshot.ply << '\t' << snapshot.board_a1_to_h8 << '\t'
           << snapshot.label_score_side_to_move << '\t' << snapshot.player_disc_count << '\t'
           << snapshot.opponent_disc_count << '\t' << snapshot.empty_count << '\n';
  }
  output << "end\n";
  output.flush();
}

inline int run_sequence_replay_cli(std::istream& input, std::ostream& output,
                                   SequenceReplayFunction replay) {
  std::string line;
  while (std::getline(input, line)) {
    std::string_view request = line;
    if (!request.empty() && request.back() == '\r') {
      request.remove_suffix(1);
    }
    const std::size_t tab = request.find('\t');
    if (tab == std::string_view::npos) {
      write_sequence_replay_result(output, "",
                                   SequenceReplayResult{
                                       .accepted = false,
                                       .error = "request is missing occurrence id",
                                   });
      continue;
    }
    write_sequence_replay_result(output, request.substr(0, tab), replay(request.substr(tab + 1)));
  }
  return 0;
}

} // namespace vibe_othello::tools::data_import

#endif // VIBE_OTHELLO_TOOLS_DATA_IMPORT_SEQUENCE_REPLAY_CLI_H_
