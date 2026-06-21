# Pattern Learning Architecture

## Purpose

Pattern learning produces evaluation weights for the runtime pattern evaluator.

It turns game records, labeled positions, engine-generated scores, and self-play
results into versioned artifacts that can be loaded by the evaluation module.

The goal is to train a strong pattern-only evaluator while keeping the engine
runtime small, deterministic, and license-safe.

Implementation status and milestone tracking live in
`docs/progress/pattern-learning.md`.

## Boundary

Pattern learning owns:

* training data ingestion
* corpus manifests
* data license and provenance tracking
* position normalization for training
* label generation
* feature extraction for training
* train/validation/test splits
* weight fitting
* regularization and smoothing
* calibration datasets
* artifact export
* training metrics and reports

Pattern learning does not own:

* board rules
* runtime move generation
* search algorithm semantics
* recursive evaluator hot path
* UI rendering
* opening book lookup
* source repository license selection

Runtime evaluation consumes trained artifacts.

Pattern learning may use search tools to generate labels, but search must not
depend on training code.

## Core Approach

Use an explicit, reproducible pipeline.

```text
external corpora / self-play / engine labels
        |
        v
tools/data-import
        | normalized records + source manifests
        v
tools/pattern/labels
        | optional local-only teacher label overlay
        v
tools/pattern/dataset
        | positions, labels, split ids
        v
tools/pattern/features
        | sparse pattern indices by phase
        v
tools/pattern/train
        | fitted weights + metrics
        v
tools/pattern/calibrate
        | optional score-to-probability mapping
        v
data/eval artifacts --> engine/src/evaluation runtime loader
```

Recommended repository ownership:

```text
engine/
|-- include/vibe_othello/evaluation/
|   |-- pattern_evaluator.h
|   |-- pattern_weights.h
|   `-- pattern_schema.h
|-- src/evaluation/
|   |-- pattern_evaluator.cc
|   |-- pattern_weights.cc
|   `-- pattern_schema.cc
`-- tests/evaluation/

tools/
|-- data-import/
|-- data-policy/
|-- pattern/
|   |-- dataset/
|   |-- features/
|   |-- train/
|   |-- calibrate/
|   `-- export/

data/
|-- corpora/README.md
`-- eval/README.md
```

The repository may store small synthetic fixtures.

Large or restricted corpora should be downloaded locally by scripts and
described by manifests.

Local sequence replay caches are allowed only as uncommitted measurement
accelerators. Cache keys must be content-addressed from source bytes, manifest
bytes, importer identity, normalized schema identity, dataset id, identity
policy, and semantic importer options. They must not include local absolute
paths, output directories, run ids, timestamps, or temporary paths. Cached
normalized TSVs and import reports must be checksum-validated before reuse and
rebuilt when metadata or payload validation fails.

## Source Data Policy

Every dataset must have a manifest before it can be used for training.

Minimum manifest fields:

```json
{
  "dataset_id": "example-dataset-v1",
  "source_name": "Example Dataset",
  "source_url": "https://example.invalid/dataset",
  "retrieved_at": "YYYY-MM-DD",
  "license_or_terms": "unknown | custom | public-domain | cc-by | ...",
  "redistribution_allowed": false,
  "commercial_use_allowed": "unknown",
  "derived_weights_allowed": "unknown",
  "required_attribution": "...",
  "local_path": "not committed",
  "sha256": "...",
  "notes": "..."
}
```

Rules:

* raw third-party data is not committed unless redistribution is explicitly
  allowed
* generated intermediate datasets are not committed unless their source terms
  allow redistribution
* trained weights derived from restricted data require a release decision before
  publication
* every training run records the exact dataset manifests used
* local measurement reports may include stage telemetry and cache hit/miss
  status so repeated runs can distinguish replay cost from training cost
* unknown license means internal experiment only
* GPL engine code, GPL evaluation weights, and GPL-derived code are not copied
  into this repository

## Data Sources

Recommended source classes:

| Source class | Use | Risk |
| --- | --- | --- |
| Human game records | opening realism and natural play distribution | database terms may restrict redistribution |
| Engine self-play transcripts | large-scale supervised labels | engine output provenance matters |
| Own self-play | safest long-term source | compute cost and bootstrapping quality |
| Solved/endgame labels | high-quality endgame supervision | expensive; source terms matter |
| Synthetic/random positions | coverage and invariants | distribution mismatch |

Practical path:

1. use tiny synthetic fixtures for importer and feature tests
2. use local-only external corpora for experiments
3. generate own self-play as soon as search is stable enough
4. publish only artifacts whose source manifests are cleared

## Position Normalization

Training positions must use board-core semantics.

Rules:

* all imported records are converted into `board_core::Position`
* side to move is explicit
* labels are side-to-move-relative
* illegal records are rejected or quarantined
* pass moves are represented explicitly when the source format requires them
* position text fixtures use canonical board-core serialization
* duplicate handling is deterministic and recorded in the dataset report
* sequence transcript identity separates semantic game groups from source
  occurrences: split assignment is derived from `dataset_id + game_group_id`,
  where `game_group_id` comes from canonical replayed move/pass content; local
  path, archive member, and line number are provenance only
* exact side-to-move-relative board identifiers are reported separately from
  game grouping so local measurements can distinguish game-held-out metrics
  from exact-board-disjoint metrics

The feature extractor must not implement independent Othello rules.

It should call board-core parsing and move application helpers.

## Labels

Supported label types:

| Label type | Meaning | First use |
| --- | --- | --- |
| `final_disc_diff` | final disc difference from side to move | human/self-play game records |
| `observed_final_disc_diff` | observed transcript final disc difference from side to move, not searched | imported complete game transcripts |
| `engine_disc_estimate` | searched engine estimate of final disc difference | teacher-generated positions |
| `wld` | win/draw/loss only | exact or solved data |
| `policy_move` | played/best move target | later policy or ordering experiments |

Pattern-only evaluation should start with observed final-disc-difference labels
and `engine_disc_estimate`. Transcript-derived observed labels must not be
reported as teacher-search estimates.

Local teacher label overlays are an explicit intermediate step between
normalized TSV generation and pattern dataset generation. The overlay consumes
normalized schema v2 rows and a local teacher label TSV keyed by `board_id`,
then writes another normalized schema v2 TSV with only label fields replaced.
Sampling and measurement split policy should run before overlay so the teacher
file is applied to the exact normalized rows that feed training.

Teacher label TSV schema:

```text
board_id	label_kind	label_unit	label_perspective	label_score_side_to_move	teacher_source	teacher_depth	teacher_nodes
```

Teacher label rules:

* `board_id` must match normalized schema v2 `board_id`
* `label_kind` should be `teacher_exact_final_disc_diff`,
  `teacher_search_final_disc_diff`, or `teacher_static_eval_disc_diff`
* `label_unit` is `disc`
* `label_perspective` is `side_to_move`
* `label_score_side_to_move` is an integer in `[-64, 64]`
* `teacher_source` is a generic non-empty source label
* `teacher_depth` and `teacher_nodes` are non-negative integers, or empty

Teacher label overlays are local-only fitting diagnostics. Generated teacher
label files and overlaid normalized TSVs must not be committed. Exact/search
teacher labels are not strength claims, Elo results, match bench results,
self-play results, or production artifacts until separate gates exist.

Exact endgame teacher labels are generated only for low-empty normalized schema
v2 rows by `tools/pattern/labels/generate_exact_endgame_teacher_labels.cc`.
The generator streams the normalized TSV, keeps only eligible de-duplicated
`board_id` candidates in memory, uses the existing public exact endgame search
API, and writes teacher label TSV rows in sorted `board_id` order. It does not
generate opening or midgame teacher labels, does not add neural evaluator logic,
does not run self-play, and does not publish production artifacts. Bounded
local comparisons should use validation MAE as the primary selector; test MAE
is reporting and tie-break only.

Rules:

* labels must declare unit and perspective
* early random opening moves may be excluded by dataset policy
* weak or noisy labels are allowed only if split metrics are tracked separately
* WLD labels are not silently converted into exact disc differences

## Pattern Definitions

Pattern definitions are versioned schema, not training script details.

Each pattern definition should include:

* `pattern_id`
* square list in board-core coordinates
* symmetry handling
* ternary index order
* phase applicability
* table offset in the artifact

Example schema:

```json
{
  "pattern_set": "pattern-v1-buro-style",
  "patterns": [
    {
      "pattern_id": "edge-8",
      "squares": ["a1", "b1", "c1", "d1", "e1", "f1", "g1", "h1"],
      "symmetry": "d4-canonical",
      "index_base": 3
    }
  ]
}
```

Runtime and training must share the same schema tests.

Current local research pattern sets:

* `pattern-v1-buro-lite` keeps the raw edge, near-edge, diagonal,
  `corner-2x5`, and `corner-3x3` families in a stable ordered schema.
* `pattern-v2-endgame-lite` preserves the `pattern-v1-buro-lite` order and
  appends bounded endgame-oriented raw tables:
  `corner-2x4-8`, `edge-plus-x-10`, `corner-wing-8`,
  `near-edge-segment-8`, and `diagonal-corner-8`. Added table lengths are at
  most 10, the largest table is `3^10`, and the set has 185,895 table entries
  per phase plus the phase-bias slot.

## Feature Encoding

Use ternary pattern indices.

For each square in a pattern:

* empty = 0
* player disc = 1
* opponent disc = 2

Index calculation:

```text
index = sum(value[i] * 3^i)
```

Rules:

* index order is documented and tested
* equivalent symmetries map to canonical indices when configured
* missing or invalid squares are impossible by construction
* phase is computed from occupied disc count using artifact metadata

The pattern dataset builder supports two TSV layouts for local training input:

* `expanded-tsv` emits one row per feature occurrence and remains the default
  compatibility format.
* `compact-tsv` emits one row per normalized position/example with
  `pattern_features` encoded as
  `pattern_id:instance:ternary_index,pattern_id:instance:ternary_index,...`.

Compact feature order is deterministic and matches expanded emission order:
pattern table order, then feature instance order. Duplicate feature occurrences
are preserved. Trainer weight keys continue to ignore `instance`, so repeated
feature occurrences remain repeated contributions rather than distinct weights.
The compact format is local measurement infrastructure for reducing dataset
row count and trainer input overhead; it is not strength evidence by itself.

## Learning Algorithm

Start with regularized linear regression or SGD over pattern indices.

The first trainer should optimize clarity and reproducibility, not maximum
throughput.

Recommended progression:

1. per-phase mean or bias baseline
2. per-pattern table fitting with L2 regularization
3. frequency-based shrinkage for rare indices
4. phase smoothing between adjacent phases
5. held-out calibration from score to win probability
6. high-throughput sparse trainer only after metrics are stable

Training must be deterministic when given the same input manifests, seed, and
options.

The local `pattern-sgd-v0c` trainer keeps the runtime-compatible v0b score
shape while making optimizer semantics and diagnostics explicit. It learns
train-split phase biases first, then keeps those biases fixed while SGD updates
only pattern weights over residual labels. The per-example error gradient is
distributed over feature occurrences, optional gradient clipping applies to
that per-feature error gradient, and weight decay applies only to pattern
weights. The learned weight key remains `phase + pattern_id + ternary_index`;
`instance` is validated input metadata, not part of the runtime weight key.

`pattern-sgd-v0c` is local research infrastructure. Its fitting reports may be
used to compare train/validation/test behavior, residuals, optimizer behavior,
and split-by-phase coverage, but they are not Elo, match bench, self-play, or
production artifact claims.

## Metrics

Every training run should emit a report.

Minimum fitting metrics:

* number of source records
* number of valid positions
* number of rejected positions
* duplicate rate
* train/validation/test split sizes
* MAE by phase
* RMSE by phase
* exact sign accuracy
* WLD accuracy when WLD labels exist
* score calibration curve when calibration exists
* artifact size
* evaluation speed with the exported artifact

Research trainer reports should also include residual metrics against fixed
phase bias, split-by-phase empty buckets, optimizer settings, epoch diagnostics,
and weight diagnostics such as nonzero count, L2 norm, maximum absolute weight,
phase/pattern counts, and largest absolute weights.

Strength metrics are separate from fitting metrics.

Minimum strength checks:

* fixed-position search benchmark
* short self-play regression
* match bench versus previous artifact
* move agreement with a trusted teacher on a fixed position set

## Artifact Export

The exporter writes both binary weights and a JSON manifest.

Rules:

* binary weight file is little-endian
* manifest includes schema version and checksums
* exporter refuses to publish artifacts with unclear dataset terms unless forced
  locally
* artifact is reproducible from the training manifest, or the run report
  explains why not
* runtime loader validates compatibility before use

## License and Provenance Gates

Pattern learning has stricter provenance rules than normal code.

Before using a dataset in a publishable artifact, answer:

1. Can we download and use the data for training?
2. Can we redistribute the raw data?
3. Can we redistribute derived datasets?
4. Can we redistribute trained weights derived from it?
5. Is attribution required?
6. Is commercial use allowed?
7. Is the source produced by GPL software, and do its terms say anything about
   generated outputs?

If any answer is unknown, mark the run as local-only and do not publish the raw
data, derived data, or weights.

Code-copying policy:

* do not copy GPL engine code
* do not copy GPL evaluation tables or weights
* do not translate GPL implementation details line-by-line
* use papers, public descriptions, black-box comparison, and independently
  written code

## Testing Strategy

Pattern-learning tests should cover:

* importer accepts known-good tiny fixtures
* importer rejects malformed records
* imported positions replay with board core
* pass handling round-trips where the source format has pass
* feature extraction matches hand-computed indices
* symmetry canonicalization is stable
* phase mapping boundaries are correct
* train split is deterministic
* tiny trainer smoke test produces deterministic weights
* exported artifact loads in runtime evaluation
* manifest checksum mismatch is rejected

Training tests should use tiny synthetic data.

They should not require external corpora.

## Build Order

Recommended build order:

1. add `data/corpora/README.md` with data policy
2. add tiny synthetic fixture records
3. add import manifest schema and validator
4. add transcript importer for one simple text format
5. add feature schema and hand-computed feature tests
6. add tiny deterministic trainer smoke test
7. add artifact exporter and runtime loader compatibility test
8. add local-only external corpus scripts
9. add match bench against previous weights
10. add publication gate for artifacts

## Completion Bar

Pattern learning is ready to support production evaluation when:

* all training inputs have manifests
* raw external corpora are kept out of git by default
* feature schema is versioned
* tiny trainer is deterministic
* exported artifacts load in runtime evaluation
* validation metrics are generated automatically
* match benchmarks can compare two artifacts
* license/provenance status is visible before publishing weights
