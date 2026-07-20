#include "sequence_replay_cli.h"

#include <iostream>

int main() {
  namespace importer = vibe_othello::tools::data_import;
  return importer::run_sequence_replay_cli(std::cin, std::cout, importer::replay_wthor_sequence);
}
