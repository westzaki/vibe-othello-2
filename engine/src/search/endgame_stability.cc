#include "endgame_stability_internal.h"

#include <array>
#include <bit>

namespace vibe_othello::search::internal {
namespace {

constexpr board_core::Bitboard kFileA = 0x0101010101010101ULL;
constexpr board_core::Bitboard kFileH = 0x8080808080808080ULL;
constexpr board_core::Bitboard kRank1 = 0x00000000000000ffULL;
constexpr board_core::Bitboard kRank8 = 0xff00000000000000ULL;
constexpr board_core::Bitboard kOuterEdge = kFileA | kFileH | kRank1 | kRank8;

constexpr std::array<board_core::Bitboard, 8> kRanks{
    0x00000000000000ffULL, 0x000000000000ff00ULL, 0x0000000000ff0000ULL, 0x00000000ff000000ULL,
    0x000000ff00000000ULL, 0x0000ff0000000000ULL, 0x00ff000000000000ULL, 0xff00000000000000ULL,
};

constexpr std::array<board_core::Bitboard, 8> kFiles{
    0x0101010101010101ULL, 0x0202020202020202ULL, 0x0404040404040404ULL, 0x0808080808080808ULL,
    0x1010101010101010ULL, 0x2020202020202020ULL, 0x4040404040404040ULL, 0x8080808080808080ULL,
};

constexpr std::array<board_core::Bitboard, 15> kStep7Diagonals{
    0x0000000000000001ULL, 0x0000000000000102ULL, 0x0000000000010204ULL, 0x0000000001020408ULL,
    0x0000000102040810ULL, 0x0000010204081020ULL, 0x0001020408102040ULL, 0x0102040810204080ULL,
    0x0204081020408000ULL, 0x0408102040800000ULL, 0x0810204080000000ULL, 0x1020408000000000ULL,
    0x2040800000000000ULL, 0x4080000000000000ULL, 0x8000000000000000ULL,
};

constexpr std::array<board_core::Bitboard, 15> kStep9Diagonals{
    0x0000000000000080ULL, 0x0000000000008040ULL, 0x0000000000804020ULL, 0x0000000080402010ULL,
    0x0000008040201008ULL, 0x0000804020100804ULL, 0x0080402010080402ULL, 0x8040201008040201ULL,
    0x4020100804020100ULL, 0x2010080402010000ULL, 0x1008040201000000ULL, 0x0804020100000000ULL,
    0x0402010000000000ULL, 0x0201000000000000ULL, 0x0100000000000000ULL,
};

template <std::size_t Size>
board_core::Bitboard full_line_mask(board_core::Bitboard occupied,
                                    const std::array<board_core::Bitboard, Size>& lines) noexcept {
  board_core::Bitboard full = 0;
  for (const board_core::Bitboard line : lines) {
    if ((occupied & line) == line) {
      full |= line;
    }
  }
  return full;
}

constexpr board_core::Bitboard shift_east(board_core::Bitboard bits) noexcept {
  return (bits & ~kFileH) << 1;
}

constexpr board_core::Bitboard shift_west(board_core::Bitboard bits) noexcept {
  return (bits & ~kFileA) >> 1;
}

constexpr board_core::Bitboard shift_north(board_core::Bitboard bits) noexcept {
  return bits << 8;
}

constexpr board_core::Bitboard shift_south(board_core::Bitboard bits) noexcept {
  return bits >> 8;
}

constexpr board_core::Bitboard shift_north_east(board_core::Bitboard bits) noexcept {
  return (bits & ~kFileH) << 9;
}

constexpr board_core::Bitboard shift_north_west(board_core::Bitboard bits) noexcept {
  return (bits & ~kFileA) << 7;
}

constexpr board_core::Bitboard shift_south_east(board_core::Bitboard bits) noexcept {
  return (bits & ~kFileH) >> 7;
}

constexpr board_core::Bitboard shift_south_west(board_core::Bitboard bits) noexcept {
  return (bits & ~kFileA) >> 9;
}

bool lower_bound_can_cut(board_core::Position position, Score beta) noexcept {
  const int maximum_lower = (2 * std::popcount(position.player)) - board_core::kSquareCount;
  return maximum_lower >= beta;
}

Score stability_probe_threshold(std::uint8_t empties) noexcept {
  // This gate only avoids proofs unlikely to reach the current window. Skipping
  // a proof cannot change the exact result.
  const int threshold = empties >= 10 ? 2 * empties : (2 * empties) - 2;
  return static_cast<Score>(threshold);
}

bool upper_bound_can_cut(board_core::Position position, Score alpha) noexcept {
  const int minimum_upper = board_core::kSquareCount - (2 * std::popcount(position.opponent));
  return minimum_upper <= alpha;
}

} // namespace

board_core::Bitboard stable_discs(board_core::Bitboard discs,
                                  board_core::Bitboard occupied) noexcept {
  if (discs == 0) {
    return 0;
  }

  const board_core::Bitboard full_horizontal = full_line_mask(occupied, kRanks);
  const board_core::Bitboard full_vertical = full_line_mask(occupied, kFiles);
  const board_core::Bitboard full_north_east = full_line_mask(occupied, kStep9Diagonals);
  const board_core::Bitboard full_north_west = full_line_mask(occupied, kStep7Diagonals);

  board_core::Bitboard stable = 0;
  board_core::Bitboard previous = ~board_core::Bitboard{0};
  while (stable != previous) {
    previous = stable;
    // A disc is added only when every axis has a full line or one protected
    // side: a boundary or an already-proven same-color stable neighbor. The
    // monotone fixed point is conservative; missing discs weaken the bound.
    const board_core::Bitboard protected_horizontal =
        full_horizontal | kFileA | kFileH | shift_east(stable) | shift_west(stable);
    const board_core::Bitboard protected_vertical =
        full_vertical | kRank1 | kRank8 | shift_north(stable) | shift_south(stable);
    const board_core::Bitboard protected_north_east =
        full_north_east | kOuterEdge | shift_north_east(stable) | shift_south_west(stable);
    const board_core::Bitboard protected_north_west =
        full_north_west | kOuterEdge | shift_north_west(stable) | shift_south_east(stable);
    stable |= discs & protected_horizontal & protected_vertical & protected_north_east &
              protected_north_west;
  }
  return stable;
}

EndgameStabilityBounds endgame_stability_bounds(board_core::Position position) noexcept {
  const board_core::Bitboard occupied = board_core::occupied(position);
  const board_core::Bitboard stable_player = stable_discs(position.player, occupied);
  const board_core::Bitboard stable_opponent = stable_discs(position.opponent, occupied);
  // In the worst case every non-proven disc belongs to the other side:
  // player >= p - (64 - p), player <= (64 - o) - o.
  return EndgameStabilityBounds{
      .lower = static_cast<Score>((2 * std::popcount(stable_player)) - board_core::kSquareCount),
      .upper = static_cast<Score>(board_core::kSquareCount - (2 * std::popcount(stable_opponent))),
      .stable_player = stable_player,
      .stable_opponent = stable_opponent,
  };
}

std::optional<EndgameStabilityCutoffCandidate>
probe_endgame_stability(EndgameContext* context, Score alpha, Score beta,
                        std::uint8_t empties) noexcept {
  if (context == nullptr || empties < 4 ||
      context->options.endgame.stability_mode == EndgameStabilityMode::off) {
    return std::nullopt;
  }

  const board_core::Position position = context->position_state.position;
  const Score probe_threshold = stability_probe_threshold(empties);
  const bool probe_lower = beta <= -probe_threshold && lower_bound_can_cut(position, beta);
  const bool probe_upper = alpha >= probe_threshold && upper_bound_can_cut(position, alpha);
  if (!probe_lower && !probe_upper) {
    return std::nullopt;
  }

  ++context->stats.endgame_stability_probes;
  const board_core::Bitboard occupied = board_core::occupied(position);
  if (probe_lower) {
    const int stable_player = std::popcount(stable_discs(position.player, occupied));
    const Score lower = static_cast<Score>((2 * stable_player) - board_core::kSquareCount);
    if (lower >= beta) {
      ++context->stats.endgame_stability_lower_candidates;
      return EndgameStabilityCutoffCandidate{
          .kind = EndgameStabilityCutoffKind::lower,
          .score = lower,
          .threshold = beta,
      };
    }
  }
  if (probe_upper) {
    const int stable_opponent = std::popcount(stable_discs(position.opponent, occupied));
    const Score upper = static_cast<Score>(board_core::kSquareCount - (2 * stable_opponent));
    if (upper <= alpha) {
      ++context->stats.endgame_stability_upper_candidates;
      return EndgameStabilityCutoffCandidate{
          .kind = EndgameStabilityCutoffKind::upper,
          .score = upper,
          .threshold = alpha,
      };
    }
  }
  return std::nullopt;
}

void verify_endgame_stability_shadow(EndgameContext* context,
                                     std::optional<EndgameStabilityCutoffCandidate> candidate,
                                     const SearchNodeResult& result) noexcept {
  if (context == nullptr || !candidate.has_value() || result.is_stopped() ||
      context->options.endgame.stability_mode != EndgameStabilityMode::shadow) {
    return;
  }

  ++context->stats.endgame_stability_shadow_verifications;
  const Score result_score = result.value().score;
  const bool false_cutoff = candidate->kind == EndgameStabilityCutoffKind::lower
                                ? result_score < candidate->threshold
                                : result_score > candidate->threshold;
  if (false_cutoff) {
    ++context->stats.endgame_stability_shadow_false_cutoffs;
  }
}

} // namespace vibe_othello::search::internal
