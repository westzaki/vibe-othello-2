#include "search_session_internal.h"

namespace vibe_othello::search {

SearchSession::SearchSession(SearchSessionConfig config) : impl_(std::make_unique<Impl>(config)) {}
SearchSession::~SearchSession() = default;
SearchSession::SearchSession(SearchSession&&) noexcept = default;
SearchSession& SearchSession::operator=(SearchSession&&) noexcept = default;

void SearchSession::clear() noexcept {
  impl_->transposition_table.clear();
  impl_->ordering_state = internal::MidgameOrderingState{};
}

void SearchSession::reset() noexcept {
  clear();
}

void SearchSession::start_new_game() noexcept {
  clear();
}

void SearchSession::prepare_analysis(SessionReusePolicy policy) noexcept {
  if (policy == SessionReusePolicy::clear) {
    clear();
  }
}

const SearchSessionConfig& SearchSession::config() const noexcept {
  return impl_->config;
}

TranspositionTableAllocation SearchSession::transposition_table_allocation() const noexcept {
  return impl_->transposition_table.allocation();
}

} // namespace vibe_othello::search
