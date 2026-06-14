#ifndef VIBE_OTHELLO_EVALUATION_PATTERN_WEIGHTS_H_
#define VIBE_OTHELLO_EVALUATION_PATTERN_WEIGHTS_H_

#include "vibe_othello/board_core/types.h"
#include "vibe_othello/search/score.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vibe_othello::evaluation {

inline constexpr std::uint16_t kPatternWeightFormatVersion = 1;
inline constexpr std::uint32_t kPatternPhaseBiasWeightCount = 1;

enum class PatternBitOrder : std::uint16_t {
  a1_lsb = 1,
};

enum class PatternScoreUnit : std::uint16_t {
  disc_diff = 1,
};

struct PatternDefinition {
  std::string id;
  std::vector<board_core::Square> squares;
};

struct PatternManifest {
  std::uint16_t format_version = kPatternWeightFormatVersion;
  PatternBitOrder bit_order = PatternBitOrder::a1_lsb;
  PatternScoreUnit score_unit = PatternScoreUnit::disc_diff;
  std::uint16_t score_scale = 1;
  std::uint16_t phase_count = 0;
  std::string pattern_set_id;
  // Ordered artifact contract: pattern tables are stored in this exact order
  // after the per-phase bias slot. Pattern ids are manifest-side labels; v1
  // artifacts validate the whole ordered set through pattern_set_id.
  std::vector<PatternDefinition> patterns;
};

struct LoadedPatternWeights {
  PatternManifest manifest;
  std::vector<search::Score> weights;
  // Number of weights per phase, including the leading bias slot.
  std::uint32_t phase_stride = 0;
  // Per-phase offsets for each pattern table. The phase bias is always at
  // offset 0, and the first pattern table starts at kPatternPhaseBiasWeightCount.
  std::vector<std::uint32_t> pattern_table_offsets;
};

struct PatternWeightTable {
  std::string pattern_id;
  std::uint8_t pattern_length = 0;
  std::vector<search::Score> weights;
};

class PatternWeights {
public:
  static constexpr std::uint8_t kDiscCountEntries = 65;

  PatternWeights(std::uint8_t phase_count,
                 std::array<std::uint8_t, kDiscCountEntries> phase_by_disc_count,
                 std::vector<PatternWeightTable> tables);

  [[nodiscard]] std::uint8_t phase_count() const noexcept {
    return phase_count_;
  }
  [[nodiscard]] std::span<const PatternWeightTable> tables() const noexcept {
    return tables_;
  }
  [[nodiscard]] std::uint8_t phase_for_disc_count(int disc_count) const noexcept;
  [[nodiscard]] search::Score weight(std::size_t table_index, std::uint8_t phase,
                                     std::uint32_t pattern_index) const noexcept;

private:
  std::uint8_t phase_count_ = 0;
  std::array<std::uint8_t, kDiscCountEntries> phase_by_disc_count_{};
  std::vector<PatternWeightTable> tables_;
};

enum class PatternWeightsLoadError : std::uint8_t {
  none,
  bad_magic,
  unsupported_format_version,
  unsupported_bit_order,
  bit_order_mismatch,
  unsupported_score_unit,
  score_unit_mismatch,
  unsupported_score_scale,
  phase_count_mismatch,
  pattern_count_mismatch,
  pattern_set_id_mismatch,
  invalid_pattern_length,
  invalid_pattern_square,
  invalid_weight_count,
  truncated,
  checksum_mismatch,
};

struct PatternWeightsLoadResult {
  std::optional<LoadedPatternWeights> weights;
  PatternWeightsLoadError error = PatternWeightsLoadError::none;

  [[nodiscard]] bool ok() const noexcept {
    return weights.has_value();
  }
};

[[nodiscard]] PatternWeightsLoadResult load_pattern_weights(const PatternManifest& manifest,
                                                            std::span<const std::uint8_t> artifact);
[[nodiscard]] std::optional<PatternWeights> make_pattern_weights(
    const LoadedPatternWeights& loaded,
    std::array<std::uint8_t, PatternWeights::kDiscCountEntries> phase_by_disc_count);

} // namespace vibe_othello::evaluation

#endif // VIBE_OTHELLO_EVALUATION_PATTERN_WEIGHTS_H_
