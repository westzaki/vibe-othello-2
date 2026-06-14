# Evaluation Architecture

## Purpose

Evaluation assigns heuristic scores to Othello positions.

Search uses those scores at depth cutoffs and may use cheap evaluation signals
for move ordering.

Tools, WASM adapters, and UI features may use evaluation for live advantage
display, candidate comparison, and post-game review.

Evaluation must be deterministic, measurable, fast enough for recursive search,
and explainable enough for analysis workflows.

Implementation status and milestone tracking live in
`docs/progress/evaluation.md`.

## Boundary

Evaluation owns:

* heuristic score scale
* runtime evaluator implementations
* runtime pattern feature definitions
* pattern weight artifact loading and validation
* optional incremental evaluator state
* optional cheap evaluators for move ordering or low-latency analysis
* score calibration helpers for non-search consumers
* evaluation breakdown data for analysis tools

Evaluation does not own:

* board representation
* legal move rules
* flip calculation
* move application semantics
* move undo semantics
* pass legality
* terminal detection
* search algorithm choice
* exact endgame solving
* training corpus ingestion
* label generation
* weight fitting
* opening book generation
* UI rendering
* review policy
* blunder thresholds

Board core is the source of truth for positions, moves, pass handling, terminal
rules, serialization, and hashing.

Search decides how many positions to evaluate and how scores affect pruning.

Pattern learning produces artifacts consumed by evaluation, but training is not
part of recursive search.

## Core Approach

Use a staged evaluator stack.

1. `BaselineEvaluator`: simple handwritten evaluator for early search and
   regression stability.
2. `PatternEvaluator`: production pattern-only evaluator backed by learned
   phase-dependent tables.
3. `CheapEvaluator`: optional low-cost evaluator for ordering, constrained WASM
   analysis, or other latency-sensitive paths.
4. `ExplainingEvaluator`: non-recursive adapter that returns pattern
   contributions and calibrated UI values.

The production target is a pattern-only evaluator.

Non-pattern terms may exist in baseline evaluators, but the learned production
evaluator should keep feature ownership explicit and avoid hiding search policy
inside evaluation.

Correctness comes before speed.

Every optimized evaluator path must have a simple testable path to compare
against.

## Dependency Direction

```text
board_core --> evaluation --> search
      |              |            |
      |              +----> tools/review adapters
      +-------------------> tools/training feature extraction
```

Rules:

* evaluation may depend on board-core public APIs
* search may depend on the search-facing evaluator interface
* evaluation must not depend on search internals
* runtime evaluation must not depend on training tools
* training feature extraction must use board-core rules
* UI must go through analysis or review adapters, not recursive evaluator
  internals

The existing search-facing interface lives under `vibe_othello::search`.

A future evaluation module may provide concrete implementations under
`vibe_othello::evaluation` while continuing to satisfy the search-facing
interface.

## Score Semantics

Evaluation returns side-to-move-relative scores.

Positive means the side to move is better.

Negative means the side to move is worse.

The default runtime score unit is expected final disc difference.

A score of `+4` means the evaluator expects the side to move to finish about
four discs ahead.

If a future artifact uses a scaled unit, the artifact must declare
`score_scale`, and the loader must convert to search score units before search
sees the score.

Rules:

* heuristic scores must stay strictly inside search sentinel values
* evaluation must not return search win/loss sentinels
* terminal and exact endgame scores remain owned by search/endgame logic
* UI win-rate values are calibrated views, not search scores
* score sign is always relative to the current node side to move
* score scale changes require artifact metadata and tests

## Runtime Evaluator Interface

Search consumes the public evaluator interface.

```cpp
class Evaluator {
public:
  virtual ~Evaluator() = default;

  // Returned scores must be strictly inside the search sentinel range:
  // kScoreLoss < score < kScoreWin.
  virtual Score evaluate(const board_core::Position& position) const noexcept = 0;
};
```

Production implementations must keep the recursive hot path allocation-free and
file-I/O-free.

Recommended implementation shape:

```cpp
namespace vibe_othello::evaluation {

struct EvaluationOptions {
  bool use_pattern_eval = true;
  bool use_calibration = false;
};

struct EvaluationBreakdown {
  search::Score raw_score = 0;
  double calibrated_win_rate = 0.0;
  // Future: compact pattern contribution list for review UI.
};

class PatternEvaluator final : public search::Evaluator {
 public:
  explicit PatternEvaluator(const PatternWeights& weights) noexcept;

  search::Score evaluate(const board_core::Position& position) const noexcept override;

  EvaluationBreakdown explain(const board_core::Position& position) const;
};

} // namespace vibe_othello::evaluation
```

`explain` is not part of recursive search.

It may allocate if needed, but review tools should measure its cost separately
from search.

## Pattern Evaluator Design

The production pattern evaluator is a phase-dependent linear model.

```text
score(position) = bias[phase] + sum(weight[phase][pattern_id][pattern_index])
```

The evaluator should:

* normalize positions to side-to-move perspective
* use board-core bit order and coordinate helpers
* map disc count to a phase
* encode each pattern as a compact ternary index
* use symmetry-normalized pattern indices when the pattern definition allows it
* sum integer weights from immutable tables
* clamp only at the final score boundary, not inside each feature contribution

Pattern definitions are runtime contracts.

Changing a pattern definition without changing the artifact version is a bug.

## Pattern Families

Start with a Buro-style pattern-only baseline rather than inventing a large
custom feature set.

Recommended first production families:

* edge lines
* near-edge lines
* diagonals
* corner `2x5` regions
* corner `3x3` regions
* phase bias

Recommended comparison families:

* short systematic n-tuples
* reduced cheap-eval subset for move ordering

Comparison families are experiments.

They must not silently replace the production pattern set without a new artifact
version and benchmark evidence.

## Phase Handling

Use disc-count-based phases.

The first version should use 13 phases because it balances game-stage
specificity and table sparsity.

Rules:

* phase mapping is deterministic
* phase mapping is stored in artifact metadata
* adjacent phase smoothing belongs to training, not runtime
* runtime only selects or interpolates according to the declared artifact policy

## Weight Artifacts

Weights are data, not code.

Use a versioned binary artifact for production and a readable JSON manifest for
metadata.

Recommended layout:

```text
data/
`-- eval/
    |-- README.md
    |-- pattern-v1.manifest.json
    `-- pattern-v1.weights.bin   # optional local/release artifact, not required in git
```

Recommended artifact metadata:

```json
{
  "format": "vibe-othello-pattern-eval",
  "format_version": 1,
  "engine_min_version": "0.1.0",
  "bit_order": "a1-lsb",
  "score_unit": "disc-diff",
  "score_scale": 1,
  "phase_count": 13,
  "pattern_set": "pattern-v1-buro-style",
  "weights_sha256": "...",
  "training_manifest": "training-run-id-or-url",
  "source_data_policy": "manifest-only"
}
```

Rules:

* artifact loading validates magic, version, bit order, phase count, and
  checksum
* runtime does not infer pattern layout from file size alone
* each phase starts with one explicit bias weight slot before pattern tables
* pattern tables are stored in manifest pattern order; `pattern_set` identifies
  the whole ordered schema, and changing that order requires a new pattern set id
* large artifacts should live in releases, Git LFS, or local download cache, not
  normal git history
* raw training corpora must not be committed to the repository

## Calibration and Explanation

Search operates on score values.

UI and review tools may display calibrated values.

Recommended non-search outputs:

* expected final disc difference
* calibrated win probability
* candidate move score list
* principal variation score graph
* post-game loss per move

Calibration is a separate mapping from score to probability.

It can be trained from held-out games, exact endgame labels, or self-play
results.

Rules:

* calibration must not change search scores
* calibration metadata must record the validation dataset and metric
* UI must label probability as a calibrated estimate, not an exact outcome
* explanation totals must match the hot-path evaluator score before calibration

## Incremental Evaluation

The first production implementation may recompute pattern indices from the
position.

After correctness is stable, add incremental evaluation only if benchmarks show
evaluation cost limits search depth.

Incremental evaluation must:

* have one scratch state per search context or worker thread
* reset from a board-core position
* apply and undo using board-core deltas
* round-trip exactly in tests
* be independently disableable

Search must be able to run with the non-incremental evaluator for regression
tests.

## Testing and Measurement

Evaluation tests should cover:

* deterministic score for the same position and artifact
* side-to-move sign consistency
* black/white view conversion correctness
* symmetry-normalized feature index stability
* phase mapping boundaries
* artifact load success and failure cases
* artifact checksum mismatch rejection
* score range inside search sentinels
* native and WASM parity for fixed positions
* `explain` totals matching `evaluate`

Evaluation benchmarks should track:

* nanoseconds per evaluation
* pattern index extraction cost
* table lookup cost
* native versus WASM score parity and latency

Benchmark results should record artifact id, compiler, build type, hardware
class, and enabled evaluator options.

## Build Order

Recommended build order:

1. keep `search::Evaluator` as the stable search-facing interface
2. add `engine/include/vibe_othello/evaluation/` public runtime types
3. add a simple baseline evaluator with tests
4. add artifact metadata structs and loader tests
5. add pattern definition and index encoder tests
6. add `PatternEvaluator` with a tiny hand-authored test artifact
7. add native/WASM parity tests for fixed positions
8. add explanation API after the hot-path evaluator is stable
9. connect pattern-learning artifacts only after training smoke tests exist

## Completion Bar

Evaluation is ready for serious search tuning when:

* evaluator output is deterministic
* score semantics are documented
* artifact versioning is documented
* artifact loader rejects incompatible data
* fixed-position evaluation tests pass
* native and WASM scores agree for the same artifact
* benchmark baselines exist
* search can run with the evaluator disabled or replaced by a reference
  evaluator
* UI calibration is separate from search scoring
