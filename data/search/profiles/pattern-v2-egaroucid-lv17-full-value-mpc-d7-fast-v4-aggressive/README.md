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

The production profile is accepted only when both aggregate speed gates pass
and every fixed-depth output matches the disabled build:

| Gate | Required | Measured |
| --- | ---: | ---: |
| Enabled/disabled nodes | <= 0.990000 | 0.948832 |
| Median enabled/disabled wall time | <= 0.990000 | 0.939281 |
| Best move, score, completed depth | exact match | 1,350/1,350 each |

The measurement used 270 independent-game positions, balanced at 30 positions
for every phase from 2 through 10, depth 8, exact handoff 8, and five trials
with alternating ON/OFF order after rebasing onto the exact-endgame changes at
`origin/main` commit `472e6f2`. `performance.json` records the corpus checksum,
environment class, thresholds, aggregates, and individual wall-time ratios.
Timing remains machine-local; deterministic node reduction and exact output
parity are the primary portable evidence.

Run the same gate with an equivalent phase-labelled corpus:

```sh
node tools/search-calibration/run_wasm_probcut_speed_gate.mjs \
  --on-module build-wasm/wasm/vibe_othello_wasm_module.mjs \
  --off-module build-wasm-off/wasm/vibe_othello_wasm_module.mjs \
  --manifest data/eval/artifacts/pattern-v2-egaroucid-lv17-full-value-v1/manifest.json \
  --weights data/eval/artifacts/pattern-v2-egaroucid-lv17-full-value-v1/weights.bin \
  --corpus /path/to/phase-balanced-selected.tsv \
  --depth 8 --positions-per-phase 30 --trials 5 \
  --maximum-node-ratio 0.99 --maximum-median-wall-ratio 0.99
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
