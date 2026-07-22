# Evaluation Architecture

## Purpose

Runtime evaluation assigns heuristic scores to Othello board positions.

Search uses those scores at depth cutoffs and may use cheap evaluation signals
for move ordering. Tools, WASM adapters, and UI features may also use runtime
evaluation for live advantage display, candidate comparison, and post-game
review.

Evaluation must be deterministic, measurable, fast enough for recursive search,
and explainable enough for analysis workflows. This document defines the
runtime architecture contract and current implementation. Committed artifact
policy and experiment history are documented separately.

## Implemented System

Runtime evaluation is implemented around `search::Evaluator` and three engine
evaluators:

* `EarlyMidgameHeuristicEvaluator`, the deterministic fallback
* `PatternEvaluator`, the immutable phase-dependent learned model
* `PhaseAwareEvaluator`, which routes fallback, learned replacement, or
  fallback-plus-learned-residual according to artifact metadata

`PatternEvaluator` compiles dynamic pattern geometry and weights into flat
runtime arrays at construction. It keeps a reference extraction path and an
incremental state with paired black/white indices. Search initializes that
state once for eligible roots, updates only affected active instances on move
and undo, and skips dormant suffix tables until a later phase activates them.
Weights use a signed 16-bit compact array when every value fits exactly and
otherwise retain the 32-bit path.

Filesystem loading and in-memory manifest-plus-bytes loading share the same
manifest, checksum, pattern-set, phase, score-unit, score-scale, and table-shape
validation. The CLI uses the filesystem path; WASM uses the byte path and owns
the resulting evaluator behind an opaque handle. Load failures are explicit and
do not silently select another evaluator.

The current experimental default is
`pattern-v2-egaroucid-lv17-full-value-v1`. It declares reviewed coverage for
all 13 phases: phases 0 through 9 add a learned residual to the fallback and
phases 10 through 12 use learned replacement weights. Its training route and
paired validation evidence are summarized in
`data/eval/artifacts/pattern-v2-egaroucid-lv17-full-value-v1/README.md`; the
earlier `pattern-v2-wthor-full-policy-v1` remains available
for rollback and comparison. “Experimental default” is not an Elo,
self-play, publication-readiness, or production-strength claim.

## Current Limitations

There is no evaluation-explanation API, calibrated display-score API, or
separately promoted production baseline evaluator. Native/WASM parity coverage
does not yet cover every evaluator path, and production-strength validation for
the default artifact remains outside the repository's current claims.

## Boundaries

Runtime evaluation owns:

* the search-facing evaluator contract
* side-to-move-relative score semantics
* runtime evaluator implementations
* runtime pattern feature definitions
* loaded pattern weights consumed by evaluators
* optional low-cost evaluators for move ordering or low-latency analysis
* optional non-search explanation and calibration adapters

Runtime evaluation does not own:

* board representation, legal move rules, move application, pass handling,
  terminal detection, serialization, or hashing
* search algorithm choice, pruning policy, terminal scoring, or exact endgame
  solving
* training corpus ingestion, dataset storage, teacher label generation, weight
  fitting, trainer reports, exporter reports, or experiment logs
* committed artifact directory layout, default artifact pointer policy,
  artifact promotion, rollback, or repository commit rules
* UI rendering, review policy, or blunder thresholds

Board core is the source of truth for positions and rules. Search decides how
many positions to evaluate and how scores affect recursive search.

Training and artifact publication may produce data consumed by evaluation, but
runtime evaluation must not depend on training tools, local datasets, generated
labels, or trainer reports.

## Runtime Evaluator Interface

Search consumes evaluation through a small deterministic interface. The
interface is a recursive hot path and should remain allocation-free,
file-I/O-free, and independent of training or committed artifact handling code.

```cpp
class Evaluator {
public:
  virtual ~Evaluator() = default;

  // Returned scores must be strictly inside the search sentinel range:
  // kScoreLoss < score < kScoreWin.
  virtual Score evaluate(const board_core::Position& position) const noexcept = 0;
};
```

Concrete evaluators may live under an evaluation namespace while satisfying the
search-facing interface. Non-search helpers such as `explain` or calibration
views are adapters around the evaluator and are measured separately from the
recursive search path.

## Evaluator Roles

The engine runtime evaluator stack is intentionally small:

* `EarlyMidgameHeuristicEvaluator`: a small deterministic baseline used as the
  complete fallback for uncovered phases and as the additive baseline for
  covered residual phases. It combines mobility, potential mobility, frontier,
  corner, empty-corner X/C-square, and phase-weighted disc-difference signals;
  it is not a production-strength claim.
* `PatternEvaluator`: the learned runtime evaluator backed by an immutable
  pattern set definition and loaded phase-dependent weights.
* `PhaseAwareEvaluator`: the artifact-facing composition that selects the
  learned pattern evaluator only for reviewed phases and otherwise selects the
  early/midgame heuristic fallback. An explicit artifact policy may treat
  covered early phases as learned residuals over that fallback.

The engine CLI's `--eval-mode static` path is a tool-local disc-difference
evaluator, not another engine evaluation implementation. Future explanation or
display-calibration adapters must stay outside recursive evaluation and keep
feature ownership explicit.

## Pattern Evaluator Runtime Contract

The pattern evaluator is a phase-dependent linear model:

```text
score(position) = round((bias[phase] + sum(weight[phase][pattern_id][pattern_index])) / score_scale)
```

The evaluator consumes a runtime-owned pattern set definition and loaded weight
tables. The pattern set defines the ordered patterns, cell order, symmetry
policy, phase mapping policy, table sizes, and score unit expected by runtime
code. Loaded weights provide the immutable numeric data for that exact runtime
contract.

The evaluator should:

* normalize positions to side-to-move perspective
* use board-core bit order and coordinate helpers
* map each position to a deterministic phase
* encode each pattern as a compact ternary index
* apply symmetry canonicalization only when the pattern definition declares it
* sum integer fixed-point weights from immutable tables and divide once using
  the manifest scale
* clamp only at the final search score boundary

The production runtime also precomputes a flat immutable layout at evaluator
construction. It contains flat instance and square descriptors, powers of
three, direct phase/table weight offsets, and a square-to-instance reverse
index. The stateless evaluator uses this layout without traversing the dynamic
feature-set vectors or dividing table sizes in its lookup loop.

Search may create an incremental pattern state once at a root that can reach a
learned phase. The state stores paired black-to-move and white-to-move ternary
indices, so the side-to-move-relative board swap does not require rebuilding
all instances. A normal move applies each placed/flipped square's ternary-digit
contributions directly to the affected active indices and deduplicates touched
instance accounting with a generation table; pass changes only the selected
perspective. Apply and undo are allocation-free after root initialization. The
original stateless extraction remains available as the correctness reference.

Changing a pattern definition without changing the runtime artifact contract is
a bug. The runtime evaluator must reject incompatible loaded weights rather than
guessing from file size or partially matching table shapes.

Pattern symmetry canonicalization is part of the pattern definition. Supported
runtime policies may include raw indices, reversed cell-order canonicalization,
and square D4 canonicalization. Symmetry transforms must preserve the
side-to-move-relative digits: empty `0`, player `1`, opponent `2`.

## Score Semantics

Evaluation returns side-to-move-relative scores:

* positive means the side to move is better
* negative means the side to move is worse
* zero means the evaluator sees the position as balanced

The default runtime score unit is expected final disc difference. A score of
`+4` means the evaluator expects the side to move to finish about four discs
ahead.

Rules:

* heuristic scores must stay strictly inside search sentinel values
* evaluation must not return search win/loss sentinels
* terminal and exact endgame scores remain owned by search and endgame logic
* score sign is always relative to the current node side to move
* score scale changes require artifact metadata, loader conversion, and tests
* UI win-rate values are calibrated views, not search scores
* calibration must not change recursive search scores
* explanation totals must match the hot-path evaluator score before
  calibration

Phase mapping is also part of the runtime contract. It must be deterministic and
declared by the loaded artifact. Adjacent phase smoothing belongs to training;
runtime only selects or interpolates according to the declared policy.

## Search Integration

Public search integrates with evaluation through the evaluator interface.
Internally, root initialization recognizes the built-in pattern and
phase-aware implementations once and binds their incremental state to the
search-position state. Other evaluator types keep the generic virtual
stateless path. A phase-aware root that cannot reach a learned phase also keeps
the generic fallback path rather than paying pattern-index update cost.

Rules:

* search owns node traversal, terminal detection, pass handling, exact scores,
  and pruning decisions
* evaluation owns heuristic scores for non-terminal cutoff positions
* evaluation must not depend on search internals
* runtime evaluation must not perform file I/O from recursive search
* incremental apply, evaluate, and undo must not allocate in recursive search
* search tests should be able to replace learned evaluation with a simple
  deterministic evaluator
* move-ordering evaluators must not change the meaning of final search scores

This keeps search behavior testable and lets learned weights change without
coupling the recursive algorithm to training or artifact publication details.

## Artifact Loading Boundary

Runtime evaluation can load committed evaluation artifacts.

Default artifact resolution and explicit override behavior are artifact
architecture concerns. Manifest, provenance, checksum, promotion, rollback, and
repository policy details are owned by
`docs/architecture/evaluation-artifacts.md`.

This document only defines how loaded runtime weights are consumed by
evaluators: after a loader validates compatibility, the evaluator receives an
immutable pattern set definition and matching weight tables.

The loader exposes optional artifact `trained_phases` metadata to callers.
`PhaseAwareEvaluator` uses it as routing policy: reviewed phases use the
learned evaluator and unreviewed phases use the deterministic fallback. It
precomputes the routing table when the evaluator is constructed, so recursive
evaluation does not inspect metadata or allocate.

An optional `fallback_additive_through_phase` artifact field changes covered
phases through that inclusive boundary from learned replacement to
`fallback + learned residual`. It does not make an uncovered phase learned and
does not affect covered phases above the boundary. Arena runtime identity must
include this policy so residual and replacement artifacts cannot be mistaken
for equivalent evaluators.

Missing `trained_phases` is legacy/unreported coverage, not a claim of
all-phase learning. To preserve legacy artifact behavior, the phase-aware
runtime routes such artifacts to the learned evaluator for every phase. A
future artifact that explicitly reports every runtime phase naturally routes
all phases to the learned evaluator and leaves the fallback unused.

## Determinism and Safety

Runtime evaluation must be deterministic for the same position, side to move,
pattern set, weights, and options.

Tests should cover:

* deterministic scores for fixed positions
* side-to-move sign consistency
* black/white view conversion correctness
* symmetry-normalized feature index stability
* phase mapping boundaries
* score range inside search sentinels
* artifact load success and rejection of incompatible data
* fallback and learned routing at phase boundaries, including legacy metadata
  compatibility, full-phase learned coverage, and explicit residual routing
* native and WASM parity for fixed positions when both runtimes are supported
* explanation totals matching `evaluate`

Benchmarks should measure evaluator cost separately from search policy:

* nanoseconds per evaluation
* pattern index extraction cost
* table lookup cost
* native versus WASM score parity and latency
* incremental root initialization and make/evaluate/undo cost
* fallback-only, learned replacement, and fallback-plus-residual routing cost

Measurements should record artifact id, compiler, build type, hardware class,
and enabled evaluator options without committing local machine identifiers or
generated measurement payloads.

## What Belongs Elsewhere

Artifact layout, manifest, default pointer, promotion, rollback, and commit
policy: `docs/architecture/evaluation-artifacts.md`.

Pattern learning data pipeline and trainer/exporter responsibilities:
`docs/architecture/pattern-learning.md`.

Evaluation artifact data directory policy: `data/eval/README.md`.

Current training route and learning contracts:
`docs/architecture/pattern-learning.md`.
