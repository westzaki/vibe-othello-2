# Pattern Learning Move-Teacher Decision History

## Scope

This note consolidates the historical decision-leverage evidence that moved
`pattern-v2-endgame-lite` from exact root-label diagnostics toward move-teacher
child-label training. It preserves the adoption rationale and the limiting
caveats only.

For the current move-teacher cache and partial-miss solve contract, use
`pattern-move-teacher-cache.md`. Generated labels, datasets, weights, artifacts,
raw reports, logs, caches, and local paths remain local-only and are not
committed.

This is not an Elo result, not self-play, not a production strength claim, not
publication readiness, and not license clearance for generated artifacts.

## Why Decision Leverage Was Measured

Earlier pattern-learning diagnostics showed that observed-label fitting and
exact root-label value training could improve scalar errors without proving
that the evaluator chose better legal moves at the root. The move-teacher
campaign asked a narrower question: when exact child labels are available for
each legal move, does training on after-move child positions improve the root
move ordering compared with an exact root-label artifact on the same selected
roots?

That question mattered because the current evaluator still consumes value
features, not a dedicated rank objective. A positive move-ordering signal from
the existing value trainer was enough to continue scaling the route before
introducing a pairwise rank trainer or a new pattern set.

## Campaign Summary

All deltas compare move-teacher child-label training against exact root-label
v2 on the same selected roots. Positive top1, top2, pairwise, and arena deltas
are better; negative regret and rank deltas are better.

The scale matrix reused the fixed exact-root v2 artifact from the initial 5k
campaign and evaluated it on each selected root set. It was directional
evidence, not a same-source 10k/20k trained baseline.

| Campaign | Roots | Seeds | Top1 delta | Pairwise delta | Mean regret delta | Arena signal |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Small bounded diagnostic | 5,000 | 1 | +0.046600 | +0.077689 | -0.821800 | 0.514250 score rate, interval included 0.5 |
| Scale matrix, 5k roots | 5,000 | 3 | +0.055000 mean | +0.081590 mean | -0.877267 mean | all non-negative, intervals included 0.5 |
| Scale matrix, 10k roots | 10,000 | 3 | +0.072733 mean | +0.094589 mean | -1.092933 mean | all non-negative, intervals included 0.5 |
| Scale matrix, 20k roots | 20,000 | 3 | +0.082533 mean | +0.107236 mean | -1.221550 mean | all non-negative, depth-3 intervals above 0.5 |

The small diagnostic also compared against an observed root-label v2 artifact
trained on the same selected roots. The child-label deltas were essentially the
same as the exact-root comparison, so the result was not explained by one
particular exact-root comparator.

## Held-Out And Arena Checks

The first 5,000-root run had mixed held-out top1/tie-aware top1, but held-out
pairwise accuracy, best-in-top2, mean regret, and exact-best rank moved in the
same useful direction. The scale matrix made that pattern more convincing:

| Roots | Held-out top1 delta | Held-out pairwise delta | Held-out regret delta | Support |
| ---: | ---: | ---: | ---: | --- |
| 5,000 | +0.009638 mean | +0.045207 mean | -0.370006 mean | 3/3 seeds |
| 10,000 | +0.043005 mean | +0.060869 mean | -0.742670 mean | 3/3 seeds |
| 20,000 | +0.054728 mean | +0.075796 mean | -0.880122 mean | 3/3 seeds |

Additional arenas for the 20,000-root artifacts stayed non-negative: depth-3
arena seed variants averaged 0.529083 score rate, and depth-5 seed-0 variants
averaged 0.517167. These remained bounded local arena diagnostics, not strength
claims.

## Metric That Changed The Adoption Decision

The adoption decision did not rely on fitting loss or full-set top1 alone. The
decisive signal was that pairwise accuracy improved and mean teacher regret
decreased across every scale-matrix run, with held-out pairwise/top2/regret
support on all nine seeds.

That changed the recommendation from "try a pairwise rank trainer immediately"
to "keep the existing child-label value trainer and scale validation breadth."
The pairwise rank trainer remained a separate future investigation rather than
a prerequisite for the growth-cycle path.

## Why This Led To Growth-Cycle Validation

The scale matrix showed repeatable decision leverage across root counts and
seeds while preserving the existing runtime artifact shape. The 20,000-root
bounded arena signal was also consistently non-negative. That combination was
strong enough to move from isolated decision diagnostics into growth-cycle work:
larger local training inputs, fair exact-root baseline comparison, repeated
move-teacher validation, broader bounded arenas, and eventually the current
experimental default route summarized in `../progress/pattern-learning.md`.

## Limits Of The Old Route

The old observed-label and exact-root-label route was useful for proving that
the data pipeline and value trainer worked, but it did not directly answer
whether root move ordering improved. MAE-only checks could pass while still
leaving legal moves poorly ranked.

The early bounded arenas were also intentionally narrow: fixed position
samples, local baselines, fixed search depths, and no self-play or Elo claim.
They justified further local validation, not production strength or publication.
