# Pattern-Learning Validation History

## Purpose

This note summarizes the local-only validation path that moved the
`pattern-v2-endgame-lite` move-teacher route from an initial growth-cycle check
through 50k, repeated 50k, connected 100k, and broader bounded arena
validation.

It preserves the evidence needed to understand the final adopted route and
artifact choice without keeping per-PR run journals. Generated TSVs, teacher
labels, datasets, candidate weights, raw reports, logs, caches, and corpora
remain local-only. Only a reviewed final runtime artifact payload may be
committed under the evaluation-artifact policy.

This history is not an Elo result, not self-play evidence, not a production
strength claim, and not publication readiness.

## Final Adopted Route

The validation path adopted the move-teacher child-value route:

| Item | Final route |
| --- | --- |
| Pattern set | `pattern-v2-endgame-lite` |
| Training signal | exact child move-teacher labels |
| Selection source | connected-board-game 1m resplit, low-empty roots |
| Final source size | 100,000 selected roots |
| Fair comparator | same-source 100k exact-root v2 baseline |
| Legacy comparator | v1 exact-teacher artifact |
| Final candidate | 100k move-teacher v2, training seed 0 |
| Final scorecard | `promote_to_experimental_default_candidate` |

The important fairness rule was that each scale needed a same-source
exact-root v2 comparator. The accepted 100k result does not compare the
move-teacher artifact against a mismatched 50k exact-root baseline.

## Timeline

| Stage | Validation question | Key evidence | Decision |
| --- | --- | --- | --- |
| initial 5k/10k/20k | Does the move-teacher route beat exact-root labels before larger local validation? | Decision-leverage deltas were positive on 9/9 runs; bounded arena was supportive for move-teacher v2 vs v1 in 14/14 runs and vs exact-root v2 in 14/14 runs. | Adopted larger local validation; did not change objective or add pattern-v3. |
| 50k seed 0 | Can a fair 50k same-source exact-root comparator and one real 50k move-teacher run be produced? | 230,045 eligible roots yielded a 50,000-root selection. Seed 0 improved top1 by +0.042720, pairwise by +0.043116, and mean regret by -0.561600; arena vs exact-root v2 was non-negative in 4/4 runs. | Positive but held for more data because only one 50k seed was complete. |
| 50k repeat | Are 50k results stable across seeds? | Seeds 0/1/2 all improved top1, top2, pairwise, and regret. Mean deltas were top1 +0.042047, top2 +0.031660, pairwise +0.043063, and regret -0.557660. Move-teacher arena vs exact-root v2 was non-negative in 12/12 runs. | Promoted to larger local validation. |
| connected 50k split check | Does the signal survive the intended connected-board-game split policy? | The connected split selected the same board-id set with connected assignments. Seed 0 improved top1 by +0.047580, pairwise by +0.044770, and regret by -0.606300; arena vs exact-root v2 was non-negative in 4/4 runs. | Split policy was accepted, but the one-seed connected 50k check stayed supporting evidence rather than a final gate. |
| connected 100k | Does the fair 100k connected route repeat across seeds? | 100,000 roots were selected from 230,045 eligible low-empty roots. Seeds 0/1/2 all improved top1, top2, pairwise, and regret against the fair 100k exact-root v2 baseline. | Repeated 100k validation completed; moved to broader bounded arena validation. |
| broader arena | Does the 100k seed 0 candidate remain non-negative across broader arena depths and seeds? | 75 runs, 150,000 games, and zero failed games. Move-teacher v2 vs exact-root v2 was non-negative in 14/15 runs with mean score rate 0.503517; vs v1 was supportive in 15/15 runs with mean score rate 0.526150. | Selected as the experimental default candidate for a later artifact PR. |

## Decisive 100k Numbers

The connected 100k source and fair exact-root baseline were the decisive scale
step:

| Metric | Value |
| --- | ---: |
| eligible low-empty roots | 230,045 |
| selected connected 100k roots | 100,000 |
| selected source checksum | `sha256:260102a58ead4522169d7298ba828fa983930c902c261c49d18da4b11b6d0ce7` |
| train / validation / test roots | 79,742 / 9,909 / 10,349 |
| move-teacher rows | 390,802 |
| exact-root overlay matched / missing / dropped rows | 100,000 / 0 / 0 |
| exact-root v2 validation MAE | 6.18869251346 |
| exact-root v2 test MAE | 6.13597468098 |
| fair exact-root v2 weights checksum | `sha256:5c881edb9d72bcb05b97a1b7c739d86e51e7d89c308c3295d51df06a7ddb2c14` |
| fair exact-root v2 manifest checksum | `sha256:077023941a9d94dc9432b3c02212b2c68993eb47dd6edc5b8b2b2e53148b9308` |

Repeated 100k decision-leverage results:

| Seed | Top1 delta | Top2 delta | Pairwise delta | Mean regret delta | All-same roots delta |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | +0.037640 | +0.024250 | +0.037097 | -0.479560 | -59 |
| 1 | +0.037640 | +0.024500 | +0.037104 | -0.480370 | -69 |
| 2 | +0.036030 | +0.023860 | +0.036797 | -0.476070 | -90 |

Held-out validation/test deltas were positive for top1, top2, and pairwise
accuracy, and negative for mean regret, for all three 100k seeds.

Bounded arena results from the repeated 100k growth cycle:

| Comparison | Runs | Non-negative runs | Mean score rate signal |
| --- | ---: | ---: | --- |
| same-artifact sanity | 5 | 5 | exactly neutral |
| exact-root v2 vs v1 | 4 | 4 | supportive |
| move-teacher v2 vs v1 | 6 | 6 | supportive |
| move-teacher v2 vs exact-root v2 | 6 | 6 | non-negative |

## Broader Arena Decision

The broader arena matrix used the 100k move-teacher seed 0 candidate, the fair
100k exact-root v2 comparator, and the v1 exact-teacher comparator over depths
3, 5, and 7 with arena seeds 0, 10, 20, 30, and 40. Each run used 1,000
selected positions with side swap, for 2,000 games per run.

| Comparison | Runs | Games | Failed | Non-negative runs | Mean score rate | Min score rate | Mean avg disc diff |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| move-teacher v2 vs exact-root v2 | 15 | 30,000 | 0 | 14 | 0.503517 | 0.499500 | +0.2020 |
| move-teacher v2 vs v1 | 15 | 30,000 | 0 | 15 | 0.526150 | 0.510000 | +1.6194 |
| exact-root v2 vs v1 | 15 | 30,000 | 0 | 15 | 0.523250 | 0.508500 | +1.4444 |
| same-artifact sanity | 15 | 30,000 | 0 | 15 | 0.500000 | 0.500000 | +0.0000 |
| swap sanity: exact-root v2 vs move-teacher v2 | 15 | 30,000 | 0 | 2 | 0.496483 | 0.490250 | -0.2020 |

Depth 7 narrowed the margin against exact-root v2: one score-rate row was
slightly negative at 0.499500, but the aggregate depth-7 score rate remained
non-negative at 0.500950. This close margin is why the result supports an
experimental default candidate, not a production-strength claim.

## Adopted And Rejected Decisions

Adopted:

* continue the `pattern-v2-endgame-lite` move-teacher child-label route
* require same-source exact-root v2 baselines for scale comparisons
* use the connected-board-game split policy for the 100k route
* treat the 100k seed 0 move-teacher artifact as the candidate selected by
  broader bounded arena validation

Rejected or deferred:

* do not use the 50k exact-root baseline for 100k comparisons
* do not add pattern-v3 as part of this validation sequence
* do not switch to a pairwise rank trainer from these value-route results
* do not run a learning-rate or weight-decay sweep as the main explanation for
  the observed gains
* do not claim Elo, self-play improvement, production strength, publication
  readiness, or artifact-publication readiness from these docs

## Source Attribution

The values above consolidate the previously separate local experiment notes for
the initial growth-cycle run, 50k run, 50k repeat, connected 100k run, and
broader arena validation. The checked-in history intentionally omits local
absolute paths, long reproduction commands, completed next-action lists,
generated-output reminders, and intermediate per-run metric tables.
