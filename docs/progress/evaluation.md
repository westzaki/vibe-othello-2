# Evaluation Progress

## Current State

This document is the short current-status entry point for runtime evaluation.
Intended design and stable contracts live in `docs/architecture/evaluation.md`.
Artifact layout, default resolution, promotion, rollback, and commit policy live
in `docs/architecture/evaluation-artifacts.md`. Pattern-learning pipeline
status lives in `docs/progress/pattern-learning.md`.

Runtime evaluation is implemented around the search-facing
`search::Evaluator` boundary in
`engine/include/vibe_othello/search/evaluator.h`.

`PatternEvaluator` is the current learned runtime evaluator. It implements
`search::Evaluator`, returns side-to-move-relative scores, performs no file I/O
in the evaluator hot path, consumes immutable `PatternWeights` plus an explicit
`PatternFeatureSet`, validates that runtime geometry matches the loaded
weights, and evaluates the documented phase-dependent linear model:

```text
bias[phase] + sum(pattern weights)
```

The runtime evaluation module also includes pattern schema types, loaded
artifact weight containers, runtime `PatternWeights`, ternary pattern indexing,
declared pattern-symmetry support, pattern-set feature geometry, and the
test-oriented `TinyPatternEvaluator`. Existing trainer/exporter tooling can
produce runtime-compatible learned artifacts; the learned evaluator itself is
not missing.

The legacy static evaluator path remains available as an explicit override and
as a simple deterministic reference path for tooling and tests.

## Runtime and Artifact Status

`pattern-v2-endgame-lite-100k-mt-v0` is the committed learned evaluation
artifact v0 and is the current experimental default.

Default resolution is controlled by `data/eval/default-artifact.json`, whose
status is `experimental-default` and whose manifest pointer resolves to:

```text
data/eval/artifacts/pattern-v2-endgame-lite-100k-mt-v0/manifest.json
```

The committed runtime payload is limited to `weights.bin`, `manifest.json`,
`provenance.json`, `README.md`, and `NOTICE.md` under the artifact directory.

The runtime loader entry points are
`vibe_othello::evaluation::load_default_pattern_artifact` and
`vibe_othello::evaluation::load_pattern_artifact`. The loader validates the
default pointer, manifest paths, weights path, runtime checksum, embedded binary
checksum, pattern set, phase count, score unit, score scale, and pattern table
layout before constructing `PatternEvaluator`.

The engine CLI uses the committed default artifact unless an explicit override
is supplied. `--eval-artifact` selects a specific artifact manifest.
`--eval-mode static` forces the legacy static evaluator. Loader failure is loud:
missing, corrupt, or incompatible artifact data exits with an error instead of
silently falling back to static evaluation.

The current default artifact is not an Elo result, not a self-play improvement
claim, not a production-strength claim, not publication readiness, and not an
official Egaroucid artifact.

## Known Gaps

Runtime evaluation still lacks:

* an evaluation explanation API for tools and UI
* a calibration API for display-only score views
* native/WASM parity coverage for fixed evaluator fixtures
* an incremental evaluator state path, if benchmarks later justify one
* a separately promoted production baseline evaluator beyond the legacy static
  override
* production-strength validation for a default artifact through Elo-style or
  self-play measurement
* hardened production trainer workflow documentation and promotion criteria

These are real gaps in the current repository. They do not mean that
`PatternEvaluator`, manifest-based loading, learned artifact export, or the
committed experimental default are absent.

## Next Work

Keep upcoming work limited to unfinished items:

* add explanation support outside the recursive search hot path
* add calibration support without changing recursive search scores
* add native/WASM parity checks once the WASM evaluator path is in scope
* decide whether an incremental evaluator state is needed from benchmarks
* define the evidence required before any learned artifact can claim production
  strength
* document the hardened trainer and artifact-promotion workflow when that
  workflow is ready to be treated as current behavior

## Update Rules

Update this document only when current repository behavior changes or a real
known gap is added, resolved, or reclassified.

Keep detailed architecture in `docs/architecture/evaluation.md`, artifact
policy in `docs/architecture/evaluation-artifacts.md`, pattern-learning status
in `docs/progress/pattern-learning.md`, and experiment history in
`docs/experiments/README.md`.

Do not add PR-by-PR history, completed implementation plans, generated output
inventories, benchmark payloads, or test inventories here.
