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
* input and build fingerprints in `full-game-artifact-arena-v2` reports
* opt-in `--persistent-session` with caller-selected `--tt-bytes`
* requested/actual TT allocation and split same-key, bucket-conflict,
  replacement, probe-slot, and generation-age telemetry

## Limitations

Wall-time limits are cooperative. Timing telemetry is intentionally observed
data rather than a deterministic engine-strength result. Local reports remain
diagnostics and are not an Elo or promotion gate by themselves.

Persistent sessions remain opt-in. Each engine gets an independent session and
each game starts from a fresh session; retention occurs only between sequential
moves of that game.
