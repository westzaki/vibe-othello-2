# pattern-v2-egaroucid-lv17-full-value-mpc-d7-fast-v4-aggressive

Speed-gated beta-direction Multi-ProbCut profile for the default learned evaluator.

## Runtime identity and schedule

| Field | Reviewed value |
| --- | --- |
| Evaluator family | `pattern-v2-endgame-lite` |
| Artifact ID | `pattern-v2-egaroucid-lv17-full-value-v1` |
| Weights checksum | `0xfe3d38f9` |
| Score scale | 100 |
| Trained phases | 0 through 12 |
| Fallback additive through phase | 9 |
| Search mode | move |
| Exact handoff | 8 empties |
| Depth pairs | `7:3`, then `7:4` |
| Maximum probes per node | 2 |
| Confidence multiplier | 3.0 |
| Maximum margin | 22 |
| Production phases | 2, 3, 4, 6, 7, 9, and 10 |
| Shallow-work cap | 20% of official nodes |
| Cold-start guard | none |

The earlier two-probe rollout paid for exact full-window shallow scores and did
not demonstrate a speedup. V4 solves each reviewed regression for the minimum
qualifying shallow score and searches only the corresponding null window. It
tries `7:4` only after `7:3` rejects, and includes only exact scheduler domains
that observed no false cuts. Holdout replay applies the same
threshold-directed decision, so an exact shallow score above the reviewed
maximum still counts as a cut when the derived threshold is within range.
Missing domains, identity mismatch, other exact thresholds, `easy`, the legacy
API, and `VIBE_OTHELLO_ENABLE_PRODUCTION_PROBCUT=OFF` all resolve to disabled.

The wider 14-empty root policy was also checked against the same ON/OFF gate
after root and internal exact thresholds were separated. Fixed-depth output
matched completely: the primary depth-8 node ratio was `0.986927`, and the
depth-8-through-12 aggregate node ratio was `0.947740`. However, the primary
median wall ratio was `0.993350` against the required `0.990000`, while the
500 ms rollout produced 80/81 best-move matches, 79/81 score matches, and a
completed-depth sum of 859 enabled versus 860 disabled. Because that policy
does not pass every mandatory gate, `normal` and `hard` fail closed to MPC off
when their root threshold is wider than the reviewed internal threshold. The
checked-in `performance.json` intentionally remains the accepted exact-8
profile evidence.

## Calibration evidence

Training and independent holdout collection each used 22,000 phase-balanced
roots across phases 0–10 with the same depth-8/exact-8 search policy. Training
produced 39,352 sample rows. Holdout produced 39,332 rows; removing every row
for the 330 canonical positions that also appeared in training left 38,672
rows. The position-level check ignores ply, search window, and search role. No
collection search stopped.

The selected phase 2/3/4/6/7/9/10 domains contain 1,305 threshold-directed
holdout cut candidates with 0 false cuts. Their joint Wilson 95% upper bound is
0.0029351. The retained ten exact domains each have at least 90 candidates and
a per-domain Wilson 95% upper bound below 5%.

## Mandatory speed gate

The production profile is accepted only when the original fixed-depth gate,
the extended fixed-depth gate, and the time-bounded rollout gate all pass. The
figures below were rerun after the phase-aware exact-handoff policy began using
a full root window and omitting reduced-depth IID when that search would itself
reach the internal exact solver. The original depth-8 gate retains its
270-position population and five alternating trials:

| Gate | Required | Measured |
| --- | ---: | ---: |
| Enabled/disabled nodes | <= 0.990000 | 0.984436 |
| Median enabled/disabled wall time | <= 0.990000 | 0.981748 |
| Best move, score, completed depth | exact match | 1,350/1,350 each |

The 8 MiB TT rollout also checks a 27-position subset balanced at three
independent games per phase through every fixed depth observed under the Web
time budget. A 1 billion node safety limit lets every depth finish; it is a
measurement bound, not a Web runtime setting:

| Depth | Enabled nodes | Disabled nodes | Ratio | Best move / score / completed depth |
| ---: | ---: | ---: | ---: | ---: |
| 8 | 2,436,067 | 2,450,878 | 0.993957 | 27/27 each |
| 9 | 5,246,651 | 5,221,905 | 1.004739 | 27/27 each |
| 10 | 155,243,862 | 162,472,700 | 0.955507 | 27/27 each |
| 11 | 430,085,211 | 454,254,387 | 0.946794 | 27/27 each |
| 12 | 643,734,203 | 680,280,344 | 0.946278 | 27/27 each |
| Aggregate | 1,236,745,994 | 1,304,680,214 | 0.947930 | 135/135 each |

The extended gate requires at least 1% aggregate node reduction, no individual
depth above a 1.01 node ratio, exact fixed-depth output parity, and no stopped
searches. Depth 9 has a 0.47% local node increase, below the per-depth guard;
the aggregate reduces nodes by 5.21%.

The same 27 positions were then run for three alternating trials with the Web
limits, depth 64 and 500 ms. Both variants had median completed depth 10 and
median per-position wall time at the 500 ms cap. Enabled search completed 1,057
total depth units versus 1,055 disabled: it was deeper in three comparisons,
equal in 77, and shallower in one. Best move and score matched in 81/81
comparisons; all 77 equal-depth comparisons also matched exactly. The timed
gate requires best-move and score parity for every comparison, equal-depth
output parity, and non-regression in both median and aggregate completed depth.

The source corpus is balanced across phases 2 through 10 and the exact handoff
remains 8. `performance.json` records its checksum, the local environment
class, every threshold and aggregate, fixed-depth completion histograms, and
individual wall-time ratios. Timing remains machine-local; deterministic node
counts and exact output parity are the primary portable evidence.

Run the same gate with an equivalent phase-labelled corpus:

```sh
node tools/search-calibration/run_wasm_probcut_speed_gate.mjs \
  --on-module build-wasm/wasm/vibe_othello_wasm_module.mjs \
  --off-module build-wasm-off/wasm/vibe_othello_wasm_module.mjs \
  --manifest data/eval/artifacts/pattern-v2-egaroucid-lv17-full-value-v1/manifest.json \
  --weights data/eval/artifacts/pattern-v2-egaroucid-lv17-full-value-v1/weights.bin \
  --corpus /path/to/phase-balanced-source.tsv \
  --profile-id pattern-v2-egaroucid-lv17-full-value-mpc-d7-fast-v4-aggressive \
  --machine-class "local arm64 desktop" \
  --depth 8 --positions-per-phase 30 --trials 5 \
  --extended-depths 8,9,10,11,12 \
  --extended-positions-per-phase 3 --extended-trials 1 \
  --fixed-max-nodes 1000000000 \
  --timed-max-depth 64 --timed-max-time-ms 500 \
  --timed-positions-per-phase 3 --timed-trials 3 \
  --maximum-node-ratio 0.99 --maximum-median-wall-ratio 0.99 \
  --maximum-extended-aggregate-node-ratio 0.99 \
  --maximum-extended-depth-node-ratio 1.01 \
  --output data/search/profiles/pattern-v2-egaroucid-lv17-full-value-mpc-d7-fast-v4-aggressive/performance.json
```

## Regeneration

`profile.json` is the checked conversion of `adoption.json`. Regenerate the
compiled include from the repository root:

```sh
python3 tools/search-calibration/export_probcut_profile_cpp.py \
  data/search/profiles/pattern-v2-egaroucid-lv17-full-value-mpc-d7-fast-v4-aggressive/profile.json \
  --weights-checksum 0xfe3d38f9 \
  --maximum-margin 22 \
  --maximum-shallow-overhead-ratio 0.20 \
  --output engine/src/search/production_probcut_profile_data.inc
```

CTest regenerates the include into a temporary directory and requires a
byte-for-byte match.
