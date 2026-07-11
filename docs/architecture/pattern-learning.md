# Pattern Learning Architecture

## Purpose

Pattern learning turns local training sources into learned runtime evaluation
artifacts for the pattern evaluator.

This document defines the stable architecture contract: ownership boundaries,
data flow, local-only data rules, normalized training schemas, label contracts,
feature contracts, trainer/exporter responsibilities, and the handoff to
runtime artifacts. It does not track current project status, experiment history,
command lines, or artifact-specific evidence.

Pattern learning must keep the engine runtime deterministic, small, and
license-safe. Learned artifacts are not strength, Elo, self-play, or production
claims unless separate validation gates say so.

## Boundaries

Pattern learning owns:

* source ingestion into training-oriented normalized data
* local corpus manifests and provenance metadata needed by training runs
* training-position normalization
* label overlays and generated teacher-label datasets
* train/validation/test measurement split assignment
* pattern dataset construction
* training-time pattern feature extraction
* fitting local weights and trainer diagnostics
* exporting candidate runtime payloads for artifact review

Pattern learning does not own:

* board rules, legal move generation, pass handling, or terminal detection
* search algorithm semantics or exact endgame ownership
* recursive runtime evaluator behavior
* runtime pattern set definitions
* default artifact resolution, promotion, rollback, or commit policy
* UI rendering or analysis presentation

Board core is the source of truth for positions and move application. Runtime
evaluation owns the production pattern definitions and artifact loading
contract. Pattern learning may use search/endgame tools to generate local
labels, but search and runtime evaluation must not depend on training code.

## Data Flow

The architecture flow is:

```text
local source records or generated positions
        |
        v
source import or sequence/transcript replay
        |
        v
normalized TSV schema v2
        |
        v
optional local teacher labels or move-teacher child labels
        |
        v
pattern dataset rows
        |
        v
trainer outputs and diagnostics
        |
        v
exporter candidate payload
        |
        v
reviewed data/eval runtime artifact
```

Source import, teacher labels, datasets, trainer outputs, diagnostics, and
candidate exports before final artifact review are local-only intermediate
outputs. Only reviewed runtime artifact payloads may cross into `data/eval/**`,
and that handoff follows `docs/architecture/evaluation-artifacts.md`.

## Source Data Policy

Raw external data is not committed to the repository.

The following generated or derived files are local-only unless a separate
artifact policy explicitly allows them:

* normalized TSVs
* selected TSVs
* teacher labels
* move-teacher TSVs
* child-normalized TSVs
* cache materialization outputs
* measurement reports
* trainer reports
* logs
* temporary pattern datasets
* sweep outputs
* trainer weights before final artifact export
* candidate artifact exports before review

Every external or generated source used for training must have enough manifest
or provenance metadata to identify the source, terms, checksum, and intended
local path. Detailed corpus-source policy belongs to
`data/corpora/README.md`. Detailed teacher-label file policy belongs to
`data/labels/README.md`.

Committed learned runtime artifacts live under `data/eval/**` and must follow
the artifact layout, default pointer, promotion, rollback, provenance, and
commit-policy contracts in `docs/architecture/evaluation-artifacts.md`.

## Normalized Position Schema

Sequence and transcript importers read local source material and write
normalized TSV schema v2. Importers must use board-core replay and serialization
semantics rather than independent Othello rules.

Normalized schema v2 records the identities needed to distinguish game,
board, source occurrence, split, and side-to-move label perspective:

```text
record_id
position_id
game_group_id
board_id
source_occurrence_id
source_dataset_id
split
board_a1_to_h8
label_kind
label_unit
label_perspective
label_score_side_to_move
occupied_count
phase
player_disc_count
opponent_disc_count
empty_count
```

Schema rules:

* `game_group_id` identifies the semantic replayed game or transcript group.
* `board_id` identifies the exact side-to-move-relative board.
* `source_occurrence_id` identifies the source occurrence without driving
  semantic grouping.
* `split` is one of `train`, `validation`, or `test`.
* `board_a1_to_h8` uses the normalized side-to-move `X`/`O` convention.
* labels are side-to-move-relative unless the row explicitly declares another
  perspective.
* `occupied_count`, `empty_count`, and `phase` are derived from board state and
  artifact-compatible phase rules.

The connected-board-game split is a measurement split policy for
sequence-derived validation. It keeps rows sharing a semantic game or identical
side-to-move-relative board in the same measurement split. It is split hygiene,
not a strength claim, label-quality claim, or artifact release gate.

## Label Contracts

Labels must declare kind, unit, perspective, and score field semantics.

Supported architecture-level label kinds include:

| Label kind | Contract |
| --- | --- |
| `observed_final_disc_diff` | Observed transcript final disc difference from the side-to-move perspective; not a searched teacher estimate. |
| `teacher_exact_final_disc_diff` | Exact final disc difference from the side-to-move perspective for eligible local rows. |
| `teacher_search_final_disc_diff` | Search-generated final disc estimate from the side-to-move perspective. |
| `teacher_static_eval_disc_diff` | Static-evaluator estimate in disc units from the side-to-move perspective. |
| `teacher_exact_move_child_final_disc_diff` | Exact after-move child final disc difference from the child side-to-move perspective. |
| `wld` | Win/draw/loss labels only; not silently converted to exact disc differences. |
| `policy_move` | Move target for later policy or ordering work. |

Teacher label overlays consume normalized schema v2 rows and a local
teacher-label TSV keyed by `board_id`, then emit normalized schema v2 rows with
only label fields changed. The teacher-label TSV contract is defined in
`data/labels/README.md`.

Move-teacher labels connect local child solves to root move choice. Root and
child boards use the normalized side-to-move `X`/`O` convention, child labels
are from the child side-to-move perspective, and predicted root move scores are
derived as the negative child score. Exact child rows use
`label_kind = teacher_exact_move_child_final_disc_diff`; artifact-search child
rows use `teacher_search_final_disc_diff` and must carry explicit artifact and
search-config provenance in the move-teacher schema. Child board identities
remain canonical normalized-board identities, not root/move identifiers;
cross-split child-board collisions must reject materialization. Search labels
written through normalized schema v2 must satisfy its disc-difference range.

Generated teacher-label, move-teacher, and child-normalized files are local-only
intermediate outputs. They are not Elo results, self-play results, production
strength claims, or runtime artifacts.

Search move-teacher TSV v2 carries mandatory teacher provenance. A trainer must
reject a v2 input unless `teacher_kind`, `teacher_source`,
`teacher_artifact_id`, `teacher_artifact_checksum`, and
`teacher_search_config_id` are identical across the complete input file.
Per-move depth and node counts may differ and are not part of this equality
check. Trainer reports preserve the accepted v2 provenance for local review.

## Pattern Dataset Contract

Pattern datasets are derived local training inputs built from normalized schema
v2 rows and runtime-owned pattern definitions.

The dataset builder must preserve:

* normalized row identity needed for diagnostics
* split assignment
* label kind, unit, perspective, and score
* phase
* deterministic feature order
* enough source/provenance metadata for local reports

Expanded dataset rows, when used, emit one row per feature occurrence.
Compact dataset rows are the scalable local training shape. A compact row emits
one normalized position/example with a deterministic `pattern_features` payload:

```text
pattern_id:instance:ternary_index,pattern_id:instance:ternary_index,...
```

Compact feature order must match expanded emission order: pattern table order,
then feature instance order. Duplicate feature occurrences are preserved.
Trainer weight keys ignore `instance`, so repeated occurrences remain repeated
contributions rather than distinct runtime weights.

## Pattern Feature Contract

Pattern feature extraction must use runtime-owned pattern set definitions and
deterministic ternary indexing.

The runtime pattern definitions specify:

* pattern set id
* pattern ids
* square lists in board-core coordinates
* symmetry policy
* ternary index order
* phase applicability
* table offsets and sizes

Ternary feature values are:

* empty square: `0`
* side-to-move disc: `1`
* opponent disc: `2`

The index is:

```text
index = sum(value[i] * 3^i)
```

Training, export, and runtime evaluation must agree on the same pattern set
definition and index semantics. Runtime definitions and production evaluator
behavior belong to `docs/architecture/evaluation.md`.

`pattern-v1-buro-lite` is the earlier production-ish schema. It keeps the raw
edge, near-edge, diagonal, `corner-2x5`, and `corner-3x3` families in a stable
ordered runtime schema.

`pattern-v2-endgame-lite` is the bounded endgame-oriented pattern set used by
the current experimental default artifact. It preserves the v1 order and adds
bounded late-phase families with table lengths capped for the existing runtime
artifact shape.

`pattern-rank-v0e` is a local pairwise-ranking trainer for move-teacher child
rows. It keeps the runtime model as `V(child)` and derives the root move score
as `-V(child)`. It joins the local move-teacher TSV to the local pattern dataset
by child board id, groups rows by `root_board_id`, excludes teacher ties from
the ranking loss according to its explicit tie margin, and may add a small
Huber child-value calibration loss to keep score scale usable. Its output uses
the existing phase-bias and sparse pattern-weight schema; it does not alter the
runtime pattern set or artifact format.

Future pattern-set investigations such as `pattern-v3` or larger objective
changes are not part of the current architecture contract until adopted through
separate design and validation.

## Trainer and Exporter Responsibilities

Trainers consume local pattern datasets and produce local fitting outputs,
diagnostics, and candidate weights. Trainer behavior must be deterministic for
the same input data, manifest/provenance metadata, seed, pattern set, and
options.

Campaign trainers report the sorted child phases actually targeted by train
updates as `trained_phases`. Campaign export forwards that reviewed coverage to
the exporter; it must not infer coverage from root phases, nonzero weights, or
missing metadata.

The pairwise rank trainer may warm-start from a schema-compatible weights JSON.
Warm-start provenance includes its content checksum and declared prior coverage.
Campaign resume metadata must also fingerprint the warm-start file contents;
the command path alone is not sufficient to validate reuse of prior outputs.
Explicit frozen phases retain both prior phase bias and pattern weights exactly;
the final coverage is the union of prior reviewed coverage and phases actually
updated by the current run.

Trainer diagnostics may include fitting error, split and phase summaries,
optimizer statistics, residual summaries, weight norms, sparsity, and local
decision diagnostics. These diagnostics are local review evidence only. They do
not publish a runtime artifact and do not imply playing strength.

Trainer outputs before final artifact export are local-only intermediate
outputs. Do not commit raw trainer weights, temporary datasets, reports, logs,
or sweep outputs as architecture artifacts.

Exporters convert reviewed local trainer outputs into candidate runtime payloads
with binary weights and manifest metadata. Export must preserve the runtime
loader contract: score unit, scale, phase count, pattern set id, weight checksum,
binary format, and pattern table layout.

Export may use a positive uint16 fixed-point scale to preserve sub-disc
updates, and may declare an inclusive fallback-additive phase boundary when
early learned weights were trained as residuals rather than replacements.
Both settings are runtime policy and must be retained in local arena evidence.

Reviewed training coverage is exported as explicit `trained_phases` metadata.
It must come from the reviewed training route, not be inferred from nonzero
weights after quantization.

Exporter and committed artifact boundaries are delegated to
`docs/architecture/evaluation-artifacts.md`. That document owns committed
artifact layout, default pointer handling, promotion, rollback, provenance, and
commit policy.

## Runtime Artifact Handoff

Pattern learning hands off only reviewed final runtime payloads. A committed
learned artifact must live under `data/eval/**` and satisfy the evaluation
artifact contract before it becomes part of default runtime resolution.

The handoff must keep local-only material out of the committed payload:

* raw source transcripts or archives
* normalized corpora
* selected TSVs
* teacher labels
* move-teacher TSVs
* child-normalized TSVs
* trainer datasets
* trainer reports
* logs
* temporary or sweep outputs

Runtime evaluation loads artifacts through the contract in
`docs/architecture/evaluation-artifacts.md` and evaluates positions through the
runtime evaluation architecture in `docs/architecture/evaluation.md`.

## What Belongs Elsewhere

Current implementation status belongs in `docs/progress/pattern-learning.md`.

Experiment history and evidence belong in `docs/experiments/README.md`.

Artifact layout, default pointer, promotion, rollback, and commit policy belong
in `docs/architecture/evaluation-artifacts.md`.

Corpus source policy belongs in `data/corpora/README.md`.

Label schema and local teacher-label policy belong in `data/labels/README.md`.

Runtime evaluation architecture belongs in `docs/architecture/evaluation.md`.
