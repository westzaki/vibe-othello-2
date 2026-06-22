# Teacher Label Data Policy

This directory documents local-only teacher label files for pattern learning.

Do not commit generated teacher label TSVs, teacher-overlaid normalized TSVs,
pattern datasets, weights, artifacts, local reports, logs, raw corpus payloads,
or personal absolute paths.

Teacher label TSVs consumed by `tools/pattern/labels/apply_teacher_labels.py`
use this header:

```text
board_id	label_kind	label_unit	label_perspective	label_score_side_to_move	teacher_source	teacher_depth	teacher_nodes
```

Field rules:

* `board_id` is the non-empty normalized schema v2 board id.
* `label_kind` is one of `teacher_exact_final_disc_diff`,
  `teacher_search_final_disc_diff`, or `teacher_static_eval_disc_diff`.
* `label_unit` is `disc`.
* `label_perspective` is `side_to_move`.
* `label_score_side_to_move` is an integer in `[-64, 64]`.
* `teacher_source` is a non-empty generic source label such as
  `synthetic-fixture`, `exact-endgame`, or `search-depth-N`.
* `teacher_depth` and `teacher_nodes` are non-negative integers, or empty when
  not applicable.

Teacher labels and overlaid normalized TSVs are fitting diagnostics only. They
are not strength claims, Elo results, match bench results, self-play results,
or production artifacts.

Exact endgame teacher labels can be generated locally for low-empty normalized
schema v2 rows:

```sh
./build/tools/pattern/labels/vibe-othello-generate-exact-endgame-teacher-labels \
  --normalized-tsv "$RUN_DIR/resplit-normalized.tsv" \
  --teacher-labels-out "$RUN_DIR/exact-teacher-labels.tsv" \
  --report-out "$RUN_DIR/exact-teacher-report.json" \
  --max-empty 12 \
  --max-positions 5000 \
  --seed 0 \
  --progress-every 100
```

The generator reads normalized schema v2 only, rejects schema v1, uses
`board_id` as the key, solves only rows with `empty_count <= --max-empty`, and
emits rows in sorted `board_id` order. The score is the exact final disc
difference from the side-to-move perspective, using the existing exact endgame
search API. It is intended for endgame/low-empty coverage only.

## Low-Empty Root Selection

Use `tools/pattern/labels/select_low_empty_roots.py` when a local experiment
needs a deterministic low-empty normalized schema v2 root TSV:

```sh
python3 tools/pattern/labels/select_low_empty_roots.py \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/<input-normalized-schema-v2.tsv>" \
  --output-tsv "$VIBE_OTHELLO_MEASUREMENTS/<run>/selected-low-empty-normalized.tsv" \
  --report-out "$VIBE_OTHELLO_MEASUREMENTS/<run>/selected-low-empty-report.json" \
  --max-empty 12 \
  --max-roots 50000 \
  --seed 0 \
  --dedupe-key board_id \
  --preserve-split \
  --require-schema-v2
```

The selector rejects schema v1, filters `empty_count <= --max-empty`,
de-duplicates by `board_id`, samples by deterministic `board_id + seed` hash,
preserves the original selected rows and split assignments, and writes a
local-only report with input/eligible/selected counts, duplicate board rows,
split/empty/phase counts, checksums, command arguments, and non-claim notes.
It fails when fewer unique roots are available than `--max-roots` unless
`--allow-less-than-requested` is explicitly set. Do not use that override for a
claimed 50k run.

Recommended local workflow:

1. Build a connected-board-game 100k normalized/dataset measurement.
2. Generate exact endgame labels from the exact normalized rows used for
   training.
3. Apply local teacher labels with `apply_teacher_labels.py`, usually with
   `--missing-policy drop` for low-empty-only experiments.
4. Train v0c/v0d on the observed-label low-empty rows and exact-teacher
   low-empty rows.
5. Compare observed-label and teacher-label runs by validation diagnostics.
6. Later, run match bench, Elo, or self-play gates before making strength
   claims.

The optional local campaign helper performs steps 2 through 5 for a bounded
late-phase diagnostic:

```sh
python3 tools/pattern/labels/run_exact_teacher_late_phase_campaign.py \
  --normalized-tsv "$RUN_DIR/resplit-normalized.tsv" \
  --output-dir "$RUN_DIR/exact-teacher-late-phase" \
  --max-empty 12 \
  --max-positions 5000 \
  --seed 0 \
  --trainer-mode pattern-sgd-v0c
```

Pass `--pattern-set pattern-v2-endgame-lite` to run the same local diagnostic
with the bounded endgame-lite research pattern set.

The campaign selects by validation MAE first. Test MAE is reporting and
tie-break only. Treat exact labels as helpful only after at least 0.2 MAE
absolute validation improvement or at least 1 percent relative validation
improvement.

## Move-Teacher Decision Labels

Static root labels did not reliably create root best-move changes in the
pattern signal bottleneck diagnostics. Move-teacher data connects exact labels
to root decisions by enumerating legal moves at low-empty roots, exact-solving
each child, and training/evaluating artifacts on after-move child positions.

Generate a local move-teacher dataset:

```sh
./build/tools/pattern/labels/vibe-othello-generate-exact-move-teacher-dataset \
  --normalized-tsv "$RUN_DIR/resplit-normalized.tsv" \
  --move-teacher-out "$RUN_DIR/move-teacher.tsv" \
  --child-normalized-out "$RUN_DIR/child-normalized.tsv" \
  --report-out "$RUN_DIR/move-teacher-report.json" \
  --max-empty 12 \
  --max-roots 5000 \
  --seed 0 \
  --progress-every 100
```

The move-teacher TSV header is:

```text
root_board_id	root_record_id	root_split	root_phase	root_empty_count	move	child_board_id	child_board_a1_to_h8	child_empty_count	child_phase	root_move_score_side_to_move	child_label_score_side_to_move	is_best_move	best_move_tie_count	move_rank	best_score_margin	teacher_source	teacher_depth	teacher_nodes
```

Sign convention:

* root and child boards use normalized side-to-move-relative `X`/`O`
* exact child labels are from the child side-to-move perspective
* root move scores are `-child_label_score_side_to_move`
* `teacher_depth` is the child empty count solved exactly

The child-normalized TSV is schema v2-compatible and uses:

```text
label_kind = teacher_exact_move_child_final_disc_diff
label_unit = disc
label_perspective = side_to_move
source_dataset_id = exact-move-teacher-v1
```

Build a child pattern dataset, train with an existing value trainer, export,
and evaluate move ranking:

```sh
python3 tools/pattern/labels/run_move_teacher_decision_campaign.py \
  --normalized-tsv "$RUN_DIR/resplit-normalized.tsv" \
  --output-dir "$RUN_DIR/move-teacher-decision" \
  --max-empty 12 \
  --max-roots 5000 \
  --seed 0 \
  --pattern-set pattern-v2-endgame-lite \
  --trainer-mode pattern-sgd-v0c \
  --learning-rate 0.1 \
  --lr-schedule inverse-sqrt \
  --weight-decay 0.0001 \
  --resume
```

Ranking reports include top-1 exact-best accuracy, tie-aware top-1 accuracy,
exact best in predicted top 2, pairwise accuracy, mean/median teacher regret,
exact-best predicted rank, all-same predicted roots, and breakdowns by empty
count, phase, and split.

`--resume` is conservative: each skipped stage must have a matching
`*.resume.json` sidecar with the current command, input checksums, and output
checksums. If metadata is missing or mismatched, the helper fails instead of
mixing stale move-teacher rows, datasets, weights, ranking reports, or arena
reports into a new campaign report.

To repeat the same campaign over bounded root-count and seed matrices, use the
local-only matrix wrapper:

```sh
python3 tools/pattern/labels/run_move_teacher_campaign_matrix.py \
  --normalized-tsv "$RUN_DIR/selected-low-empty-normalized.tsv" \
  --output-dir "$RUN_DIR/move-teacher-decision-matrix" \
  --root-counts 5000,10000 \
  --seeds 0,1,2 \
  --pattern-set pattern-v2-endgame-lite \
  --trainer-mode pattern-sgd-v0c \
  --resume
```

The matrix wrapper delegates generation, training, export, ranking, arena, and
resume safety to `run_move_teacher_decision_campaign.py`, then writes local-only
`matrix-report.json` and `matrix-summary.md`.

To turn that matrix into a policy-driven local growth cycle, run the higher
level orchestrator:

```sh
python3 tools/pattern/train/run_pattern_growth_cycle.py \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/<connected-or-low-empty-normalized.tsv>" \
  --output-dir "$VIBE_OTHELLO_MEASUREMENTS/<pattern-growth-cycle-run>" \
  --pattern-set pattern-v2-endgame-lite \
  --baseline-root-label-weights "$VIBE_OTHELLO_MEASUREMENTS/<exact-root-v2.weights.bin>" \
  --baseline-root-label-manifest "$VIBE_OTHELLO_MEASUREMENTS/<exact-root-v2.manifest.json>" \
  --baseline-v1-weights "$VIBE_OTHELLO_MEASUREMENTS/<v1.weights.bin>" \
  --baseline-v1-manifest "$VIBE_OTHELLO_MEASUREMENTS/<v1.manifest.json>" \
  --root-counts 5000,10000,20000 \
  --seeds 0,1,2 \
  --arena-depths 3,5 \
  --arena-seeds 0,10,20 \
  --arena-max-positions 1000 \
  --resume
```

The growth-cycle runner writes local-only `growth-cycle-report.json` and
`growth-cycle-summary.md`. It preflights input availability, downgrades root
counts when the low-empty input is smaller than requested, runs or reuses the
move-teacher matrix, schedules bounded artifact arenas for promotable
candidates, checks same-artifact and swap sanity, and emits a promotion
category plus the next recommended action. It may also stop early when the
decision-leverage gate fails, a critical sanity check fails, or local inputs are
insufficient.

When `--resume` is used without an explicit `--matrix-report`, the growth-cycle
runner delegates back to the matrix helper so its resume metadata and checksum
validation runs before the growth-cycle scorecard loads the matrix report.

These files remain local-only diagnostics. They are not Elo, not self-play, not
production strength, and not publication gates.
