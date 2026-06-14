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
  static SearchNodeResult completed(SearchValue value) noexcept;
  static SearchNodeResult stopped() noexcept;

  bool is_complete() const noexcept;
  bool is_stopped() const noexcept;

  const SearchValue& value() const noexcept;

private:
  SearchNodeStatus status_ = SearchNodeStatus::stopped;
  SearchValue value_{};
};

} // namespace vibe_othello::search::internal
