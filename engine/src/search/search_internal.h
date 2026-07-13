#pragma once

#include "move_ordering_internal.h"
#include "search_context_internal.h"
#include "search_node_internal.h"
#include "search_util_internal.h"
#include "shadow_calibration_internal.h"
#include "transposition_table_internal.h"
#include "vibe_othello/search/search.h"

#include <optional>

namespace vibe_othello::search::internal {

bool should_stop_search(SearchContext* context);
bool note_node_visited(SearchContext* context);

std::optional<SearchNodeResult> prepare_search_node(SearchContext* context, Score alpha, Score beta,
                                                    Depth depth, Ply ply,
                                                    std::optional<TTEntry>* tt_entry);
MoveOrderingHints build_midgame_ordering_hints(const SearchContext& context,
                                               const std::optional<TTEntry>& tt_entry,
                                               std::optional<board_core::Move> iid_best_move,
                                               Ply ply) noexcept;
std::optional<board_core::Move> maybe_find_iid_best_move(SearchContext* context, Score alpha,
                                                         Score beta, Depth depth, Ply ply,
                                                         const std::optional<TTEntry>& tt_entry,
                                                         bool* stopped);
SearchNodeResult search_pass_child(SearchContext* context, Score alpha, Score beta, Depth depth,
                                   Ply ply, SearchDispatch dispatch);
SearchNodeResult search_full_window_child(SearchContext* context, board_core::Move move,
                                          Score alpha, Score beta, Depth depth, Ply ply,
                                          SearchDispatch dispatch);
SearchNodeResult search_null_window_child(SearchContext* context, board_core::Move move, Score beta,
                                          Depth depth, Ply ply);
bool update_alpha_and_check_cutoff(SearchContext* context, Score score, Score* alpha,
                                   Score beta) noexcept;
void update_midgame_ordering_on_beta_cutoff(SearchContext* context, board_core::Move move,
                                            Depth depth, Ply ply) noexcept;
void maybe_store_midgame_tt(SearchContext* context, Depth depth, Score score, BoundType bound,
                            std::optional<board_core::Move> best_move,
                            bool subtree_selective) noexcept;

SearchNodeResult alphabeta(SearchContext* context, Score alpha, Score beta, Depth depth, Ply ply,
                           bool cut_node = false);
SearchNodeResult null_window_search(SearchContext* context, Score beta, Depth depth, Ply ply);
SearchNodeResult pvs(SearchContext* context, Score alpha, Score beta, Depth depth, Ply ply,
                     bool cut_node = false);
SearchNodeResult full_window_search(SearchContext* context, Score alpha, Score beta, Depth depth,
                                    Ply ply);

SearchResult search_fixed_depth_with_hint(board_core::Position position, const Evaluator& evaluator,
                                          Depth depth, MoveOrderingHints root_hints,
                                          SearchOptions options, TranspositionTable* tt,
                                          RootSearchWindow root_window = {},
                                          MidgameOrderingState* ordering_state = nullptr,
                                          SearchLimitState* limit_state = nullptr,
                                          std::uint32_t incremental_eval_verify_interval = 0,
                                          ShadowCalibrationRun* shadow_calibration = nullptr);
SearchResult search_fixed_depth_with_hint(board_core::Position position, const Evaluator& evaluator,
                                          Depth depth, MoveOrderingHints root_hints,
                                          ResolvedSearchOptions options, TranspositionTable* tt,
                                          RootSearchWindow root_window = {},
                                          MidgameOrderingState* ordering_state = nullptr,
                                          SearchLimitState* limit_state = nullptr,
                                          std::uint32_t incremental_eval_verify_interval = 0,
                                          ShadowCalibrationRun* shadow_calibration = nullptr);

} // namespace vibe_othello::search::internal
