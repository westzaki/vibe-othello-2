#ifndef VIBE_OTHELLO_TOOLS_DATA_IMPORT_EGAROUCID_SEQUENCE_REPLAY_H_
#define VIBE_OTHELLO_TOOLS_DATA_IMPORT_EGAROUCID_SEQUENCE_REPLAY_H_

#include <string>
#include <string_view>
#include <vector>

namespace vibe_othello::tools::data_import {

struct EgaroucidReplaySnapshot {
  int ply = 0;
  std::string board_a1_to_h8;
  int label_score_side_to_move = 0;
  int player_disc_count = 0;
  int opponent_disc_count = 0;
  int empty_count = 0;
};

struct EgaroucidReplayResult {
  bool accepted = false;
  std::string error;
  int pass_count = 0;
  bool terminal = false;
  std::string canonical_moves;
  std::vector<EgaroucidReplaySnapshot> snapshots;
};

[[nodiscard]] EgaroucidReplayResult replay_egaroucid_sequence(std::string_view text);

} // namespace vibe_othello::tools::data_import

#endif // VIBE_OTHELLO_TOOLS_DATA_IMPORT_EGAROUCID_SEQUENCE_REPLAY_H_
