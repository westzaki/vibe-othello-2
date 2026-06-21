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
