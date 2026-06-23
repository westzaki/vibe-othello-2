# Pattern Learning Early Diagnostics

## Scope

This is a historical summary of the early pattern-learning route after the
sequence importer. It consolidates the local v0002 measurement notes, v0c/v0d
sweeps, late-phase exact-root teacher diagnostics, signal-bottleneck checks,
and the v2-vs-v1 artifact arena diagnostic.

The purpose is to preserve why the observed-label MAE route was insufficient
and why later work moved to move-ranking and move-teacher child labels. It is
not the current workflow, an architecture contract, an Elo result, a self-play
result, a production strength claim, or a publication gate. Current contracts
remain in the pattern-learning and evaluation-artifact architecture docs.

Generated corpora, normalized TSVs, teacher labels, pattern datasets, reports,
weights, exported candidate artifacts, logs, and local paths remain local-only
intermediate outputs.

## Pipeline Milestones

The sequence importer established the first usable local training route by
replaying source games through board-core semantics and writing normalized TSV
schema v2. The important schema decision was not the importer command line; it
was the identity model needed for diagnostics:

| Field group | Why it mattered |
| --- | --- |
| `record_id`, `position_id` | Let reports refer to stable examples without relying on run layout. |
| `game_group_id` | Kept semantically connected positions from the same replayed game together. |
| `board_id` | Identified exact side-to-move-relative board repeats. |
| `source_occurrence_id`, `source_dataset_id` | Preserved source provenance without making occurrence identity the split key. |
| `split` | Carried train/validation/test assignment into later datasets and reports. |
| `board_a1_to_h8` | Preserved board-core serialization under normalized side-to-move `X`/`O` semantics. |
| label fields | Made label kind, unit, perspective, and side-to-move score explicit. |
| phase/count fields | Kept phase diagnostics reproducible against runtime phase rules. |

The first 10k/100k/1m observed-label runs proved that import, caching, compact
dataset generation, trainer output, export, evaluation smoke, and search smoke
could complete end to end. They also showed the old split and metric route was
too weak for adoption decisions: exact boards crossed train/test boundaries,
1m did not cleanly dominate 100k by validation MAE, and early phases regressed
against the phase-bias baseline even when aggregate MAE improved.

The connected-board-game split was adopted for sequence-derived diagnostics
because it assigns connected components of `game_group_id` and exact
side-to-move-relative `board_id` to the same measurement split. On the 100k
diagnostic it removed both exact-board and game-group cross-split collisions.
This made held-out fitting metrics cleaner, but it did not make observed final
disc difference a searched teacher label or a strength signal.

Compact pattern datasets became the scalable training shape. Expanded rows were
useful for inspection, but compact rows kept one training example per normalized
position with deterministic `pattern_features` payload order. That preserved
duplicate feature occurrences while avoiding the size and runner cost that made
expanded datasets awkward for sweeps.

Trainer v0c and v0d then tested whether optimizer changes solved the fitting
problem on the connected 100k dataset. v0c residual pattern-SGD and v0d
phase-balanced residual pattern-SGD were diagnostic trainer modes, not strength
gates. Their best connected-100k validation MAE values were effectively tied:
best v0c reached 14.1978155715, best v0d reached 14.1985360468, a gap of
0.0007204753 MAE. The result did not justify a v0d promotion or a 1m observed-
label route by itself.

The late-phase exact-root teacher diagnostic introduced
`pattern-v2-endgame-lite` against `pattern-v1-buro-lite` on a bounded low-empty
selection. The pattern-set jump was real as a fitting diagnostic: v2 exact-root
validation MAE beat v1 exact-root by 0.366876, about 3.68 percent relative. The
exact-label jump was not real under the same trainer and root set: v2
exact-root improved over v2 observed by only 0.001910 validation MAE, about
0.020 percent relative, and test MAE did not support an exact-root adoption
signal.

Persistent artifact arenas then checked whether the fitting signal became a
play signal. A small 48-game late-game check was weakly positive for v2 over
v1, but the larger deterministic side-swapped 1,000-position arena did not
preserve that signal. Both observed and exact-root variants scored 1001/2000,
or 0.500500, with intervals spanning 0.5. That made the v2-vs-v1 arena result
inconclusive rather than promotable.

Signal-bottleneck diagnostics ruled out several easy implementation failures:
same-artifact mirror runs tied exactly, v2/v1 and v1/v2 swap checks
complemented at depth 3, v2-added features activated on selected late-game
positions, static scores differed on most roots, and exact-label sanity checks
did not show an obvious sign inversion. The remaining bottleneck was decision
leverage: only about 20 percent of selected roots changed best move at depth 3,
and the exactly adjudicated disagreement subset was mildly positive for v2 but
too sparse to move the whole arena.

This diagnosis led to the move-teacher route. Instead of asking whether root
values lowered average observed-label or exact-root MAE, later experiments used
exact after-move child labels to score legal root moves and measured top1,
top2, pairwise accuracy, regret, and bounded arena behavior. That route made
root decision leverage visible with the existing value trainer and became the
path carried forward in later validation-history docs.

## Diagnostic Summary Table

| Stage | Evidence kept | Diagnostic outcome | Effect on route |
| --- | --- | --- | --- |
| Sequence importer | Normalized TSV schema v2 with explicit identity, split, and label semantics. | End-to-end local training became reproducible enough to diagnose. | Keep schema v2 as the local training interchange shape. |
| Original split | Exact-board cross-split collisions and non-monotonic 100k-to-1m MAE. | Held-out MAE was not clean enough for route adoption. | Replace with connected-board-game split for sequence diagnostics. |
| Connected split | 100k diagnostic had zero board and game-group cross-split collisions. | Improved split hygiene without changing label quality. | Keep for sequence-derived validation. |
| Compact dataset | One compact row per normalized example with deterministic feature payload. | Reduced sweep cost while preserving feature occurrence multiplicity. | Use compact rows for scalable local trainer diagnostics. |
| v0c/v0d sweep | Best v0c and v0d differed by 0.0007204753 validation MAE. | Optimizer family was effectively tied. | Treat v0c/v0d as diagnostics, not promotion evidence. |
| v2 exact-root fitting | v2 beat v1 by 0.366876 validation MAE on exact-root labels. | Pattern capacity improved fitting. | Keep v2 as a useful pattern-set candidate. |
| Exact-root overlay | v2 exact-root beat v2 observed by only 0.001910 validation MAE. | Exact root labels did not unlock a meaningful root-value signal. | Do not adopt exact-root training as the default route. |
| v2-vs-v1 arena | Larger side-swapped arena scored 0.500500 for both label sources. | Fitting gain did not survive as clear play signal. | Require decision-level diagnostics. |
| Bottleneck checks | Runtime loading, side swap, phase mapping, signs, and added features looked sane. | No small wiring bug explained the missing arena signal. | Focus on decision leverage rather than more MAE-only tuning. |
| Move-teacher transition | Child exact labels expose root move ranking and regret. | Ranking metrics match the failure mode better than observed-label MAE. | Carry forward move-ranking and move-teacher validation. |

## What Failed Or Remained Inconclusive

Observed-label MAE alone failed as an adoption signal. It could improve while
split leakage, phase regressions, noisy transcript outcomes, and weak root move
changes still blocked confidence.

The connected split fixed leakage hygiene, not label semantics. It made
validation cleaner, but the labels were still observed final outcomes rather
than searched teacher estimates.

The v0c/v0d sweep remained inconclusive. v0d's phase balancing was useful to
test phase imbalance, but it did not produce a meaningful validation-selected
winner over v0c.

The exact-root diagnostic remained inconclusive as an adopted route. It showed
that `pattern-v2-endgame-lite` had better fitting capacity than v1, but exact
root labels barely improved over observed labels within v2 and did not produce
a robust artifact arena signal.

The v2-vs-v1 persistent arena did not validate the fitting story. The larger
side-swapped run canceled to roughly even, so the earlier small positive arena
was not enough to justify artifact promotion.

Signal-bottleneck checks did not find a simple bug to fix. They instead showed
that v2 produced real score differences, but too few high-leverage root move
changes for a clear arena result.

## Decisions Carried Forward

Keep normalized TSV schema v2 for local pattern-learning interchange because it
records the identities and label semantics needed to diagnose split leakage,
source provenance, and side-to-move perspective.

Use connected-board-game split for sequence-derived validation diagnostics
because it groups semantically connected games and repeated normalized boards
before assigning train/validation/test.

Use compact pattern datasets for scalable trainer diagnostics because they
preserve deterministic feature order and duplicate occurrence contribution
without expanded-row overhead.

Treat v0c and v0d as local trainer diagnostic modes. Their early sweep did not
make v0d an adopted route or make observed-label MAE sufficient.

Treat `pattern-v2-endgame-lite` as useful pattern capacity evidence, not as a
standalone promotion reason. Exact-root fitting improved v2 over v1, but the
root-label overlay and arena evidence did not make exact-root training the
adopted route.

Review future pattern-learning changes with decision-level evidence when they
affect route selection: move ranking, pairwise accuracy, regret, bounded arena
checks, sign/sanity diagnostics, and fair baselines. Average fitting MAE remains
a useful diagnostic, but it is not enough to explain why the current route was
chosen.

The route carried forward was move-teacher child-label training followed by
fair exact-root baseline comparisons and broader bounded validation, as covered
by the later validation-history archive and current progress doc.
