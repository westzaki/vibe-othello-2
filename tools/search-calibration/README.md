# Selective Search Shadow Calibration

This directory owns the local-only MPC/ProbCut calibration workflow. The engine
can collect reduced-depth versus deep-search observations. Runtime search also
has a separate conservative, typed, caller-supplied ProbCut path, but the
repository contains no production profile or coefficient and enables nothing
by default. A generated analyzer report is not itself runtime authorization.

## Collection contract

Configure `SearchOptions::selective` with `SelectiveSearchOptionsV1`. Collection
is active only when `enable_shadow_calibration` is true, the integer
`sample_rate` is positive, `max_samples_per_search` is positive,
`ordered_depth_pairs` is a non-empty unique list of valid deep/shallow pairs,
and a caller-owned `ShadowCalibrationSink` is present.
All four identity strings must also be non-empty.
The rate scale is `kShadowCalibrationSampleRateScale == 1'000'000`.

The sink receives a const reference to a `ShadowCalibrationSample` whose string
fields own their values. A local collector may copy it for retention or
serialize one object per line. Serialize enums and moves as:

- `search_role`: `pv`, `non_pv_scout`, or `other`
- `node_type`: `pv`, `cut`, or `all`
- `official_deep_bound`, `shallow_verification_bound`, and
  `deep_verification_bound`: `upper`, `exact`, or `lower`
- `actual_official_deep_result`: `fail_low`, `exact`, or `fail_high`
- moves: `a1` through `h8`, `pass`, or JSON `null`
- `canonical_position_hash`: 16 lowercase hexadecimal characters

JSON field names match the C++ member names exactly. Every JSON/JSONL sample
must contain all schema-v5 fields validated by `analyze_shadow_samples.py`.
`repo_sha`, `search_config_id`, `evaluator_id`, and `artifact_id` must identify
the actual run; `repo_sha` is a 7-64 character lowercase hexadecimal Git SHA.
Use `artifact_id: "none"` only for an evaluator with no
artifact. Do not insert a path, hostname, or user name into identity fields.

The engine derives `collection_config_id` from the effective sample rate,
sample cap, the entire ordered depth-pair list, PV/pass/near-exact inclusion
flags, and sample schema version. Pair order and subset therefore belong to one
population identity. This ID is stored in every sample and mixed
into `search_identity`, so changing collection policy changes the population
identity without relying on caller metadata. The analyzer rejects mixed
collection IDs.

The canonical position identity is an FNV-1a hash of the lexicographically
smallest of the eight side-to-move-relative board symmetries. It is compact
position identity metadata, not a serialized board or a collision-proof data
store key. Phase v1 uses
`min(12, floor(max(0, occupied_count - 4) * 13 / 60))`; it matches the current
13-phase artifact layout.

At official node entry, `search_role` is assigned without using the result:
`pv` for PV/full-window work, `non_pv_scout` for an explicit PVS scout
candidate, and `other` otherwise. This is the ProbCut calibration population.
After the official search completes at a sampled node, collection runs one
isolated deep verification over `[kScoreLoss, kScoreWin]`, then one isolated
shallow verification for every configured pair whose deep depth matches that
node. The whole same-deep pair set is reserved against the sample cap, and no
partial node population is emitted if any verification stops. Schema v5 stores
`collection_pair_index/count` and `same_deep_pair_index/count` so the analyzer
can enforce completeness. It also records `search_mode`,
`exact_handoff_enabled`, `exact_handoff_threshold`, and the derived
`exact_handoff_distance`; these observations define the calibrated domain.

- `official_alpha`, `official_beta`, `official_deep_score`, and
  `official_deep_bound` retain the official result for `pv`/`cut`/`all`
  classification and window diagnostics.
- `shallow_verification_score` / `bound` and
  `deep_verification_score` / `bound` are the value-regression pair.
- verification best moves and agreement are also independent of the official
  result PV.

This separation is required for non-PV nodes: their one-point integer
null-window cannot produce an exact official score, while their full-window
verification searches can. `hypothetical_cut_high` and `_low` compare the
shallow verification score with the official window. False-cut candidates use
the deep verification score.

Verification searches use isolated TT and ordering state and never contribute to
official `SearchStats`, `SearchResult::nodes`, root move nodes, or node limits.
They explicitly disable both ProbCut and nested MPC collection. Conversely,
ProbCut shallow searches temporarily detach MPC collection, so neither feature
can contaminate the other's calibration tree.
The dedicated counters are in `SearchResult::shadow_calibration`, including
separate shallow and deep-verification node counts. Fixed-time deadline
accounting excludes verification work, but local calibration should still use
fixed-depth or fixed-node runs: an external stop can naturally arrive while any
diagnostic callback is running. Deep verification is intentionally expensive;
control it with sample rate and the per-search cap.

Normal C++ defaults, runtime presets, WASM `easy`/`normal`/`hard` presets,
default benchmark runs, and Arena runs remain disabled. The search benchmark
can opt in only when an external reviewed profile TSV is explicitly supplied.
Long collection runs belong under a local measurement root outside the
repository, for example:

```sh
export VIBE_OTHELLO_MEASUREMENTS="${VIBE_OTHELLO_MEASUREMENTS:-$HOME/vibe-othello-local/measurements}"
mkdir -p "$VIBE_OTHELLO_MEASUREMENTS/mpc-shadow"
```

Do not commit generated sample JSON/JSONL, fitted reports, or Markdown
summaries.

## Analyzer

Analyze any mix of JSON/JSONL files or directories:

```sh
python3 tools/search-calibration/analyze_shadow_samples.py \
  "$VIBE_OTHELLO_MEASUREMENTS/mpc-shadow/samples" \
  --json-output "$VIBE_OTHELLO_MEASUREMENTS/mpc-shadow/report.json" \
  --markdown-output "$VIBE_OTHELLO_MEASUREMENTS/mpc-shadow/summary.md"
```

The optional `--minimum-exact-pairs` controls the per-group recommendation
threshold and defaults to 30.

Input schema is rejected strictly for missing fields, invalid types/ranges,
inconsistent node flags, move agreement, window classification, hypothetical
cuts, or false-cut candidates. Unsupported schema versions and malformed JSON
also fail with exit code 2. By default a report must contain exactly one
`(repo_sha, search_config_id, evaluator_id, artifact_id)` provenance tuple and
one `collection_config_id`; mixed input is rejected instead of silently fitted.
The JSON report retains both inventories and their sample counts. Empty inputs
produce a valid deterministic report with zero samples and groups.

The analyzer's adoptable groups are keyed by phase, deep depth, shallow depth,
result-independent search role, search mode, exact-handoff enablement and
threshold, four-empties bucket, and four-distance bucket. Each group carries
the exact observed empties and distance inventory. Only `non_pv_scout` is recommendation
eligible, and its fit includes both eventual fail-high and fail-low outcomes.
Separate post-result `pv`/`cut`/`all` groups remain in
`result_node_type_groups` for diagnostics only. Each group reports:

- total sample count, exact-pair count, and bound-observation count
- `deep = intercept + slope * shallow` regression with explicit
  `linear_regression.intercept` and `.slope` keys, using only rows where both
  `shallow_verification_bound` and `deep_verification_bound` are `exact`
- residual mean, population standard deviation, and residual percentiles
- MAE and RMSE for fitted regression residuals
- best-move agreement
- hypothetical high/low cut counts and observed false-cut estimate
- 90%, 95%, and 99% empirical residual-margin cut coverage
- `ceil(p99 absolute fitted residual)` as a conservative diagnostic margin
  only when the minimum exact-pair threshold is met

Upper/lower verification rows are censored observations and never enter OLS or
residual estimation. An upper/lower `official_deep_bound` does not exclude an
otherwise exact verification pair; it remains the basis for non-PV node
classification and window diagnostics. A one-row or otherwise under-threshold group reports
`insufficient_samples: true`, `recommendation_eligible: false`, and a null
margin. A fit also requires at least two exact pairs with non-zero shallow-score
variance.

The input checksum is SHA-256 over sorted per-file content digests, so it does
not include local paths. Samples are sorted canonically before floating-point
aggregation, reports contain no timestamp, and JSON keys/group ordering are
stable. The recommended margin is an in-sample, report-only diagnostic. Before
runtime adoption, coefficients and margins require validation against a
separate seed or holdout campaign and a separate measured search change.

The report also retains the collection pair order and per-node
`scheduler_observations`. A report is rejected when a sampled node is missing
one of its same-deep pairs or maps one collection index to multiple pairs.
CTest includes a synthetic same-deep two-pair pipeline test through analyzer,
converter, and the native Arena loader. That fixture checks the evidence chain;
it is not calibration or strength evidence.

## Runtime profile adoption

Do not copy a regression group into runtime merely because
`recommendation_eligible` is true. Review the source report and holdout result,
then create a versioned `ProbCutCalibrationProfileV1` that records:

- profile ID and schema version
- SHA-256 of the exact reviewed analyzer report file
- SHA-256 of an independent joint holdout report
- evaluator and artifact families
- exact node class `non_pv_scout_beta_only`
- `validated_pair_order` and `validated_maximum_probes_per_node`
- full-scheduler joint false-cut count, cut-candidate count, and Wilson 95% upper bound
- per-prefix/per-probe/per-exact-domain holdout node count, cut-candidate count,
  false-cut count, and Wilson 95% upper bound
- exact phase/search mode, inclusive empties range, and exact-handoff
  enabled/threshold/distance domain
- regression slope/intercept and residual sigma
- audited confidence multiplier
- inclusive shallow-score and beta validity ranges

Runtime rejects any other or missing node class, incomplete/duplicate pair
preferences, overlapping domains, invalid identity/checksum data, non-finite
coefficients, and every missing complete match. It never fills a missing group
from an adjacent phase, empties range, depth, handoff proximity, evaluator, or
artifact. Arena binds evaluator family to the loaded artifact `pattern_set_id`
and artifact family to `artifact_id`; benchmark callers must provide the same
actual identity.
Runtime `ordered_depth_pairs` may only be an identical prefix of
`validated_pair_order`, and `maximum_probes_per_node` may not exceed the
reviewed maximum. That structural match is not sufficient: the exact prefix
length and probe count must have passing evidence for every profile domain
enabled by that prefix. Reordering, suffix-only selection, an unreviewed extra
probe, or a prefix/domain combination missing from the evidence inventory
disables MPC during option normalization.

Do not manually translate analyzer coefficient names. Create a local reviewed
adoption specification containing only the explicit decisions that the report
cannot supply (profile ID, confidence multiplier, and audited validity ranges),
then run the checked converter:

```sh
python3 tools/search-calibration/convert_probcut_profile.py \
  "$VIBE_OTHELLO_MEASUREMENTS/mpc-shadow/report.json" \
  "$VIBE_OTHELLO_MEASUREMENTS/mpc-shadow/adoption.json" \
  --joint-holdout-report "$VIBE_OTHELLO_MEASUREMENTS/mpc-shadow/holdout-report.json" \
  --output "$VIBE_OTHELLO_MEASUREMENTS/mpc-shadow/reviewed-profile.tsv"
```

The adoption file uses `schema_version: "probcut-profile-adoption-v3"`, exact
report `evaluator_id`/`artifact_id` values as `evaluator_family` and
`artifact_family`, `node_class: "non_pv_scout_beta_only"`, an explicit unique
`validated_pair_order` array, `validated_maximum_probes_per_node`,
`minimum_joint_cut_candidates`, `maximum_joint_false_cut_rate_upper_bound`, and
an `entries` array. Each entry selects `phase`, `search_mode`, `deep_depth`, and
`shallow_depth`; declares `minimum_empties`, `maximum_empties`,
`exact_handoff_enabled`, `exact_handoff_threshold`,
`minimum_exact_handoff_distance`, and `maximum_exact_handoff_distance`; and
supplies `confidence_multiplier`, `minimum_shallow_score`,
`maximum_shallow_score`, `minimum_beta`, and `maximum_beta`. These domains are
review decisions constrained by the report: every integer in an adopted
empties/distance range must occur in that group's observed inventory. The
converter selects exactly one eligible
`non_pv_scout` group, maps `.slope` to `regression_slope` and `.intercept` to
`intercept`, copies residual sigma, verifies that the preference exactly matches
the collected pair order, and records both report SHA-256 values. Training and
holdout provenance/collection policy must match and their sampled-node
identities must be disjoint. The converter replays the runtime scheduler—pair
1, then pair 2 after a rejection, stopping at the first success—for every pair
prefix, probe cap, and exact profile domain. Only combinations meeting the
candidate minimum and reviewed false-cut upper bound are stored. The full
validated scheduler must pass every enabled domain or conversion fails; an
optional prefix that fails remains absent and cannot be selected at runtime.
Pair-local margins and a global aggregate alone cannot authorize a
Multi-ProbCut profile.

The one-sided integer condition is:

```text
predicted_deep = slope * shallow_score + intercept
k = max(profile_confidence_multiplier, option_confidence_multiplier, minimum_confidence)
margin = max(ceil(k * residual_sigma), minimum_margin)
cut-high only if floor(predicted_deep) - margin >= beta
```

If `margin > maximum_margin`, the candidate is rejected; the maximum is never
used to reduce a confidence margin. Scores and bounds near search sentinels are
also rejected. `shadow_verify=true` applies the same eligibility and shallow
search but does not cut; normal deep search classifies a candidate as false
when its result is below beta.

For local benchmark input, use a TSV outside the repository with this exact
header. Row order carries the reviewed first-occurrence pair preference; rows
within a pair are ordered by the converter's exact domain key:

```text
schema_version	profile_id	source_checksum_sha256	joint_holdout_checksum_sha256	evaluator_family	artifact_family	node_class	validated_maximum_probes_per_node	joint_false_cut_count	joint_cut_candidate_count	joint_false_cut_rate_upper_bound	scheduler_domain_evidence	phase	search_mode	minimum_empties	maximum_empties	deep_depth	shallow_depth	exact_handoff_enabled	exact_handoff_threshold	minimum_exact_handoff_distance	maximum_exact_handoff_distance	regression_slope	intercept	residual_sigma	confidence_multiplier	minimum_shallow_score	maximum_shallow_score	minimum_beta	maximum_beta
```

Profile schema v3 serializes each scheduler evidence record in the repeated
`scheduler_domain_evidence` field as colon-separated values, with records
separated by semicolons:

```text
prefix_length:maximum_probes:phase:search_mode:min_empties:max_empties:deep_depth:exact_enabled:exact_threshold:min_distance:max_distance:holdout_nodes:false_cuts:cut_candidates:wilson95_upper
```

The benchmark requires an explicit positive `--probcut-maximum-margin`; it does
not infer one from the report. Keep the TSV, source report, samples, and run
outputs under the local measurement root. Do not commit them as generated
reports or calibration raw data.

The repository intentionally contains no reviewed calibration TSV. Multi-pair
runtime capability therefore remains off in C++ defaults, native presets,
teacher generation, and every WASM/Web preset. A calibration report authorizes
neither a preset change nor a strength claim; use the separate same-artifact
fixed-time campaign and review its holdouts before enablement.
