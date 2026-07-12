#ifndef VIBE_OTHELLO_SEARCH_SEARCH_SESSION_H_
#define VIBE_OTHELLO_SEARCH_SEARCH_SESSION_H_

#include <cstddef>
#include <cstdint>
#include <memory>

namespace vibe_othello::search {

namespace internal {
struct SearchSessionAccess;
}

enum class TranspositionTableCapacityUnit : std::uint8_t {
  entries,
  bytes,
};

struct TranspositionTableConfig {
  // Zero disables the table. The default is deliberately platform-neutral;
  // Native and WASM callers should supply their own byte budget.
  std::size_t capacity = 65'536;
  TranspositionTableCapacityUnit unit = TranspositionTableCapacityUnit::entries;
};

struct TranspositionTableAllocation {
  std::size_t requested_bytes = 0;
  std::size_t actual_bytes = 0;
  std::size_t entry_count = 0;
  std::size_t bucket_count = 0;
  std::size_t entry_size = 0;
  bool enabled = false;
  bool allocation_succeeded = true;
};

enum class SearchPlatformProfile : std::uint8_t {
  custom,
  native,
  wasm,
};

struct SearchSessionConfig {
  SearchPlatformProfile profile = SearchPlatformProfile::custom;
  TranspositionTableConfig transposition_table{};
  // Debug-only parity guard. Zero disables it; a positive value recomputes the
  // stateless reference score every N incremental leaf evaluations and aborts
  // immediately on a mismatch.
  std::uint32_t incremental_eval_verify_interval = 0;
};

enum class SessionReusePolicy : std::uint8_t {
  retain,
  clear,
};

// Mutable, deterministic single-thread search state. One session may be
// retained across sequential moves in one game, but must not be used by
// concurrent searches. Root entry points automatically clear TT contents when
// evaluator identity/revision or normalized search semantics change.
class SearchSession {
public:
  explicit SearchSession(SearchSessionConfig config = {});
  ~SearchSession();
  SearchSession(SearchSession&&) noexcept;
  SearchSession& operator=(SearchSession&&) noexcept;
  SearchSession(const SearchSession&) = delete;
  SearchSession& operator=(const SearchSession&) = delete;

  // Deterministic reset: clears TT, history, killers, and root age.
  void clear() noexcept;
  void reset() noexcept;
  void start_new_game() noexcept;
  // Use `clear` for unrelated roots unless cross-root analysis reuse is
  // intentional. `retain` preserves learned ordering and TT knowledge.
  void prepare_analysis(SessionReusePolicy policy) noexcept;

  const SearchSessionConfig& config() const noexcept;
  TranspositionTableAllocation transposition_table_allocation() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  friend struct internal::SearchSessionAccess;
};

} // namespace vibe_othello::search

#endif // VIBE_OTHELLO_SEARCH_SEARCH_SESSION_H_
