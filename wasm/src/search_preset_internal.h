#pragma once

#include "vibe_othello/search/search.h"
#include "vibe_othello_wasm/wasm_api.h"

#include <cstdint>

namespace vibe_othello::wasm_adapter::internal {

inline bool is_valid_search_preset(std::uint32_t preset) noexcept {
  return preset == VIBE_OTHELLO_WASM_SEARCH_PRESET_EASY ||
         preset == VIBE_OTHELLO_WASM_SEARCH_PRESET_NORMAL ||
         preset == VIBE_OTHELLO_WASM_SEARCH_PRESET_HARD;
}

inline search::SearchOptions
search_options_for_preset(std::uint32_t preset, std::uint8_t exact_endgame_empties) noexcept {
  const bool use_full_search_stack = preset == VIBE_OTHELLO_WASM_SEARCH_PRESET_NORMAL ||
                                     preset == VIBE_OTHELLO_WASM_SEARCH_PRESET_HARD;

  return search::SearchOptions{
      .midgame =
          search::MidgameSearchOptions{
              .use_pvs = use_full_search_stack,
              .use_aspiration = use_full_search_stack,
              .use_iid = use_full_search_stack,
              .use_midgame_tt = use_full_search_stack,
          },
      .ordering =
          search::MoveOrderingOptions{
              .use_tt_best_move_ordering = use_full_search_stack,
              .use_history = use_full_search_stack,
              .use_killers = use_full_search_stack,
              .use_endgame_parity_ordering = true,
          },
      .endgame =
          search::EndgameSearchOptions{
              .exact_endgame = exact_endgame_empties != 0,
              .use_endgame_tt = use_full_search_stack,
              .endgame_exact_empties = exact_endgame_empties,
              .endgame_wld_empties = 0,
          },
      .reporting = search::SearchReportingOptions{},
      .experimental = search::ExperimentalSearchOptions{},
      .mode = search::SearchMode::move,
  };
}

} // namespace vibe_othello::wasm_adapter::internal
