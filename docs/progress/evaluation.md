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

At construction it now compiles the dynamic feature geometry and weights into
a flat runtime layout. It provides a flat stateless path, the prior stateless
extraction as a parity reference, and an incremental state with paired
black/white-perspective indices. Search binds that state once per applicable
root and updates only instances touched by placed or flipped squares. Pass and
undo are supported without rebuilding indices. Phase-aware fallback-only roots
remain on the generic stateless path.

The compiled layout also records the last nonzero pattern-family instance for
each phase, so dormant suffix tables are neither extracted nor incrementally
updated. When a later phase activates more families, the incremental state
rebuilds the newly active prefix from its maintained absolute board. Runtime
weights use a compact signed-16-bit array when the loaded values fit exactly
and retain the signed-32-bit path otherwise; evaluator parity tests cover both
the flat/reference paths and full apply/undo games.

`PhaseAwareEvaluator` is the artifact-facing runtime composition. It uses
`PatternEvaluator` only in metadata-declared `trained_phases` and otherwise
uses the small deterministic `EarlyMidgameHeuristicEvaluator`. The fallback is
board-derived (mobility, potential mobility, frontier, corner, empty-corner
X/C-square, and phase-weighted disc difference) and is not a strength claim.

The runtime evaluation module also includes pattern schema types, loaded
artifact weight containers, runtime `PatternWeights`, ternary pattern indexing,
declared pattern-symmetry support, pattern-set feature geometry, and the
test-oriented `TinyPatternEvaluator`. Existing trainer/exporter tooling can
produce runtime-compatible learned artifacts; the learned evaluator itself is
not missing.

The legacy static evaluator path remains available as an explicit override and
as a simple deterministic reference path for tooling and tests.

## Runtime and Artifact Status

`pattern-v2-wthor-full-policy-v1` is the current learned experimental default.
It supersedes `pattern-v2-progressive-search-d5-fast6-p5-v1`, which remains
committed for rollback and comparison.

Default resolution is controlled by `data/eval/default-artifact.json`, whose
status is `experimental-default` and whose manifest pointer resolves to:

```text
data/eval/artifacts/pattern-v2-wthor-full-policy-v1/manifest.json
```

The committed runtime payload is limited to `weights.bin`, `manifest.json`,
`provenance.json`, `README.md`, and `NOTICE.md` under the artifact directory.

The current default reports reviewed learning coverage for all 13 phases.
Phases 0 through 9 add a WHTOR played-move residual to the deterministic
fallback, and phases 10 through 12 retain the previous exact-teacher pattern
weights. No search option changes are required.

The runtime loader entry points are
`vibe_othello::evaluation::load_default_pattern_artifact` and
`vibe_othello::evaluation::load_pattern_artifact` for filesystem artifacts, and
`vibe_othello::evaluation::load_pattern_artifact_from_bytes` for an in-memory
manifest text plus weights byte buffer. Both artifact paths share manifest
contract validation, runtime pattern-set resolution, runtime checksum
validation, embedded binary checksum validation, pattern set, phase count, score
unit, score scale, and pattern table layout validation before constructing
runtime weights for phase-aware evaluation.

The WASM C ABI and plain JavaScript `WasmCore` wrapper can load manifest text
plus weights bytes through the in-memory loader, keep the resulting
`PhaseAwareEvaluator` behind an opaque WASM-side handle, evaluate positions,
and run bounded best-move search with that evaluator. The browser Worker fetches
`/eval/default-artifact.json` and uses the loaded evaluator for bounded CPU
moves.

The engine CLI uses the committed default artifact unless an explicit override
is supplied. `--eval-artifact` selects a specific artifact manifest.
`--eval-mode static` forces the legacy static evaluator. Loader failure is loud:
missing, corrupt, or incompatible artifact data exits with an error instead of
silently falling back to static evaluation.

The current artifact cleared direct paired local arena gates against the
previous default: 73.35% at depth 3, 69.14% at depth 5, and 66.99% at
10 ms plus exact8. Each paired 95% interval excluded 50%, all games were clean,
and the depth-3 argument-order and same-artifact controls passed. The
board-core-generated promotion suite had zero audited board and
transcript-prefix overlap with WHTOR. It is still not an Elo result, not a
self-play improvement claim, not a production-strength claim, not publication
readiness, and not an official WHTOR, FFO, or Egaroucid artifact.

## Known Gaps

Runtime evaluation still lacks:

* an evaluation explanation API for tools and UI
* a calibration API for display-only score views
* broader native/WASM parity coverage beyond fixed phase-aware artifact routing
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
* broaden native/WASM parity checks beyond fixed phase-aware artifact fixtures
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
