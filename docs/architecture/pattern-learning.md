# Pattern Learning Architecture

## Purpose

Pattern learning turns local training sources into learned runtime evaluation
artifacts for the pattern evaluator.

This document defines the stable architecture contract: ownership boundaries,
data flow, local-only data rules, normalized training schemas, label contracts,
feature contracts, trainer/exporter responsibilities, and the handoff to
runtime artifacts. It also records the currently adopted route at a level that
helps implementation work. Detailed commands, run history, and superseded
experiments remain outside the default reading path.

Pattern learning must keep the engine runtime deterministic, small, and
license-safe. Learned artifacts are not strength, Elo, self-play, or production
claims unless separate validation gates say so.

## Implemented System and Current Route

The repository implements the pipeline as independent tools under
`tools/data-import/` and `tools/pattern/`:

```text
external corpus
  -> board-core-backed import/normalization
  -> optional root selection and exact/search move teachers
  -> compact pattern datasets or bounded-memory streaming trainers
  -> trainer-weight JSON
  -> runtime artifact export
  -> paired artifact Arena review
```

Shared normalized-schema validation, canonical board identities, split audits,
pattern catalog parity checks, checksum-bound reports, and resume sidecars keep
the stages composable without making generated data part of the repository.
Exact teacher generation calls the public endgame solver; search teachers load
an explicit artifact and search configuration. Training and export reuse the
runtime-owned pattern catalog and ternary indexing contract.

The current experimental default is
`pattern-v2-egaroucid-lv17-full-value-v1` on the
`pattern-v2-endgame-lite` runtime schema. Its adopted final training route
streams all 25,514,097 Egaroucid board-score positions without materializing an
expanded dataset: 1,514,097 positions at 4–15 occupied squares use the declared
Egaroucid 7.4.0 lv17 enumeration/evaluation/negamax route, while 24,000,000
positions at 16–63 occupied squares use declared Egaroucid 7.5.1 lv17 self-play
terminal outcomes. Phases 0–9 receive five full-corpus residual-training passes;
phases 10–12 receive one replacement-weight pass.

Promotion used independent paired fixed-depth and fixed-time Arena comparisons
against the prior full-WHTOR default, a zero-overlap audit between the 1,000-pair
16-ply promotion suite and all training boards, and short-opening gates that
directly exercised phases 0–2. Exact metrics and source notices live in the
artifact README and experiment archive. The headline score rates were 68.05%
at depth 3, 67.97% at depth 5, and 68.85% at 10 ms plus exact-8; short-opening
depth-3 gates scored 69.92%, 71.48%, and 66.70% at 4, 8, and 11 plies. Every
paired interval excluded 50% and all games were clean. These are evidence for
selecting an experimental engineering default, not a production-strength or
publication claim. The prior full-WHTOR artifact remains committed for
comparison and rollback.

The current default was reached through earlier normalized-v2, move-teacher,
pairwise-ranking, hard-root overlay, WHTOR policy, and independent-Arena stages.
Those facilities remain supported tooling, but their chronological run log is
history rather than architecture and lives under `docs/experiments/`.

## Current Limitations

Training workflows are local campaign tools, not a hosted or productionized
trainer service. Generated corpora, labels, caches, datasets, weights, reports,
and logs remain local-only until a reviewed minimal runtime artifact crosses the
publication boundary. Fitting metrics alone never authorize promotion, and the
repository does not claim external-engine Elo or self-play production strength
for the current default.

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
  For an independently supplied board-score row with no transcript identity,
  it identifies the canonical board group so identical boards remain grouped.
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
| `teacher_value_disc_diff` | Neutral teacher value in disc-difference units whose generation procedure is declared separately by source range. |
| `teacher_exact_move_child_final_disc_diff` | Exact after-move child final disc difference from the child side-to-move perspective. |
| `wld` | Win/draw/loss labels only; not silently converted to exact disc differences. |
| `policy_move` | Move target for later policy or ordering work. |

WTHOR numeric actual and theoretical scores use a 64-square scoring convention
that can differ from the engine's terminal actual-disc difference when play
ends with empty squares. A WTHOR importer must not silently emit that numeric
score as `teacher_exact_final_disc_diff`. Engine-replayed terminal transcripts
may emit `observed_final_disc_diff`; the WTHOR theoretical score may emit exact
`wld` only at the empty-count cutoff declared by the WTB header. Such a WLD
sidecar remains separate from the current disc-difference pattern trainer
until an explicitly WLD-aware objective is adopted.

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

Search move-teacher TSV v3 carries mandatory teacher provenance and a
`child_baseline_score_side_to_move` field for residual training. A trainer must
reject a v3 input unless `teacher_kind`, `teacher_source`,
`teacher_artifact_id`, `teacher_artifact_checksum`, and
`teacher_search_config_id` are identical across each input TSV. Progressive
training passes retained shallow roots and deeper overlay roots as disjoint
sidecars, preserving one explicit provenance per file in the trainer report.
Per-move depth and node counts may differ and are not part of this equality
check.
Ordinary teacher coverage requires declared phases `0..12`; an explicit
phase-aware bootstrap may use reported fallback phases, but legacy implicit
coverage remains invalid.

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
runtime pattern set or artifact format. When an inclusive residual boundary is
selected, `V(child)` becomes the v3 static baseline plus the learned phase bias
and pattern term through that boundary. The baseline is constant for gradient
calculation. Export must carry the identical
`fallback_additive_through_phase`, otherwise training and runtime semantics do
not match.

An optional aggregate played-move policy may augment `pattern-rank-v0e`
without modifying the search move-teacher contract. It joins by
`root_board_id`, validates split, phase, and legal-move membership, and uses
observed move frequency as a cross-entropy target over `-V(child)` logits.
This objective does not overwrite search scores or child labels. Its weight,
matched/unmatched coverage, occurrence counts, and split metrics must remain
visible in the trainer report. A played move is empirical policy evidence, not
an exact-best-move assertion.

The full-corpus WTHOR policy route avoids materializing an expanded child
dataset. Its streaming trainer replays one game at a time, visits every
recorded decision, and ranks the played child against deterministic legal
alternatives. Forced single choices are provenance counts rather than
zero-information updates. A transcript-identity holdout scores every legal
move for final top-1 and cross-entropy diagnostics. The objective preserves
`-V(child)` root semantics and the early/midgame fallback residual; reviewed
late exact/search phases may be frozen. Streaming trainer output uses the same
v2 trainer-weight and runtime exporter contracts as `pattern-rank-v0e`.
Because played moves are provisional empirical targets, a deeper-search
correction campaign and independent paired arenas are required before
promotion. The correction campaign must be reviewed, but its candidate need
not be adopted when the measured result is neutral or regressive. In that
case, the uncorrected candidate may advance only when the independent direct
arenas against the current default pass.

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

Experiment history and evidence belong in `docs/experiments/README.md`.

Artifact layout, default pointer, promotion, rollback, and commit policy belong
in `docs/architecture/evaluation-artifacts.md`.

Corpus source policy belongs in `data/corpora/README.md`.

Label schema and local teacher-label policy belong in `data/labels/README.md`.

Runtime evaluation architecture belongs in `docs/architecture/evaluation.md`.
