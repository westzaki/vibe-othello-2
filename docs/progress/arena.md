# Arena and Strength-Gate Progress

## Current Status

The persistent full-game artifact arena provides:

* deterministic paired-opening selection and color swaps
* manifest-backed candidate and baseline evaluators loaded once per run
* explicit fixed-depth, fixed-node, and fixed-wall-time modes
* per-search candidate/baseline telemetry and phase aggregation
* nanosecond arena timing with engine-timer accounting diagnostics
* deterministic opening-pair cluster-bootstrap confidence intervals
* explicit run-level strength-gate eligibility and descriptive-only invalid runs
* same-artifact, color-swap, and argument-order sanity support
* input and build fingerprints in `full-game-artifact-arena-v4` reports
* opt-in `--persistent-session` with caller-selected `--tt-bytes`
* requested/actual TT allocation and split same-key, bucket-conflict,
  replacement, probe-slot, and generation-age telemetry
* a local-only fixed-time artifact strength campaign with a default
  50/100/500-ms by exact-8/10/12 matrix
* per-cell forward, argument-order reverse, candidate/candidate, and
  baseline/baseline reports with checksum-guarded resume
* one schema-checked decision report containing paired confidence intervals,
  outcome and disc-difference aggregates, phase/side exposure rates, completed
  depth, throughput, exact, and incremental-evaluation telemetry
* optional independent holdout-corpus evaluation and configurable promotion
  thresholds without artifact or default-pointer mutation
* a fixed 95% promotion interval, a default 100-opening-pair floor, and breadth
  across at least two distinct passing time limits
* fixed-time same-artifact and argument-order symmetry as diagnostics rather
  than timing-sensitive correctness rejection gates
* strict report-to-request binding for search config, runtime artifacts,
  executable identity, and opening selection
* independent candidate/baseline off, single-pair, and Multi-ProbCut policies
  with phase/depth-pair telemetry
* fail-fast shared runtime normalization for non-off ProbCut requests, plus
  separate requested/effective policy fields in reports
* a local off/single/multi/shadow campaign across fixed depth, fixed nodes, and
  50/100/500-ms cells, with direct multi-vs-single comparisons, multiple
  seeds/opening subsets, and policy swaps
* strict binding of profile training/holdout checksums, joint scheduler
  evidence, requested limits, openings, bootstrap settings, artifact runtime
  identity, and Arena pair-completeness gate
* automatic later-pair attempt/success, 100-opening-pair primary floor,
  100/500-ms direction, and scheduler-level shadow false-cut checks

## Limitations

Wall-time limits are cooperative. Timing telemetry is intentionally observed
data rather than a deterministic engine-strength result. Local reports remain
diagnostics and are not an Elo or promotion gate by themselves.

Persistent sessions remain opt-in. Each engine gets an independent session and
each game starts from a fresh session; retention occurs only between sequential
moves of that game. The selected TT allocation is used in both persistent and
non-persistent modes.

The fixed-time campaign enables persistent sessions by default as an explicit
campaign configuration. Its wall-time results remain machine- and load-sensitive;
the suggested decision is a local validation aid, not an Elo or production
strength claim. Long default-matrix campaigns are intentionally excluded from
CI; CI runs only a tiny real-tool override.

The matrix-wide outcome aggregate is descriptive and deliberately omits a
confidence interval. Cell-level intervals cluster by opening pair; repeated
openings under different time and exact conditions are not treated as
independent observations.

No reviewed Multi-ProbCut calibration or fixed-time strength report is checked
in. The MPC campaign deliberately cannot authorize preset enablement;
fixed-depth differential correctness and exact holdouts remain manual gates.
