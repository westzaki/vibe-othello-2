#pragma once

#include "vibe_othello/search/production_probcut.h"
#include "vibe_othello/search/search.h"
#include "vibe_othello_wasm/wasm_api.h"

#include <algorithm>
#include <bit>
#include <cstdint>

namespace vibe_othello::wasm_adapter::internal {

inline constexpr std::uint8_t kProductionInternalExactEndgameEmpties = 8;

inline bool is_valid_search_preset(std::uint32_t preset) noexcept {
  return preset == VIBE_OTHELLO_WASM_SEARCH_PRESET_EASY ||
         preset == VIBE_OTHELLO_WASM_SEARCH_PRESET_NORMAL ||
         preset == VIBE_OTHELLO_WASM_SEARCH_PRESET_HARD;
}

inline search::SearchOptions
search_options_for_preset(std::uint32_t preset, std::uint8_t exact_endgame_empties,
                          search::ProbCutRuntimeIdentityV1 runtime_identity = {}) noexcept {
  const bool use_full_search_stack = preset == VIBE_OTHELLO_WASM_SEARCH_PRESET_NORMAL ||
                                     preset == VIBE_OTHELLO_WASM_SEARCH_PRESET_HARD;
  const std::uint8_t internal_exact_endgame_empties =
      use_full_search_stack
          ? std::min(exact_endgame_empties, kProductionInternalExactEndgameEmpties)
          : exact_endgame_empties;

  search::SearchOptions options{
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
              .use_midgame_mobility_ordering = use_full_search_stack,
              .use_endgame_parity_ordering = true,
          },
      .endgame =
          search::EndgameSearchOptions{
              .exact_endgame = exact_endgame_empties != 0,
              .use_endgame_tt = use_full_search_stack,
              .endgame_exact_empties = internal_exact_endgame_empties,
              .endgame_wld_empties = 0,
              .root_exact_endgame_empties =
                  use_full_search_stack ? exact_endgame_empties : std::uint8_t{0},
          },
      .reporting = search::SearchReportingOptions{.multi_pv = 1},
      .selective = search::SelectiveSearchOptionsV1{},
      .mode = search::SearchMode::move,
  };
  if (use_full_search_stack) {
    options.probcut_options = search::production_probcut_configuration_v1(
                                  runtime_identity, options.mode, internal_exact_endgame_empties)
                                  .options;
  }
  return options;
}

inline void apply_root_position_policy(board_core::Position position,
                                       search::SearchOptions* options) noexcept {
  const std::uint8_t root_exact_endgame_empties = options->endgame.root_exact_endgame_empties == 0
                                                      ? options->endgame.endgame_exact_empties
                                                      : options->endgame.root_exact_endgame_empties;
  const std::uint8_t empty_count = static_cast<std::uint8_t>(
      board_core::kSquareCount - std::popcount(board_core::occupied(position)));
  const bool uses_root_exact_endgame =
      options->endgame.exact_endgame && options->mode != search::SearchMode::win_loss_draw &&
      root_exact_endgame_empties != 0 && empty_count <= root_exact_endgame_empties;
  if (uses_root_exact_endgame) {
    options->probcut_options = {};
  }
}

} // namespace vibe_othello::wasm_adapter::internal
