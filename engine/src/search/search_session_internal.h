#pragma once

#include "search_context_internal.h"
#include "vibe_othello/search/search_session.h"

#include <optional>

namespace vibe_othello::search {

namespace internal {

enum class SearchSemanticDomain : std::uint8_t {
  midgame,
  exact_endgame,
  wld_endgame,
};

struct SearchSemanticFingerprint {
  const Evaluator* evaluator = nullptr;
  std::uint64_t evaluator_revision = 0;
  ResolvedSearchOptions options{};
  SearchSemanticDomain domain = SearchSemanticDomain::midgame;

  friend bool operator==(const SearchSemanticFingerprint&,
                         const SearchSemanticFingerprint&) = default;
};

inline SearchSemanticFingerprint
make_search_semantic_fingerprint(const Evaluator* evaluator, ResolvedSearchOptions options,
                                 SearchSemanticDomain domain) noexcept {
  return SearchSemanticFingerprint{
      .evaluator = evaluator,
      .evaluator_revision = evaluator == nullptr ? 0 : evaluator->transposition_table_revision(),
      .options = options,
      .domain = domain,
  };
}

} // namespace internal

struct SearchSession::Impl {
  explicit Impl(SearchSessionConfig session_config)
      : config(session_config), transposition_table(session_config.transposition_table) {}

  SearchSessionConfig config{};
  internal::TranspositionTable transposition_table;
  internal::MidgameOrderingState ordering_state{};
  std::optional<internal::SearchSemanticFingerprint> semantic_fingerprint;
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

  static void begin_root(SearchSession& session, SearchSemanticFingerprint fingerprint) noexcept {
    if (!session.impl_->semantic_fingerprint.has_value()) {
      session.impl_->semantic_fingerprint = fingerprint;
    } else if (*session.impl_->semantic_fingerprint != fingerprint) {
      session.impl_->transposition_table.clear();
      session.impl_->semantic_fingerprint = fingerprint;
    }
    session.impl_->transposition_table.new_generation();
  }
};

} // namespace internal
} // namespace vibe_othello::search
