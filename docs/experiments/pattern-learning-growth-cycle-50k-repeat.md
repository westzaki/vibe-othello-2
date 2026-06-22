# Pattern-Learning Growth Cycle 50k Repeat

## Scope

This note records the local-only repeatability and split-policy follow-up for
the `pattern-v2-endgame-lite` move-teacher growth route.

This is not an Elo result, not self-play, not a production strength claim, not
a publication gate, and not a reason to commit generated weights. Generated
TSVs, teacher labels, datasets, weights, artifacts, raw reports, logs, and
local paths remain local-only and are not committed.

## Same-Source 50k Input

The preserve-split source reused the 50,000-root selection from the previous
50k run:

```sh
$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-source-v1/selected-low-empty-normalized.tsv
```

Selection summary:

| Metric | Value |
| --- | ---: |
| unique eligible low-empty roots | 230,045 |
| selected roots | 50,000 |
| selected checksum | `sha256:e7f42779439114b431fb01b4393cf467dd454fdf25559badbe0b7c18856de6bb` |
| train / validation / test roots | 40,007 / 4,970 / 5,023 |

The same-source exact-root v2 baseline was fair for this matrix: it was built
from the same selected 50k roots and checksum, used
`pattern-v2-endgame-lite`, and was reused for all seeds through resume-checked
metadata.

## Same-Source Matrix

The resumed command was:

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

## Runtime Summary

The table below reports only wall times emitted by local reports. It does not
try to infer unmeasured human/session elapsed time.

| Stage | Reported wall time |
| --- | ---: |
| same-source 50k seed 0 exact move-teacher generation | 3,745.804 sec |
| same-source 50k seed 1 exact move-teacher generation | 3,742.711 sec |
| same-source 50k seed 2 exact move-teacher generation | 3,695.612 sec |
| connected-board-game 1m suite | 669.817 sec |
| connected 50k seed 0 exact move-teacher generation | 4,636.122 sec |

Seed 0 on the same-source 50k matrix was reused through validated resume
metadata during this PR; its generation time is included because it is the
reported cost of the artifact that participated in the completed matrix. The
new progress-streaming helper change made connected seed 0 progress visible
during the long exact move-teacher generation stage.

Seed 0 was reused via validated resume metadata. Seeds 1 and 2 were newly
completed. Each seed generated 194,835 child rows, used 1,591,232,496 exact
teacher nodes, and had zero solve failures.

| Seed | Generation wall sec | Top1 delta | Tie-aware top1 delta | Top2 delta | Pairwise delta | Mean regret delta | All-same roots delta |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 3,745.804 | +0.042720 | +0.043620 | +0.031560 | +0.043116 | -0.561600 | -63 |
| 1 | 3,742.711 | +0.041580 | +0.042760 | +0.031440 | +0.043101 | -0.552260 | -39 |
| 2 | 3,695.612 | +0.041840 | +0.043220 | +0.031980 | +0.042972 | -0.559120 | -26 |
| mean | - | +0.042047 | +0.043200 | +0.031660 | +0.043063 | -0.557660 | -42.667 |

Held-out validation+test deltas were positive for ranking and negative for
regret on all three seeds:

| Seed | Held-out top1 delta | Held-out top2 delta | Held-out pairwise delta | Held-out mean regret delta |
| ---: | ---: | ---: | ---: | ---: |
| 0 | +0.028420 | +0.017212 | +0.023005 | -0.342039 |
| 1 | +0.026318 | +0.018213 | +0.023357 | -0.328129 |
| 2 | +0.029021 | +0.017813 | +0.022516 | -0.350846 |

Same-source arena summary:

| Comparison | Runs | Non-negative | Mean score rate | Failed games |
| --- | ---: | ---: | ---: | ---: |
| same-artifact sanity | 1 | 1 | 0.500000 | 0 |
| exact-root v2 vs v1 | 4 | 4 | 0.534188 | 0 |
| move-teacher v2 vs v1 | 12 | 12 | 0.538833 | 0 |
| move-teacher v2 vs exact-root v2 | 12 | 12 | 0.504937 | 0 |
| candidate/baseline swap sanity | 1 | 0 | 0.456500 | 0 |

The same-source scorecard category was:

```text
promote_to_larger_local_validation
```

Reason: decision-leverage and arena gates passed with stable 50k seed support.

## Connected-Board-Game Split Check

A connected-board-game 1m resplit source was generated from the local sequence
v0002 corpus with a sequence-cache hit:

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

Suite summary:

| Metric | Value |
| --- | ---: |
| preset | 1m |
| sampled rows | 1,000,000 |
| split policy | connected-board-game |
| sequence cache | hit |
| suite wall sec | 669.817 |

The connected 50k selection used the connected `resplit-normalized.tsv` and
preserved the connected split assignments:

| Metric | Value |
| --- | ---: |
| unique eligible low-empty roots | 230,045 |
| selected roots | 50,000 |
| selected checksum | `sha256:eaac563fa80d398ebda26b230987327586eb8a0912614aa18ca95e3c864900d5` |
| train / validation / test roots | 39,746 / 5,004 / 5,250 |

The selected board id set matched the preserve-split 50k selection exactly.
Existing exact-root labels were therefore safely reused by `board_id`; the
overlay matched 50,000/50,000 rows with zero missing labels. A connected-split
exact-root v2 baseline was then trained from the connected split assignments.
Its `pattern-v2-endgame-lite` artifact checksum was `0x5b864f8a`.

Connected exact-root v2 trainer summary:

| Split | MAE |
| --- | ---: |
| train | 5.172637 |
| validation | 6.903873 |
| test | 6.862269 |

Connected 50k seed 0 growth-cycle command:

```sh
python3 tools/pattern/train/run_pattern_growth_cycle.py \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-connected-source-v1/selected-low-empty-normalized.tsv" \
  --output-dir "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-connected-v1" \
  --pattern-set pattern-v2-endgame-lite \
  --baseline-root-label-weights "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-connected-source-v1/exact-root-v2.weights.bin" \
  --baseline-root-label-manifest "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-connected-source-v1/exact-root-v2.manifest.json" \
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

Connected seed 0 generated 194,835 child rows, used 1,591,232,496 exact
teacher nodes, had zero solve failures, and took 4,636.122 seconds for exact
move-teacher generation.

| Metric | Delta |
| --- | ---: |
| top1 accuracy | +0.047580 |
| tie-aware top1 | +0.046760 |
| best in top2 | +0.030720 |
| pairwise accuracy | +0.044770 |
| mean teacher regret | -0.606300 |
| exact-best rank mean | -0.116760 |
| all-same predicted roots | -7 |

Connected held-out validation+test deltas:

| Metric | Delta |
| --- | ---: |
| top1 accuracy | +0.019895 |
| tie-aware top1 | +0.020870 |
| best in top2 | +0.015214 |
| pairwise accuracy | +0.022077 |
| mean teacher regret | -0.304271 |
| all-same predicted roots | -30 |

Connected arena summary:

| Comparison | Runs | Non-negative | Mean score rate | Failed games |
| --- | ---: | ---: | ---: | ---: |
| same-artifact sanity | 1 | 1 | 0.500000 | 0 |
| exact-root v2 vs v1 | 4 | 4 | 0.531688 | 0 |
| move-teacher v2 vs v1 | 4 | 4 | 0.537250 | 0 |
| move-teacher v2 vs exact-root v2 | 4 | 4 | 0.507313 | 0 |
| candidate/baseline swap sanity | 1 | 0 | 0.455750 | 0 |

The connected scorecard category remained:

```text
hold_for_more_data
```

Reason: the connected split check has only one completed 50k seed, even though
its decision-leverage and bounded arena gates passed.

## 100k Stretch

A connected 100k low-empty source was selected from the connected 1m resplit:

| Metric | Value |
| --- | ---: |
| unique eligible low-empty roots | 230,045 |
| selected roots | 100,000 |
| selected checksum | `sha256:260102a58ead4522169d7298ba828fa983930c902c261c49d18da4b11b6d0ce7` |
| train / validation / test roots | 79,742 / 9,909 / 10,349 |

No 100k exact-root v2 baseline or 100k growth-cycle run was started in this PR.
The blocker is runtime and fairness: 100k must not compare against the 50k
exact-root baseline, and producing the fair 100k exact-root baseline plus a
100k move-teacher seed would add another long exact-solve campaign after the
completed 50k same-source and connected-split validation.

## Decision

The move-teacher route should promote to larger local validation, not shift
objective or capacity yet.

Evidence:

* same-source 50k seeds 0, 1, and 2 all improved pairwise accuracy and mean
  regret versus the fair exact-root v2 baseline
* same-source held-out validation+test support was positive on all three seeds
* same-source bounded arena support was non-negative in all 24 move-teacher
  comparisons against v1 and exact-root v2
* connected-board-game split seed 0 preserved the positive ranking direction
  and had supportive bounded arena results
* same-artifact sanity passed, swap sanity reversed direction, and failed games
  were zero

Next action:

* build the fair 100k exact-root v2 baseline for the connected 100k source
* run 100k seed 0 with bounded arena depth 3 seed 0 first
* if 100k seed 0 remains positive, run connected 50k seeds 1 and 2 or 100k
  seeds 1 and 2 depending on available runtime
* add root-shard resume and safe split-remap reuse before much larger campaigns

What not to do next:

* do not add pattern-v3 yet
* do not add pairwise rank trainer v0e from these positive value-route results
* do not run an LR/WD sweep as the main next step
* do not claim Elo, self-play improvement, production strength, publication
  readiness, or artifact publication readiness
* do not run 100k move-teacher against a mismatched 50k exact-root baseline

## Resume Commands

To continue connected 50k repeatability:

```sh
python3 tools/pattern/train/run_pattern_growth_cycle.py \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-connected-source-v1/selected-low-empty-normalized.tsv" \
  --output-dir "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-connected-v1" \
  --pattern-set pattern-v2-endgame-lite \
  --baseline-root-label-weights "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-connected-source-v1/exact-root-v2.weights.bin" \
  --baseline-root-label-manifest "$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-50k-connected-source-v1/exact-root-v2.manifest.json" \
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

To start the fair connected 100k baseline, first generate exact labels:

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

Then overlay, build the compact `pattern-v2-endgame-lite` dataset, train with
the same v0c settings, export, and run:

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
