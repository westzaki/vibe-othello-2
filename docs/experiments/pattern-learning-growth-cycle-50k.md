# Pattern-Learning Growth Cycle 50k

## Scope

This note records a local-only 50,000-root growth-cycle attempt for the
`pattern-v2-endgame-lite` move-teacher route. It follows the growth-cycle
recommendation from the 5k/10k/20k matrix and answers whether the next local
input, exact-root v2 comparator, and at least one 50k growth-cycle run can be
produced.

This is not an Elo result, not self-play, not a production strength claim, not
a publication gate, and not a reason to commit generated weights. Generated
TSVs, teacher labels, datasets, weights, artifacts, raw reports, logs, and
local paths remain local-only and are not committed.

## Input Source

The selected source was the existing 1m sequence v0002 normalized schema v2
measurement:

```sh
$VIBE_OTHELLO_MEASUREMENTS/sequence-v0002-real-20260621/runs/sequence-v0002-real-20260621-1m/sequence-normalized.tsv
```

The local preflight found 230,045 unique roots with `empty_count <= 12`, enough
for a 50,000-root selection. No connected-board-game 1m `resplit-normalized.tsv`
was already present locally, so this run preserved the source split assignments.
The existing 100k connected-board-game source still had only 22,954 eligible
low-empty roots and could not support 50k.

To rebuild the preferred connected split before repeating the full matrix, use a
sequence measurement command with the connected policy:

```sh
python3 tools/pattern/train/run_pattern_measurement_suite.py \
  --sequence-input "$VIBE_OTHELLO_CORPORA" \
  --sequence-manifest data/corpora/manifests/egaroucid-sequence-v0002-local.manifest.json \
  --preset 1m \
  --run-prefix sequence-v0002-real-<date>-connected-split \
  --measurement-split-policy connected-board-game \
  --sequence-cache-dir "$VIBE_OTHELLO_SEQUENCE_CACHE" \
  --resume
```

## Selection

The selector command was:

```sh
python3 tools/pattern/labels/select_low_empty_roots.py \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/sequence-v0002-real-20260621/runs/sequence-v0002-real-20260621-1m/sequence-normalized.tsv" \
  --output-tsv "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-source-v1/selected-low-empty-normalized.tsv" \
  --report-out "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-source-v1/selected-low-empty-report.json" \
  --max-empty 12 \
  --max-roots 50000 \
  --seed 0 \
  --dedupe-key board_id \
  --preserve-split \
  --require-schema-v2
```

Selection result:

| Metric | Value |
| --- | ---: |
| input rows | 1,000,000 |
| eligible low-empty rows | 230,045 |
| unique eligible roots | 230,045 |
| selected roots | 50,000 |
| duplicate eligible board rows | 0 |
| selected checksum | `sha256:e7f42779439114b431fb01b4393cf467dd454fdf25559badbe0b7c18856de6bb` |

Selected split counts were train 40,007, validation 4,970, and test 5,023.
Selected phase counts were phase 10: 12,482, phase 11: 20,959, and phase 12:
16,559.

## Exact-Root Baseline

The previous exact-root v2 artifact did not include metadata tying it to this
50k selected input checksum, so it was not reused as the fair comparator. A new
exact-root v2 baseline was generated locally from the selected roots:

```sh
./build/tools/pattern/labels/vibe-othello-generate-exact-endgame-teacher-labels \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-source-v1/selected-low-empty-normalized.tsv" \
  --teacher-labels-out "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-source-v1/exact-root-teacher-labels.tsv" \
  --report-out "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-source-v1/exact-root-teacher-report.json" \
  --max-empty 12 \
  --max-positions 50000 \
  --seed 0 \
  --progress-every 100
```

The exact-root label step solved 50,000 unique boards, had zero solve failures,
used 689,459,188 exact nodes, and took 1,619.801 seconds. The overlay matched
50,000/50,000 rows with no missing labels. The compact
`pattern-v2-endgame-lite` dataset accepted 50,000 examples and emitted
2,900,000 feature occurrences.

The exact-root v2 baseline used the existing v0c configuration:

```sh
python3 tools/pattern/train/train_v0a.py \
  --dataset "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-source-v1/exact-root-v2-dataset.tsv" \
  --mode pattern-sgd-v0c \
  --epochs 8 \
  --learning-rate 0.1 \
  --lr-schedule inverse-sqrt \
  --weight-decay 0.0001 \
  --weights-out "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-source-v1/exact-root-v2-weights.json" \
  --report-out "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-source-v1/exact-root-v2-trainer-report.json" \
  --seed 0
```

Baseline trainer summary:

| Split | MAE | RMSE | Sign accuracy |
| --- | ---: | ---: | ---: |
| train | 5.134166 | 6.579373 | 0.909616 |
| validation | 6.897217 | 8.929603 | 0.880282 |
| test | 6.961254 | 9.024337 | 0.879554 |

The exported exact-root v2 artifact had weights checksum `0xa2dc8d0d`.

## Growth-Cycle Run

Runtime allowed the minimum real 50k run: one completed 50,000-root seed with
the fair exact-root v2 comparator and bounded arenas. Seeds 1 and 2 were not run
in this PR because 50k exact move-teacher generation was the dominant runtime
cost.

```sh
python3 tools/pattern/train/run_pattern_growth_cycle.py \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-source-v1/selected-low-empty-normalized.tsv" \
  --output-dir "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-v1" \
  --pattern-set pattern-v2-endgame-lite \
  --baseline-root-label-weights "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-source-v1/exact-root-v2.weights.bin" \
  --baseline-root-label-manifest "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-source-v1/exact-root-v2.manifest.json" \
  --baseline-root-label-pattern-set pattern-v2-endgame-lite \
  --baseline-v1-weights "$VIBE_OTHELLO_MEASUREMENTS/pattern-signal-bottleneck-diagnostics/artifacts/v1-exact-teacher.weights.bin" \
  --baseline-v1-manifest "$VIBE_OTHELLO_MEASUREMENTS/pattern-signal-bottleneck-diagnostics/artifacts/v1-exact-teacher.manifest.json" \
  --root-counts 50000 \
  --seeds 0 \
  --arena-depths 3,5 \
  --arena-seeds 0,10,20 \
  --arena-max-positions 1000 \
  --resume
```

The 50k move-teacher stage generated 194,835 move rows, used 1,591,232,496
exact child nodes, had zero terminal roots skipped, and took 3,745.804 seconds.
The child dataset accepted 194,835 examples and emitted 11,300,430 feature
occurrences.

## Decision Leverage

All deltas compare the 50k seed0 move-teacher child-label artifact against the
same-source exact-root v2 baseline.

| Metric | Exact-root v2 | Move-teacher v2 | Delta |
| --- | ---: | ---: | ---: |
| top1 accuracy | 0.676800 | 0.719520 | +0.042720 |
| tie-aware top1 | 0.700100 | 0.743720 | +0.043620 |
| best in top2 | 0.853840 | 0.885400 | +0.031560 |
| pairwise accuracy | 0.710701 | 0.753817 | +0.043116 |
| mean teacher regret | 2.309280 | 1.747680 | -0.561600 |
| exact-best rank mean | 1.587840 | 1.476140 | -0.111700 |
| all-same predicted roots | 11,932 | 11,869 | -63 |

Held-out validation+test also supported the full-set direction:

| Metric | Delta |
| --- | ---: |
| top1 accuracy | +0.028420 |
| tie-aware top1 | +0.027719 |
| best in top2 | +0.017212 |
| pairwise accuracy | +0.023005 |
| mean teacher regret | -0.342039 |
| all-same predicted roots | -27 |

## Arena Results

The growth-cycle runner completed 14 bounded side-swapped arena variants. Each
arena used 1,000 selected positions side-swapped into 2,000 games. Failed games
were zero throughout.

| Comparison | Runs | Non-negative | Mean score rate | Note |
| --- | ---: | ---: | ---: | --- |
| same-artifact sanity | 1 | 1 | 0.500000 | neutral sanity passed |
| exact-root v2 vs v1 | 4 | 4 | 0.534188 | supportive |
| move-teacher v2 vs v1 | 4 | 4 | 0.540188 | supportive |
| move-teacher v2 vs exact-root v2 | 4 | 4 | 0.504000 | non-negative, close |
| candidate/baseline swap sanity | 1 | 0 | 0.456500 | reverse direction as expected |

Depth-3 arena seeds 0, 10, and 20 were run for the main comparisons. Depth 5
ran arena seed 0. Move-teacher v2 was non-negative against both v1 and
exact-root v2 at depth 3 and depth 5.

## Scorecard

After a small policy fix, the growth-cycle scorecard treats one completed 50k
seed as positive but not stable enough to promote:

```text
hold_for_more_data
```

Reasons:

* decision-leverage gate passed for the completed 50k seed
* arena support gate passed
* same-artifact sanity passed
* swap sanity moved in the expected opposite direction
* failed games were zero
* 50k seed coverage is incomplete, so root-count/seed stability is not yet
  established

## Next Action

Repeat the 50k matrix for seeds 1 and 2, preferably from a
connected-board-game 1m resplit source if runtime allows. Reuse the selected
source, exact-root v2 baseline, and growth-cycle `--resume` metadata when the
same input checksum is intended.

What not to do next:

* do not add pattern-v3 yet
* do not add pairwise rank trainer v0e from this positive single-seed result
* do not run an LR/WD sweep as the main next step
* do not claim Elo, self-play improvement, production strength, or publication
  readiness

## Resume Commands

To complete the preferred seed matrix on the same selected input:

```sh
python3 tools/pattern/train/run_pattern_growth_cycle.py \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-source-v1/selected-low-empty-normalized.tsv" \
  --output-dir "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-v1" \
  --pattern-set pattern-v2-endgame-lite \
  --baseline-root-label-weights "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-source-v1/exact-root-v2.weights.bin" \
  --baseline-root-label-manifest "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-source-v1/exact-root-v2.manifest.json" \
  --baseline-root-label-pattern-set pattern-v2-endgame-lite \
  --baseline-v1-weights "$VIBE_OTHELLO_MEASUREMENTS/pattern-signal-bottleneck-diagnostics/artifacts/v1-exact-teacher.weights.bin" \
  --baseline-v1-manifest "$VIBE_OTHELLO_MEASUREMENTS/pattern-signal-bottleneck-diagnostics/artifacts/v1-exact-teacher.manifest.json" \
  --root-counts 50000 \
  --seeds 0,1,2 \
  --arena-depths 3,5 \
  --arena-seeds 0,10,20 \
  --arena-max-positions 1000 \
  --resume
```

To recreate only the selected low-empty input, rerun the selector command in
the selection section. Do not use `--allow-less-than-requested` for a claimed
50k run.
