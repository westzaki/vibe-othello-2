#pragma once

#include "vibe_othello/search/score.h"

#include <cstdint>

namespace vibe_othello::search::internal {

struct SearchValue {
  Score score = 0;
  Line pv{};
};

enum class SearchNodeStatus : std::uint8_t {
  complete,
  stopped,
};

class SearchNodeResult {
public:
  static SearchNodeResult completed(SearchValue value, bool selective = false) noexcept;
  static SearchNodeResult stopped() noexcept;

  bool is_complete() const noexcept;
  bool is_stopped() const noexcept;
  bool is_selective() const noexcept;

  const SearchValue& value() const noexcept;

private:
  SearchNodeStatus status_ = SearchNodeStatus::stopped;
  SearchValue value_{};
  bool selective_ = false;
};

} // namespace vibe_othello::search::internal
