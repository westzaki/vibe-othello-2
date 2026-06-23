# Pattern Move-Teacher Decision-Leverage Scale Matrix

## Scope

This is a local-only repeatability and scale diagnostic for
`pattern-v2-endgame-lite` move-teacher child-label training. It repeats the
decision-leverage campaign from PR #163 over multiple deterministic seeds and
three bounded root counts.

It is not an Elo result, not self-play, not a production strength claim, not a
publication gate, and not a reason to publish generated weights. Generated
labels, datasets, weights, artifacts, raw reports, logs, and local paths remain
local-only and are not committed.

## Command Shape

The local run used the connected-board-game 100k low-empty normalized schema v2
selection that contains enough roots for the 20,000-root runs. The exact local
paths are intentionally replaced with environment placeholders:

```sh
python3 tools/pattern/labels/run_move_teacher_campaign_matrix.py \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/exact-teacher-late-phase-v1/connected-100k-corpus-maxempty12-all/selected-low-empty-normalized.tsv" \
  --output-dir "$VIBE_OTHELLO_MEASUREMENTS/move-teacher-decision-campaign-scale-v1" \
  --root-counts 5000,10000,20000 \
  --seeds 0,1,2 \
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
  --arena-max-positions 1000 \
  --arena-depth 3 \
  --arena-side-swap \
  --resume
```

The helper wrote local-only `matrix-report.json` and `matrix-summary.md`. The
matrix did not run 50,000 roots because this selected low-empty input contains
22,954 root rows, or 22,955 TSV lines including the header. The 20,000-root
exact generation stage took about 1,224 to 1,233 seconds per seed.

Resume shape for a larger follow-up:

```sh
python3 tools/pattern/labels/run_move_teacher_campaign_matrix.py \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/<connected-or-low-empty-normalized.tsv>" \
  --output-dir "$VIBE_OTHELLO_MEASUREMENTS/<move-teacher-scale-run>" \
  --root-counts 50000 \
  --seeds 0,1,2 \
  --pattern-set pattern-v2-endgame-lite \
  --trainer-mode pattern-sgd-v0c \
  --epochs 8 \
  --learning-rate 0.1 \
  --lr-schedule inverse-sqrt \
  --weight-decay 0.0001 \
  --previous-weights "$VIBE_OTHELLO_MEASUREMENTS/<previous-root-label-v2.weights.bin>" \
  --previous-manifest "$VIBE_OTHELLO_MEASUREMENTS/<previous-root-label-v2.manifest.json>" \
  --previous-pattern-set pattern-v2-endgame-lite \
  --arena-baseline-weights "$VIBE_OTHELLO_MEASUREMENTS/<v1-baseline.weights.bin>" \
  --arena-baseline-manifest "$VIBE_OTHELLO_MEASUREMENTS/<v1-baseline.manifest.json>" \
  --arena-baseline-name pattern-v1-buro-lite \
  --arena-max-positions 1000 \
  --arena-depth 3 \
  --arena-side-swap \
  --resume
```

## Matrix

| Dimension | Value |
| --- | --- |
| root counts | 5,000, 10,000, and 20,000 |
| seeds | 0, 1, 2 |
| trainer | `pattern-sgd-v0c` |
| pattern set | `pattern-v2-endgame-lite` |
| arena | 1,000 selected positions, side-swapped, fixed depth 3 |
| comparator | previous exact root-label v2 artifact from PR #163 |
| arena baseline | local `pattern-v1-buro-lite` exact-teacher artifact |

## Per-Run Results

All deltas compare the trained move-teacher child-label v2 artifact against the
previous exact root-label v2 artifact on the same selected roots. Positive
accuracy deltas and negative regret/rank/all-same deltas are better.

| Run | Roots | Move rows | Exact nodes | Generation sec | Top1 d | Tie-aware d | Top2 d | Pairwise d | Mean regret d | Rank-mean d | All-same d | Arena score |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| roots-5000-seed-0 | 5,000 | 18,253 | 130,581,992 | 307.089 | +0.046600 | +0.031200 | +0.054400 | +0.077689 | -0.821800 | -0.182600 | -38 | 0.517500 |
| roots-5000-seed-1 | 5,000 | 17,942 | 126,700,381 | 298.558 | +0.057400 | +0.049400 | +0.056000 | +0.085563 | -0.864200 | -0.194400 | -57 | 0.514000 |
| roots-5000-seed-2 | 5,000 | 18,175 | 129,362,164 | 303.775 | +0.061000 | +0.044200 | +0.053800 | +0.081517 | -0.945800 | -0.199800 | -28 | 0.515750 |
| roots-10000-seed-0 | 10,000 | 36,326 | 261,581,130 | 614.732 | +0.077200 | +0.059500 | +0.065600 | +0.094187 | -1.086900 | -0.237100 | -79 | 0.517250 |
| roots-10000-seed-1 | 10,000 | 36,208 | 260,564,844 | 612.253 | +0.068500 | +0.052400 | +0.062400 | +0.094988 | -1.063500 | -0.221600 | -89 | 0.521000 |
| roots-10000-seed-2 | 10,000 | 36,519 | 261,200,688 | 614.243 | +0.072500 | +0.054100 | +0.063000 | +0.094591 | -1.128400 | -0.227900 | -93 | 0.518500 |
| roots-20000-seed-0 | 20,000 | 72,831 | 521,899,710 | 1224.247 | +0.081600 | +0.064150 | +0.073350 | +0.107062 | -1.199100 | -0.257950 | -194 | 0.528750 |
| roots-20000-seed-1 | 20,000 | 72,901 | 525,904,684 | 1232.571 | +0.083250 | +0.065650 | +0.069800 | +0.106230 | -1.216400 | -0.254450 | -197 | 0.528000 |
| roots-20000-seed-2 | 20,000 | 72,807 | 521,657,141 | 1225.300 | +0.082750 | +0.065700 | +0.072750 | +0.108417 | -1.249150 | -0.264050 | -210 | 0.527500 |

## Aggregate Ranking

| Roots | Runs | Positive seeds | Negative seeds | Mean top1 d | Median top1 d | Mean pairwise d | Median pairwise d | Mean regret d | Median regret d | Mean top2 d | Mean all-same d |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 5,000 | 3 | 3 | 0 | +0.055000 | +0.057400 | +0.081590 | +0.081517 | -0.877267 | -0.864200 | +0.054733 | -41.000000 |
| 10,000 | 3 | 3 | 0 | +0.072733 | +0.072500 | +0.094589 | +0.094591 | -1.092933 | -1.086900 | +0.063667 | -87.000000 |
| 20,000 | 3 | 3 | 0 | +0.082533 | +0.082750 | +0.107236 | +0.107062 | -1.221550 | -1.216400 | +0.071967 | -200.333333 |
| overall | 9 | 9 | 0 | +0.070089 | +0.072500 | +0.094472 | +0.094591 | -1.063917 | -1.086900 | +0.063456 | -109.444444 |

The full selected sets are positive on all nine completed runs. Pairwise spread
remains small at larger root counts: 10,000 roots had a 0.000801 pairwise delta
range, and 20,000 roots had a 0.002187 range.

## Held-Out Summary

Held-out combines validation and test splits from the move-teacher ranking
reports. The 5,000-root runs still have mixed tie-aware top1, but pairwise,
top2, top1, and mean regret support the full-set direction on every seed.

| Roots | Mean top1 d | Mean tie-aware d | Mean top2 d | Mean pairwise d | Mean regret d | Support seeds |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 5,000 | +0.009638 | -0.008249 | +0.024044 | +0.045207 | -0.370006 | 3/3 |
| 10,000 | +0.043005 | +0.026761 | +0.040586 | +0.060869 | -0.742670 | 3/3 |
| 20,000 | +0.054728 | +0.036123 | +0.049236 | +0.075796 | -0.880122 | 3/3 |
| overall | +0.035790 | +0.018212 | +0.037955 | +0.060624 | -0.664266 | 9/9 |

## Arena Summary

The bounded arena direction is non-negative in every completed run. The 5,000
and 10,000 root intervals still include 0.5, but the 20,000-root campaign arena
intervals are above 0.5 in all three seeds. This remains local bounded arena
evidence only, not a strength claim.

| Roots | Runs | Mean score rate | Score-rate range | Mean disc diff | Non-negative runs | Interval note |
| ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 5,000 | 3 | 0.515750 | 0.514000 to 0.517500 | +0.892167 | 3/3 | intervals include 0.5 |
| 10,000 | 3 | 0.518917 | 0.517250 to 0.521000 | +1.214000 | 3/3 | intervals include 0.5 |
| 20,000 | 3 | 0.528083 | 0.527500 to 0.528750 | +1.659167 | 3/3 | intervals above 0.5 |
| overall | 9 | 0.520917 | 0.514000 to 0.528750 | +1.255111 | 9/9 | mixed intervals |

## Arena Variations

After the 20,000-root artifacts were available, additional arenas were run
without retraining:

* each 20,000-root seed kept its original depth-3 arena
* each 20,000-root seed added depth-3 arena seeds 10 and 20
* each 20,000-root seed added one depth-5 arena at arena seed 0

| Arena set | Runs | Mean score rate | Score-rate range | Mean disc diff | Non-negative runs | Interval note |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| 20k depth 3, arena seeds original/10/20 | 9 | 0.529083 | 0.523750 to 0.536250 | +1.702944 | 9/9 | all intervals above 0.5 |
| 20k depth 5, arena seed 0 | 3 | 0.517167 | 0.516500 to 0.517750 | +0.979667 | 3/3 | intervals include 0.5 |
| 20k all arena variants | 12 | 0.526104 | 0.516500 to 0.536250 | +1.522125 | 12/12 | 9/12 intervals above 0.5 |

## Interpretation

This clears the strong robust local decision-leverage policy for this bounded
matrix:

* top1 improves on all nine matrix runs
* pairwise improves on all nine matrix runs
* mean teacher regret decreases on all nine matrix runs
* held-out pairwise, top2, and regret support the full-set direction on all nine
  runs
* bounded arena score rate is non-negative on all nine matrix arenas and all
  twelve 20,000-root arena variants

The most defensible interpretation is robust local decision-leverage signal.
It is still not a strength claim. The 20,000-root depth-3 arena intervals are
above 0.5 in this bounded setup, but the depth-5 intervals still include 0.5,
and the arena compares against one local v1 baseline under bounded position
policies.

Do not implement the pairwise rank trainer in this PR. The repeated child-label
value training campaign did not fail to generalize in this matrix, so the next
step is scale and validation breadth rather than a new objective.

## Next Recommended Action

1. Build or select a larger low-empty input that can support a 50,000-root
   campaign.
2. Repeat bounded arenas across more position samples and baseline artifacts.
3. Compare the resulting artifacts against both v1 and exact-root v2 under the
   same arena policy.
4. Plan production validation only after the larger local diagnostics remain
   positive.

## Non-Claims

This note does not claim engine strength, Elo, self-play improvement,
production readiness, publication readiness, or license clearance for any
generated artifact.
