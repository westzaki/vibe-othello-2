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

Recommended local workflow:

1. Build a connected-board-game 100k normalized/dataset measurement.
2. Apply local teacher labels to the exact normalized rows used for training.
3. Train v0c/v0d on the teacher-labeled dataset.
4. Compare observed-label and teacher-label runs by validation diagnostics.
5. Later, run match bench, Elo, or self-play gates before making strength
   claims.

Next: add exact endgame teacher label generator for low-empty positions using
the existing search/endgame APIs.
