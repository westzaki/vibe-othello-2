#pragma once

#include "move_ordering_internal.h"
#include "search_context_internal.h"
#include "search_node_internal.h"
#include "transposition_table_internal.h"
#include "vibe_othello/search/search.h"

#include <optional>

namespace vibe_othello::search::internal {

struct ExactEndgameTtProbe {
  std::optional<Score> cutoff_score;
  std::optional<board_core::Move> best_move;
};

Score terminal_score(board_core::Position position) noexcept;
bool is_valid_evaluator_score(Score score) noexcept;
void require_invariant(bool condition) noexcept;
void prepend_move(board_core::Move move, const Line& child, Line* line) noexcept;
void add_stats(SearchStats* total, SearchStats delta) noexcept;
SearchLimitState initialize_limit_state(SearchLimits limits);
bool should_stop(SearchLimitState* state);
bool note_node_visited(SearchLimitState* state, SearchStats* stats,
                       SearchNodeAccounting accounting);
bool should_stop_search(SearchContext* context);
bool note_node_visited(SearchContext* context);
bool is_better_root_move(Score score, board_core::Move move, Score best_score,
                         std::optional<board_core::Move> best_move) noexcept;

BoundType classify_bound(Score score, Score original_alpha, Score original_beta) noexcept;
ExactEndgameTtProbe exact_endgame_score_tt_probe(const TTEntry& entry,
                                                 board_core::Position position,
                                                 Depth remaining_empties, Score alpha,
                                                 Score beta) noexcept;
ExactEndgameTtProbe exact_endgame_wld_tt_probe(const TTEntry& entry, board_core::Position position,
                                               Depth remaining_empties, Score alpha,
                                               Score beta) noexcept;
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
void update_best_line_and_move(const SearchValue& child, board_core::Move move, SearchValue* best,
                               std::optional<board_core::Move>* best_move, StackFrame* frame);
bool update_alpha_and_check_cutoff(SearchContext* context, Score score, Score* alpha,
                                   Score beta) noexcept;
void update_midgame_ordering_on_beta_cutoff(SearchContext* context, board_core::Move move,
                                            Depth depth, Ply ply) noexcept;
void maybe_store_midgame_tt(SearchContext* context, Depth depth, Score score, BoundType bound,
                            std::optional<board_core::Move> best_move) noexcept;

SearchNodeResult alphabeta(SearchContext* context, Score alpha, Score beta, Depth depth, Ply ply);
SearchNodeResult null_window_search(SearchContext* context, Score beta, Depth depth, Ply ply);
SearchNodeResult pvs(SearchContext* context, Score alpha, Score beta, Depth depth, Ply ply);
SearchNodeResult full_window_search(SearchContext* context, Score alpha, Score beta, Depth depth,
                                    Ply ply);
std::uint8_t empty_count(board_core::Position position) noexcept;
bool should_use_exact_endgame(board_core::Position position,
                              ResolvedSearchOptions options) noexcept;
bool should_use_wld_endgame(board_core::Position position, ResolvedSearchOptions options) noexcept;
SearchNodeResult exact_score_search(EndgameContext* context, Score alpha, Score beta,
                                    std::uint8_t empties, Ply ply);
SearchNodeResult wld_search(EndgameContext* context, Score alpha, Score beta, std::uint8_t empties,
                            Ply ply);
SearchResult solve_exact_endgame(board_core::Position position, SearchLimits limits,
                                 SearchOptions options, TranspositionTable* tt,
                                 SearchLimitState* limit_state = nullptr);
SearchResult solve_wld_endgame(board_core::Position position, SearchLimits limits,
                               SearchOptions options, TranspositionTable* tt,
                               SearchLimitState* limit_state = nullptr);
SearchResult solve_exact_endgame_with_small_endgame_policy(board_core::Position position,
                                                           SearchLimits limits,
                                                           SearchOptions options,
                                                           TranspositionTable* tt,
                                                           SmallEndgamePolicy small_endgame_policy,
                                                           SearchLimitState* limit_state = nullptr);

SearchResult search_fixed_depth_with_hint(board_core::Position position, const Evaluator& evaluator,
                                          Depth depth, MoveOrderingHints root_hints,
                                          SearchOptions options, TranspositionTable* tt,
                                          RootSearchWindow root_window = {},
                                          MidgameOrderingState* ordering_state = nullptr,
                                          SearchLimitState* limit_state = nullptr);

} // namespace vibe_othello::search::internal
