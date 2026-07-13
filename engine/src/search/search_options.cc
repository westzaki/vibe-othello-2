#include "search_options_internal.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace vibe_othello::search {
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

bool same_scheduler_domain(const ProbCutSchedulerEvidenceV1& evidence,
                           const ProbCutCalibrationEntryV1& entry) noexcept {
  return evidence.phase == entry.phase && evidence.search_mode == entry.search_mode &&
         evidence.minimum_empties == entry.minimum_empties &&
         evidence.maximum_empties == entry.maximum_empties &&
         evidence.deep_depth == entry.deep_depth &&
         evidence.exact_handoff_enabled == entry.exact_handoff_enabled &&
         evidence.exact_handoff_threshold == entry.exact_handoff_threshold &&
         evidence.minimum_exact_handoff_distance == entry.minimum_exact_handoff_distance &&
         evidence.maximum_exact_handoff_distance == entry.maximum_exact_handoff_distance;
}

bool same_scheduler_evidence_key(const ProbCutSchedulerEvidenceV1& lhs,
                                 const ProbCutSchedulerEvidenceV1& rhs) noexcept {
  return lhs.pair_prefix_length == rhs.pair_prefix_length &&
         lhs.maximum_probes_per_node == rhs.maximum_probes_per_node && lhs.phase == rhs.phase &&
         lhs.search_mode == rhs.search_mode && lhs.minimum_empties == rhs.minimum_empties &&
         lhs.maximum_empties == rhs.maximum_empties && lhs.deep_depth == rhs.deep_depth &&
         lhs.exact_handoff_enabled == rhs.exact_handoff_enabled &&
         lhs.exact_handoff_threshold == rhs.exact_handoff_threshold &&
         lhs.minimum_exact_handoff_distance == rhs.minimum_exact_handoff_distance &&
         lhs.maximum_exact_handoff_distance == rhs.maximum_exact_handoff_distance;
}

bool pair_is_in_prefix(const ProbCutCalibrationProfileV1& profile,
                       const ProbCutCalibrationEntryV1& entry, std::size_t prefix_length) noexcept {
  const ProbCutDepthPairV1 pair{
      .deep_depth = entry.deep_depth,
      .shallow_depth = entry.shallow_depth,
  };
  return std::find(profile.validated_pair_order.begin(),
                   profile.validated_pair_order.begin() +
                       static_cast<std::ptrdiff_t>(prefix_length),
                   pair) !=
         profile.validated_pair_order.begin() + static_cast<std::ptrdiff_t>(prefix_length);
}

bool scheduler_evidence_covers_entry(const ProbCutCalibrationProfileV1& profile,
                                     std::size_t prefix_length, std::uint8_t maximum_probes,
                                     const ProbCutCalibrationEntryV1& entry) noexcept {
  return std::any_of(
      profile.scheduler_evidence.begin(), profile.scheduler_evidence.end(),
      [prefix_length, maximum_probes, &entry](const ProbCutSchedulerEvidenceV1& evidence) {
        return evidence.pair_prefix_length == prefix_length &&
               evidence.maximum_probes_per_node == maximum_probes &&
               same_scheduler_domain(evidence, entry);
      });
}

bool scheduler_evidence_covers_configuration(const ProbCutCalibrationProfileV1& profile,
                                             std::size_t prefix_length,
                                             std::uint8_t maximum_probes) noexcept {
  bool enables_a_domain = false;
  for (const ProbCutCalibrationEntryV1& entry : profile.entries) {
    if (!pair_is_in_prefix(profile, entry, prefix_length)) {
      continue;
    }
    enables_a_domain = true;
    if (!scheduler_evidence_covers_entry(profile, prefix_length, maximum_probes, entry)) {
      return false;
    }
  }
  return enables_a_domain;
}

bool valid_profile(const ProbCutOptionsV1& options, std::uint64_t* fingerprint) noexcept {
  const ProbCutCalibrationProfileV1* profile = options.calibration_profile;
  if (profile == nullptr || profile->schema_version != kProbCutCalibrationProfileSchemaVersion ||
      profile->profile_id.empty() || profile->profile_id != options.calibration_profile_id ||
      !valid_sha256(profile->source_calibration_report_checksum_sha256) ||
      !valid_sha256(profile->joint_holdout_checksum_sha256) || profile->evaluator_family.empty() ||
      profile->artifact_family.empty() ||
      profile->node_class != ProbCutNodeClassV1::non_pv_scout_beta_only ||
      profile->validated_maximum_probes_per_node == 0 || profile->joint_cut_candidate_count == 0 ||
      profile->joint_false_cut_count > profile->joint_cut_candidate_count ||
      !std::isfinite(profile->joint_false_cut_rate_upper_bound) ||
      profile->joint_false_cut_rate_upper_bound < 0.0 ||
      profile->joint_false_cut_rate_upper_bound > 1.0 ||
      profile->joint_false_cut_rate_upper_bound <
          static_cast<double>(profile->joint_false_cut_count) /
              static_cast<double>(profile->joint_cut_candidate_count) ||
      profile->scheduler_evidence.empty() || profile->entries.empty()) {
    return false;
  }

  std::size_t unique_pair_count = 0;
  for (std::size_t index = 0; index < profile->entries.size(); ++index) {
    const ProbCutCalibrationEntryV1& entry = profile->entries[index];
    if (entry.phase > 12 ||
        static_cast<std::uint8_t>(entry.search_mode) >
            static_cast<std::uint8_t>(SearchMode::win_loss_draw) ||
        entry.minimum_empties > entry.maximum_empties || entry.maximum_empties > 60 ||
        entry.deep_depth <= 0 || entry.shallow_depth <= 0 ||
        entry.deep_depth <= entry.shallow_depth || entry.exact_handoff_threshold > 60 ||
        (entry.exact_handoff_enabled != (entry.exact_handoff_threshold != 0)) ||
        (!entry.exact_handoff_enabled && (entry.minimum_exact_handoff_distance != 0 ||
                                          entry.maximum_exact_handoff_distance != 0)) ||
        entry.minimum_exact_handoff_distance > entry.maximum_exact_handoff_distance ||
        entry.maximum_exact_handoff_distance > 60 || !std::isfinite(entry.regression_slope) ||
        entry.regression_slope <= 0.0 || !std::isfinite(entry.intercept) ||
        !std::isfinite(entry.residual_sigma) || entry.residual_sigma < 0.0 ||
        !std::isfinite(entry.confidence_multiplier) || entry.confidence_multiplier <= 0.0 ||
        entry.minimum_shallow_score > entry.maximum_shallow_score ||
        entry.minimum_beta > entry.maximum_beta || entry.minimum_shallow_score <= kScoreLoss ||
        entry.maximum_shallow_score >= kScoreWin || entry.minimum_beta <= kScoreLoss ||
        entry.maximum_beta >= kScoreWin) {
      return false;
    }
    const ProbCutDepthPairV1 pair{
        .deep_depth = entry.deep_depth,
        .shallow_depth = entry.shallow_depth,
    };
    const bool pair_seen = std::any_of(
        profile->entries.begin(), profile->entries.begin() + static_cast<std::ptrdiff_t>(index),
        [pair](const ProbCutCalibrationEntryV1& previous) {
          return previous.deep_depth == pair.deep_depth &&
                 previous.shallow_depth == pair.shallow_depth;
        });
    if (!pair_seen) {
      ++unique_pair_count;
    }
    for (std::size_t previous = 0; previous < index; ++previous) {
      const ProbCutCalibrationEntryV1& other = profile->entries[previous];
      const bool empties_overlap = other.minimum_empties <= entry.maximum_empties &&
                                   entry.minimum_empties <= other.maximum_empties;
      const bool proximity_overlap =
          other.minimum_exact_handoff_distance <= entry.maximum_exact_handoff_distance &&
          entry.minimum_exact_handoff_distance <= other.maximum_exact_handoff_distance;
      if (other.phase == entry.phase && other.search_mode == entry.search_mode &&
          other.deep_depth == entry.deep_depth && other.shallow_depth == entry.shallow_depth &&
          other.exact_handoff_enabled == entry.exact_handoff_enabled &&
          other.exact_handoff_threshold == entry.exact_handoff_threshold && empties_overlap &&
          proximity_overlap) {
        return false;
      }
    }
  }

  if (profile->validated_pair_order.empty() ||
      profile->validated_pair_order.size() != unique_pair_count ||
      profile->validated_maximum_probes_per_node > profile->validated_pair_order.size()) {
    return false;
  }
  for (std::size_t index = 0; index < profile->validated_pair_order.size(); ++index) {
    const ProbCutDepthPairV1 pair = profile->validated_pair_order[index];
    if (pair.deep_depth <= pair.shallow_depth || pair.shallow_depth <= 0 ||
        std::find(profile->validated_pair_order.begin(),
                  profile->validated_pair_order.begin() + static_cast<std::ptrdiff_t>(index),
                  pair) !=
            profile->validated_pair_order.begin() + static_cast<std::ptrdiff_t>(index) ||
        std::none_of(profile->entries.begin(), profile->entries.end(),
                     [pair](const ProbCutCalibrationEntryV1& entry) {
                       return entry.deep_depth == pair.deep_depth &&
                              entry.shallow_depth == pair.shallow_depth;
                     })) {
      return false;
    }
  }

  for (std::size_t index = 0; index < profile->scheduler_evidence.size(); ++index) {
    const ProbCutSchedulerEvidenceV1& evidence = profile->scheduler_evidence[index];
    if (evidence.pair_prefix_length == 0 ||
        evidence.pair_prefix_length > profile->validated_pair_order.size() ||
        evidence.maximum_probes_per_node == 0 ||
        evidence.maximum_probes_per_node > profile->validated_maximum_probes_per_node ||
        evidence.maximum_probes_per_node > evidence.pair_prefix_length || evidence.phase > 12 ||
        static_cast<std::uint8_t>(evidence.search_mode) >
            static_cast<std::uint8_t>(SearchMode::win_loss_draw) ||
        evidence.minimum_empties > evidence.maximum_empties || evidence.maximum_empties > 60 ||
        evidence.deep_depth <= 0 || evidence.exact_handoff_threshold > 60 ||
        evidence.exact_handoff_enabled != (evidence.exact_handoff_threshold != 0) ||
        (!evidence.exact_handoff_enabled && (evidence.minimum_exact_handoff_distance != 0 ||
                                             evidence.maximum_exact_handoff_distance != 0)) ||
        evidence.minimum_exact_handoff_distance > evidence.maximum_exact_handoff_distance ||
        evidence.maximum_exact_handoff_distance > 60 || evidence.holdout_node_count == 0 ||
        evidence.cut_candidate_count == 0 ||
        evidence.false_cut_count > evidence.cut_candidate_count ||
        !std::isfinite(evidence.false_cut_rate_upper_bound) ||
        evidence.false_cut_rate_upper_bound < 0.0 || evidence.false_cut_rate_upper_bound > 1.0 ||
        evidence.false_cut_rate_upper_bound <
            static_cast<double>(evidence.false_cut_count) /
                static_cast<double>(evidence.cut_candidate_count)) {
      return false;
    }
    const bool matches_enabled_entry =
        std::any_of(profile->entries.begin(), profile->entries.end(),
                    [&profile, &evidence](const ProbCutCalibrationEntryV1& entry) {
                      return pair_is_in_prefix(*profile, entry, evidence.pair_prefix_length) &&
                             same_scheduler_domain(evidence, entry);
                    });
    if (!matches_enabled_entry ||
        std::any_of(profile->scheduler_evidence.begin(),
                    profile->scheduler_evidence.begin() + static_cast<std::ptrdiff_t>(index),
                    [&evidence](const ProbCutSchedulerEvidenceV1& previous) {
                      return same_scheduler_evidence_key(previous, evidence);
                    })) {
      return false;
    }
  }

  if (!probcut_configuration_is_reviewed(*profile, profile->validated_pair_order,
                                         profile->validated_maximum_probes_per_node)) {
    return false;
  }

  std::uint64_t hash = kFnvOffset;
  mix_string("probcut-calibration-profile-v3", &hash);
  mix_integer(profile->schema_version, &hash);
  mix_string(profile->profile_id, &hash);
  mix_string(profile->source_calibration_report_checksum_sha256, &hash);
  mix_string(profile->joint_holdout_checksum_sha256, &hash);
  mix_string(profile->evaluator_family, &hash);
  mix_string(profile->artifact_family, &hash);
  mix_integer(static_cast<std::uint8_t>(profile->node_class), &hash);
  mix_integer(profile->validated_maximum_probes_per_node, &hash);
  mix_integer(profile->joint_false_cut_count, &hash);
  mix_integer(profile->joint_cut_candidate_count, &hash);
  mix_double(profile->joint_false_cut_rate_upper_bound, &hash);
  mix_integer(static_cast<std::uint64_t>(profile->validated_pair_order.size()), &hash);
  for (const ProbCutDepthPairV1 pair : profile->validated_pair_order) {
    mix_integer(pair.deep_depth, &hash);
    mix_integer(pair.shallow_depth, &hash);
  }
  mix_integer(static_cast<std::uint64_t>(profile->scheduler_evidence.size()), &hash);
  for (const ProbCutSchedulerEvidenceV1& evidence : profile->scheduler_evidence) {
    mix_integer(evidence.pair_prefix_length, &hash);
    mix_integer(evidence.maximum_probes_per_node, &hash);
    mix_integer(evidence.phase, &hash);
    mix_integer(static_cast<std::uint8_t>(evidence.search_mode), &hash);
    mix_integer(evidence.minimum_empties, &hash);
    mix_integer(evidence.maximum_empties, &hash);
    mix_integer(evidence.deep_depth, &hash);
    mix_integer(static_cast<std::uint8_t>(evidence.exact_handoff_enabled), &hash);
    mix_integer(evidence.exact_handoff_threshold, &hash);
    mix_integer(evidence.minimum_exact_handoff_distance, &hash);
    mix_integer(evidence.maximum_exact_handoff_distance, &hash);
    mix_integer(evidence.holdout_node_count, &hash);
    mix_integer(evidence.false_cut_count, &hash);
    mix_integer(evidence.cut_candidate_count, &hash);
    mix_double(evidence.false_cut_rate_upper_bound, &hash);
  }
  mix_integer(static_cast<std::uint64_t>(profile->entries.size()), &hash);
  for (const ProbCutCalibrationEntryV1& entry : profile->entries) {
    mix_integer(entry.phase, &hash);
    mix_integer(static_cast<std::uint8_t>(entry.search_mode), &hash);
    mix_integer(entry.minimum_empties, &hash);
    mix_integer(entry.maximum_empties, &hash);
    mix_integer(entry.deep_depth, &hash);
    mix_integer(entry.shallow_depth, &hash);
    mix_integer(static_cast<std::uint8_t>(entry.exact_handoff_enabled), &hash);
    mix_integer(entry.exact_handoff_threshold, &hash);
    mix_integer(entry.minimum_exact_handoff_distance, &hash);
    mix_integer(entry.maximum_exact_handoff_distance, &hash);
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
  if (!options.use_probcut || use_legacy_search_kernel || options.minimum_depth <= 0 ||
      options.maximum_probes_per_node == 0 || !options.stop_after_first_success ||
      !std::isfinite(options.confidence_multiplier) || options.confidence_multiplier < 0.0 ||
      !std::isfinite(options.minimum_confidence) || options.minimum_confidence < 0.0 ||
      options.minimum_margin < 0 || options.maximum_margin < options.minimum_margin ||
      options.maximum_margin <= 0 || !std::isfinite(options.maximum_shallow_overhead_ratio) ||
      options.maximum_shallow_overhead_ratio < 0.0 || options.enabled_phase_mask == 0 ||
      (options.enabled_phase_mask & ~kAllProbCutPhasesMask) != 0 || !options.non_pv_only ||
      !options.beta_only || !options.disable_near_exact ||
      options.near_exact_disable_empties > 60 || options.evaluator_family.empty() ||
      options.artifact_family.empty() || !valid_profile(options, profile_fingerprint)) {
    return false;
  }

  const ProbCutCalibrationProfileV1& profile = *options.calibration_profile;
  if (options.evaluator_family != profile.evaluator_family ||
      options.artifact_family != profile.artifact_family) {
    return false;
  }

  const std::span<const ProbCutDepthPairV1> preference = options.ordered_depth_pairs.empty()
                                                             ? profile.validated_pair_order
                                                             : options.ordered_depth_pairs;
  if (options.maximum_probes_per_node > profile.validated_maximum_probes_per_node) {
    return false;
  }
  if (preference.empty() || preference.size() > profile.validated_pair_order.size()) {
    return false;
  }
  for (std::size_t index = 0; index < preference.size(); ++index) {
    if (preference[index] != profile.validated_pair_order[index]) {
      return false;
    }
  }
  if (!probcut_configuration_is_reviewed(profile, preference, options.maximum_probes_per_node)) {
    return false;
  }

  std::uint64_t hash = *profile_fingerprint;
  mix_string(options.evaluator_family, &hash);
  mix_string(options.artifact_family, &hash);
  mix_integer(static_cast<std::uint64_t>(preference.size()), &hash);
  mix_integer(options.maximum_probes_per_node, &hash);
  for (const ProbCutDepthPairV1 pair : preference) {
    mix_integer(pair.deep_depth, &hash);
    mix_integer(pair.shallow_depth, &hash);
  }
  *profile_fingerprint = hash;
  return true;
}

} // namespace

bool probcut_configuration_is_reviewed(const ProbCutCalibrationProfileV1& profile,
                                       std::span<const ProbCutDepthPairV1> ordered_depth_pairs,
                                       std::uint8_t maximum_probes_per_node) noexcept {
  if (ordered_depth_pairs.empty() ||
      ordered_depth_pairs.size() > profile.validated_pair_order.size() ||
      maximum_probes_per_node == 0 || maximum_probes_per_node > ordered_depth_pairs.size() ||
      maximum_probes_per_node > profile.validated_maximum_probes_per_node) {
    return false;
  }
  for (std::size_t index = 0; index < ordered_depth_pairs.size(); ++index) {
    if (ordered_depth_pairs[index] != profile.validated_pair_order[index]) {
      return false;
    }
  }
  return scheduler_evidence_covers_configuration(profile, ordered_depth_pairs.size(),
                                                 maximum_probes_per_node);
}

ResolvedProbCutConfigurationV1
resolve_probcut_configuration(ProbCutOptionsV1 options, bool use_legacy_search_kernel) noexcept {
  std::uint64_t profile_fingerprint = 0;
  if (!valid_probcut_options(options, use_legacy_search_kernel, &profile_fingerprint)) {
    return {};
  }
  return ResolvedProbCutConfigurationV1{
      .options = options,
      .semantic_fingerprint = profile_fingerprint,
  };
}

internal::ResolvedSearchOptions internal::normalize_search_options(SearchOptions options) noexcept {
  // Compatibility rule for the staged migration: legacy flat fields remain
  // sticky, while typed config can express the same behavior for new callers.
  internal::ResolvedSearchOptions resolved{
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

  const ResolvedProbCutConfigurationV1 probcut_resolution = resolve_probcut_configuration(
      options.probcut_options, resolved.experimental.use_legacy_search_kernel);
  resolved.probcut = probcut_resolution.options;
  resolved.probcut_profile_semantic_fingerprint = probcut_resolution.semantic_fingerprint;
  return resolved;
}

} // namespace vibe_othello::search
