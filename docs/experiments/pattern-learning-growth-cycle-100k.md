# Pattern-Learning Growth Cycle 100k

## Scope

This note records the 100,000-root stretch status for the local-only
`pattern-v2-endgame-lite` move-teacher growth route.

This is not an Elo result, not self-play, not a production strength claim, not
a publication gate, and not a reason to commit generated weights. Generated
TSVs, teacher labels, datasets, weights, artifacts, raw reports, logs, and
local paths remain local-only and are not committed.

## Source

The 100k source was selected from the connected-board-game 1m resplit built for
the 50k repeat check:

```sh
$VIBE_OTHELLO_MEASUREMENTS/sequence-v0002-real-<date>-connected-split/runs/sequence-v0002-real-<date>-connected-split-1m/resplit-normalized.tsv
```

Selection command:

```sh
python3 tools/pattern/labels/select_low_empty_roots.py \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/sequence-v0002-real-<date>-connected-split/runs/sequence-v0002-real-<date>-connected-split-1m/resplit-normalized.tsv" \
  --output-tsv "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/selected-low-empty-normalized.tsv" \
  --report-out "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/selected-low-empty-report.json" \
  --max-empty 12 \
  --max-roots 100000 \
  --seed 0 \
  --dedupe-key board_id \
  --preserve-split \
  --require-schema-v2
```

Selection result:

| Metric | Value |
| --- | ---: |
| unique eligible low-empty roots | 230,045 |
| selected roots | 100,000 |
| selected checksum | `sha256:260102a58ead4522169d7298ba828fa983930c902c261c49d18da4b11b6d0ce7` |
| train / validation / test roots | 79,742 / 9,909 / 10,349 |
| phase 10 / 11 / 12 roots | 25,171 / 41,758 / 33,071 |

## Status

The 100k selected source exists. The fair 100k exact-root v2 baseline was not
built in this PR, and no 100k move-teacher growth-cycle attempt was started.

Exact blocker:

* a fair 100k comparison cannot reuse the 50k exact-root baseline
* after completing same-source 50k seeds 0/1/2, connected 1m resplit, connected
  50k exact-root baseline, connected 50k seed 0, and bounded arenas, the
  remaining local runtime was better spent documenting and validating the
  completed evidence
* the next 100k step starts with exact-root baseline generation, then one
  100k move-teacher seed; both are long exact-solve stages

## Resume Commands

Generate exact root labels:

```sh
./build/tools/pattern/labels/vibe-othello-generate-exact-endgame-teacher-labels \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/selected-low-empty-normalized.tsv" \
  --teacher-labels-out "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/exact-root-teacher-labels.tsv" \
  --report-out "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/exact-root-teacher-report.json" \
  --max-empty 12 \
  --max-positions 100000 \
  --seed 0 \
  --progress-every 100
```

Overlay labels onto the exact selected rows:

```sh
python3 tools/pattern/labels/apply_teacher_labels.py \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/selected-low-empty-normalized.tsv" \
  --teacher-labels "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/exact-root-teacher-labels.tsv" \
  --output "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/exact-root-v2-normalized.tsv" \
  --report "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/exact-root-v2-overlay-report.json" \
  --missing-policy drop
```

Build, train, and export the fair exact-root v2 baseline:

```sh
./build/tools/pattern/dataset/vibe-othello-pattern-dataset-smoke \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/exact-root-v2-normalized.tsv" \
  --report "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/exact-root-v2-dataset-report.json" \
  --output-format compact-tsv \
  --pattern-set pattern-v2-endgame-lite \
  > "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/exact-root-v2-dataset.tsv"

python3 tools/pattern/train/train_v0a.py \
  --dataset "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/exact-root-v2-dataset.tsv" \
  --mode pattern-sgd-v0c \
  --epochs 8 \
  --learning-rate 0.1 \
  --lr-schedule inverse-sqrt \
  --weight-decay 0.0001 \
  --weights-out "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/exact-root-v2-weights.json" \
  --report-out "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/exact-root-v2-trainer-report.json" \
  --seed 0

python3 tools/pattern/export/export_v0b.py \
  --weights-json "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/exact-root-v2-weights.json" \
  --weights-out "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/exact-root-v2.weights.bin" \
  --manifest-out "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/exact-root-v2.manifest.json" \
  --pattern-set pattern-v2-endgame-lite
```

Run the first fair 100k growth-cycle attempt:

```sh
python3 tools/pattern/train/run_pattern_growth_cycle.py \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/selected-low-empty-normalized.tsv" \
  --output-dir "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-v1" \
  --pattern-set pattern-v2-endgame-lite \
  --baseline-root-label-weights "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/exact-root-v2.weights.bin" \
  --baseline-root-label-manifest "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1/exact-root-v2.manifest.json" \
  --baseline-root-label-pattern-set pattern-v2-endgame-lite \
  --baseline-v1-weights "$VIBE_OTHELLO_MEASUREMENTS/pattern-signal-bottleneck-diagnostics/artifacts/v1-exact-teacher.weights.bin" \
  --baseline-v1-manifest "$VIBE_OTHELLO_MEASUREMENTS/pattern-signal-bottleneck-diagnostics/artifacts/v1-exact-teacher.manifest.json" \
  --root-counts 100000 \
  --seeds 0 \
  --arena-depths 3 \
  --arena-seeds 0 \
  --arena-max-positions 1000 \
  --resume
```

## Next Action

Run the fair 100k exact-root v2 baseline and 100k seed 0 before adding a new
objective, feature capacity, or sweep. Do not compare 100k move-teacher against
the 50k exact-root baseline.
