# Pattern Learning History

## Scope and Non-claims

This archive consolidates the historical pattern-learning experiment summaries
that led from observed-label diagnostics to the current move-teacher
experimental default. It keeps adoption evidence only, not per-PR notes, run
journals, reproduction details, or local paths.

Current workflow and contracts live in the progress, architecture,
evaluation-artifact, and data-policy docs. Generated corpora, teacher labels,
selected TSVs, datasets, reports, weights, logs, caches, and intermediate
artifacts remain local-only unless a reviewed runtime artifact is committed
under the evaluation-artifact policy.

This history is not an Elo result, not self-play evidence, not a production
strength claim, not publication readiness, and not artifact-publication
readiness.

## Timeline

### Observed-label and connected-split diagnostics

The sequence importer established normalized TSV schema v2 with explicit game,
board, occurrence, split, label, phase, and side-to-move identity. That made
leakage, provenance, and held-out diagnostics inspectable.

The first 10k/100k/1m observed-label runs proved the local pipeline could
import, cache, generate compact datasets, train, export, and smoke-test
artifacts. They did not prove a promotable evaluator. Exact boards crossed
train/test boundaries, 1m did not clearly dominate 100k by validation MAE, and
early phases could regress against a phase-bias baseline even when aggregate
MAE improved.

The connected-board-game split was adopted for sequence-derived diagnostics
because it assigns connected components of `game_group_id` and exact
side-to-move-relative `board_id` to one measurement split. On the 100k
diagnostic it removed exact-board and game-group cross-split collisions. This
made validation cleaner, but it did not turn observed final disc difference
into a searched teacher label or a strength signal.

### Pattern-v2 and exact-root diagnostics

Trainer v0c and v0d tested whether optimizer changes solved the observed-label
fitting problem on the connected 100k dataset. Best v0c validation MAE was
14.1978155715 and best v0d validation MAE was 14.1985360468, a difference of
0.0007204753. That did not justify promoting v0d or returning to a larger
observed-label MAE route.

The late-phase exact-root diagnostic showed useful pattern capacity:
`pattern-v2-endgame-lite` beat `pattern-v1-buro-lite` by 0.366876 validation
MAE, about 3.68 percent relative. The label route still did not unlock
adoption. Under the same v2 trainer and root set, v2 exact-root beat v2
observed labels by only 0.001910 validation MAE, about 0.020 percent relative,
and test MAE did not support adoption. A larger deterministic side-swapped
1,000-position arena also canceled out: observed and exact-root variants each
scored 1001/2000, or 0.500500, with intervals spanning 0.5.

Signal-bottleneck checks did not find a simple wiring failure. The remaining
issue was decision leverage: only about 20 percent of selected roots changed
the depth-3 best move, so scalar fitting gains were too sparse to move the
arena.

### Move-teacher decision leverage

Move-teacher diagnostics changed the question from "does root value MAE
improve?" to "do exact after-move child labels improve legal root move
ordering?" Child labels exposed top1, top2, pairwise accuracy, regret, and rank
metrics while keeping the existing value-trainer artifact shape.

Against exact-root v2 on the same selected roots, the move-teacher route showed
repeatable decision leverage:

| Campaign | Roots | Seeds | Top1 delta | Pairwise delta | Mean regret delta | Arena signal |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Small bounded diagnostic | 5,000 | 1 | +0.046600 | +0.077689 | -0.821800 | 0.514250 score rate, interval included 0.5 |
| Scale matrix | 5,000 | 3 | +0.055000 mean | +0.081590 mean | -0.877267 mean | all non-negative, intervals included 0.5 |
| Scale matrix | 10,000 | 3 | +0.072733 mean | +0.094589 mean | -1.092933 mean | all non-negative, intervals included 0.5 |
| Scale matrix | 20,000 | 3 | +0.082533 mean | +0.107236 mean | -1.221550 mean | all non-negative, depth-3 intervals above 0.5 |

Held-out pairwise accuracy and mean regret improved across all nine scale
matrix seeds. That was the adoption-changing evidence: the route no longer
depended on MAE alone, and the existing child-label value trainer was strong
enough to scale before introducing a pairwise rank trainer.

### 50k / connected 100k / broader arena

The validation path then required fair same-source exact-root comparators at
each scale. The accepted 100k result does not compare move-teacher training
against a mismatched 50k exact-root baseline.

At 50k seed 0, the move-teacher artifact improved top1 by +0.042720, pairwise
by +0.043116, and mean regret by -0.561600; arena vs exact-root v2 was
non-negative in 4/4 runs. Repeating 50k across seeds 0/1/2 kept top1, top2,
pairwise, and regret positive, with 12/12 non-negative arena runs. A connected
50k split check selected the same board-id set with connected assignments and
accepted the split policy, but remained supporting evidence rather than the
final gate.

Connected 100k became the decisive scale. The selected source checksum was
`sha256:260102a58ead4522169d7298ba828fa983930c902c261c49d18da4b11b6d0ce7`.
It selected 100,000 roots from 230,045 eligible low-empty roots, with
79,742 / 9,909 / 10,349 train / validation / test roots, 390,802 move-teacher
rows, and complete exact-root overlay coverage. The fair exact-root v2
validation/test MAE was 6.18869251346 / 6.13597468098.

Repeated connected 100k validation completed on seeds 0, 1, and 2:

| Seed | Top1 delta | Top2 delta | Pairwise delta | Mean regret delta | All-same roots delta |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | +0.037640 | +0.024250 | +0.037097 | -0.479560 | -59 |
| 1 | +0.037640 | +0.024500 | +0.037104 | -0.480370 | -69 |
| 2 | +0.036030 | +0.023860 | +0.036797 | -0.476070 | -90 |

Held-out validation/test deltas were positive for top1, top2, and pairwise
accuracy, and negative for mean regret, for all three seeds. The repeated 100k
bounded arena stayed neutral or supportive across same-artifact, v2-vs-v1, and
move-teacher-vs-exact-root checks.

The broader arena used the 100k move-teacher seed 0 candidate, the fair 100k
exact-root v2 comparator, and the v1 exact-teacher comparator over depths 3,
5, and 7 with five arena seeds. It ran 75 jobs, 150,000 games, and had zero
failed games.

| Comparison | Runs | Games | Non-negative runs | Mean score rate | Min score rate | Mean avg disc diff |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| move-teacher v2 vs exact-root v2 | 15 | 30,000 | 14 | 0.503517 | 0.499500 | +0.2020 |
| move-teacher v2 vs v1 | 15 | 30,000 | 15 | 0.526150 | 0.510000 | +1.6194 |
| exact-root v2 vs v1 | 15 | 30,000 | 15 | 0.523250 | 0.508500 | +1.4444 |
| same-artifact sanity | 15 | 30,000 | 15 | 0.500000 | 0.500000 | +0.0000 |
| swap sanity: exact-root v2 vs move-teacher v2 | 15 | 30,000 | 2 | 0.496483 | 0.490250 | -0.2020 |

Depth 7 narrowed the margin against exact-root v2: one score-rate row was
slightly negative at 0.499500, while aggregate depth-7 score rate remained
non-negative at 0.500950. This supported an experimental default candidate,
not a production-strength claim.

## Decisive Evidence

The decisive evidence was repeated 100k decision leverage plus broader arena
behavior: pairwise accuracy improved and teacher regret decreased across the
connected 100k seeds, while the broader arena was neutral-to-positive against
the fair exact-root comparator and clearly supportive against v1. The committed
artifact provenance preserves the same summary for
`pattern-v2-endgame-lite-100k-mt-v0`: selected 100k source checksum, seeds
0/1/2, and the 75-run broader arena. It is the current experimental default,
not a strength claim.

## Adopted Route

The adopted route is `pattern-v2-endgame-lite` move-teacher child-label
training with connected-board-game split, compact datasets, fair same-source
exact-root v2 comparison, repeated 100k validation, broader bounded arena
validation, and a reviewed runtime artifact payload. The current artifact is
`pattern-v2-endgame-lite-100k-mt-v0`.

## Rejected or Deferred Routes

Observed-label MAE alone was rejected as a promotion signal because it could
improve while leakage, phase regressions, noisy transcript outcomes, and weak
root move changes still blocked confidence. Exact-root training did not become
the default route because exact root labels barely improved over observed
labels within v2 and did not produce a robust arena signal. The 50k exact-root
baseline was rejected for 100k comparisons; each scale needs a fair same-source
exact-root comparator. Pattern-v3, a pairwise rank trainer, learning-rate or
weight-decay sweeps, and larger objective changes were deferred to separate
investigations so they would not be mixed with default-artifact selection.
