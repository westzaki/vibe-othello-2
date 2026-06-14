#ifndef VIBE_OTHELLO_TOOLS_DATA_IMPORT_REPLAY_RECORDS_H_
#define VIBE_OTHELLO_TOOLS_DATA_IMPORT_REPLAY_RECORDS_H_

#include "vibe_othello/board_core/position.h"

#include <optional>
#include <string>
#include <vector>

namespace vibe_othello::tools::data_import {

struct Record {
  int line_number = 0;
  std::string id;
  bool expect_accept = false;
  std::string moves;
  std::optional<int> expected_final_disc_diff;
  std::string notes;
  std::string parse_error;
};

struct Summary {
  int total_records = 0;
  int accepted_records = 0;
  int rejected_records = 0;
  int expectation_failures = 0;
};

struct ReplayResult {
  bool accepted = false;
  std::string error;
  std::vector<board_core::Position> positions;
};

[[nodiscard]] bool load_records(const std::string& path, std::vector<Record>* records,
                                Summary* summary);

[[nodiscard]] ReplayResult replay_record(const Record& record, bool collect_positions);

[[nodiscard]] bool manifest_is_readable(const std::string& path);

} // namespace vibe_othello::tools::data_import

#endif // VIBE_OTHELLO_TOOLS_DATA_IMPORT_REPLAY_RECORDS_H_
