#include "search_options_internal.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace vibe_othello::search::internal {
namespace {

constexpr std::uint64_t kFnvOffset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;

void mix_byte(std::uint8_t value, std::uint64_t* hash) noexcept {
  *hash ^= value;
  *hash *= kFnvPrime;
}

template <typename Integer> void mix_integer(Integer value, std::uint64_t* hash) noexcept {
  using Unsigned = std::make_unsigned_t<Integer>;
  auto bits = static_cast<std::uint64_t>(static_cast<Unsigned>(value));
  for (std::size_t index = 0; index < sizeof(Unsigned); ++index) {
    mix_byte(static_cast<std::uint8_t>(bits & 0xffU), hash);
    bits >>= 8U;
  }
}

void mix_double(double value, std::uint64_t* hash) noexcept {
  mix_integer(std::bit_cast<std::uint64_t>(value), hash);
}

void mix_string(std::string_view value, std::uint64_t* hash) noexcept {
  mix_integer(static_cast<std::uint64_t>(value.size()), hash);
  for (const char character : value) {
    mix_byte(static_cast<std::uint8_t>(character), hash);
  }
}

bool valid_sha256(std::string_view checksum) noexcept {
  if (checksum.size() != 64) {
    return false;
  }
  for (const char character : checksum) {
    if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) {
      return false;
    }
  }
  return true;
}

bool valid_profile(const ProbCutOptionsV1& options, std::uint64_t* fingerprint) noexcept {
  const ProbCutCalibrationProfileV1* profile = options.calibration_profile;
  if (profile == nullptr || profile->schema_version != kProbCutCalibrationProfileSchemaVersion ||
      profile->profile_id.empty() || profile->profile_id != options.calibration_profile_id ||
      !valid_sha256(profile->source_calibration_report_checksum_sha256) ||
      profile->evaluator_family.empty() || profile->artifact_family.empty() ||
      profile->node_class != ProbCutNodeClassV1::non_pv_scout_beta_only ||
      profile->entries.empty()) {
    return false;
  }

  const Depth profile_deep_depth = profile->entries.front().deep_depth;
  const Depth profile_shallow_depth = profile->entries.front().shallow_depth;
  for (std::size_t index = 0; index < profile->entries.size(); ++index) {
    const ProbCutCalibrationEntryV1& entry = profile->entries[index];
    if (entry.phase > 12 || entry.deep_depth <= 0 || entry.shallow_depth <= 0 ||
        entry.deep_depth <= entry.shallow_depth || entry.deep_depth != profile_deep_depth ||
        entry.shallow_depth != profile_shallow_depth || !std::isfinite(entry.regression_slope) ||
        entry.regression_slope <= 0.0 || !std::isfinite(entry.intercept) ||
        !std::isfinite(entry.residual_sigma) || entry.residual_sigma < 0.0 ||
        !std::isfinite(entry.confidence_multiplier) || entry.confidence_multiplier <= 0.0 ||
        entry.minimum_shallow_score > entry.maximum_shallow_score ||
        entry.minimum_beta > entry.maximum_beta || entry.minimum_shallow_score <= kScoreLoss ||
        entry.maximum_shallow_score >= kScoreWin || entry.minimum_beta <= kScoreLoss ||
        entry.maximum_beta >= kScoreWin) {
      return false;
    }
    for (std::size_t previous = 0; previous < index; ++previous) {
      const ProbCutCalibrationEntryV1& other = profile->entries[previous];
      if (other.phase == entry.phase && other.deep_depth == entry.deep_depth &&
          other.shallow_depth == entry.shallow_depth) {
        return false;
      }
    }
  }

  std::uint64_t hash = kFnvOffset;
  mix_string("probcut-calibration-profile-v1", &hash);
  mix_integer(profile->schema_version, &hash);
  mix_string(profile->profile_id, &hash);
  mix_string(profile->source_calibration_report_checksum_sha256, &hash);
  mix_string(profile->evaluator_family, &hash);
  mix_string(profile->artifact_family, &hash);
  mix_integer(static_cast<std::uint8_t>(profile->node_class), &hash);
  mix_integer(static_cast<std::uint64_t>(profile->entries.size()), &hash);
  for (const ProbCutCalibrationEntryV1& entry : profile->entries) {
    mix_integer(entry.phase, &hash);
    mix_integer(entry.deep_depth, &hash);
    mix_integer(entry.shallow_depth, &hash);
    mix_double(entry.regression_slope, &hash);
    mix_double(entry.intercept, &hash);
    mix_double(entry.residual_sigma, &hash);
    mix_double(entry.confidence_multiplier, &hash);
    mix_integer(entry.minimum_shallow_score, &hash);
    mix_integer(entry.maximum_shallow_score, &hash);
    mix_integer(entry.minimum_beta, &hash);
    mix_integer(entry.maximum_beta, &hash);
  }
  *fingerprint = hash;
  return true;
}

bool valid_probcut_options(const ProbCutOptionsV1& options, bool use_legacy_search_kernel,
                           std::uint64_t* profile_fingerprint) noexcept {
  return options.use_probcut && !use_legacy_search_kernel && options.minimum_depth > 0 &&
         options.shallow_depth_reduction > 0 && std::isfinite(options.confidence_multiplier) &&
         options.confidence_multiplier >= 0.0 && options.minimum_margin >= 0 &&
         options.maximum_margin >= options.minimum_margin && options.maximum_margin > 0 &&
         options.non_pv_only && options.beta_only && options.disable_near_exact &&
         valid_profile(options, profile_fingerprint);
}

} // namespace

ResolvedSearchOptions normalize_search_options(SearchOptions options) noexcept {
  // Compatibility rule for the staged migration: legacy flat fields remain
  // sticky, while typed config can express the same behavior for new callers.
  ResolvedSearchOptions resolved{
      .mode = options.mode,
      .midgame =
          MidgameSearchOptions{
              .use_pvs = options.use_pvs || options.midgame.use_pvs,
              .use_aspiration = options.use_aspiration || options.midgame.use_aspiration,
              .use_iid = options.use_iid || options.midgame.use_iid,
              .use_midgame_tt = options.use_midgame_tt || options.midgame.use_midgame_tt,
              .pass_consumes_depth =
                  options.pass_consumes_depth && options.midgame.pass_consumes_depth,
          },
      .ordering =
          MoveOrderingOptions{
              .use_tt_best_move_ordering =
                  options.use_tt_best_move_ordering || options.ordering.use_tt_best_move_ordering,
              .use_history = options.use_history || options.ordering.use_history,
              .use_killers = options.use_killers || options.ordering.use_killers,
              .use_endgame_parity_ordering = options.use_endgame_parity_ordering &&
                                             options.ordering.use_endgame_parity_ordering,
          },
      .endgame =
          EndgameSearchOptions{
              .exact_endgame = options.exact_endgame || options.endgame.exact_endgame,
              .use_endgame_tt = options.use_endgame_tt || options.endgame.use_endgame_tt,
              .endgame_exact_empties = options.endgame_exact_empties != 0
                                           ? options.endgame_exact_empties
                                           : options.endgame.endgame_exact_empties,
              .endgame_wld_empties = options.endgame_wld_empties != 0
                                         ? options.endgame_wld_empties
                                         : options.endgame.endgame_wld_empties,
          },
      .reporting =
          SearchReportingOptions{
              .multi_pv = options.multi_pv != 0 ? options.multi_pv : options.reporting.multi_pv,
          },
      .experimental =
          ExperimentalSearchOptions{
              .probcut = options.probcut || options.experimental.probcut,
              .use_pv_table = options.use_pv_table || options.experimental.use_pv_table,
              .use_parallel = options.use_parallel || options.experimental.use_parallel,
              .selectivity_level = options.selectivity_level != 0
                                       ? options.selectivity_level
                                       : options.experimental.selectivity_level,
              .use_legacy_search_kernel =
                  options.use_legacy_search_kernel || options.experimental.use_legacy_search_kernel,
          },
      .selective = options.selective,
  };

  std::uint64_t profile_fingerprint = 0;
  if (valid_probcut_options(options.probcut_options, resolved.experimental.use_legacy_search_kernel,
                            &profile_fingerprint)) {
    resolved.probcut = options.probcut_options;
    resolved.probcut_profile_semantic_fingerprint = profile_fingerprint;
  }
  return resolved;
}

} // namespace vibe_othello::search::internal
