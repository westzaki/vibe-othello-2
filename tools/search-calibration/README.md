# Selective Search Shadow Calibration

This directory owns the local-only MPC/ProbCut calibration workflow. The
engine can collect reduced-depth versus deep-search observations, but it does
not perform a ProbCut, change a search cutoff, or load any fitted coefficient.

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

- `node_type`: `pv`, `cut`, or `all`
- `official_deep_bound`, `shallow_verification_bound`, and
  `deep_verification_bound`: `upper`, `exact`, or `lower`
- `actual_official_deep_result`: `fail_low`, `exact`, or `fail_high`
- moves: `a1` through `h8`, `pass`, or JSON `null`
- `canonical_position_hash`: 16 lowercase hexadecimal characters

JSON field names match the C++ member names exactly. Every JSON/JSONL sample
must contain all schema-v3 fields validated by `analyze_shadow_samples.py`.
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

After the official search completes at a sampled node, collection runs two
isolated searches over `[kScoreLoss, kScoreWin]`: one at `shallow_depth` and one
at `deep_depth`. Schema v3 separates their exact value observations from the
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
The dedicated counters are in `SearchResult::shadow_calibration`, including
separate shallow and deep-verification node counts. Fixed-time deadline
accounting excludes verification work, but local calibration should still use
fixed-depth or fixed-node runs: an external stop can naturally arrive while any
diagnostic callback is running. Deep verification is intentionally expensive;
control it with sample rate and the per-search cap.

Normal C++ defaults, runtime presets, WASM `easy`/`normal`/`hard` presets,
benchmarks, and Arena runs remain disabled. Long collection runs belong under a
local measurement root outside the repository, for example:

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

The analyzer groups by phase, deep depth, shallow depth, and node type. Each
group reports:

- total sample count, exact-pair count, and bound-observation count
- `deep = a + b * shallow` regression using only rows where both
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
