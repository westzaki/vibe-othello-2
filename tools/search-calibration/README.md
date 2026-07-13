# Selective Search Shadow Calibration

This directory owns the local-only MPC/ProbCut calibration workflow. The engine
can collect reduced-depth versus deep-search observations. Runtime search also
has a separate conservative, typed, caller-supplied ProbCut path, but the
repository contains no production profile or coefficient and enables nothing
by default. A generated analyzer report is not itself runtime authorization.

## Collection contract

Configure `SearchOptions::selective` with `SelectiveSearchOptionsV1`. Collection
is active only when `enable_shadow_calibration` is true, the integer
`sample_rate` is positive, `max_samples_per_search` is positive, the depth
reduction is positive, and a caller-owned `ShadowCalibrationSink` is present.
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
must contain all schema-v4 fields validated by `analyze_shadow_samples.py`.
`repo_sha`, `search_config_id`, `evaluator_id`, and `artifact_id` must identify
the actual run; `repo_sha` is a 7-64 character lowercase hexadecimal Git SHA.
Use `artifact_id: "none"` only for an evaluator with no
artifact. Do not insert a path, hostname, or user name into identity fields.

The engine derives `collection_config_id` from the effective sample rate,
sample cap, minimum deep depth, shallow reduction, PV/pass/near-exact inclusion
flags, and sample schema version. This ID is stored in every sample and mixed
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
After the official search completes at a sampled node, collection runs two
isolated searches over `[kScoreLoss, kScoreWin]`: one at `shallow_depth` and one
at `deep_depth`. Schema v4 separates their exact value observations from the
official window result:

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
and result-independent search role. Only `non_pv_scout` is recommendation
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

## Runtime profile adoption

Do not copy a regression group into runtime merely because
`recommendation_eligible` is true. Review the source report and holdout result,
then create a versioned `ProbCutCalibrationProfileV1` that records:

- profile ID and schema version
- SHA-256 of the exact reviewed analyzer report file
- evaluator and artifact families
- exact node class `non_pv_scout_beta_only`
- phase and the single supported deep/shallow depth pair
- regression slope/intercept and residual sigma
- audited confidence multiplier
- inclusive shallow-score and beta validity ranges

The first runtime version rejects any other or missing node class, multiple
depth pairs, duplicate groups, invalid identity/checksum data, non-finite
coefficients, and every missing phase/depth pair. It never fills a missing
group from an adjacent phase or depth. The caller must ensure that the profile
evaluator/artifact family names describe the actual evaluator supplied to
search.

Do not manually translate analyzer coefficient names. Create a local reviewed
adoption specification containing only the explicit decisions that the report
cannot supply (profile ID, confidence multiplier, and audited validity ranges),
then run the checked converter:

```sh
python3 tools/search-calibration/convert_probcut_profile.py \
  "$VIBE_OTHELLO_MEASUREMENTS/mpc-shadow/report.json" \
  "$VIBE_OTHELLO_MEASUREMENTS/mpc-shadow/adoption.json" \
  --output "$VIBE_OTHELLO_MEASUREMENTS/mpc-shadow/reviewed-profile.tsv"
```

The adoption file uses `schema_version: "probcut-profile-adoption-v1"`, exact
report `evaluator_id`/`artifact_id` values as `evaluator_family` and
`artifact_family`, `node_class: "non_pv_scout_beta_only"`, and an `entries`
array. Each entry selects `phase`, `deep_depth`, and `shallow_depth` and supplies
`confidence_multiplier`, `minimum_shallow_score`, `maximum_shallow_score`,
`minimum_beta`, and `maximum_beta`. The converter selects exactly one eligible
`non_pv_scout` group, maps `.slope` to `regression_slope` and `.intercept` to
`intercept`, copies residual sigma, rejects multiple depth pairs, and records
the SHA-256 of the exact report file bytes. It never derives confidence or
validity ranges.

The one-sided integer condition is:

```text
predicted_deep = slope * shallow_score + intercept
k = max(profile_confidence_multiplier, option_confidence_multiplier)
margin = max(ceil(k * residual_sigma), minimum_margin)
cut-high only if floor(predicted_deep) - margin >= beta
```

If `margin > maximum_margin`, the candidate is rejected; the maximum is never
used to reduce a confidence margin. Scores and bounds near search sentinels are
also rejected. `shadow_verify=true` applies the same eligibility and shallow
search but does not cut; normal deep search classifies a candidate as false
when its result is below beta.

For local benchmark input, use a TSV outside the repository with this exact
header (one row per supported phase):

```text
schema_version	profile_id	source_checksum_sha256	evaluator_family	artifact_family	node_class	phase	deep_depth	shallow_depth	regression_slope	intercept	residual_sigma	confidence_multiplier	minimum_shallow_score	maximum_shallow_score	minimum_beta	maximum_beta
```

The benchmark requires an explicit positive `--probcut-maximum-margin`; it does
not infer one from the report. Keep the TSV, source report, samples, and run
outputs under the local measurement root. Do not commit them as generated
reports or calibration raw data.
