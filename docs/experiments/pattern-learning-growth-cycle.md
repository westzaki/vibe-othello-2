# Pattern-Learning Growth Cycle

## Scope

This note records a local-only growth-cycle run for the move-teacher
`pattern-v2-endgame-lite` route. The run exercised the new
`tools/pattern/train/run_pattern_growth_cycle.py` orchestrator over the existing
5,000 / 10,000 / 20,000 root move-teacher matrix, then added bounded artifact
arena comparisons and promotion scoring.

This is not an Elo result, not self-play, not a production strength claim, not a
publication gate, and not a reason to commit generated weights. Generated
labels, datasets, weights, artifacts, raw reports, logs, and local paths remain
local-only and are not committed.

## Command Shape

The local run reused the existing move-teacher scale matrix from PR #164 and
wrote growth-cycle outputs to an uncommitted local output directory. Exact local
paths are intentionally replaced with placeholders:

```sh
python3 tools/pattern/train/run_pattern_growth_cycle.py \
  --normalized-tsv "$VIBE_OTHELLO_MEASUREMENTS/exact-teacher-late-phase-v1/connected-100k-corpus-maxempty12-all/selected-low-empty-normalized.tsv" \
  --output-dir "$LOCAL_TMP/<pattern-growth-cycle-run>" \
  --pattern-set pattern-v2-endgame-lite \
  --baseline-root-label-weights "$VIBE_OTHELLO_MEASUREMENTS/move-teacher-decision-campaign-v1/root-label-comparator/exact-root-label-v2.weights.bin" \
  --baseline-root-label-manifest "$VIBE_OTHELLO_MEASUREMENTS/move-teacher-decision-campaign-v1/root-label-comparator/exact-root-label-v2.manifest.json" \
  --baseline-v1-weights "$VIBE_OTHELLO_MEASUREMENTS/pattern-signal-bottleneck-diagnostics/artifacts/v1-exact-teacher.weights.bin" \
  --baseline-v1-manifest "$VIBE_OTHELLO_MEASUREMENTS/pattern-signal-bottleneck-diagnostics/artifacts/v1-exact-teacher.manifest.json" \
  --root-counts 5000,10000,20000 \
  --seeds 0,1,2 \
  --arena-depths 3,5 \
  --arena-seeds 0,10,20 \
  --arena-max-positions 1000 \
  --matrix-report "$VIBE_OTHELLO_MEASUREMENTS/move-teacher-decision-campaign-scale-v1/matrix-report.json" \
  --resume
```

The selected low-empty input had 22,954 available roots. Requested root counts
5,000, 10,000, and 20,000 were all available.

## Decision-Leverage Summary

The runner reused the existing completed matrix. The decision-leverage gate
passed:

| Metric | Support |
| --- | ---: |
| top1 delta positive | 9/9 |
| pairwise delta positive | 9/9 |
| mean regret delta negative | 9/9 |
| held-out validation+test support | 9/9 |

Aggregate full-set deltas:

| Metric | Mean delta | Median delta |
| --- | ---: | ---: |
| top1 accuracy | +0.070089 | +0.072500 |
| pairwise accuracy | +0.094472 | +0.094591 |
| mean teacher regret | -1.063917 | -1.086900 |
| best in top2 | +0.063456 | +0.063000 |

All-same predicted roots decreased versus the exact-root baseline on all nine
runs, with mean delta -109.444444. Some all-same roots remain, so this remains a
diagnostic to monitor, but the current local evidence does not justify moving to
a rank objective before larger validation.

## Arena Summary

The runner completed 34 bounded side-swapped arena variants. Every arena used
1,000 selected low-empty positions, side-swapped into 2,000 games. Failed games
were zero throughout.

| Comparison | Runs | Non-negative | Mean score rate | Note |
| --- | ---: | ---: | ---: | --- |
| same-artifact sanity | 1 | 1 | 0.500000 | exact neutral |
| exact-root v2 vs v1 | 4 | 3 | 0.498938 | mixed, effectively neutral |
| move-teacher v2 vs v1 | 14 | 14 | 0.525911 | supportive |
| move-teacher v2 vs exact-root v2 | 14 | 14 | 0.524607 | supportive |
| candidate/baseline swap sanity | 1 | 0 | 0.468250 | reverse of supportive pair |

The strongest 20,000-root artifacts were tested across arena seeds 0, 10, and
20 at depth 3, plus seed 0 at depth 5. Representative 5,000-root and
10,000-root artifacts were tested at depth 3 seed 0. The move-teacher artifact
was non-negative in every v1 and exact-root v2 comparison, including depth 5.

## Promotion Decision

The growth-cycle scorecard selected:

```text
promote_to_larger_local_validation
```

Reasons:

* decision-leverage gate passed on all completed matrix runs
* arena support gate passed
* support was stable across requested root counts and seeds
* same-artifact sanity passed
* swap sanity moved in the expected opposite direction
* failed games were zero

## Next Recommended Action

Run larger low-empty input generation to support a 50,000-root campaign, then
repeat the growth cycle against v1 and exact-root v2 baselines.

Evidence:

* 5,000 / 10,000 / 20,000 roots all improved top1, pairwise, and regret
* held-out validation+test support was positive on all nine matrix runs
* 34 bounded arena variants completed with supportive move-teacher comparisons
* current selected input has 22,954 roots, so it cannot support 50,000 roots

What not to do next:

* do not add pattern-v3 yet
* do not add a pairwise rank trainer before the 50,000-root local validation
  fails or exposes a ranking plateau
* do not claim Elo, self-play, production strength, or publication readiness
