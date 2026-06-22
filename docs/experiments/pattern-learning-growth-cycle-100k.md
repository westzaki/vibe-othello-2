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

## Results

The full connected 100k partial-miss move-teacher solve completed. The run
reused the already cached 50k roots, solved the 50k missing roots, merged those
roots into the local cache, re-probed to a full hit, and materialized complete
100k `move-teacher.tsv` and `child-normalized.tsv` outputs from the original
selected normalized TSV.

Generated TSVs, teacher labels, datasets, weights, artifacts, raw reports,
logs, and cache entries remain local-only and are not committed.

### Cache And Label Generation

| Metric | Value |
| --- | ---: |
| initial cache hits / misses | 50,000 / 50,000 |
| final cache hits / misses | 100,000 / 0 |
| roots newly solved | 50,000 |
| exact nodes reused / saved before solve | 1,591,232,496 |
| exact nodes newly solved | 1,630,297,760 |
| exact nodes materialized after merge | 3,221,530,256 |
| missing-root exact solve wall time | 3,790.633 sec |
| cache merge wall time | 20.123 sec |
| final 100k cache materialization wall time | 48.759 sec |
| final move rows / child-normalized rows | 390,802 / 390,802 |
| move-teacher checksum | `sha256:5cc5910392dd87d026835879cc6e11254165cd1a5caefc19396b10762b767a16` |
| child-normalized checksum | `sha256:0f9a1a3dedf922fdda70c1db926495080dcd5e620fd611494db5a049e57130d7` |

The exact-root labels were derived from the complete 100k move-teacher TSV with
`--missing-policy fail`.

| Metric | Value |
| --- | ---: |
| derived roots | 100,000 |
| missing roots | 0 |
| move-teacher rows consumed | 390,802 |
| root score rule | `max(root_move_score_side_to_move)` |
| teacher node aggregate | sum child `teacher_nodes` for each root |
| derived label checksum | `sha256:1dd049f72772cc009230b2bbd5cbde83ac587de6e7b19c144de9b065bb5a9a5f` |

### Fair Exact-Root v2 Baseline

The fair exact-root v2 baseline uses the same selected 100k normalized input and
the derived exact-root labels. The overlay used `--missing-policy fail`.

| Metric | Value |
| --- | ---: |
| overlay matched / missing / dropped rows | 100,000 / 0 / 0 |
| exact-root v2 normalized checksum | `sha256:26cf9c2f3023d426b27b6a6816db5b2e692447de3224e1305894b7b927baab72` |
| dataset rows train / validation / test | 79,742 / 9,909 / 10,349 |
| dataset checksum | `sha256:163130de008393d589c11ae04e39a786ac4cea86538f3de9cc970eed4de7b182` |
| trainer mode / epochs | `pattern-sgd-v0c` / 8 |
| validation MAE / test MAE | 6.18869251346 / 6.13597468098 |
| weights JSON checksum | `sha256:5637f1e3f8620824439d62427a29196ccba740d4ae3455434c91d06a2b2e413a` |
| exported weights checksum | `sha256:5c881edb9d72bcb05b97a1b7c739d86e51e7d89c308c3295d51df06a7ddb2c14` |
| exported manifest checksum | `sha256:077023941a9d94dc9432b3c02212b2c68993eb47dd6edc5b8b2b2e53148b9308` |

This is the fair same-source 100k exact-root v2 comparator. No 100k result in
this note compares against a 50k exact-root baseline.

### Decision-Leverage Results

The connected 100k growth-cycle ran seeds 0, 1, and 2 against the fair
exact-root v2 baseline. All three seeds reused the complete move-teacher cache
with 100,000 hits, 0 misses, and 0 newly solved exact nodes.

| Seed | Top1 Delta | Top2 Delta | Pairwise Delta | Mean Regret Delta | All-Same Roots Delta |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | +0.037640 | +0.024250 | +0.037097 | -0.479560 | -59 |
| 1 | +0.037640 | +0.024500 | +0.037104 | -0.480370 | -69 |
| 2 | +0.036030 | +0.023860 | +0.036797 | -0.476070 | -90 |

Held-out validation/test support was positive for each repeated 100k seed:

| Seed | Split | Top1 Delta | Top2 Delta | Pairwise Delta | Mean Regret Delta |
| ---: | --- | ---: | ---: | ---: | ---: |
| 0 | validation | +0.016752 | +0.018669 | +0.020849 | -0.270057 |
| 0 | test | +0.026862 | +0.014784 | +0.021599 | -0.330467 |
| 1 | validation | +0.018771 | +0.018972 | +0.021381 | -0.260672 |
| 1 | test | +0.023867 | +0.014494 | +0.020740 | -0.319258 |
| 2 | validation | +0.017459 | +0.018064 | +0.020596 | -0.272782 |
| 2 | test | +0.025799 | +0.015268 | +0.021710 | -0.346314 |

### Arena Results

All arena entries below are bounded local artifact comparisons over 1,000
selected positions with side swap, for 2,000 games per row. Failed games were
zero in every row.

| Comparison | Depth | Training Seed | Arena Seed | Score Rate | Avg Disc Diff |
| --- | ---: | ---: | ---: | ---: | ---: |
| same artifact sanity | 3 | 0 | 0 | 0.500000 | +0.0000 |
| same artifact sanity | 3 | 0 | 10 | 0.500000 | +0.0000 |
| same artifact sanity | 5 | 0 | 0 | 0.500000 | +0.0000 |
| same artifact sanity | 3 | 1 | 0 | 0.500000 | +0.0000 |
| same artifact sanity | 3 | 2 | 0 | 0.500000 | +0.0000 |
| exact-root v2 vs v1 | 3 | n/a | 0 | 0.537750 | +2.2670 |
| exact-root v2 vs v1 | 3 | n/a | 10 | 0.538750 | +2.1350 |
| exact-root v2 vs v1 | 3 | n/a | 20 | 0.529500 | +2.2750 |
| exact-root v2 vs v1 | 5 | n/a | 0 | 0.529250 | +1.5480 |
| move-teacher v2 vs v1 | 3 | 0 | 0 | 0.543000 | +2.7020 |
| move-teacher v2 vs v1 | 3 | 0 | 10 | 0.535750 | +2.3430 |
| move-teacher v2 vs v1 | 3 | 0 | 20 | 0.533750 | +2.5270 |
| move-teacher v2 vs v1 | 5 | 0 | 0 | 0.529250 | +1.6835 |
| move-teacher v2 vs v1 | 3 | 1 | 0 | 0.541750 | +2.6105 |
| move-teacher v2 vs v1 | 3 | 2 | 0 | 0.544000 | +2.7565 |
| move-teacher v2 vs exact-root v2 | 3 | 0 | 0 | 0.509750 | +0.4475 |
| move-teacher v2 vs exact-root v2 | 3 | 0 | 10 | 0.505750 | +0.4865 |
| move-teacher v2 vs exact-root v2 | 3 | 0 | 20 | 0.507250 | +0.4415 |
| move-teacher v2 vs exact-root v2 | 5 | 0 | 0 | 0.503250 | +0.1480 |
| move-teacher v2 vs exact-root v2 | 3 | 1 | 0 | 0.507500 | +0.3925 |
| move-teacher v2 vs exact-root v2 | 3 | 2 | 0 | 0.507000 | +0.4385 |

### Scorecard

Scorecard category: `promote_to_repeated_100k_validation_complete`.

Rationale:

* seed 0 passed the top1, pairwise, regret, held-out, arena, sanity, and failed-game gates
* repeated 100k seeds 1 and 2 also improved top1, top2, pairwise, and regret
* move-teacher v2 vs exact-root v2 was non-negative at depth 3 seeds 0/10/20,
  depth 5 seed 0, and training seeds 1/2
* move-teacher v2 vs v1 was supportive in every bounded arena run
* same-artifact sanity passed in every bounded arena run
* failed games were zero

Important non-claims:

* no Elo result
* no self-play improvement claim
* no production strength claim
* no publication-readiness or artifact-publication-readiness claim

### Runner Fix

The optional seed 1 run exposed an arena resume-key collision in
`candidate_baseline_swap_sanity`: the output directory key used only the
candidate artifact name, so different training seeds collided against the seed
0 swap sanity report. The runner now uses the `swap_of` run id when present,
which keeps swap sanity resume metadata distinct across training seeds. A smoke
check covers the key identity.

## Follow-Up

The immediate repeated 100k validation target is complete. The follow-up
broader bounded arena/search-depth validation also completed and selected
`promote_to_experimental_default_candidate`; details live in
`docs/experiments/pattern-arena-100k-move-teacher-broader.md`.

The next evidence-backed action may be adding the learned eval artifact v0 as
an experimental default candidate in a separate PR. This note still does not
commit or publish generated weights.

Do not do the following next:

* do not claim Elo, self-play improvement, production strength, publication
  readiness, or artifact publication readiness
* do not introduce pattern-v3 as part of the artifact-default PR
* do not add a pairwise rank trainer as part of the artifact-default PR
* do not run an LR/WD sweep as the main next step

No stages are incomplete. If local reports need to be regenerated, rerun the
commands in the PR body with `--resume`; generated outputs remain local-only.
