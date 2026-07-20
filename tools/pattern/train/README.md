# Progressive Search-Teacher Training

The promoted v2 route extends an existing artifact without changing its
pattern schema:

```text
runtime artifact
  -> trainer-weight import
  -> phase-balanced hard-root selection
  -> deeper search teaching
  -> complete-root overlay
  -> warm-start residual rank training
  -> runtime export and independent arenas
```

All corpora, teacher rows, trainer weights, and reports are local-only. Store
source archives under the external `$VIBE_OTHELLO_CORPORA` directory and
generated runs under the external `$VIBE_OTHELLO_TRAINING` directory. Only a
reviewed runtime artifact with its manifest, provenance, README, and NOTICE
belongs under `data/eval/artifacts/`.

## Full Egaroucid board-score pretraining

`vibe-othello-egaroucid-board-score-pretrain` is the bounded-memory route for
using every position in extracted Egaroucid board-score `.txt` files. It loads
an existing `pattern-v2-endgame-lite` runtime artifact, streams each source row,
and applies Huber value updates without materializing normalized or expanded
training data.

Keep the source ZIP under `$VIBE_OTHELLO_CORPORA` and extract its text members
under `$VIBE_OTHELLO_TRAINING/cache/`. A reproducible multi-pass run can supply
one `--learning-rate` per epoch. `--quantize-between-epochs` matches chained
runtime-artifact training exactly.

```sh
mkdir -p \
  "$VIBE_OTHELLO_TRAINING/cache/egaroucid-board-score-v2025-02-02"
unzip -q "$VIBE_OTHELLO_CORPORA/Egaroucid_Train_Data.zip" \
  -d "$VIBE_OTHELLO_TRAINING/cache/egaroucid-board-score-v2025-02-02"
```

The promoted main-stage schedule is:

```sh
build/tools/pattern/train/vibe-othello-egaroucid-board-score-pretrain \
  --input "$VIBE_OTHELLO_TRAINING/cache/egaroucid-board-score-v2025-02-02/0001_egaroucid_7_5_1_lv17" \
  --initial-artifact \
    data/eval/artifacts/pattern-v2-wthor-full-policy-v1/manifest.json \
  --weights-out "$VIBE_OTHELLO_TRAINING/egaroucid-board-score/main/weights.json" \
  --report-out "$VIBE_OTHELLO_TRAINING/egaroucid-board-score/main/report.json" \
  --epochs 5 \
  --learning-rate 0.02 \
  --learning-rate 0.05 \
  --learning-rate 0.02 \
  --learning-rate 0.05 \
  --learning-rate 0.1 \
  --quantize-between-epochs \
  --validation-modulus 0 \
  --seed 20260720
```

Phases 10 through 12 are frozen by default. The promoted route exports the
main-stage weights, reloads that artifact, then makes one full-corpus
late-phase pass at learning rate `0.05` while freezing phases 0 through 9.
`--train-all-phases` explicitly disables the default freeze.

The trainer report records source-file checksums, per-phase error metrics,
learning-rate schedule, position visits, updated weight occurrences, frozen
phases, and output checksum. `--validation-modulus` defines a canonical-board
holdout for tuning; `0` uses every row for refitting. The companion
`audit_egaroucid_board_score_openings.py` scans the entire source archive and
rejects promotion opening suites that overlap a training board.

## Sampled Egaroucid board-score bootstrap

`Egaroucid_Train_Data.zip` contains side-to-move-relative boards and Egaroucid
score estimates. The local runner connects that archive to normalized schema
v2, compact pattern dataset generation, and `pattern-sgd-v0d` value training:

```sh
export VIBE_OTHELLO_LOCAL="${VIBE_OTHELLO_LOCAL:-$HOME/vibe-othello-local}"
export VIBE_OTHELLO_CORPORA="${VIBE_OTHELLO_CORPORA:-$VIBE_OTHELLO_LOCAL/corpora}"
export VIBE_OTHELLO_TRAINING="${VIBE_OTHELLO_TRAINING:-$VIBE_OTHELLO_LOCAL/training}"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target vibe_othello_pattern_dataset_smoke

python3 tools/pattern/train/run_egaroucid_board_score_training.py \
  --run-id board-score-p10k-seed0-v0d
```

The default import scans the complete archive and retains a deterministic
maximum of 10,000 unique boards per phase. This bounds trainer memory while
avoiding a sample dominated by the largest source phases. Use
`--positions-per-phase` and `--seed` to define another reproducible sample.
`--max-source-files` is only a bounded development shortcut; its import report
sets `source_scan_complete` to false.

The run layout is:

```text
$VIBE_OTHELLO_TRAINING/egaroucid-board-score/<run-id>/
├─ source-manifest.json
├─ run-report.json
├─ normalized/
├─ dataset/
├─ training/
└─ logs/
```

Source ZIPs stay untouched in `$VIBE_OTHELLO_CORPORA`. Existing run directories
are never overwritten. Reports use role-based local paths rather than personal
absolute paths.

These labels use `teacher_static_eval_disc_diff`. The runner produces local
trainer weights without changing the default artifact. The `_v0002_0.zip` and
`_v0002_1.zip` transcript archives remain inputs to
`tools/data-import/import_egaroucid_sequences.py`, not this board-score route.

## Reproducible stages

1. `tools/pattern/export/import_artifact_weights.py` validates a runtime
   artifact against its manifest and compiled pattern catalog, then emits
   deterministic trainer-weight JSON. Its smoke test requires byte-identical
   re-export.
2. `tools/pattern/labels/select_hard_search_teacher_roots.py` ranks complete
   roots by baseline regret and takes deterministic per-phase quotas.
3. `tools/pattern/labels/run_search_move_teacher_generation.py` generates the
   deeper labels with explicit artifact, checksum, search configuration, and
   resume fingerprints.
4. `tools/pattern/labels/overlay_search_move_teacher.py` replaces complete
   shallow roots, excludes cross-depth child conflicts, and records both input
   provenances and checksums.
5. `train_pattern.py --mode pattern-rank-v0e` warm-starts the imported
   weights. Use `--residual-baseline-through-phase` for early/midgame residual
   training, repeat `--trainable-pattern-id` to bound updated pattern families,
   and repeat `--freeze-phase` for retained late phases. An aggregate WTHOR
   played-move sidecar may be joined with `--played-move-policy`; its
   cross-entropy coefficient is explicit in `--policy-loss-weight`.
6. `tools/pattern/export/export_v0b.py` exports fixed-point runtime weights.
   The export must use the same residual boundary as training.
7. Rank diagnostics and paired fixed-depth/fixed-time arenas decide promotion.
   Training loss alone is not a strength result.

Every stage writes a machine-readable report with source and output checksums.
The hard-root selector preserves the teacher's explicit split assignments, then
rejects board or game-group cross-split collisions before output and records
both audit counts. The generator and overlay reject incomplete roots and
cross-split child collisions.
Progressive training passes the retained shallow roots and deeper overlay roots
as separate, disjoint sidecars so each input retains one search configuration
in the trainer report.

Played-move policy is a secondary empirical objective. For a matched root it
normalizes aggregate occurrence counts into a target move distribution and
applies cross-entropy to the same root logits, `-V(child) / rank_temperature`.
Search pairwise loss and optional child-value calibration remain unchanged.
Unmatched policy rows are reported and ignored; matched split, phase, and legal
move metadata must agree with the move-teacher root. Validation/test policy
roots are diagnostic only and never update weights.

## Full WTHOR Policy Pretraining

`vibe-othello-wthor-policy-pretrain` is the bounded-memory route for using
every decision in extracted WTHOR `.wtb` files. It loads an existing
`pattern-v2-endgame-lite` runtime artifact, replays games in a deterministic
shuffled order, and treats each recorded move as a provisional ranking target.
No expanded root/child TSV is created.

For a non-forced root, the trainer compares the played child with a
deterministic set of other legal children. `--negative-count 0` uses all
alternatives; a small positive count gives a sampled pairwise approximation
while still visiting every recorded decision. The root move score is
`-V(child)`, matching `pattern-rank-v0e`. The source artifact's
`fallback_additive_through_phase` is included in logits, but gradients update
only learned pattern terms. Phases 10 through 12 are frozen by default.

Transcript identity drives the optional validation split, keeping duplicate
games together. Validation roots evaluate every legal child and never update
weights. After hyperparameter selection, `--validation-modulus 0` enables an
all-game refit. `--epochs 0` evaluates an initial artifact without training.
The report records per-phase pairwise metrics, validation top-1 and
cross-entropy, source-file checksums, visited decisions, forced choices,
updates, and frozen phases.

The output is `pattern-eval-weights-v2` JSON for `export_v0b.py`. WTHOR moves
remain empirical human-game evidence, not exact labels; search-teacher
correction and paired fixed-depth/fixed-time arenas decide whether a candidate
is stronger.

Run each tool with `--help` for its complete interface. The smoke tests wired
through `tools/pattern/{export,labels,train}/CMakeLists.txt` cover artifact
round-tripping, hard-root selection, overlay conflict handling, provenance
validation, and residual training.
