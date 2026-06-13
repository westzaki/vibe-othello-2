#ifndef VIBE_OTHELLO_SEARCH_SCORE_H_
#define VIBE_OTHELLO_SEARCH_SCORE_H_

#include "vibe_othello/board_core/board.h"

#include <array>
#include <cstdint>

namespace vibe_othello::search {

using Score = std::int32_t;
using Depth = std::int16_t;
using Ply = std::uint8_t;
using NodeCount = std::uint64_t;

inline constexpr Score kScoreInf = 32'000;
inline constexpr Score kScoreWin = 30'000;
inline constexpr Score kScoreLoss = -kScoreWin;
// Principal variations are truncated to this fixed search-owned capacity.
inline constexpr std::uint8_t kMaxPly = 128;
static_assert(board_core::kSquareCount < kMaxPly);

struct Line {
  std::array<board_core::Move, kMaxPly> moves{};
  std::uint8_t size = 0;

  friend constexpr bool operator==(const Line&, const Line&) noexcept = default;
};

enum class BoundType : std::uint8_t {
  exact,
  lower,
  upper,
};

enum class SearchMode : std::uint8_t {
  move,
  analyze,
  exact_score,
  win_loss_draw,
};

enum class WldResult : std::int8_t {
  loss = -1,
  draw = 0,
  win = 1,
};

} // namespace vibe_othello::search

#endif // VIBE_OTHELLO_SEARCH_SCORE_H_
