#include "vibe_othello/evaluation/pattern_weights.h"

#include "vibe_othello/board_core/coordinates.h"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <utility>

namespace vibe_othello::evaluation {
namespace {

inline constexpr std::array<std::uint8_t, 8> kMagic{'V', 'O', 'P', 'W', 'G', 'T', '\0', '\0'};
inline constexpr std::uint16_t kReserved = 0;
inline constexpr std::uint32_t kCrc32Initial = 0xFFFF'FFFFU;
inline constexpr std::uint32_t kCrc32Polynomial = 0xEDB8'8320U;

struct Header {
  std::uint16_t format_version = 0;
  std::uint16_t bit_order = 0;
  std::uint16_t score_unit = 0;
  std::uint16_t score_scale = 0;
  std::uint16_t phase_count = 0;
  std::uint16_t pattern_count = 0;
  std::uint16_t pattern_set_id_length = 0;
  std::uint16_t reserved = 0;
  std::uint32_t weight_count = 0;
};

struct Reader {
  std::span<const std::uint8_t> bytes;
  std::size_t offset = 0;

  [[nodiscard]] std::optional<std::uint16_t> read_u16() noexcept {
    if (bytes.size() - offset < sizeof(std::uint16_t)) {
      return std::nullopt;
    }

    const std::uint16_t value =
        static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
    offset += sizeof(std::uint16_t);
    return value;
  }

  [[nodiscard]] std::optional<std::uint32_t> read_u32() noexcept {
    if (bytes.size() - offset < sizeof(std::uint32_t)) {
      return std::nullopt;
    }

    const std::uint32_t value = static_cast<std::uint32_t>(bytes[offset]) |
                                (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
                                (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
                                (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
    offset += sizeof(std::uint32_t);
    return value;
  }

  [[nodiscard]] std::optional<std::int32_t> read_i32() noexcept {
    const std::optional<std::uint32_t> value = read_u32();
    if (!value.has_value()) {
      return std::nullopt;
    }
    return std::bit_cast<std::int32_t>(*value);
  }

  [[nodiscard]] std::optional<std::span<const std::uint8_t>> read_bytes(std::size_t size) noexcept {
    if (bytes.size() - offset < size) {
      return std::nullopt;
    }

    const std::span<const std::uint8_t> result = bytes.subspan(offset, size);
    offset += size;
    return result;
  }
};

[[nodiscard]] PatternWeightsLoadResult fail(PatternWeightsLoadError error) noexcept {
  return PatternWeightsLoadResult{.weights = std::nullopt, .error = error};
}

[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> bytes) noexcept {
  std::uint32_t crc = kCrc32Initial;
  for (const std::uint8_t byte : bytes) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      const std::uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (kCrc32Polynomial & mask);
    }
  }
  return ~crc;
}

[[nodiscard]] std::optional<Header> read_header(Reader* reader) noexcept {
  const std::optional<std::uint16_t> format_version = reader->read_u16();
  const std::optional<std::uint16_t> bit_order = reader->read_u16();
  const std::optional<std::uint16_t> score_unit = reader->read_u16();
  const std::optional<std::uint16_t> score_scale = reader->read_u16();
  const std::optional<std::uint16_t> phase_count = reader->read_u16();
  const std::optional<std::uint16_t> pattern_count = reader->read_u16();
  const std::optional<std::uint16_t> pattern_set_id_length = reader->read_u16();
  const std::optional<std::uint16_t> reserved = reader->read_u16();
  const std::optional<std::uint32_t> weight_count = reader->read_u32();

  if (!format_version.has_value() || !bit_order.has_value() || !score_unit.has_value() ||
      !score_scale.has_value() || !phase_count.has_value() || !pattern_count.has_value() ||
      !pattern_set_id_length.has_value() || !reserved.has_value() || !weight_count.has_value()) {
    return std::nullopt;
  }

  return Header{
      .format_version = *format_version,
      .bit_order = *bit_order,
      .score_unit = *score_unit,
      .score_scale = *score_scale,
      .phase_count = *phase_count,
      .pattern_count = *pattern_count,
      .pattern_set_id_length = *pattern_set_id_length,
      .reserved = *reserved,
      .weight_count = *weight_count,
  };
}

[[nodiscard]] bool checked_add(std::uint64_t value, std::uint64_t addend,
                               std::uint64_t* result) noexcept {
  if (value > std::numeric_limits<std::uint64_t>::max() - addend) {
    return false;
  }
  *result = value + addend;
  return true;
}

[[nodiscard]] std::optional<std::uint64_t>
pattern_table_size(const PatternDefinition& pattern) noexcept {
  if (pattern.squares.empty() || pattern.squares.size() > board_core::kSquareCount) {
    return std::nullopt;
  }

  std::uint64_t size = 1;
  for (std::size_t index = 0; index < pattern.squares.size(); ++index) {
    if (size > std::numeric_limits<std::uint64_t>::max() / 3U) {
      return std::nullopt;
    }
    size *= 3U;
  }
  return size;
}

[[nodiscard]] std::optional<std::uint32_t>
expected_phase_stride(const PatternManifest& manifest) noexcept {
  std::uint64_t stride = 1;
  for (const PatternDefinition& pattern : manifest.patterns) {
    const std::optional<std::uint64_t> table_size = pattern_table_size(pattern);
    if (!table_size.has_value()) {
      return std::nullopt;
    }
    if (!checked_add(stride, *table_size, &stride)) {
      return std::nullopt;
    }
  }
  if (stride > std::numeric_limits<std::uint32_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(stride);
}

[[nodiscard]] PatternWeightsLoadError
validate_manifest(const PatternManifest& manifest, std::uint32_t* phase_stride,
                  std::uint32_t* expected_weight_count) noexcept {
  if (manifest.format_version != kPatternWeightFormatVersion) {
    return PatternWeightsLoadError::unsupported_format_version;
  }
  if (manifest.bit_order != PatternBitOrder::a1_lsb) {
    return PatternWeightsLoadError::unsupported_bit_order;
  }
  if (manifest.score_unit != PatternScoreUnit::disc_diff) {
    return PatternWeightsLoadError::unsupported_score_unit;
  }
  if (manifest.score_scale != 1) {
    return PatternWeightsLoadError::unsupported_score_scale;
  }
  if (manifest.phase_count == 0) {
    return PatternWeightsLoadError::phase_count_mismatch;
  }
  for (const PatternDefinition& pattern : manifest.patterns) {
    if (pattern.squares.empty() || pattern.squares.size() > board_core::kSquareCount) {
      return PatternWeightsLoadError::invalid_pattern_length;
    }
    for (const board_core::Square square : pattern.squares) {
      if (!board_core::is_valid(square)) {
        return PatternWeightsLoadError::invalid_pattern_square;
      }
    }
  }

  const std::optional<std::uint32_t> stride = expected_phase_stride(manifest);
  if (!stride.has_value()) {
    return PatternWeightsLoadError::invalid_weight_count;
  }
  const std::uint64_t weight_count =
      static_cast<std::uint64_t>(*stride) * static_cast<std::uint64_t>(manifest.phase_count);
  if (weight_count > std::numeric_limits<std::uint32_t>::max()) {
    return PatternWeightsLoadError::invalid_weight_count;
  }

  *phase_stride = *stride;
  *expected_weight_count = static_cast<std::uint32_t>(weight_count);
  return PatternWeightsLoadError::none;
}

[[nodiscard]] PatternWeightsLoadError
validate_header(const Header& header, const PatternManifest& manifest,
                std::uint32_t expected_weight_count) noexcept {
  if (header.format_version != kPatternWeightFormatVersion) {
    return PatternWeightsLoadError::unsupported_format_version;
  }
  if (header.format_version != manifest.format_version) {
    return PatternWeightsLoadError::unsupported_format_version;
  }
  if (header.bit_order != static_cast<std::uint16_t>(PatternBitOrder::a1_lsb)) {
    return PatternWeightsLoadError::unsupported_bit_order;
  }
  if (header.bit_order != static_cast<std::uint16_t>(manifest.bit_order)) {
    return PatternWeightsLoadError::bit_order_mismatch;
  }
  if (header.score_unit != static_cast<std::uint16_t>(PatternScoreUnit::disc_diff)) {
    return PatternWeightsLoadError::unsupported_score_unit;
  }
  if (header.score_unit != static_cast<std::uint16_t>(manifest.score_unit)) {
    return PatternWeightsLoadError::score_unit_mismatch;
  }
  if (header.score_scale != 1 || header.score_scale != manifest.score_scale) {
    return PatternWeightsLoadError::unsupported_score_scale;
  }
  if (header.phase_count != manifest.phase_count) {
    return PatternWeightsLoadError::phase_count_mismatch;
  }
  if (header.pattern_count != manifest.patterns.size()) {
    return PatternWeightsLoadError::pattern_count_mismatch;
  }
  if (header.reserved != kReserved) {
    return PatternWeightsLoadError::truncated;
  }
  if (header.weight_count != expected_weight_count) {
    return PatternWeightsLoadError::invalid_weight_count;
  }
  return PatternWeightsLoadError::none;
}

} // namespace

PatternWeightsLoadResult load_pattern_weights(const PatternManifest& manifest,
                                              std::span<const std::uint8_t> artifact) noexcept {
  std::uint32_t phase_stride = 0;
  std::uint32_t expected_weight_count = 0;
  const PatternWeightsLoadError manifest_error =
      validate_manifest(manifest, &phase_stride, &expected_weight_count);
  if (manifest_error != PatternWeightsLoadError::none) {
    return fail(manifest_error);
  }

  if (artifact.size() < kMagic.size() + sizeof(Header) + sizeof(std::uint32_t)) {
    return fail(PatternWeightsLoadError::truncated);
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), artifact.begin())) {
    return fail(PatternWeightsLoadError::bad_magic);
  }

  Reader reader{.bytes = artifact};
  reader.offset = kMagic.size();
  const std::optional<Header> header = read_header(&reader);
  if (!header.has_value()) {
    return fail(PatternWeightsLoadError::truncated);
  }

  const PatternWeightsLoadError header_error =
      validate_header(*header, manifest, expected_weight_count);
  if (header_error != PatternWeightsLoadError::none) {
    return fail(header_error);
  }

  const std::optional<std::span<const std::uint8_t>> pattern_set_id_bytes =
      reader.read_bytes(header->pattern_set_id_length);
  if (!pattern_set_id_bytes.has_value()) {
    return fail(PatternWeightsLoadError::truncated);
  }
  const std::string pattern_set_id(pattern_set_id_bytes->begin(), pattern_set_id_bytes->end());
  if (pattern_set_id != manifest.pattern_set_id) {
    return fail(PatternWeightsLoadError::pattern_set_id_mismatch);
  }

  std::vector<search::Score> weights;
  weights.reserve(header->weight_count);
  for (std::uint32_t index = 0; index < header->weight_count; ++index) {
    const std::optional<std::int32_t> weight = reader.read_i32();
    if (!weight.has_value()) {
      return fail(PatternWeightsLoadError::truncated);
    }
    weights.push_back(*weight);
  }

  const std::optional<std::uint32_t> expected_checksum = reader.read_u32();
  if (!expected_checksum.has_value() || reader.offset != artifact.size()) {
    return fail(PatternWeightsLoadError::truncated);
  }
  const std::uint32_t actual_checksum =
      crc32(artifact.first(artifact.size() - sizeof(std::uint32_t)));
  if (*expected_checksum != actual_checksum) {
    return fail(PatternWeightsLoadError::checksum_mismatch);
  }

  return PatternWeightsLoadResult{
      .weights =
          PatternWeights{
              .manifest = manifest,
              .weights = std::move(weights),
              .phase_stride = phase_stride,
          },
      .error = PatternWeightsLoadError::none,
  };
}

} // namespace vibe_othello::evaluation
