#pragma once

#include "search_context_internal.h"
#include "vibe_othello/search/search_session.h"

namespace vibe_othello::search {

struct SearchSession::Impl {
  explicit Impl(SearchSessionConfig session_config)
      : config(session_config), transposition_table(session_config.transposition_table) {}

  SearchSessionConfig config{};
  internal::TranspositionTable transposition_table;
  internal::MidgameOrderingState ordering_state{};
};

namespace internal {

struct SearchSessionAccess {
  static TranspositionTable* transposition_table(SearchSession& session) noexcept {
    return session.impl_->transposition_table.enabled() ? &session.impl_->transposition_table
                                                        : nullptr;
  }

  static MidgameOrderingState* ordering_state(SearchSession& session) noexcept {
    return &session.impl_->ordering_state;
  }

  static void begin_root(SearchSession& session) noexcept {
    session.impl_->transposition_table.new_generation();
  }
};

} // namespace internal
} // namespace vibe_othello::search
