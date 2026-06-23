# Pattern Move-Teacher Decision Leverage Campaign

## Scope

This is a local-only decision-leverage diagnostic for
`pattern-v2-endgame-lite`. It compares an exact root-label artifact against a
move-teacher child-label artifact on the same selected low-empty roots.

It is not an Elo result, not self-play, not a production strength claim, not a
publication gate, and not a reason to publish generated weights. Generated
labels, datasets, weights, artifacts, logs, and reports remain local-only and
are not committed.

## Input Source

The root TSV was the 5,000-board low-empty normalized schema v2 selection from
the local exact-teacher campaign:

```text
$VIBE_OTHELLO_MEASUREMENTS/exact-teacher-late-phase-v2/connected-100k-corpus-maxempty12-cap5000/selected-low-empty-normalized.tsv
```

That selection was derived from the connected-board-game 100k
sequence-derived corpus sample. It contains 5,000 unique low-empty roots and
uses the existing split assignments from that measurement.

The previous/root-label comparator was exported locally from the existing
`pattern-v2-endgame-lite` exact root-label weights trained on the same selected
roots:

```sh
python3 tools/pattern/export/export_v0b.py \
  --weights-json "$VIBE_OTHELLO_MEASUREMENTS/exact-teacher-late-phase-v2/connected-100k-corpus-maxempty12-cap5000/exact-teacher-weights.json" \
  --weights-out "$VIBE_OTHELLO_MEASUREMENTS/move-teacher-decision-campaign-v1/root-label-comparator/exact-root-label-v2.weights.bin" \
  --manifest-out "$VIBE_OTHELLO_MEASUREMENTS/move-teacher-decision-campaign-v1/root-label-comparator/exact-root-label-v2.manifest.json" \
  --pattern-set pattern-v2-endgame-lite
```

The optional bounded arena baseline used the existing local
`pattern-v1-buro-lite` exact-teacher artifact from the earlier bottleneck
diagnostics.

## Command

```sh
python3 tools/pattern/labels/run_move_teacher_decision_campaign.py \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/exact-teacher-late-phase-v2/connected-100k-corpus-maxempty12-cap5000/selected-low-empty-normalized.tsv" \
  --output-dir "$VIBE_OTHELLO_MEASUREMENTS/move-teacher-decision-campaign-v1" \
  --max-empty 12 \
  --max-roots 5000 \
  --seed 0 \
  --pattern-set pattern-v2-endgame-lite \
  --trainer-mode pattern-sgd-v0c \
  --epochs 8 \
  --learning-rate 0.1 \
  --lr-schedule inverse-sqrt \
  --weight-decay 0.0001 \
  --previous-weights "$VIBE_OTHELLO_MEASUREMENTS/move-teacher-decision-campaign-v1/root-label-comparator/exact-root-label-v2.weights.bin" \
  --previous-manifest "$VIBE_OTHELLO_MEASUREMENTS/move-teacher-decision-campaign-v1/root-label-comparator/exact-root-label-v2.manifest.json" \
  --previous-pattern-set pattern-v2-endgame-lite \
  --previous-name previous-exact-root-label-v2 \
  --arena-baseline-weights "$VIBE_OTHELLO_MEASUREMENTS/<local-v1-exact-teacher-baseline>/v1-exact-teacher.weights.bin" \
  --arena-baseline-manifest "$VIBE_OTHELLO_MEASUREMENTS/<local-v1-exact-teacher-baseline>/v1-exact-teacher.manifest.json" \
  --arena-baseline-name pattern-v1-buro-lite \
  --arena-depth 3 \
  --arena-max-positions 1000 \
  --arena-side-swap \
  --resume
```

## Dataset

| Metric | Value |
| --- | ---: |
| input roots | 5,000 |
| eligible roots | 5,000 |
| selected roots | 5,000 |
| roots with normal moves | 4,552 |
| roots with forced pass move | 448 |
| terminal roots skipped | 0 |
| move-teacher rows | 18,253 |
| child-normalized rows | 18,253 |
| exact teacher nodes | 130,581,992 |
| generation wall time | 307.054 seconds |

Child pattern dataset:

| Split | Rows |
| --- | ---: |
| train | 14,697 |
| validation | 1,862 |
| test | 1,694 |

| Child phase | Rows |
| --- | ---: |
| 10 | 5,017 |
| 11 | 9,061 |
| 12 | 4,175 |

The child dataset uses
`label_kind = teacher_exact_move_child_final_disc_diff`.

## Ranking Results

Primary comparison:

| Metric | Exact root-label v2 | Move-teacher child-label v2 | Delta |
| --- | ---: | ---: | ---: |
| top1 accuracy | 0.623400 | 0.670000 | +0.046600 |
| tie-aware top1 | 0.667000 | 0.698200 | +0.031200 |
| best move in top2 | 0.800600 | 0.855000 | +0.054400 |
| pairwise accuracy | 0.613624 | 0.691313 | +0.077689 |
| mean teacher regret | 3.009200 | 2.187400 | -0.821800 |
| median teacher regret | 0.000000 | 0.000000 | 0.000000 |
| exact-best predicted rank mean | 1.786600 | 1.604000 | -0.182600 |
| exact-best predicted rank median | 1.000000 | 1.000000 | 0.000000 |
| all-same predicted-score roots | 1,370 | 1,332 | -38 |

The move-teacher child-label artifact improves top1 by 4.66 percentage points,
pairwise accuracy by 7.77 percentage points, and mean teacher regret by 0.82
discs on the full selected set. This clears the strong positive
decision-leverage threshold on the full bounded diagnostic set.

As an additional sanity check, an observed root-label v2 artifact trained on the
same selected roots was exported and evaluated on the same move-teacher rows.
The child-label deltas were effectively the same:

| Baseline | Top1 delta | Tie-aware delta | Top2 delta | Pairwise delta | Mean regret delta | Rank-mean delta |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| exact root-label v2 | +0.046600 | +0.031200 | +0.054400 | +0.077689 | -0.821800 | -0.182600 |
| observed root-label v2 | +0.045800 | +0.030200 | +0.054800 | +0.077615 | -0.808600 | -0.182800 |

Held-out validation+test aggregate:

| Metric | Exact root-label v2 | Move-teacher child-label v2 | Delta |
| --- | ---: | ---: | ---: |
| roots | 992 | 992 | 0 |
| top1 accuracy | 0.636089 | 0.646170 | +0.010081 |
| tie-aware top1 | 0.681452 | 0.671371 | -0.010081 |
| best move in top2 | 0.816532 | 0.846774 | +0.030242 |
| pairwise accuracy | 0.615154 | 0.658538 | +0.043384 |
| mean teacher regret | 3.022177 | 2.640121 | -0.382056 |
| exact-best predicted rank mean | 1.728830 | 1.648185 | -0.080645 |
| all-same predicted-score roots | 265 | 252 | -13 |

The held-out aggregate does not clear the top1 threshold and tie-aware top1 is
slightly lower, but it does clear the pairwise/regret criterion and also
improves top2, exact-best rank, and all-same predicted-score roots. This makes
the result positive for decision leverage, with the caveat that top1 needs a
larger multi-seed confirmation.

Split-level checks:

| Split | Top1 delta | Pairwise delta | Mean regret delta |
| --- | ---: | ---: | ---: |
| train | +0.055639 | +0.085758 | -0.930639 |
| validation | +0.000000 | +0.041203 | -0.339768 |
| test | +0.021097 | +0.045940 | -0.428270 |

Validation top1 was flat and validation tie-aware top1 regressed slightly, but
validation pairwise accuracy and mean regret improved. The overall signal is
positive, but a larger multi-seed campaign is still needed before making any
strength-facing decision.

## Bounded Arena

The optional bounded arena compared the move-teacher child-label v2 artifact
against the existing v1 exact-teacher artifact over 1,000 selected positions,
side-swapped, at fixed depth 3.

| Metric | Value |
| --- | ---: |
| selected positions | 1,000 |
| games | 2,000 |
| candidate wins | 1,001 |
| baseline wins | 944 |
| draws | 55 |
| candidate score rate | 0.514250 |
| approximate 95% interval | [0.492345, 0.536155] |
| average disc diff, candidate perspective | +0.741000 |
| failed games | 0 |

This is a non-negative bounded arena signal, but the interval includes 0.5.
Treat it as supportive local evidence only, not as strength, Elo, self-play, or
production readiness.

## Interpretation

Move-teacher child-label training improved root decision leverage over the
same-root exact root-label artifact in this local campaign. The improvement is
visible in top1, tie-aware top1, best-in-top2, pairwise accuracy, mean regret,
and exact-best predicted rank. All-same predicted-score roots also decreased
slightly.

This answers the immediate PR question positively: exact after-move child-label
training improved root move choice more than exact root-label value training in
this bounded setting. The most defensible reading is positive decision
leverage, not a strength claim: the full selected set is clearly positive, and
held-out validation+test supports the result through pairwise accuracy, top2,
regret, and rank, while held-out top1 is only mildly positive and held-out
tie-aware top1 is slightly negative.

Do not implement the pairwise rank trainer in this PR. The value trainer on
child labels already produced a clear positive decision-leverage signal, so the
next useful work is scale and repeatability rather than a new objective.

## Next Recommendation

1. Run a larger `--max-roots` campaign from the connected-board-game resplit
   normalized TSV.
2. Repeat with multiple deterministic seeds.
3. Run bounded side-swapped arena diagnostics for the larger artifacts.
4. Compare exact root-label, move-teacher child-label, and v1 baselines under
   the same arena selection policy.
5. Only after those local checks, consider production-facing validation gates.

## Non-Claims

This note does not claim engine strength, Elo, self-play improvement,
production readiness, publication readiness, or license clearance for any
generated artifact.
