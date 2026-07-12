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
- `deep_bound`: `upper`, `exact`, or `lower`
- `actual_deep_result`: `fail_low`, `exact`, or `fail_high`
- moves: `a1` through `h8`, `pass`, or JSON `null`
- `canonical_position_hash`: 16 lowercase hexadecimal characters

JSON field names match the C++ member names exactly. Every JSON/JSONL sample
must contain all schema-v1 fields validated by `analyze_shadow_samples.py`.
`repo_sha`, `search_config_id`, `evaluator_id`, and `artifact_id` must identify
the actual run; `repo_sha` is a 7-64 character lowercase hexadecimal Git SHA.
Use `artifact_id: "none"` only for an evaluator with no
artifact. Do not insert a path, hostname, or user name into identity fields.

The canonical position identity is an FNV-1a hash of the lexicographically
smallest of the eight side-to-move-relative board symmetries. It is compact
position identity metadata, not a serialized board or a collision-proof data
store key. Phase v1 uses
`min(12, floor(max(0, occupied_count - 4) * 13 / 60))`; it matches the current
13-phase artifact layout.

Shadow searches use isolated TT and ordering state and never contribute to
official `SearchStats`, `SearchResult::nodes`, root move nodes, or node limits.
The dedicated counters are in `SearchResult::shadow_calibration`. Fixed-time
deadline accounting excludes shallow work, but local calibration should still
use fixed-depth or fixed-node runs: an external stop can naturally arrive while
any diagnostic callback is running.

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

Input schema is rejected strictly for missing fields, invalid types/ranges,
inconsistent node flags, move agreement, window classification, hypothetical
cuts, or false-cut candidates. Unsupported schema versions and malformed JSON
also fail with exit code 2. Empty inputs produce a valid deterministic report
with zero samples and groups.

The analyzer groups by phase, deep depth, shallow depth, and node type. Each
group reports:

- sample count and `deep = a + b * shallow` regression
- residual mean, population standard deviation, and residual percentiles
- MAE and RMSE for fitted regression residuals
- best-move agreement
- hypothetical high/low cut counts and observed false-cut estimate
- 90%, 95%, and 99% empirical residual-margin cut coverage
- `ceil(p99 absolute fitted residual)` as a conservative diagnostic margin

The input checksum is SHA-256 over sorted per-file content digests, so it does
not include local paths. Samples are sorted canonically before floating-point
aggregation, reports contain no timestamp, and JSON keys/group ordering are
stable. The recommended margin is a report-only diagnostic. Adopting any
coefficient or margin in runtime search requires a separate measured change.
