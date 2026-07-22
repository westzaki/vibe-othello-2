# pattern-v2-egaroucid-lv17-full-value-mpc-d7-v1

Reviewed beta-direction Multi-ProbCut profile for the default learned evaluator.

## Runtime identity and rollout

| Field | Reviewed value |
| --- | --- |
| Evaluator family | `pattern-v2-endgame-lite` |
| Artifact ID | `pattern-v2-egaroucid-lv17-full-value-v1` |
| Weights checksum | `0xfe3d38f9` |
| Search mode | move |
| Exact handoff | 8 empties |
| Pair order | depth `7:3`, then `7:4` |
| Maximum probes per node | 2 |
| Confidence multiplier | 3.0 |
| Maximum margin | 22 |
| Production phases | 2 and 3 |
| Shallow-work cap | 0.5% of official nodes |
| Cold-start guard | 25,000 official nodes per fixed-depth search |

The JSON profile contains every independently accepted domain, while the initial
production rollout deliberately enables only phases 2 and 3. Missing domains,
identity mismatch, other exact thresholds, `easy`, the legacy API, and
`VIBE_OTHELLO_ENABLE_PRODUCTION_PROBCUT=OFF` all resolve to disabled.

## Calibration evidence

Training and independent holdout collection each used 22,000 phase-balanced
roots across phases 0–10 with the same depth-8/exact-8 search policy and ordered
pair population. Training produced 39,352 sample rows. Holdout produced 39,332
rows; removing 196 scheduler nodes that overlapped training left 38,940 rows.
No collection search stopped.

All 40 fitted groups exceeded 250 exact pairs. Adoption required at least 90
full-scheduler holdout cut candidates per exact domain and a per-domain Wilson
95% false-cut upper bound no greater than 5%. Twelve domains passed, producing
24 coefficient rows. Across them, the reviewed two-probe scheduler observed 3
false cuts in 1,690 cut candidates, with a joint Wilson upper bound of
0.0052063. The production phase-2/3 subset observed 0 false cuts in 426
candidates.

## Promotion measurements

An intentionally more permissive all-domain, 20%-shallow-budget stress run used
100 independent opening pairs at 100,000 nodes per move. Multi-ProbCut scored
49.25% (97 wins, 100 losses, 3 draws), with no failed or illegal games and the
same completed-depth percentiles as the disabled baseline. This is neutral
strength evidence, not an Elo claim.

The final restricted production schedule was then measured in the actual WASM
module against a build with the production kill switch off:

| Gate | Result |
| --- | --- |
| Depth 8, 27 phase-spread positions | all 27 moves and scores matched; node ratio 1.000253 |
| Depth 8, five repeated wall-time trials | median enabled/disabled ratio 0.999853 |
| 100 ms, three trials | enabled was deeper on 3 searches, disabled on 0; all other depths equal |
| 500 ms, one browser-policy trial | all 27 depths, moves, and scores matched; wall ratio 0.999805 |

Timing numbers are local non-regression gates and are not portable benchmark
baselines. The exact outputs, node counts, and completed-depth comparisons are
the primary review evidence.

## Regeneration

`profile.json` is the checked conversion of `adoption.json`. Regenerate the
compiled include from the repository root:

```sh
python3 tools/search-calibration/export_probcut_profile_cpp.py \
  data/search/profiles/pattern-v2-egaroucid-lv17-full-value-mpc-d7-v1/profile.json \
  --weights-checksum 0xfe3d38f9 \
  --maximum-margin 22 \
  --maximum-shallow-overhead-ratio 0.005 \
  --output engine/src/search/production_probcut_profile_data.inc
```

CTest regenerates the include into a temporary directory and requires a
byte-for-byte match.
