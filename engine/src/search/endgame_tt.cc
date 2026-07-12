#include "endgame_policy_internal.h"

namespace vibe_othello::search::internal {
namespace {

bool should_use_endgame_tt(const EndgameContext& context) noexcept {
  return context.options.endgame.use_endgame_tt && context.transposition_table != nullptr;
}

ExactEndgameTtProbe probe_endgame_tt_entry(const TTEntry& entry, TTEntryKind kind,
                                           board_core::Bitboard legal_moves,
                                           Depth remaining_empties, Score alpha,
                                           Score beta) noexcept {
  ExactEndgameTtProbe probe{};
  if (entry.kind != kind) {
    return probe;
  }

  if (entry.has_best_move && entry.best_move.kind == board_core::MoveKind::normal &&
      (legal_moves & board_core::bit(entry.best_move.square)) != 0) {
    probe.best_move = entry.best_move;
  }

  if (entry.depth < remaining_empties) {
    // Best-move hints are ordering-only: a shallow exact-endgame entry cannot
    // cut off this node, but its legal normal best move may still order moves.
    return probe;
  }
  if (entry.bound == BoundType::exact || (entry.bound == BoundType::lower && entry.score >= beta) ||
      (entry.bound == BoundType::upper && entry.score <= alpha)) {
    probe.cutoff_score = entry.score;
  }
  return probe;
}

ExactEndgameTtProbe probe_endgame_tt(EndgameContext* context, TTEntryKind kind,
                                     Depth remaining_empties, Score alpha, Score beta) {
  if (!should_use_endgame_tt(*context)) {
    return {};
  }

  const std::optional<TTEntry> entry =
      context->transposition_table->probe(context->position_state.key, kind, &context->stats);
  if (!entry.has_value()) {
    return {};
  }

  ExactEndgameTtProbe probe = probe_endgame_tt_entry(
      *entry, kind, legal_moves(&context->position_state), remaining_empties, alpha, beta);
  if (probe.cutoff_score.has_value()) {
    ++context->stats.tt_cutoffs;
  }
  return probe;
}

std::optional<board_core::Move> probe_endgame_root_tt_best_move(EndgameContext* context,
                                                                TTEntryKind kind,
                                                                Depth remaining_empties,
                                                                Score alpha, Score beta) {
  if (!should_use_endgame_tt(*context)) {
    return std::nullopt;
  }

  const std::optional<TTEntry> entry =
      context->transposition_table->probe(context->position_state.key, kind, &context->stats);
  if (!entry.has_value()) {
    return std::nullopt;
  }

  return probe_endgame_tt_entry(*entry, kind, legal_moves(&context->position_state),
                                remaining_empties, alpha, beta)
      .best_move;
}

void store_endgame_tt(EndgameContext* context, TTEntryKind kind, Depth remaining_empties,
                      Score score, BoundType bound,
                      std::optional<board_core::Move> best_move) noexcept {
  if (!should_use_endgame_tt(*context) || should_stop_endgame(context)) {
    return;
  }

  if (best_move.has_value()) {
    context->transposition_table->store(context->position_state.key, remaining_empties, score,
                                        bound, *best_move, kind, &context->stats);
    return;
  }

  context->transposition_table->store_value(context->position_state.key, remaining_empties, score,
                                            bound, kind, &context->stats);
}

} // namespace

ExactEndgameTtProbe exact_endgame_score_tt_probe(const TTEntry& entry,
                                                 board_core::Position position,
                                                 Depth remaining_empties, Score alpha,
                                                 Score beta) noexcept {
  return probe_endgame_tt_entry(entry, TTEntryKind::exact_endgame_score,
                                board_core::legal_moves(position), remaining_empties, alpha, beta);
}

ExactEndgameTtProbe exact_endgame_wld_tt_probe(const TTEntry& entry, board_core::Position position,
                                               Depth remaining_empties, Score alpha,
                                               Score beta) noexcept {
  return probe_endgame_tt_entry(entry, TTEntryKind::exact_endgame_wld,
                                board_core::legal_moves(position), remaining_empties, alpha, beta);
}

std::optional<Score> exact_endgame_score_tt_cutoff_score(const TTEntry& entry,
                                                         Depth remaining_empties, Score alpha,
                                                         Score beta) noexcept {
  if (entry.kind != TTEntryKind::exact_endgame_score || entry.depth < remaining_empties) {
    return std::nullopt;
  }
  if (entry.bound == BoundType::exact) {
    return entry.score;
  }
  if (entry.bound == BoundType::lower && entry.score >= beta) {
    return entry.score;
  }
  if (entry.bound == BoundType::upper && entry.score <= alpha) {
    return entry.score;
  }
  return std::nullopt;
}

ExactEndgameTtProbe probe_exact_score_endgame_tt(EndgameContext* context, Depth remaining_empties,
                                                 Score alpha, Score beta) {
  return probe_endgame_tt(context, TTEntryKind::exact_endgame_score, remaining_empties, alpha,
                          beta);
}

ExactEndgameTtProbe probe_wld_endgame_tt(EndgameContext* context, Depth remaining_empties,
                                         Score alpha, Score beta) {
  return probe_endgame_tt(context, TTEntryKind::exact_endgame_wld, remaining_empties, alpha, beta);
}

std::optional<board_core::Move>
probe_exact_score_endgame_root_tt_best_move(EndgameContext* context, Depth remaining_empties) {
  return probe_endgame_root_tt_best_move(context, TTEntryKind::exact_endgame_score,
                                         remaining_empties, ExactScoreEndgamePolicy::kInitialAlpha,
                                         ExactScoreEndgamePolicy::kInitialBeta);
}

std::optional<board_core::Move> probe_wld_endgame_root_tt_best_move(EndgameContext* context,
                                                                    Depth remaining_empties) {
  return probe_endgame_root_tt_best_move(context, TTEntryKind::exact_endgame_wld, remaining_empties,
                                         WldEndgamePolicy::kInitialAlpha,
                                         WldEndgamePolicy::kInitialBeta);
}

void store_exact_score_endgame_tt(EndgameContext* context, Depth remaining_empties, Score score,
                                  BoundType bound, std::optional<board_core::Move> best_move) {
  store_endgame_tt(context, TTEntryKind::exact_endgame_score, remaining_empties, score, bound,
                   best_move);
}

void store_wld_endgame_tt(EndgameContext* context, Depth remaining_empties, Score score,
                          BoundType bound, std::optional<board_core::Move> best_move) {
  store_endgame_tt(context, TTEntryKind::exact_endgame_wld, remaining_empties, score, bound,
                   best_move);
}

} // namespace vibe_othello::search::internal
